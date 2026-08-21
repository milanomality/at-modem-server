// SPDX-License-Identifier: MIT
#include "modem_server.h"


#include "log.h"

namespace ndm {
namespace {

std::string toUpperAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 'a' && u <= 'z') c = static_cast<char>(u - 'a' + 'A');
    }
    return out;
}

}  // namespace

std::string normalizeCommand(const std::string& raw) {
    std::size_t beg = 0;
    std::size_t end = raw.size();
    while (beg < end && (raw[beg] == ' ' || raw[beg] == '\t')) ++beg;
    while (end > beg && (raw[end - 1] == ' ' || raw[end - 1] == '\t')) --end;
    return raw.substr(beg, end - beg);
}

ModemServer::ModemServer(SerialPort& port, const Dictionary& dictionary, ServerConfig config)
    : port_(port), dict_(dictionary), cfg_(std::move(config)) {}

void ModemServer::run(const volatile std::sig_atomic_t& stop) {
    char buf[512];

    logInfo("сервер запущен на " + port_.name() + ", правил в словаре: " +
            std::to_string(dict_.size()) + ", эхо " + (cfg_.echo ? "включено" : "выключено"));

    while (stop == 0) {
        // Таймаут нужен, чтобы регулярно проверять флаг остановки:
        // read() без него завис бы до прихода байта.
        if (!port_.waitReadable(200)) continue;

        const long n = port_.readSome(buf, sizeof(buf));
        if (n == 0) {
            logInfo("порт закрыт удалённой стороной (EOF)");
            break;
        }
        if (n < 0) continue;

        if (logLevel() == LogLevel::Debug) {
            logDebug("RX " + visualize(std::string(buf, static_cast<std::size_t>(n))));
        }

        for (long i = 0; i < n; ++i) onByte(buf[i]);
    }

    logInfo("остановка: обработано команд " + std::to_string(handled_) +
            ", без совпадения " + std::to_string(unmatched_));
}

void ModemServer::onByte(char c) {
    // S4 (LF) в потоке от терминала приходит парой к CR — как разделитель не
    // используется, иначе на каждый Enter отвечали бы дважды.
    if (c == cfg_.s4) return;

    if (c == cfg_.s3) {
        if (cfg_.echo) port_.writeAll(&c, 1);

        if (overflow_) {
            // Строка была обрезана — команду не восстановить, честно отвечаем ошибкой.
            overflow_ = false;
            line_.clear();
            sendResponse(cfg_.errorResponse);
            return;
        }

        const std::string command = normalizeCommand(line_);
        line_.clear();
        if (!command.empty()) handleCommand(command);
        return;
    }

    if (c == cfg_.s5 || c == 0x7f) {  // Backspace / DEL
        if (!line_.empty()) {
            line_.pop_back();
            if (cfg_.echo) port_.writeAll("\b \b", 3);
        }
        return;
    }

    if (line_.size() >= cfg_.maxLine) {
        overflow_ = true;  // дальше символы отбрасываем до конца строки
        return;
    }

    line_ += c;
    if (cfg_.echo) port_.writeAll(&c, 1);
}

bool ModemServer::applyStateCommand(const std::string& command) {
    const std::string up = toUpperAscii(command);

    // ATE / ATE0 / ATE1 — управление эхом. Ответ при этом всё равно берётся из
    // словаря, здесь мы меняем только состояние.
    if (up == "ATE" || up == "ATE0") {
        cfg_.echo = false;
        logDebug("эхо выключено (ATE0)");
        return true;
    }
    if (up == "ATE1") {
        cfg_.echo = true;
        logDebug("эхо включено (ATE1)");
        return true;
    }
    if (up == "ATQ" || up == "ATQ0") {
        cfg_.quiet = false;
        logDebug("выдача результатов включена (ATQ0)");
        return true;
    }
    if (up == "ATQ1") {
        cfg_.quiet = true;
        logDebug("выдача результатов подавлена (ATQ1)");
        return true;
    }
    return false;
}

void ModemServer::handleCommand(const std::string& command) {
    // A/ — повтор предыдущей команды, стандартное поведение Hayes-модема.
    if (toUpperAscii(command) == "A/") {
        if (lastCommand_.empty()) {
            sendResponse(cfg_.errorResponse);
            return;
        }
        logDebug("A/ -> повтор \"" + lastCommand_ + "\"");
        handleCommand(lastCommand_);
        return;
    }

    lastCommand_ = command;
    ++handled_;

    const bool stateChanged = applyStateCommand(command);

    if (const Rule* rule = dict_.find(command)) {
        logInfo("команда \"" + command + "\" -> правило \"" + rule->expr + "\" (строка " +
                std::to_string(rule->line) + ")");
        sendResponse(rule->answer);
        return;
    }

    if (stateChanged) {
        // Команда распознана модемом, но в словаре её нет — не отвечать ошибкой
        // на успешно применённую настройку было бы неверно.
        logWarn("команда \"" + command + "\" изменила состояние, но правила в словаре нет — OK");
        sendResponse("OK");
        return;
    }

    ++unmatched_;
    logInfo("команда \"" + command + "\" -> совпадений нет, " + cfg_.errorResponse);
    sendResponse(cfg_.errorResponse);
}

void ModemServer::sendResponse(const std::string& body) {
    if (cfg_.quiet) {
        logDebug("ATQ1: ответ подавлен");
        return;
    }

    // Формат ответа модема в verbose-режиме: <S3><S4>текст<S3><S4>.
    std::string out;
    out.reserve(body.size() + 4);
    out += cfg_.s3;
    out += cfg_.s4;
    out += body;
    out += cfg_.s3;
    out += cfg_.s4;

    if (logLevel() == LogLevel::Debug) logDebug("TX " + visualize(out));
    port_.writeAll(out);
}

}  // namespace ndm
