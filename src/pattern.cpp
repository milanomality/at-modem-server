// SPDX-License-Identifier: MIT
#include "pattern.h"

namespace ndm {
namespace {

constexpr std::size_t kNoPos = static_cast<std::size_t>(-1);

/// Регистронезависимость делаем по ASCII, а не через std::toupper:
/// поведение не должно зависеть от текущей локали процесса.
inline unsigned char asciiUpper(unsigned char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<unsigned char>(c - 'a' + 'A') : c;
}

inline unsigned char asciiLower(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
}

}  // namespace

PatternError::PatternError(const std::string& expr, std::size_t pos, const std::string& reason)
    : std::runtime_error("шаблон \"" + expr + "\", позиция " + std::to_string(pos) + ": " + reason),
      pos_(pos) {}

Pattern::Token Pattern::makeLiteral(char c) const {
    Token t;
    t.kind = Token::Kind::Literal;
    const auto u = static_cast<unsigned char>(c);
    t.ch = caseInsensitive_ ? asciiUpper(u) : u;
    return t;
}

void Pattern::addChar(Token& t, unsigned char c) const {
    t.set.set(c);
    if (caseInsensitive_) {
        t.set.set(asciiUpper(c));
        t.set.set(asciiLower(c));
    }
}

std::size_t Pattern::appendClass(const std::string& expr, std::size_t open) {
    const std::size_t n = expr.size();
    std::size_t i = open + 1;

    Token t;
    t.kind = Token::Kind::Class;

    bool negate = false;
    if (i < n && expr[i] == '^') {
        negate = true;
        ++i;
    }

    // Пустой класс запрещён, поэтому ']' первым символом трактуется как литерал.
    bool empty = true;
    while (i < n && (expr[i] != ']' || empty)) {
        unsigned char lo;
        if (expr[i] == '\\') {
            if (i + 1 >= n) throw PatternError(expr, i, "обрыв строки после '\\' внутри [...]");
            lo = static_cast<unsigned char>(expr[++i]);
        } else {
            lo = static_cast<unsigned char>(expr[i]);
        }

        const bool isRange = (i + 2 < n) && expr[i + 1] == '-' && expr[i + 2] != ']';
        if (isRange) {
            i += 2;
            unsigned char hi;
            if (expr[i] == '\\') {
                if (i + 1 >= n) throw PatternError(expr, i, "обрыв строки после '\\' внутри [...]");
                hi = static_cast<unsigned char>(expr[++i]);
            } else {
                hi = static_cast<unsigned char>(expr[i]);
            }
            if (hi < lo) throw PatternError(expr, i, "перевёрнутый диапазон в [...]");
            for (int c = lo; c <= static_cast<int>(hi); ++c) {
                addChar(t, static_cast<unsigned char>(c));
            }
        } else {
            addChar(t, lo);
        }

        empty = false;
        ++i;
    }

    if (i >= n) throw PatternError(expr, open, "не закрыт символьный класс '['");
    if (negate) t.set.flip();

    tokens_.push_back(std::move(t));
    return i;  // позиция ']'
}

Pattern Pattern::compile(const std::string& expr, bool caseInsensitive) {
    Pattern p;
    p.source_ = expr;
    p.caseInsensitive_ = caseInsensitive;

    const std::size_t n = expr.size();
    for (std::size_t i = 0; i < n; ++i) {
        switch (expr[i]) {
        case '*': {
            // Цепочка '***' эквивалентна одному '*' — схлопываем, чтобы не плодить
            // лишние точки возврата в matches().
            if (!p.tokens_.empty() && p.tokens_.back().kind == Token::Kind::Star) break;
            Token t;
            t.kind = Token::Kind::Star;
            p.tokens_.push_back(std::move(t));
            break;
        }
        case '.': {
            Token t;
            t.kind = Token::Kind::AnyChar;
            p.tokens_.push_back(std::move(t));
            break;
        }
        case '[':
            i = p.appendClass(expr, i);
            break;
        case '\\':
            if (i + 1 >= n) throw PatternError(expr, i, "обрыв строки после '\\'");
            p.tokens_.push_back(p.makeLiteral(expr[++i]));
            break;
        default:
            p.tokens_.push_back(p.makeLiteral(expr[i]));
            break;
        }
    }
    return p;
}

bool Pattern::matchOne(const Token& t, char c) const noexcept {
    const auto u = static_cast<unsigned char>(c);
    switch (t.kind) {
    case Token::Kind::Literal:
        return (caseInsensitive_ ? asciiUpper(u) : u) == t.ch;
    case Token::Kind::AnyChar:
        return true;
    case Token::Kind::Class:
        return t.set.test(u);
    case Token::Kind::Star:
        return false;  // обрабатывается отдельной веткой в matches()
    }
    return false;
}

bool Pattern::matches(const std::string& text) const noexcept {
    const std::size_t n = text.size();
    const std::size_t m = tokens_.size();

    std::size_t i = 0;           // позиция в тексте
    std::size_t j = 0;           // позиция в шаблоне
    std::size_t starJ = kNoPos;  // последняя встреченная '*'
    std::size_t starI = 0;       // сколько текста ей уже отдано

    // Итеративный поиск с одной точкой возврата: при неудаче откатываемся к
    // последней '*' и отдаём ей на один символ больше. Рекурсии нет, стек не
    // растёт; худший случай O(n*m) при O(1) дополнительной памяти.
    while (i < n) {
        if (j < m && tokens_[j].kind != Token::Kind::Star && matchOne(tokens_[j], text[i])) {
            ++i;
            ++j;
        } else if (j < m && tokens_[j].kind == Token::Kind::Star) {
            starJ = j;
            starI = i;
            ++j;
        } else if (starJ != kNoPos) {
            j = starJ + 1;
            i = ++starI;
        } else {
            return false;
        }
    }

    // Текст кончился — хвост шаблона допустим, только если это одни '*'.
    while (j < m && tokens_[j].kind == Token::Kind::Star) ++j;
    return j == m;
}

}  // namespace ndm
