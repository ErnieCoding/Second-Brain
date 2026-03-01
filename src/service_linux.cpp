#ifndef _WIN32
#include <string>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <csignal>
#include "logger.h"
#include "platform.h"
#include "sync_loop.h"

// Global pointer so the signal handler can stop the loop cleanly.
static SyncLoop* g_loop_ptr = nullptr;

static void sigterm_handler(int /*sig*/) {
    if (g_loop_ptr) g_loop_ptr->stop();
}

static const char* UNIT_TEMPLATE = R"([Unit]
Description=Second Brain Git Sync Daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=SECOND_BRAIN_EXE --run
Restart=on-failure
RestartSec=10

[Install]
WantedBy=default.target
)";

// Called by --run on Linux: installs SIGTERM/SIGINT handlers then runs the loop.
void run_with_signals(SyncLoop& loop) {
    g_loop_ptr = &loop;
    std::signal(SIGTERM, sigterm_handler);
    std::signal(SIGINT,  sigterm_handler);
    loop.run();
    g_loop_ptr = nullptr;
}

bool service_install(const std::string& exe_path) {
    const char* home = std::getenv("HOME");
    if (!home) {
        log_error("Cannot determine HOME directory.");
        return false;
    }

    std::string unit_dir  = std::string(home) + "/.config/systemd/user/";
    std::string unit_path = unit_dir + "second-brain.service";

    if (!make_dirs(unit_dir)) {
        log_error("Cannot create systemd user directory: " + unit_dir);
        return false;
    }

    std::string content = UNIT_TEMPLATE;
    size_t pos = content.find("SECOND_BRAIN_EXE");
    if (pos != std::string::npos) content.replace(pos, 16, exe_path);

    std::ofstream f(unit_path);
    if (!f.is_open()) {
        log_error("Cannot write unit file: " + unit_path);
        return false;
    }
    f << content;
    f.close();

    std::system("systemctl --user daemon-reload");
    std::system("systemctl --user enable second-brain.service");
    std::system("systemctl --user start  second-brain.service");

    log_info("Service installed: " + unit_path);
    log_info("Run 'systemctl --user status second-brain' to check.");
    return true;
}

bool service_uninstall() {
    std::system("systemctl --user stop    second-brain.service");
    std::system("systemctl --user disable second-brain.service");

    const char* home = std::getenv("HOME");
    if (home) {
        std::string unit_path = std::string(home)
            + "/.config/systemd/user/second-brain.service";
        std::error_code ec;
        std::filesystem::remove(unit_path, ec);
    }

    std::system("systemctl --user daemon-reload");
    log_info("Service removed.");
    return true;
}

#endif // !_WIN32
