// SPDX-License-Identifier: MIT
#include "modem_server.h"

#include <cstdio>

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

bool startsWithAt(const std::string& s) {
    if (s.size() < 2) return false;
    const auto a = static_cast<unsigned char>(s[0]);
    const auto t = static_cast<unsigned char>(s[1]);
    return (a == 'A' || a == 'a') && (t == 'T' || t == 't');
}

bool hasPrefix(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

/// Собирает "+CME ERROR: ..." / "+CMS ERROR: ..." по текущему режиму +CMEE.
std::string renderError(const char* kind,
                        int code,
                        int cmee,
                        const std::string& plainError,
                        const std::string& verbose) {
    if (cmee <= 0) return plainError;
    const std::string head = std::string("+") + kind + " ERROR: ";
    if (cmee == 1) return head + std::to_string(code);
    return head + verbose;
}

/// Разбирает тело макроса вида "CME 16". Возвращает false, если это не он.
bool parseErrorMacro(const std::string& macro, std::string& kind, int& code) {
    const std::string up = toUpperAscii(macro);
    if (hasPrefix(up, "CME")) kind = "CME";
    else if (hasPrefix(up, "CMS")) kind = "CMS";
    else return false;

    std::size_t i = 3;
    while (i < up.size() && (up[i] == ' ' || up[i] == ':')) ++i;
    if (i == up.size()) return false;

    long n = 0;
    std::size_t digits = 0;
    while (i < up.size() && up[i] >= '0' && up[i] <= '9') {
        n = n * 10 + (up[i] - '0');
        if (n > 99999) return false;
        ++i;
        ++digits;
    }
    if (digits == 0 || i != up.size()) return false;

    code = static_cast<int>(n);
    return true;
}

}  // namespace

std::string normalizeCommand(const std::string& raw) {
    std::size_t beg = 0;
    std::size_t end = raw.size();
    while (beg < end && (raw[beg] == ' ' || raw[beg] == '\t')) ++beg;
    while (end > beg && (raw[end - 1] == ' ' || raw[end - 1] == '\t')) --end;
    return raw.substr(beg, end - beg);
}

