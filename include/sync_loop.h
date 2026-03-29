#pragma once
#include "config.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class SyncLoop {
public:
    // Takes the path to config.json; loads it on start and re-checks it
    // every ~10 s to pick up added/removed repositories without a restart.
    explicit SyncLoop(const std::string& config_path);
    ~SyncLoop();

    // Start threads for all configured repos and the config watcher.
    // Blocks until stop() is called.
    void run();

    // Signal all threads to exit cleanly.
    void stop();

private:
    struct RepoEntry {
        std::shared_ptr<std::atomic<bool>> running;
        std::thread                        thread;
    };

    // Spawn a sync thread for cfg. Must be called with mutex_ held.
    void start_repo(const RepoConfig& cfg);

    // Background thread: re-reads config every ~10 s on file change and
    // starts/stops repo threads to match.
    void watch_config();

    // Per-repo sync loop. Runs until *running becomes false.
    void sync_repo(RepoConfig cfg, std::shared_ptr<std::atomic<bool>> running);

    std::string                      config_path_;
    std::atomic<bool>                loop_running_{false};
    std::mutex                       mutex_;
    std::map<std::string, RepoEntry> repos_;        // active repo threads
    std::vector<std::thread>         pending_join_; // stopped threads awaiting join
    std::thread                      watcher_thread_;
};

// Convenience wrapper used from main.cpp
void run_sync_loop(const std::string& config_path);
