#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct RepoConfig {
    std::string path;
    std::string remote               = "origin";
    std::string branch               = "main";
    std::string https_token;
    std::string author_name          = "Second Brain";
    std::string author_email         = "sync@second-brain.local";
    uint32_t    poll_interval_seconds = 30;
};

struct AppConfig {
    std::vector<RepoConfig> repos;
};

// Load config from disk. Returns a default AppConfig if the file is absent.
// Throws std::runtime_error on JSON parse errors.
AppConfig load_config(const std::string& path);

// Persist config to disk (write to temp file, then rename).
// Returns true on success.
bool save_config(const AppConfig& cfg, const std::string& path);

void add_repo_interactive(AppConfig& cfg, const std::string& config_path);
