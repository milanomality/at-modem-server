// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace ndm {

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3 };

void setLogLevel(LogLevel level);
LogLevel logLevel();

/// Все логи идут в stderr, чтобы не смешиваться с потоком tty (актуально
/// в режиме --stdio, где stdout занят ответами «модема»).
void logMessage(LogLevel level, const std::string& text);

inline void logError(const std::string& t) { logMessage(LogLevel::Error, t); }
inline void logWarn(const std::string& t)  { logMessage(LogLevel::Warn,  t); }
inline void logInfo(const std::string& t)  { logMessage(LogLevel::Info,  t); }
inline void logDebug(const std::string& t) { logMessage(LogLevel::Debug, t); }

/// Печатное представление строки с управляющими символами: "AT\r" -> AT<CR>.
std::string visualize(const std::string& data);

}  // namespace ndm
