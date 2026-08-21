// SPDX-License-Identifier: MIT
#pragma once

#include <bitset>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace ndm {

/// Ошибка компиляции шаблона: хранит позицию проблемного символа.
class PatternError : public std::runtime_error {
public:
    PatternError(const std::string& expr, std::size_t pos, const std::string& reason);
    std::size_t position() const noexcept { return pos_; }

private:
    std::size_t pos_;
};

/// Шаблон ограниченного синтаксиса регулярных выражений.
///
/// Поддерживается (без использования каких-либо regex-библиотек):
///   .        ровно один любой символ
///   *        ноль или более любых символов
///   [abc]    один символ из набора; диапазоны [0-9] и инверсия [^0-9]
///   \x       экранирование спецсимвола (\. \* \[ \\ \=)
/// Остальные символы — литералы. Совпадение всегда полное: шаблон должен
/// покрыть строку целиком (как если бы стояли якоря ^ и $).
class Pattern {
public:
    /// Компилирует выражение. Бросает PatternError на некорректном синтаксисе.
    static Pattern compile(const std::string& expr, bool caseInsensitive = true);

    /// Полное совпадение со строкой. Не бросает исключений.
    bool matches(const std::string& text) const noexcept;

    const std::string& source() const noexcept { return source_; }
    bool caseInsensitive() const noexcept { return caseInsensitive_; }
    std::size_t tokenCount() const noexcept { return tokens_.size(); }

private:
    struct Token {
        enum class Kind { Literal, AnyChar, Star, Class };
        Kind kind = Kind::Literal;
        unsigned char ch = 0;   ///< Literal (уже приведён к верхнему регистру при ci)
        std::bitset<256> set;   ///< Class (регистр учтён на этапе компиляции)
    };

    bool matchOne(const Token& t, char c) const noexcept;
    Token makeLiteral(char c) const;
    void addChar(Token& t, unsigned char c) const;
    /// Разбирает [...] начиная с позиции открывающей скобки, возвращает позицию ']'.
    std::size_t appendClass(const std::string& expr, std::size_t open);

    std::string source_;
    std::vector<Token> tokens_;
    bool caseInsensitive_ = true;
};

}  // namespace ndm