std::vector<std::string> splitCommandChain(const std::string& command) {
    std::vector<std::string> raw;
    std::string cur;
    bool inQuotes = false;

    for (char c : command) {
        if (c == '"') {
            inQuotes = !inQuotes;
            cur += c;
            continue;
        }
        // Точка с запятой внутри кавычек — часть параметра (номер, текст SMS),
        // а не разделитель команд.
        if (c == ';' && !inQuotes) {
            raw.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    raw.push_back(cur);

    // Префикс AT пишется только перед первой командой строки; берём его
    // в том регистре, в каком набрал клиент.
    std::string prefix = "AT";
    if (startsWithAt(command)) prefix = command.substr(0, 2);

    std::vector<std::string> parts;
    for (std::size_t k = 0; k < raw.size(); ++k) {
        std::string p = normalizeCommand(raw[k]);
        if (p.empty()) continue;
        if (k > 0 && !startsWithAt(p)) p = prefix + p;
        parts.push_back(std::move(p));
    }
    return parts;
}

bool isFinalResultCode(const std::string& line) {
    static const char* const kCodes[] = {
        "OK", "ERROR", "NO CARRIER", "BUSY", "NO DIALTONE", "NO ANSWER", "CONNECT",
    };
    for (const char* code : kCodes) {
        if (line == code) return true;
    }
    return hasPrefix(line, "CONNECT ") || hasPrefix(line, "+CME ERROR:") ||
           hasPrefix(line, "+CMS ERROR:");
}

AnswerParts splitFinalResult(const std::string& body, char s3, char s4) {
    AnswerParts out;

    std::string text = body;
    while (!text.empty() && (text.back() == s3 || text.back() == s4)) text.pop_back();

    const std::string terminators = std::string() + s3 + s4;
    const std::size_t cut = text.find_last_of(terminators);
    const std::string last = (cut == std::string::npos) ? text : text.substr(cut + 1);

    if (!isFinalResultCode(last)) {
        out.info = text;
        return out;
    }

    out.result = last;
    if (cut != std::string::npos) {
        std::string head = text.substr(0, cut);
        while (!head.empty() && (head.back() == s3 || head.back() == s4)) head.pop_back();
        out.info = head;
    }
    return out;
}

std::string cmeErrorText(int code) {
    switch (code) {
    case 0:   return "phone failure";
    case 1:   return "no connection to phone";
    case 2:   return "phone-adaptor link reserved";
    case 3:   return "operation not allowed";
    case 4:   return "operation not supported";
    case 5:   return "PH-SIM PIN required";
    case 10:  return "SIM not inserted";
    case 11:  return "SIM PIN required";
    case 12:  return "SIM PUK required";
    case 13:  return "SIM failure";
    case 14:  return "SIM busy";
    case 15:  return "SIM wrong";
    case 16:  return "incorrect password";
    case 17:  return "SIM PIN2 required";
    case 18:  return "SIM PUK2 required";
    case 20:  return "memory full";
    case 21:  return "invalid index";
    case 22:  return "not found";
    case 23:  return "memory failure";
    case 24:  return "text string too long";
    case 25:  return "invalid characters in text string";
    case 26:  return "dial string too long";
    case 27:  return "invalid characters in dial string";
    case 30:  return "no network service";
    case 31:  return "network timeout";
    case 32:  return "network not allowed - emergency calls only";
    case 50:  return "incorrect parameters";
    case 100: return "unknown";
    default:  return "unknown";
    }
}

std::string cmsErrorText(int code) {
    switch (code) {
    case 300: return "ME failure";
    case 301: return "SMS service of ME reserved";
    case 302: return "operation not allowed";
    case 303: return "operation not supported";
    case 304: return "invalid PDU mode parameter";
    case 305: return "invalid text mode parameter";
    case 310: return "SIM not inserted";
    case 311: return "SIM PIN required";
    case 313: return "SIM failure";
    case 314: return "SIM busy";
    case 315: return "SIM wrong";
    case 316: return "SIM PUK required";
    case 317: return "SIM PIN2 required";
    case 318: return "SIM PUK2 required";
    case 320: return "memory failure";
    case 321: return "invalid memory index";
    case 322: return "memory full";
    case 330: return "SMSC address unknown";
    case 331: return "no network service";
    case 332: return "network timeout";
    case 500: return "unknown error";
    default:  return "unknown error";
    }
}

std::string expandErrorMacros(const std::string& body, int cmee, const std::string& plainError) {
    if (body.find('%') == std::string::npos) return body;

    std::string out;
    out.reserve(body.size());

    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] != '%') {
            out += body[i];
            continue;
        }
        if (i + 1 < body.size() && body[i + 1] == '%') {  // %% — литеральный процент
            out += '%';
            ++i;
            continue;
        }

        const std::size_t end = body.find('%', i + 1);
        if (end == std::string::npos) {
            out += body[i];
            continue;
        }

        std::string kind;
        int code = 0;
        if (!parseErrorMacro(body.substr(i + 1, end - i - 1), kind, code)) {
            out += body[i];  // не наш макрос — оставляем как написано
            continue;
        }

        const std::string verbose = (kind == "CME") ? cmeErrorText(code) : cmsErrorText(code);
        out += renderError(kind.c_str(), code, cmee, plainError, verbose);
        i = end;
    }
    return out;
}

SRegParse parseSRegister(const std::string& upper, int& index, int& value) {
    if (!hasPrefix(upper, "ATS")) return SRegParse::NotSRegister;

    std::size_t i = 3;
    long reg = 0;
    std::size_t digits = 0;
    while (i < upper.size() && upper[i] >= '0' && upper[i] <= '9') {
        reg = reg * 10 + (upper[i] - '0');
        if (reg > 9999) return SRegParse::Invalid;
        ++i;
        ++digits;
    }
    if (digits == 0) return SRegParse::NotSRegister;  // "ATSOMETHING" — не про регистры
    if (reg > 255) return SRegParse::Invalid;
    index = static_cast<int>(reg);

    if (i == upper.size()) return SRegParse::Invalid;  // "ATS3" без ? и =
    if (upper[i] == '?') {
        return (i + 1 == upper.size()) ? SRegParse::Query : SRegParse::Invalid;
    }
    if (upper[i] != '=') return SRegParse::Invalid;

    ++i;
    long v = 0;
    std::size_t vdigits = 0;
    while (i < upper.size() && upper[i] >= '0' && upper[i] <= '9') {
        v = v * 10 + (upper[i] - '0');
        if (v > 9999) return SRegParse::Invalid;
        ++i;
        ++vdigits;
    }
    if (vdigits == 0 || i != upper.size() || v > 255) return SRegParse::Invalid;

    value = static_cast<int>(v);
    return SRegParse::Assign;
}

