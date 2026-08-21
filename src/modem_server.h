// SPDX-License-Identifier: MIT
#pragma once

#include <csignal>
#include <cstddef>
#include <string>

#include "dictionary.h"
#include "serial_port.h"

namespace ndm {

/// Настройки «модема». Значения по умолчанию соответствуют типичному
/// AT-совместимому модему сразу после включения: эхо включено (ATE1),
/// результаты выдаются (ATQ0), терминаторы — CR/LF (S3=13, S4=10, S5=8).
struct ServerConfig {
    bool echo = true;
    bool quiet = false;
    char s3 = '\r';
    char s4 = '\n';
    char s5 = '\b';
    std::string errorResponse = "ERROR";
    std::size_t maxLine = 1024;
};

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
    void onByte(char c);
    void handleCommand(const std::string& command);
    void sendResponse(const std::string& body);
    /// Обрабатывает команды, меняющие состояние модема (ATE, ATQ).
    /// Возвращает true, если команда распознана как таковая.
    bool applyStateCommand(const std::string& command);

    SerialPort& port_;
    const Dictionary& dict_;
    ServerConfig cfg_;

    std::string line_;
    std::string lastCommand_;  ///< для A/ — повтор последней команды
    bool overflow_ = false;
    std::size_t handled_ = 0;
    std::size_t unmatched_ = 0;
};

/// Приводит команду к каноническому виду: убирает пробелы по краям.
std::string normalizeCommand(const std::string& raw);

}  // namespace ndm
