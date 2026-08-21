// SPDX-License-Identifier: MIT
#include "log.h"

#include <cstdio>

namespace ndm {
namespace {

LogLevel g_level = LogLevel::Info;

const char* levelTag(LogLevel level) {
    switch (level) {
    case LogLevel::Error: return "ERROR";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Debug: return "DEBUG";
    }
    return "?????";
}

}  // namespace

void setLogLevel(LogLevel level) { g_level = level; }
LogLevel logLevel() { return g_level; }

void logMessage(LogLevel level, const std::string& text) {
    if (static_cast<int>(level) > static_cast<int>(g_level)) return;
    std::fprintf(stderr, "[%s] %s\n", levelTag(level), text.c_str());
    std::fflush(stderr);
}

std::string visualize(const std::string& data) {
    std::string out;
    out.reserve(data.size() + 8);

    for (unsigned char c : data) {
        switch (c) {
        case '\r': out += "<CR>";  break;
        case '\n': out += "<LF>";  break;
        case '\t': out += "<TAB>"; break;
        case 0x08: out += "<BS>";  break;
        case 0x1b: out += "<ESC>"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "<%02X>", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

}  // namespace ndm
