// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <csignal>
#include <cstddef>
#include <string>
#include <vector>

#include "dictionary.h"
#include "serial_port.h"

namespace ndm {

/// Настройки «модема». Значения по умолчанию соответствуют типичному
/// AT-совместимому модему сразу после включения: эхо включено (ATE1),
/// результаты выдаются (ATQ0), ошибки краткие (AT+CMEE=0),
/// терминаторы — CR/LF (S3=13, S4=10, S5=8).
struct ServerConfig {
    bool echo = true;
    bool quiet = false;
    int cmee = 0;   ///< 0 — ERROR, 1 — числовой +CME ERROR, 2 — текстовый
    char s3 = '\r';
    char s4 = '\n';
    char s5 = '\b';
    std::string errorResponse = "ERROR";
    std::size_t maxLine = 1024;
};

/// Ответ, разделённый на информационную часть и финальный код результата.
struct AnswerParts {
    std::string info;
    std::string result;
};

/// Результат разбора S-регистровой команды ATS<n>? / ATS<n>=<v>.
enum class SRegParse { NotSRegister, Invalid, Query, Assign };

/// Читает поток с tty, собирает командные строки и отвечает по словарю.
class ModemServer {
public:
    ModemServer(SerialPort& port, const Dictionary& dictionary, ServerConfig config);

    /// Основной цикл. Завершается, когда stop становится ненулевым либо
    /// когда на стороне порта наступает EOF.
    void run(const volatile std::sig_atomic_t& stop);

    std::size_t handledCommands() const noexcept { return handled_; }
    std::size_t unmatchedCommands() const noexcept { return unmatched_; }

private:
    /// Что сделала с командой машина состояний модема.
    struct StateAction {
        enum class Kind {
            None,     ///< не наша команда, идём в словарь
            Applied,  ///< состояние изменено, ответ берём из словаря
            Answer,   ///< ответ сформирован здесь (динамическое чтение)
            Error     ///< команда опознана, но параметр неверен
        };
        Kind kind = Kind::None;
        std::string answer;
        int errorCode = 50;  ///< «incorrect parameters» по 3GPP TS 27.007
    };

    void onByte(char c);
    void handleCommand(const std::string& command);
    /// Вычисляет тело ответа на ОДНУ команду, ничего не отправляя.
    std::string evaluate(const std::string& command);
    void sendResponse(const std::string& body);

    StateAction applyStateCommand(const std::string& command);
    std::string expand(const std::string& body) const;
    std::string errorFor(int cmeCode) const;

    char s3() const noexcept { return static_cast<char>(sreg_[3]); }
    char s4() const noexcept { return static_cast<char>(sreg_[4]); }
    char s5() const noexcept { return static_cast<char>(sreg_[5]); }
    /// Разделитель между блоками ответа: <S3><S4><S3><S4>.
    std::string blockSeparator() const;

    SerialPort& port_;
    const Dictionary& dict_;
    ServerConfig cfg_;

    std::array<unsigned char, 256> sreg_{};
    int cmee_ = 0;

    std::string line_;
    std::string lastCommand_;  ///< для A/ — повтор последней команды
    bool overflow_ = false;
    std::size_t handled_ = 0;
    std::size_t unmatched_ = 0;
};

/// Приводит команду к каноническому виду: убирает пробелы по краям.
std::string normalizeCommand(const std::string& raw);

/// Делит составную строку "AT+CSQ;+CREG?" на отдельные команды, дописывая
/// префикс AT ко второй и последующим. Точка с запятой внутри двойных
/// кавычек разделителем не считается. Для строки без ';' возвращает
/// один элемент.
std::vector<std::string> splitCommandChain(const std::string& command);

/// Отделяет финальный код результата (OK, ERROR, +CME ERROR: ... ) от
/// информационной части ответа. Если финального кода нет, result пуст.
AnswerParts splitFinalResult(const std::string& body, char s3, char s4);

/// true, если строка — финальный код результата AT-протокола.
bool isFinalResultCode(const std::string& line);

/// Текстовые расшифровки кодов ошибок 3GPP TS 27.007 (+CME) и 27.005 (+CMS).
std::string cmeErrorText(int code);
std::string cmsErrorText(int code);

/// Разворачивает макросы %CME <код>% и %CMS <код>% по текущему режиму +CMEE:
/// 0 — plainError, 1 — числовой код, 2 — текстовая расшифровка.
/// %% превращается в литеральный '%', неизвестные макросы остаются как есть.
std::string expandErrorMacros(const std::string& body, int cmee, const std::string& plainError);

/// Разбирает ATS<n>? и ATS<n>=<v>. Команда должна быть в верхнем регистре.
SRegParse parseSRegister(const std::string& upper, int& index, int& value);

}  // namespace ndm