// ---------------------------------------------------------------------------

ModemServer::ModemServer(SerialPort& port, const Dictionary& dictionary, ServerConfig config)
    : port_(port), dict_(dictionary), cfg_(std::move(config)) {
    cmee_ = cfg_.cmee;

    // Значения S-регистров по умолчанию (Hayes / 3GPP TS 27.007).
    sreg_[0] = 0;    // автоответ выключен
    sreg_[3] = static_cast<unsigned char>(cfg_.s3);
    sreg_[4] = static_cast<unsigned char>(cfg_.s4);
    sreg_[5] = static_cast<unsigned char>(cfg_.s5);
    sreg_[6] = 2;    // пауза перед набором, с
    sreg_[7] = 50;   // ожидание несущей, с
    sreg_[8] = 2;    // пауза на запятую в номере, с
    sreg_[10] = 14;  // задержка отбоя после потери несущей, десятые доли с
    sreg_[12] = 50;  // защитный интервал escape-последовательности
}

std::string ModemServer::blockSeparator() const {
    std::string sep;
    sep += s3();
    sep += s4();
    sep += s3();
    sep += s4();
    return sep;
}

std::string ModemServer::expand(const std::string& body) const {
    return expandErrorMacros(body, cmee_, cfg_.errorResponse);
}

std::string ModemServer::errorFor(int cmeCode) const {
    return renderError("CME", cmeCode, cmee_, cfg_.errorResponse, cmeErrorText(cmeCode));
}

