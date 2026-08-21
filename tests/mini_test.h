// SPDX-License-Identifier: MIT
//
// Минимальный тестовый каркас: внешних зависимостей у проекта нет,
// тянуть ради двух файлов gtest/Catch2 не хотелось.
#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace mt {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry();
void reportFailure(const char* file, int line, const std::string& message);
int runAll();

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn);
};

template <typename T>
std::string show(const T& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

inline std::string show(bool v) { return v ? "true" : "false"; }

/// Строки печатаем с видимыми управляющими символами, иначе диагностика
/// «ожидалось OK, получено OK» невозможна к прочтению.
std::string show(const std::string& v);
inline std::string show(const char* v) { return show(std::string(v)); }

}  // namespace mt

#define TEST(name)                                                    \
    static void name();                                               \
    static ::mt::Registrar mt_reg_##name(#name, name);                \
    static void name()

#define CHECK(cond)                                                   \
    do {                                                              \
        if (!(cond)) ::mt::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(actual, expected)                                    \
    do {                                                              \
        const auto mt_a = (actual);                                   \
        const auto mt_e = (expected);                                 \
        if (!(mt_a == mt_e)) {                                        \
            ::mt::reportFailure(__FILE__, __LINE__,                   \
                                std::string(#actual) + ": ожидалось [" + ::mt::show(mt_e) + \
                                    "], получено [" + ::mt::show(mt_a) + "]");              \
        }                                                             \
    } while (0)

#define CHECK_THROWS(expr)                                            \
    do {                                                              \
        bool mt_thrown = false;                                       \
        try { (void)(expr); } catch (...) { mt_thrown = true; }       \
        if (!mt_thrown) ::mt::reportFailure(__FILE__, __LINE__, "ожидалось исключение: " #expr); \
    } while (0)
