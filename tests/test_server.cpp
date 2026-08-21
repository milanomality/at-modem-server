// SPDX-License-Identifier: MIT
#include <string>

#include "../src/log.h"
#include "../src/modem_server.h"
#include "mini_test.h"

using ndm::normalizeCommand;
using ndm::visualize;

TEST(normalize_trims_surrounding_whitespace) {
    CHECK_EQ(normalizeCommand("  AT  "), "AT");
    CHECK_EQ(normalizeCommand("\tAT+COPS?\t"), "AT+COPS?");
    CHECK_EQ(normalizeCommand("AT"), "AT");
    CHECK_EQ(normalizeCommand("   "), "");
    CHECK_EQ(normalizeCommand(""), "");
}

TEST(normalize_keeps_inner_spaces) {
    // Внутренние пробелы значимы: "AT+CMGS=\"+7900 1234567\"" терять нельзя.
    CHECK_EQ(normalizeCommand("AT+X= a b "), "AT+X= a b");
}

TEST(visualize_renders_control_characters) {
    CHECK_EQ(visualize("AT\r\n"), "AT<CR><LF>");
    CHECK_EQ(visualize(std::string("\x01", 1)), "<01>");
    CHECK_EQ(visualize("\x1b"), "<ESC>");
    CHECK_EQ(visualize("OK"), "OK");
}
