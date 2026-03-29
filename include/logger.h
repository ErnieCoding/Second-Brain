#pragma once
#include <string>

enum class LogLevel { Debug, Info, Warn, Error };

void log(LogLevel level, const std::string& msg);
void log_info(const std::string& msg);
void log_warn(const std::string& msg);
void log_error(const std::string& msg);
void log_git_error(const std::string& context, int error_code);

// Call once at service startup to also write log output to a file.
void log_enable_file(const std::string& path);
