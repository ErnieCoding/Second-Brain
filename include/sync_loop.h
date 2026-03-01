#pragma once
#include "config.h"
#include <atomic>
#include <thread>
#include <vector>

class SyncLoop {
public:
    explicit SyncLoop(const AppConfig& cfg);
    ~SyncLoop();

    // Start one thread per repo. Blocks until stop() is called.
    void run();

    // Signal all threads to exit cleanly.
    void stop();

private:
    void sync_repo(const RepoConfig& repo_cfg);

    AppConfig             cfg_;
    std::atomic<bool>     running_{false};
    std::vector<std::thread> threads_;
};

// Convenience wrapper used from main.cpp
void run_sync_loop(const AppConfig& cfg);
