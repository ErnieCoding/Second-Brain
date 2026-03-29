#include <iostream>
#include <string>
#include "config.h"
#include "platform.h"
#include "logger.h"
#include "sync_loop.h"

// Forward declarations — defined in service_win32.cpp or service_linux.cpp
bool service_install(const std::string& exe_path);
bool service_uninstall();

#ifdef _WIN32
// Enters the Windows Service Control Dispatcher (blocks until service stops).
// config_path overrides the default location — used when invoked via --service.
void run_as_service(const std::string& config_path);
#else
void run_with_signals(SyncLoop& loop);
#endif

static void print_usage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " <command>\n\n"
        << "Commands:\n"
        << "  --add-repo      Interactively add a repository to the config\n"
        << "  --list-repos    List all configured repositories\n"
        << "  --run           Run the sync daemon in the foreground\n"
        << "  --install       Install as a system service\n"
        << "  --uninstall     Remove the system service\n";
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
#ifdef _WIN32
        run_as_service("");
        return 0;
#else
        print_usage(argv[0]);
        return 1;
#endif
    }

    const std::string cmd = argv[1];

#ifdef _WIN32
    if (cmd == "--service") {
        std::string config_path = (argc > 2) ? argv[2] : "";
        run_as_service(config_path);
        return 0;
    }
#endif

    const std::string config_path = get_config_path();

    if (cmd == "--add-repo") {
        AppConfig cfg = load_config(config_path);
        add_repo_interactive(cfg, config_path);
        return 0;
    }

    if (cmd == "--list-repos") {
        AppConfig cfg = load_config(config_path);
        if (cfg.repos.empty()) {
            std::cout << "No repositories configured. Run --add-repo first.\n";
            return 0;
        }
        for (const auto& r : cfg.repos) {
            std::cout << r.path
                      << "  [" << r.remote << "/" << r.branch << "]"
                      << "  poll=" << r.poll_interval_seconds << "s"
                      << "  author=" << r.author_name << "\n";
        }
        return 0;
    }

    if (cmd == "--run") {
        {
            AppConfig cfg = load_config(config_path);
            if (cfg.repos.empty()) {
                std::cerr << "No repositories configured. Run --add-repo first.\n";
                return 1;
            }
        }
        log_info("Starting sync daemon (foreground mode)...");
#ifdef _WIN32
        run_sync_loop(config_path);
#else
        {
            SyncLoop loop(config_path);
            run_with_signals(loop);
        }
#endif
        return 0;
    }

    if (cmd == "--install") {
        std::string exe = (argc > 2) ? argv[2] : argv[0];
        if (!service_install(exe)) {
            log_error("Service installation failed.");
            return 1;
        }
        std::cout << "Service installed successfully.\n";
        return 0;
    }

    if (cmd == "--uninstall") {
        if (!service_uninstall()) {
            log_error("Service removal failed.");
            return 1;
        }
        std::cout << "Service removed successfully.\n";
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n\n";
    print_usage(argv[0]);
    return 1;
}
