// SPDX-License-Identifier: MIT
//
// at-modem-server — сервер, прикидывающийся AT-совместимым модемом на tty.

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <iostream>
#include <string>
#include <vector>

#include "dictionary.h"
#include "log.h"
#include "modem_server.h"
#include "serial_port.h"

namespace {

volatile std::sig_atomic_t g_stop = 0;

extern "C" void onSignal(int) { g_stop = 1; }

void installSignalHandlers() {
    struct sigaction sa {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // без SA_RESTART: poll() должен прерваться и увидеть флаг
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    // Обрыв pty на записи не должен убивать процесс — обработаем как ошибку записи.
    signal(SIGPIPE, SIG_IGN);
}

void printUsage(const char* argv0) {
    std::cout <<
        "at-modem-server — эмулятор AT-модема на tty\n"
        "\n"
        "Использование:\n"
        "  " << argv0 << " -d /dev/ttyUSB0 [-f config/modem.dict] [опции]\n"
        "  " << argv0 << " --stdio [-f config/modem.dict]\n"
        "  " << argv0 << " -f config/modem.dict --dry-run \"AT+COPS?\"\n"
        "\n"
        "Опции:\n"
        "  -d, --device PATH    tty-устройство (/dev/ttyUSB0, /dev/pts/N, ...)\n"
        "  -b, --baud RATE      скорость порта, по умолчанию 115200\n"
        "  -f, --dict PATH      словарь ожиданий, по умолчанию config/modem.dict\n"
        "                       (*.csv разбирается как expect,answer)\n"
        "      --stdio          вместо tty работать через stdin/stdout\n"
        "      --no-echo        стартовать с выключенным эхом (ATE0)\n"
        "      --case-sensitive шаблоны чувствительны к регистру\n"
        "      --cmee N         форма выдачи ошибок: 0 — ERROR (по умолчанию),\n"
        "                       1 — +CME ERROR: <код>, 2 — +CME ERROR: <текст>\n"
        "      --error TEXT     ответ на нераспознанную команду при +CMEE=0,\n"
        "                       по умолчанию ERROR\n"
        "      --dry-run CMD    проверить команду по словарю и выйти (порт не нужен)\n"
        "      --list           показать загруженные правила и выйти\n"
        "  -v, --verbose        подробный лог (побайтовый обмен)\n"
        "  -q, --quiet          только ошибки\n"
        "  -h, --help           эта справка\n"
        "      --version        версия\n";
}

/// Возвращает значение опции, требующей аргумент.
std::string requireValue(int argc, char** argv, int& i, const char* name) {
    if (i + 1 >= argc) {
        throw std::runtime_error(std::string("опция ") + name + " требует аргумент");
    }
    return argv[++i];
}

struct Options {
    std::string device;
    std::string dict = "config/modem.dict";
    unsigned baud = 115200;
    bool stdio = false;
    bool echo = true;
    bool caseInsensitive = true;
    int cmee = 0;
    std::string errorResponse = "ERROR";
    std::string dryRun;
    bool hasDryRun = false;
    bool list = false;
};

Options parseArgs(int argc, char** argv, bool& shouldExit, int& exitCode) {
    Options o;
    shouldExit = false;
    exitCode = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];

        if (a == "-h" || a == "--help") {
            printUsage(argv[0]);
            shouldExit = true;
            return o;
        }
        if (a == "--version") {
            std::cout << "at-modem-server 1.0.0\n";
            shouldExit = true;
            return o;
        }
        if (a == "-d" || a == "--device")      o.device = requireValue(argc, argv, i, "--device");
        else if (a == "-f" || a == "--dict")   o.dict = requireValue(argc, argv, i, "--dict");
        else if (a == "-b" || a == "--baud") {
            const std::string v = requireValue(argc, argv, i, "--baud");
            o.baud = static_cast<unsigned>(std::strtoul(v.c_str(), nullptr, 10));
            if (o.baud == 0) throw std::runtime_error("некорректная скорость: " + v);
        }
        else if (a == "--stdio")               o.stdio = true;
        else if (a == "--no-echo")             o.echo = false;
        else if (a == "--case-sensitive")      o.caseInsensitive = false;
        else if (a == "--cmee") {
            const std::string v = requireValue(argc, argv, i, "--cmee");
            if (v != "0" && v != "1" && v != "2") {
                throw std::runtime_error("--cmee принимает только 0, 1 или 2, получено: " + v);
            }
            o.cmee = v[0] - '0';
        }
        else if (a == "--error")               o.errorResponse = requireValue(argc, argv, i, "--error");
        else if (a == "--dry-run") {
            o.dryRun = requireValue(argc, argv, i, "--dry-run");
            o.hasDryRun = true;
        }
        else if (a == "--list")                o.list = true;
        else if (a == "-v" || a == "--verbose") ndm::setLogLevel(ndm::LogLevel::Debug);
        else if (a == "-q" || a == "--quiet")   ndm::setLogLevel(ndm::LogLevel::Error);
        else {
            printUsage(argv[0]);
            throw std::runtime_error("неизвестная опция: " + a);
        }
    }

    if (!o.stdio && o.device.empty() && !o.hasDryRun && !o.list) {
        printUsage(argv[0]);
        throw std::runtime_error("не задано устройство: укажите --device или --stdio");
    }
    if (o.stdio && !o.device.empty()) {
        throw std::runtime_error("--stdio и --device взаимоисключающие");
    }

    return o;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        bool shouldExit = false;
        int exitCode = 0;
        const Options opt = parseArgs(argc, argv, shouldExit, exitCode);
        if (shouldExit) return exitCode;

        const ndm::Dictionary dict = ndm::Dictionary::load(opt.dict, opt.caseInsensitive);
        if (dict.empty()) {
            ndm::logWarn("словарь " + opt.dict + " не содержит ни одного правила");
        }
        ndm::logInfo("словарь " + opt.dict + ": правил " + std::to_string(dict.size()));

        if (opt.list) {
            for (const ndm::Rule& r : dict.rules()) {
                std::cout << r.line << ": " << r.expr << " => " << ndm::visualize(r.answer) << "\n";
            }
            return 0;
        }

        if (opt.hasDryRun) {
            const std::string cmd = ndm::normalizeCommand(opt.dryRun);
            if (const ndm::Rule* r = dict.find(cmd)) {
                std::cout << "MATCH  \"" << cmd << "\"  <-  " << r->expr
                          << "  (строка " << r->line << ")\n"
                          << ndm::visualize(r->answer) << "\n";
                return 0;
            }
            std::cout << "NOMATCH \"" << cmd << "\"  ->  " << opt.errorResponse << "\n";
            return 1;
        }

        installSignalHandlers();

        ndm::SerialPort port;
        if (opt.stdio) port.openStdio();
        else port.openDevice(opt.device, opt.baud);

        ndm::ServerConfig cfg;
        cfg.echo = opt.echo;
        cfg.cmee = opt.cmee;
        cfg.errorResponse = opt.errorResponse;

        ndm::ModemServer server(port, dict, cfg);
        server.run(g_stop);

        port.close();
        return 0;
    } catch (const std::exception& e) {
        ndm::logError(e.what());
        return 1;
    }
}
