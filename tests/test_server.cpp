// SPDX-License-Identifier: MIT
#include <string>
#include <vector>

#include "../src/log.h"
#include "../src/modem_server.h"
#include "mini_test.h"

using ndm::AnswerParts;
using ndm::cmeErrorText;
using ndm::cmsErrorText;
using ndm::expandErrorMacros;
using ndm::isFinalResultCode;
using ndm::normalizeCommand;
using ndm::parseSRegister;
using ndm::SRegParse;
using ndm::splitCommandChain;
using ndm::splitFinalResult;
using ndm::visualize;

namespace {

AnswerParts split(const std::string& body) { return splitFinalResult(body, '\r', '\n'); }

std::string expand(const std::string& body, int cmee) {
    return expandErrorMacros(body, cmee, "ERROR");
}

}  // namespace

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

// --------------------------------------------------------- составные команды

TEST(chain_split_single_command) {
    const auto parts = splitCommandChain("AT+CSQ");
    CHECK_EQ(parts.size(), static_cast<std::size_t>(1));
    CHECK_EQ(parts[0], "AT+CSQ");
}

TEST(chain_split_adds_at_prefix_to_following_commands) {
    const auto parts = splitCommandChain("AT+CSQ;+CREG?;+CPIN?");
    CHECK_EQ(parts.size(), static_cast<std::size_t>(3));
    CHECK_EQ(parts[0], "AT+CSQ");
    CHECK_EQ(parts[1], "AT+CREG?");
    CHECK_EQ(parts[2], "AT+CPIN?");
}

TEST(chain_split_preserves_prefix_case) {
    const auto parts = splitCommandChain("at+csq;+creg?");
    CHECK_EQ(parts.size(), static_cast<std::size_t>(2));
    CHECK_EQ(parts[1], "at+creg?");
}

TEST(chain_split_does_not_double_prefix) {
    const auto parts = splitCommandChain("AT+CSQ;AT+CREG?");
    CHECK_EQ(parts.size(), static_cast<std::size_t>(2));
    CHECK_EQ(parts[1], "AT+CREG?");
}

TEST(chain_split_ignores_semicolon_inside_quotes) {
    // Точка с запятой встречается в параметрах: номера, тексты SMS.
    const auto parts = splitCommandChain("AT+CMGS=\"+7900;123\"");
    CHECK_EQ(parts.size(), static_cast<std::size_t>(1));
    CHECK_EQ(parts[0], "AT+CMGS=\"+7900;123\"");

    const auto mixed = splitCommandChain("AT+CMGS=\"a;b\";+CSQ");
    CHECK_EQ(mixed.size(), static_cast<std::size_t>(2));
    CHECK_EQ(mixed[0], "AT+CMGS=\"a;b\"");
    CHECK_EQ(mixed[1], "AT+CSQ");
}

TEST(chain_split_drops_empty_segments) {
    const auto parts = splitCommandChain("AT+CSQ;;");
    CHECK_EQ(parts.size(), static_cast<std::size_t>(1));
    CHECK_EQ(parts[0], "AT+CSQ");

    CHECK_EQ(splitCommandChain("").size(), static_cast<std::size_t>(0));
    CHECK_EQ(splitCommandChain(";").size(), static_cast<std::size_t>(0));
}

TEST(chain_split_trims_spaces_around_segments) {
    const auto parts = splitCommandChain("AT+CSQ ; +CREG?");
    CHECK_EQ(parts.size(), static_cast<std::size_t>(2));
    CHECK_EQ(parts[0], "AT+CSQ");
    CHECK_EQ(parts[1], "AT+CREG?");
}

// -------------------------------------------- разделение ответа и кода итога

TEST(final_result_codes_are_recognized) {
    CHECK(isFinalResultCode("OK"));
    CHECK(isFinalResultCode("ERROR"));
    CHECK(isFinalResultCode("NO CARRIER"));
    CHECK(isFinalResultCode("BUSY"));
    CHECK(isFinalResultCode("CONNECT"));
    CHECK(isFinalResultCode("CONNECT 115200"));
    CHECK(isFinalResultCode("+CME ERROR: 16"));
    CHECK(isFinalResultCode("+CMS ERROR: 500"));
    CHECK(!isFinalResultCode("+CSQ: 21,99"));
    CHECK(!isFinalResultCode(""));
    CHECK(!isFinalResultCode("OKAY"));
}

TEST(split_answer_into_info_and_result) {
    const AnswerParts a = split("+CSQ: 21,99\r\n\r\nOK");
    CHECK_EQ(a.info, "+CSQ: 21,99");
    CHECK_EQ(a.result, "OK");
}

TEST(split_answer_without_information_part) {
    const AnswerParts a = split("OK");
    CHECK_EQ(a.info, "");
    CHECK_EQ(a.result, "OK");

    const AnswerParts e = split("+CME ERROR: incorrect password");
    CHECK_EQ(e.info, "");
    CHECK_EQ(e.result, "+CME ERROR: incorrect password");
}

