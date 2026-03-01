#include "platform.h"
#include <filesystem>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

std::string get_config_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, buf))) {
        return std::string(buf) + "\\second-brain";
    }
    throw std::runtime_error("Cannot determine APPDATA directory");
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        return std::string(xdg) + "/second-brain";
    }
    const char* home = std::getenv("HOME");
    if (!home) throw std::runtime_error("Cannot determine HOME directory");
    return std::string(home) + "/.config/second-brain";
#endif
}

std::string get_config_path() {
    return get_config_dir() +
#ifdef _WIN32
        "\\config.json";
#else
        "/config.json";
#endif
}

bool make_dirs(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}
