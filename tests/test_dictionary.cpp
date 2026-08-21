// SPDX-License-Identifier: MIT
#include <string>

#include "../src/dictionary.h"
#include "mini_test.h"

using ndm::Dictionary;
using ndm::Rule;

namespace {

Dictionary parse(const std::string& text, bool csv = false) {
    return Dictionary::parse(text, "<test>", csv);
}

std::string answerFor(const Dictionary& d, const std::string& cmd) {
    const Rule* r = d.find(cmd);
    return r ? r->answer : std::string("<нет совпадения>");
}

}  // namespace

TEST(parses_basic_expect_answer) {
    const Dictionary d = parse("AT=OK\nATI=NDM\n");
    CHECK_EQ(d.size(), static_cast<std::size_t>(2));
    CHECK_EQ(answerFor(d, "AT"), "OK");
    CHECK_EQ(answerFor(d, "ATI"), "NDM");
    CHECK(d.find("AT+COPS") == nullptr);
}

TEST(skips_comments_and_blank_lines) {
    const Dictionary d = parse(
        "# комментарий\n"
        "\n"
        "   \n"
        "  # с отступом\n"
        "; тоже комментарий\n"
        "AT=OK\n");
    CHECK_EQ(d.size(), static_cast<std::size_t>(1));
    CHECK_EQ(d.rules()[0].line, static_cast<std::size_t>(6));
}

TEST(first_matching_rule_wins) {
    const Dictionary d = parse(
        "AT+CPIN\\=[0-9][0-9][0-9][0-9]=OK\n"
        "AT+CPIN\\=*=+CME ERROR: incorrect password\n");
    CHECK_EQ(answerFor(d, "AT+CPIN=1234"), "OK");
    CHECK_EQ(answerFor(d, "AT+CPIN=abc"), "+CME ERROR: incorrect password");
}

TEST(separator_is_first_unescaped_equals) {
    // Шаблон содержит '=', экранированный как \=; ответ тоже содержит '='.
    const Dictionary d = parse("AT+COPS\\=?=+COPS: mode=0\n");
    CHECK_EQ(d.size(), static_cast<std::size_t>(1));
    CHECK_EQ(d.rules()[0].expr, "AT+COPS\\=?");
    CHECK_EQ(answerFor(d, "AT+COPS=?"), "+COPS: mode=0");
    CHECK(d.find("AT+COPS?") == nullptr);
}

TEST(escapes_in_answer_are_expanded) {
    const Dictionary d = parse("ATI=line1\\r\\nline2\\r\\n\\r\\nOK\n");
    CHECK_EQ(answerFor(d, "ATI"), "line1\r\nline2\r\n\r\nOK");
}

TEST(unknown_escape_is_kept_verbatim) {
    CHECK_EQ(ndm::unescape("a\\qb"), "a\\qb");
    CHECK_EQ(ndm::unescape("tail\\"), "tail\\");
    CHECK_EQ(ndm::unescape("\\\\"), "\\");
    CHECK_EQ(ndm::unescape("\\e"), "\x1b");
}

TEST(empty_answer_is_allowed) {
    const Dictionary d = parse("ATQ1=\n");
    CHECK_EQ(d.size(), static_cast<std::size_t>(1));
    CHECK_EQ(answerFor(d, "ATQ1"), "");
}

TEST(trailing_whitespace_and_crlf_are_trimmed) {
    const Dictionary d = parse("AT=OK   \r\nATI=NDM\r\n");
    CHECK_EQ(answerFor(d, "AT"), "OK");
    CHECK_EQ(answerFor(d, "ATI"), "NDM");
}

TEST(malformed_lines_are_reported) {
    CHECK_THROWS(parse("AT\n"));          // нет разделителя
    CHECK_THROWS(parse("=OK\n"));         // пустой шаблон
    CHECK_THROWS(parse("AT[0-9=OK\n"));   // битый шаблон
}

TEST(error_message_carries_origin_and_line) {
    try {
        parse("AT=OK\nбез разделителя\n");
        CHECK(false);
    } catch (const std::exception& e) {
        const std::string msg = e.what();
        CHECK(msg.find("<test>:2") != std::string::npos);
    }
}

TEST(csv_format_with_quotes_and_commas) {
    const Dictionary d = parse(
        "AT,OK\n"
        "AT+CSQ,\"+CSQ: 21,99\\r\\n\\r\\nOK\"\n"
        "AT+COPS?,\"+COPS: 0,0,\"\"MegaFon\"\",7\"\n"
        "AT+CPIN=*,OK\n",
        /*csv=*/true);

    CHECK_EQ(d.size(), static_cast<std::size_t>(4));
    CHECK_EQ(answerFor(d, "AT"), "OK");
    CHECK_EQ(answerFor(d, "AT+CSQ"), "+CSQ: 21,99\r\n\r\nOK");
    CHECK_EQ(answerFor(d, "AT+COPS?"), "+COPS: 0,0,\"MegaFon\",7");
    // В CSV '=' в шаблоне экранировать не нужно — разделитель другой.
    CHECK_EQ(answerFor(d, "AT+CPIN=9999"), "OK");
}

TEST(csv_unquoted_answer_keeps_commas) {
    const Dictionary d = parse("AT+X,a,b,c\n", /*csv=*/true);
    CHECK_EQ(answerFor(d, "AT+X"), "a,b,c");
}

TEST(case_sensitivity_is_configurable) {
    const Dictionary ci = Dictionary::parse("AT=OK\n", "<test>", false, true);
    CHECK_EQ(answerFor(ci, "at"), "OK");

    const Dictionary cs = Dictionary::parse("AT=OK\n", "<test>", false, false);
    CHECK(cs.find("at") == nullptr);
    CHECK_EQ(answerFor(cs, "AT"), "OK");
}
