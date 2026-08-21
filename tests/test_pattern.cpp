// SPDX-License-Identifier: MIT
#include <string>

#include "../src/pattern.h"
#include "mini_test.h"

using ndm::Pattern;
using ndm::PatternError;

namespace {

bool m(const std::string& expr, const std::string& text, bool ci = true) {
    return Pattern::compile(expr, ci).matches(text);
}

}  // namespace

TEST(literals_match_whole_string) {
    CHECK(m("AT", "AT"));
    CHECK(!m("AT", "ATI"));      // совпадение только полное
    CHECK(!m("ATI", "AT"));
    CHECK(!m("AT", ""));
    CHECK(m("", ""));
    CHECK(!m("", "AT"));
}

TEST(case_insensitive_by_default) {
    CHECK(m("AT+COPS?", "at+cops?"));
    CHECK(m("at+cops?", "AT+COPS?"));
    CHECK(m("AtI", "aTi"));
}

TEST(case_sensitive_mode) {
    CHECK(m("AT", "AT", false));
    CHECK(!m("AT", "at", false));
    CHECK(m("[a-z]", "q", false));
    CHECK(!m("[a-z]", "Q", false));
}

TEST(dot_is_exactly_one_character) {
    CHECK(m("AT+CFUN=.", "AT+CFUN=1"));
    CHECK(m("AT+CFUN=.", "AT+CFUN=x"));
    CHECK(!m("AT+CFUN=.", "AT+CFUN="));     // ноль символов не подходит
    CHECK(!m("AT+CFUN=.", "AT+CFUN=12"));   // два тоже
    CHECK(m("...", "abc"));
    CHECK(!m("...", "ab"));
}

// Явно зафиксированные в FAQ к заданию случаи.
TEST(star_is_zero_or_more_any_characters) {
    CHECK(m("A*E", "AE"));
    CHECK(m("A*E", "AbE"));
    CHECK(m("A*E", "AzE"));
    CHECK(m("A*E", "AbcdE"));
    CHECK(m("A*E", "AbcdxyzE"));
    CHECK(!m("A*E", "AbcdEx"));
    CHECK(!m("A*E", "A"));
}

TEST(star_edge_positions) {
    CHECK(m("*", ""));
    CHECK(m("*", "что угодно"));
    CHECK(m("AT*", "AT"));
    CHECK(m("AT*", "AT+COPS=0,0"));
    CHECK(m("*OK", "OK"));
    CHECK(m("*OK", "\r\nOK"));
    CHECK(!m("*OK", "OK\r\n"));
}

TEST(multiple_stars_collapse_and_still_match) {
    CHECK(m("***", "abc"));
    CHECK(m("a**b", "ab"));
    CHECK(m("a**b", "axxxb"));
    CHECK(m("*a*b*c*", "zzazzbzzczz"));
    CHECK(!m("*a*b*c*", "zzazzczzbzz"));   // порядок якорей важен
}

TEST(character_class_basics) {
    CHECK(m("ATE[01]", "ATE0"));
    CHECK(m("ATE[01]", "ATE1"));
    CHECK(!m("ATE[01]", "ATE2"));
    CHECK(!m("ATE[01]", "ATE"));
    CHECK(!m("ATE[01]", "ATE01"));

    // Запись из текста задания: [1,0] — это набор {'1', ',', '0'}.
    CHECK(m("ATE[1,0]", "ATE0"));
    CHECK(m("ATE[1,0]", "ATE,"));
}

TEST(character_class_ranges_and_negation) {
    CHECK(m("AT+CPIN=[0-9][0-9][0-9][0-9]", "AT+CPIN=1234"));
    CHECK(!m("AT+CPIN=[0-9][0-9][0-9][0-9]", "AT+CPIN=12345"));
    CHECK(!m("AT+CPIN=[0-9][0-9][0-9][0-9]", "AT+CPIN=12a4"));

    CHECK(m("[a-fA-F0-9]", "e"));
    CHECK(m("[a-fA-F0-9]", "7"));
    CHECK(!m("[a-fA-F0-9]", "z"));

    CHECK(m("[^0-9]", "a"));
    CHECK(!m("[^0-9]", "5"));
}

TEST(character_class_special_members) {
    CHECK(m("[]]", "]"));        // ']' первым символом — литерал
    CHECK(m("[a-]", "-"));       // '-' последним символом — литерал
    CHECK(m("[a-]", "a"));
    CHECK(m("[\\]]", "]"));      // экранированная скобка
    CHECK(m("[.*]", "*"));       // внутри класса метасимволы не действуют
    CHECK(m("[.*]", "."));
    CHECK(!m("[.*]", "x"));
}

TEST(escaping_metacharacters) {
    CHECK(m("AT\\*", "AT*"));
    CHECK(!m("AT\\*", "ATxyz"));
    CHECK(m("AT\\.", "AT."));
    CHECK(!m("AT\\.", "ATx"));
    CHECK(m("AT\\[", "AT["));
    CHECK(m("AT\\\\", "AT\\"));
    CHECK(m("AT+CPIN\\=*", "AT+CPIN=1234"));
}

TEST(invalid_patterns_are_rejected) {
    CHECK_THROWS(Pattern::compile("[abc"));
    CHECK_THROWS(Pattern::compile("AT\\"));
    CHECK_THROWS(Pattern::compile("[z-a]"));
    CHECK_THROWS(Pattern::compile("[a\\"));
}

TEST(pattern_error_reports_position) {
    try {
        Pattern::compile("AT[0-9");
        CHECK(false);
    } catch (const PatternError& e) {
        CHECK_EQ(e.position(), static_cast<std::size_t>(2));
    }
}

// Классический «катастрофический» для наивного backtracking случай.
// Алгоритм с одной точкой возврата укладывается в O(n*m) и не уходит в стек.
TEST(pathological_input_terminates) {
    const std::string text(4000, 'a');
    CHECK(!m("a*a*a*a*a*b", text));
    CHECK(m("a*a*a*a*a*a", text));
    CHECK(m(std::string(200, '*') + "a", text));
}

TEST(non_ascii_bytes_pass_through) {
    // Кириллица в UTF-8 — многобайтовая; '.' считает БАЙТЫ, а не символы.
    CHECK(m("*", "тест"));
    CHECK(m("AT+X=*", "AT+X=тест"));
    CHECK(!m("AT+X=.", "AT+X=т"));  // 'т' — два байта
    CHECK(m("AT+X=..", "AT+X=т"));
}
