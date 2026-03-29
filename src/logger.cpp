#include "logger.h"
#include <git2.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

static std::ofstream g_log_file;

static std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

static const char* level_str(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

void log_enable_file(const std::string& path) {
    g_log_file.open(path, std::ios::app);
}

void log(LogLevel level, const std::string& msg) {
    std::string line = "[" + current_timestamp() + "] [" +
                       level_str(level) + "] " + msg + "\n";
    std::cerr << line;
    if (g_log_file.is_open()) {
        g_log_file << line;
        g_log_file.flush();
    }
}

void log_info(const std::string& msg)  { log(LogLevel::Info,  msg); }
void log_warn(const std::string& msg)  { log(LogLevel::Warn,  msg); }
void log_error(const std::string& msg) { log(LogLevel::Error, msg); }

void log_git_error(const std::string& context, int error_code) {
    const git_error* e = git_error_last();
    std::string detail = e ? e->message : "(no detail)";
    log(LogLevel::Error, context + " [code=" + std::to_string(error_code) + "]: " + detail);
}