void ModemServer::run(const volatile std::sig_atomic_t& stop) {
    char buf[512];

    logInfo("сервер запущен на " + port_.name() + ", правил в словаре: " +
            std::to_string(dict_.size()) + ", эхо " + (cfg_.echo ? "включено" : "выключено") +
            ", +CMEE=" + std::to_string(cmee_));

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
    if (c == s4()) return;

    if (c == s3()) {
        if (cfg_.echo) port_.writeAll(&c, 1);

        if (overflow_) {
            // Строка была обрезана — команду не восстановить, честно отвечаем ошибкой.
            overflow_ = false;
            line_.clear();
            sendResponse(errorFor(4));
            return;
        }

        const std::string command = normalizeCommand(line_);
        line_.clear();
        if (!command.empty()) handleCommand(command);
        return;
    }

    if (c == s5() || c == 0x7f) {  // Backspace / DEL
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

ModemServer::StateAction ModemServer::applyStateCommand(const std::string& command) {
    StateAction action;
    const std::string up = toUpperAscii(command);

    // ATE / ATE0 / ATE1 — управление эхом. Ответ при этом всё равно берётся из
    // словаря, здесь мы меняем только состояние.
    if (up == "ATE" || up == "ATE0") {
        cfg_.echo = false;
        logDebug("эхо выключено (ATE0)");
        action.kind = StateAction::Kind::Applied;
        return action;
    }
    if (up == "ATE1") {
        cfg_.echo = true;
        logDebug("эхо включено (ATE1)");
        action.kind = StateAction::Kind::Applied;
        return action;
    }
    if (up == "ATQ" || up == "ATQ0") {
        cfg_.quiet = false;
        logDebug("выдача результатов включена (ATQ0)");
        action.kind = StateAction::Kind::Applied;
        return action;
    }
    if (up == "ATQ1") {
        cfg_.quiet = true;
        logDebug("выдача результатов подавлена (ATQ1)");
        action.kind = StateAction::Kind::Applied;
        return action;
    }

    // AT+CMEE — форма выдачи ошибок.
    if (up == "AT+CMEE?") {
        action.kind = StateAction::Kind::Answer;
        action.answer = "+CMEE: " + std::to_string(cmee_) + blockSeparator() + "OK";
        return action;
    }
    if (up == "AT+CMEE=?") {
        action.kind = StateAction::Kind::Answer;
        action.answer = "+CMEE: (0-2)" + blockSeparator() + "OK";
        return action;
    }
    if (hasPrefix(up, "AT+CMEE=")) {
        const std::string arg = up.substr(8);
        if (arg == "0" || arg == "1" || arg == "2") {
            cmee_ = arg[0] - '0';
            logDebug("режим ошибок +CMEE=" + arg);
            action.kind = StateAction::Kind::Applied;
        } else {
            action.kind = StateAction::Kind::Error;
        }
        return action;
    }

    // S-регистры: ATS<n>? и ATS<n>=<v>.
    int index = 0;
    int value = 0;
    switch (parseSRegister(up, index, value)) {
    case SRegParse::Query: {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%03u", static_cast<unsigned>(sreg_[static_cast<std::size_t>(index)]));
        action.kind = StateAction::Kind::Answer;
        action.answer = std::string(buf) + blockSeparator() + "OK";
        return action;
    }
    case SRegParse::Assign:
        sreg_[static_cast<std::size_t>(index)] = static_cast<unsigned char>(value);
        logDebug("S" + std::to_string(index) + "=" + std::to_string(value));
        action.kind = StateAction::Kind::Applied;
        return action;
    case SRegParse::Invalid:
        action.kind = StateAction::Kind::Error;
        return action;
    case SRegParse::NotSRegister:
        break;
    }

    return action;  // Kind::None
}

std::string ModemServer::evaluate(const std::string& command) {
    ++handled_;

    const StateAction state = applyStateCommand(command);

    if (state.kind == StateAction::Kind::Error) {
        logInfo("команда \"" + command + "\" -> недопустимый параметр");
        return errorFor(state.errorCode);
    }
    if (state.kind == StateAction::Kind::Answer) {
        return expand(state.answer);
    }

    if (const Rule* rule = dict_.find(command)) {
        logInfo("команда \"" + command + "\" -> правило \"" + rule->expr + "\" (строка " +
                std::to_string(rule->line) + ")");
        return expand(rule->answer);
    }

    if (state.kind == StateAction::Kind::Applied) {
        // Команда распознана модемом, но в словаре её нет — не отвечать ошибкой
        // на успешно применённую настройку было бы неверно.
        logDebug("команда \"" + command + "\" изменила состояние, правила в словаре нет — OK");
        return "OK";
    }

    ++unmatched_;
    logInfo("команда \"" + command + "\" -> совпадений нет");
    return errorFor(4);  // operation not supported
}

void ModemServer::handleCommand(const std::string& command) {
    // A/ — повтор предыдущей команды, стандартное поведение Hayes-модема.
    if (toUpperAscii(command) == "A/") {
        if (lastCommand_.empty()) {
            sendResponse(errorFor(4));
            return;
        }
        logDebug("A/ -> повтор \"" + lastCommand_ + "\"");
        handleCommand(lastCommand_);
        return;
    }

    lastCommand_ = command;

    const std::vector<std::string> parts = splitCommandChain(command);
    if (parts.empty()) return;
    if (parts.size() == 1) {
        // Одиночная команда — ответ уходит ровно так, как записан в словаре.
        sendResponse(evaluate(parts[0]));
        return;
    }

    // Составная строка: информационные ответы идут подряд, финальный код
    // результата один на всю строку. Обработка прекращается на первой ошибке —
    // так же ведёт себя настоящий модем.
    const std::string sep = blockSeparator();
    std::string info;
    std::string result = "OK";

    for (const std::string& part : parts) {
        const AnswerParts answer = splitFinalResult(evaluate(part), s3(), s4());
        if (!answer.info.empty()) {
            if (!info.empty()) info += sep;
            info += answer.info;
        }
        if (!answer.result.empty() && answer.result != "OK") {
            result = answer.result;
            logDebug("составная строка прервана на \"" + part + "\": " + result);
            break;
        }
    }

    sendResponse(info.empty() ? result : info + sep + result);
}

void ModemServer::sendResponse(const std::string& body) {
    if (cfg_.quiet) {
        logDebug("ATQ1: ответ подавлен");
        return;
    }

    // Формат ответа модема в verbose-режиме: <S3><S4>текст<S3><S4>.
    std::string out;
    out.reserve(body.size() + 4);
    out += s3();
    out += s4();
    out += body;
    out += s3();
    out += s4();

    if (logLevel() == LogLevel::Debug) logDebug("TX " + visualize(out));
    port_.writeAll(out);
}

}  // namespace ndm
