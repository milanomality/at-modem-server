// SPDX-License-Identifier: MIT
#include "dictionary.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ndm {
namespace {

std::string trimRight(const std::string& s) {
    std::size_t end = s.size();
    while (end > 0) {
        const auto c = static_cast<unsigned char>(s[end - 1]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') --end;
        else break;
    }
    return s.substr(0, end);
}

std::string trimLeft(const std::string& s) {
    std::size_t beg = 0;
    while (beg < s.size()) {
        const auto c = static_cast<unsigned char>(s[beg]);
        if (c == ' ' || c == '\t') ++beg;
        else break;
    }
    return s.substr(beg);
}

bool isComment(const std::string& s) {
    return !s.empty() && (s[0] == '#' || s[0] == ';');
}

/// Делит строку по первому НЕэкранированному '='. Экранированные (\=) остаются
/// в шаблоне и будут разобраны Pattern::compile как литеральный '='.
bool splitOnSeparator(const std::string& line, std::string& expr, std::string& answer) {
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\') {
            ++i;  // пропускаем экранированный символ целиком
            continue;
        }
        if (line[i] == '=') {
            expr = line.substr(0, i);
            answer = line.substr(i + 1);
            return true;
        }
    }
    return false;
}

/// Разбор одной строки CSV по RFC 4180. Возвращает поля; удвоенная кавычка
/// внутри поля в кавычках означает одну кавычку.
std::vector<std::string> parseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    bool quoted = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cur += c;
            }
        } else if (c == '"' && cur.empty()) {
            quoted = true;
        } else if (c == ',') {
            fields.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    fields.push_back(cur);
    return fields;
}

bool endsWithCsv(const std::string& path) {
    if (path.size() < 4) return false;
    const std::string tail = path.substr(path.size() - 4);
    std::string lower;
    for (char c : tail) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower == ".csv";
}

}  // namespace

std::string unescape(const std::string& in) {
    std::string out;
    out.reserve(in.size());

    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 1 == in.size()) {
            out += in[i];
            continue;
        }
        switch (in[++i]) {
        case 'r':  out += '\r';   break;
        case 'n':  out += '\n';   break;
        case 't':  out += '\t';   break;
        case 'e':  out += '\x1b'; break;
        case '0':  out += '\0';   break;
        case '\\': out += '\\';   break;
        case '=':  out += '=';    break;
        case '"':  out += '"';    break;
        case ',':  out += ',';    break;
        default:
            // Неизвестная последовательность — оставляем как написано.
            out += '\\';
            out += in[i];
            break;
        }
    }
    return out;
}

Dictionary Dictionary::parse(const std::string& text,
                             const std::string& origin,
                             bool csv,
                             bool caseInsensitive) {
    Dictionary dict;
    std::istringstream in(text);
    std::string raw;
    std::size_t lineNo = 0;

    while (std::getline(in, raw)) {
        ++lineNo;

        const std::string line = trimRight(raw);
        const std::string probe = trimLeft(line);
        if (probe.empty() || isComment(probe)) continue;

        std::string expr;
        std::string answer;

        if (csv) {
            const auto fields = parseCsvLine(line);
            if (fields.size() < 2) {
                throw std::runtime_error(origin + ":" + std::to_string(lineNo) +
                                         ": ожидалось два поля CSV (expect,answer)");
            }
            expr = fields[0];
            // Запятые внутри незакавыченного ответа не теряем — склеиваем хвост.
            answer = fields[1];
            for (std::size_t k = 2; k < fields.size(); ++k) answer += "," + fields[k];
        } else if (!splitOnSeparator(line, expr, answer)) {
            throw std::runtime_error(origin + ":" + std::to_string(lineNo) +
                                     ": нет разделителя '=' (ожидался формат expect=answer)");
        }

        expr = trimLeft(expr);
        if (expr.empty()) {
            throw std::runtime_error(origin + ":" + std::to_string(lineNo) + ": пустой шаблон");
        }

        Rule rule;
        rule.expr = expr;
        rule.line = lineNo;
        rule.answer = unescape(answer);
        try {
            rule.pattern = Pattern::compile(expr, caseInsensitive);
        } catch (const PatternError& e) {
            throw std::runtime_error(origin + ":" + std::to_string(lineNo) + ": " + e.what());
        }

        dict.rules_.push_back(std::move(rule));
    }

    return dict;
}

Dictionary Dictionary::load(const std::string& path, bool caseInsensitive) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("не удалось открыть словарь: " + path);
    }

    std::ostringstream buf;
    buf << file.rdbuf();

    std::string text = buf.str();
    // BOM от «дружелюбных» редакторов ломает первый шаблон — срезаем.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }

    return parse(text, path, endsWithCsv(path), caseInsensitive);
}

const Rule* Dictionary::find(const std::string& command) const noexcept {
    for (const Rule& r : rules_) {
        if (r.pattern.matches(command)) return &r;
    }
    return nullptr;
}

}  // namespace ndm
