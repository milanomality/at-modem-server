// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "pattern.h"

namespace ndm {

/// Одно правило словаря: скомпилированный шаблон и готовый к отправке ответ.
struct Rule {
    Pattern pattern;
    std::string answer;   ///< escape-последовательности уже развёрнуты
    std::string expr;     ///< исходный текст шаблона (для логов и диагностики)
    std::size_t line = 0; ///< номер строки в файле
};

/// Словарь ожиданий: список правил, проверяемых сверху вниз.
/// Побеждает ПЕРВОЕ подошедшее правило, поэтому специфичные шаблоны
/// должны стоять в файле выше общих.
class Dictionary {
public:
    /// Загружает словарь. Формат определяется расширением:
    ///   *.csv          — expect,answer (RFC 4180: кавычки, "" внутри поля)
    ///   всё остальное  — expect=answer (разделитель — первый неэкранированный '=')
    /// Бросает std::runtime_error с указанием файла и строки.
    static Dictionary load(const std::string& path, bool caseInsensitive = true);

    /// Разбор из уже прочитанного текста — используется в юнит-тестах.
    static Dictionary parse(const std::string& text,
                            const std::string& origin,
                            bool csv = false,
                            bool caseInsensitive = true);

    /// Первое подошедшее правило или nullptr.
    const Rule* find(const std::string& command) const noexcept;

    const std::vector<Rule>& rules() const noexcept { return rules_; }
    std::size_t size() const noexcept { return rules_.size(); }
    bool empty() const noexcept { return rules_.empty(); }

private:
    std::vector<Rule> rules_;
};

/// Разворачивает \r \n \t \e \0 \\ \= \" \, ; неизвестные последовательности
/// остаются как есть (обратный слэш сохраняется).
std::string unescape(const std::string& in);

}  // namespace ndm