TEST(split_answer_without_result_code) {
    const AnswerParts a = split("+CSQ: 21,99");
    CHECK_EQ(a.info, "+CSQ: 21,99");
    CHECK_EQ(a.result, "");
}

TEST(split_answer_keeps_multiline_information) {
    const AnswerParts a = split("NDM Systems\r\nNDM-LTE-1000\r\n\r\nOK");
    CHECK_EQ(a.info, "NDM Systems\r\nNDM-LTE-1000");
    CHECK_EQ(a.result, "OK");
}

TEST(split_answer_tolerates_trailing_terminators) {
    const AnswerParts a = split("+CSQ: 21,99\r\n\r\nOK\r\n");
    CHECK_EQ(a.info, "+CSQ: 21,99");
    CHECK_EQ(a.result, "OK");
}

// ---------------------------------------------------------- коды ошибок CME

TEST(cme_and_cms_error_tables) {
    CHECK_EQ(cmeErrorText(16), "incorrect password");
    CHECK_EQ(cmeErrorText(4), "operation not supported");
    CHECK_EQ(cmeErrorText(11), "SIM PIN required");
    CHECK_EQ(cmeErrorText(50), "incorrect parameters");
    CHECK_EQ(cmeErrorText(31337), "unknown");

    CHECK_EQ(cmsErrorText(322), "memory full");
    CHECK_EQ(cmsErrorText(500), "unknown error");
    CHECK_EQ(cmsErrorText(31337), "unknown error");
}

TEST(cme_macro_follows_cmee_mode) {
    CHECK_EQ(expand("%CME 16%", 0), "ERROR");
    CHECK_EQ(expand("%CME 16%", 1), "+CME ERROR: 16");
    CHECK_EQ(expand("%CME 16%", 2), "+CME ERROR: incorrect password");
}

TEST(cms_macro_follows_cmee_mode) {
    CHECK_EQ(expand("%CMS 322%", 0), "ERROR");
    CHECK_EQ(expand("%CMS 322%", 1), "+CMS ERROR: 322");
    CHECK_EQ(expand("%CMS 322%", 2), "+CMS ERROR: memory full");
}

TEST(macro_expansion_inside_larger_answer) {
    CHECK_EQ(expand("+CPIN: SIM PIN\r\n\r\n%CME 11%", 1),
             "+CPIN: SIM PIN\r\n\r\n+CME ERROR: 11");
}

TEST(macro_syntax_edge_cases) {
    CHECK_EQ(expand("100%% done", 2), "100% done");        // %% -> литеральный процент
    CHECK_EQ(expand("%НЕИЗВЕСТНО%", 2), "%НЕИЗВЕСТНО%");   // чужой макрос не трогаем
    CHECK_EQ(expand("%CME 16", 2), "%CME 16");             // незакрытый макрос
    CHECK_EQ(expand("%CME%", 2), "%CME%");                 // без кода
    CHECK_EQ(expand("%CME abc%", 2), "%CME abc%");         // код не число
    CHECK_EQ(expand("без макросов", 2), "без макросов");
    CHECK_EQ(expand("%cme 16%", 1), "+CME ERROR: 16");     // регистр не важен
}

// -------------------------------------------------------------- S-регистры

TEST(s_register_query_and_assignment) {
    int index = -1;
    int value = -1;

    CHECK(parseSRegister("ATS3?", index, value) == SRegParse::Query);
    CHECK_EQ(index, 3);

    CHECK(parseSRegister("ATS0=5", index, value) == SRegParse::Assign);
    CHECK_EQ(index, 0);
    CHECK_EQ(value, 5);

    CHECK(parseSRegister("ATS255=255", index, value) == SRegParse::Assign);
    CHECK_EQ(index, 255);
    CHECK_EQ(value, 255);
}

TEST(s_register_rejects_bad_syntax_and_ranges) {
    int index = 0;
    int value = 0;

    CHECK(parseSRegister("ATS3", index, value) == SRegParse::Invalid);      // без ? и =
    CHECK(parseSRegister("ATS3=", index, value) == SRegParse::Invalid);     // без значения
    CHECK(parseSRegister("ATS3=999", index, value) == SRegParse::Invalid);  // значение > 255
    CHECK(parseSRegister("ATS300=1", index, value) == SRegParse::Invalid);  // номер > 255
    CHECK(parseSRegister("ATS3?X", index, value) == SRegParse::Invalid);    // мусор после ?
    CHECK(parseSRegister("ATS3=1X", index, value) == SRegParse::Invalid);   // мусор после числа
}

TEST(s_register_ignores_other_commands) {
    int index = 0;
    int value = 0;

    CHECK(parseSRegister("AT", index, value) == SRegParse::NotSRegister);
    CHECK(parseSRegister("ATI", index, value) == SRegParse::NotSRegister);
    CHECK(parseSRegister("AT+CSQ", index, value) == SRegParse::NotSRegister);
    CHECK(parseSRegister("ATSOMETHING", index, value) == SRegParse::NotSRegister);
}
