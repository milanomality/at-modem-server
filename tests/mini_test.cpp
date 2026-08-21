// SPDX-License-Identifier: MIT
#include "mini_test.h"

#include <cstdio>
#include <string>

namespace mt {
namespace {

int g_failuresInCurrent = 0;

}  // namespace

std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

Registrar::Registrar(const std::string& name, std::function<void()> fn) {
    registry().push_back(TestCase{name, std::move(fn)});
}

void reportFailure(const char* file, int line, const std::string& message) {
    ++g_failuresInCurrent;
    std::printf("    %s:%d: %s\n", file, line, message.c_str());
}

std::string show(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (unsigned char c : v) {
        switch (c) {
        case '\r': out += "\\r"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\x%02X", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

int runAll() {
    int failedTests = 0;

    // Пустой реестр означал бы «0 провалено» и зелёный прогон на пустом месте.
    if (registry().empty()) {
        std::printf("НИ ОДНОГО ТЕСТА НЕ ЗАРЕГИСТРИРОВАНО\n");
        return 1;
    }

    for (const TestCase& tc : registry()) {
        g_failuresInCurrent = 0;
        std::printf("[ RUN  ] %s\n", tc.name.c_str());
        try {
            tc.fn();
        } catch (const std::exception& e) {
            reportFailure("<test>", 0, std::string("неожиданное исключение: ") + e.what());
        } catch (...) {
            reportFailure("<test>", 0, "неожиданное исключение неизвестного типа");
        }

        if (g_failuresInCurrent == 0) {
            std::printf("[  OK  ] %s\n", tc.name.c_str());
        } else {
            std::printf("[ FAIL ] %s (%d)\n", tc.name.c_str(), g_failuresInCurrent);
            ++failedTests;
        }
    }

    std::printf("\nВсего тестов: %zu, провалено: %d\n", registry().size(), failedTests);
    return failedTests == 0 ? 0 : 1;
}

}  // namespace mt

int main() { return mt::runAll(); }
