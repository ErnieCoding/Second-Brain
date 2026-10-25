#include "config.h"
#include "platform.h"
#include "logger.h"
#include "git_ops.h"
#include <nlohmann/json.hpp>
#include <git2.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <cctype>

using json = nlohmann::json;

// ── nlohmann serialisation ──────────────────────────────────────────────────

static void from_json(const json& j, RepoConfig& r) {
    j.at("path").get_to(r.path);
    if (j.contains("remote"))               j.at("remote").get_to(r.remote);
    if (j.contains("branch"))               j.at("branch").get_to(r.branch);
    if (j.contains("https_token"))          j.at("https_token").get_to(r.https_token);
    if (j.contains("author_name"))          j.at("author_name").get_to(r.author_name);
    if (j.contains("author_email"))         j.at("author_email").get_to(r.author_email);
    if (j.contains("poll_interval_seconds"))
        j.at("poll_interval_seconds").get_to(r.poll_interval_seconds);
}

static void to_json(json& j, const RepoConfig& r) {
    j = json{
        {"path",                  r.path},
        {"remote",                r.remote},
        {"branch",                r.branch},
        {"https_token",           r.https_token},
        {"author_name",           r.author_name},
        {"author_email",          r.author_email},
        {"poll_interval_seconds", r.poll_interval_seconds}
    };
}

// ── load / save ──────────────────────────────────────────────────────────────

AppConfig load_config(const std::string& path) {
    AppConfig cfg;
    if (!std::filesystem::exists(path)) {
        return cfg;  // empty config — caller should run --add-repo
    }
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot open config file: " + path);
    }
    json j;
    f >> j;
    if (j.contains("repos") && j["repos"].is_array()) {
        for (const auto& item : j["repos"]) {
            RepoConfig r;
            from_json(item, r);
            cfg.repos.push_back(std::move(r));
        }
    }
    return cfg;
}

bool save_config(const AppConfig& cfg, const std::string& path) {
    make_dirs(std::filesystem::path(path).parent_path().string());

    json j;
    j["repos"] = json::array();
    for (const auto& r : cfg.repos) {
        json rj;
        to_json(rj, r);
        j["repos"].push_back(rj);
    }

    // Atomic write: write to temp file then rename
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f.is_open()) {
            log_error("Cannot write config temp file: " + tmp);
            return false;
        }
        f << j.dump(2) << "\n";
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        log_error("Cannot rename config temp file: " + ec.message());
        return false;
    }
    return true;
}

// ── interactive prompt ───────────────────────────────────────────────────────

static std::string prompt(const std::string& label, const std::string& default_val = "") {
    if (default_val.empty()) {
        std::cout << label << ": ";
    } else {
        std::cout << label << " [" << default_val << "]: ";
    }
    std::string val;
    std::getline(std::cin, val);
    if (val.empty()) return default_val;
    return val;
}

void add_repo_interactive(AppConfig& cfg, const std::string& config_path) {
    std::cout << "\n=== Add Repository ===\n";
    RepoConfig r;
    r.path   = prompt("Local repo path (absolute)");
    r.remote = prompt("Remote name", "origin");
    r.branch = prompt("Branch", "main");

    // Check whether the remote already exists in the repo; offer to create it
    // if not. This also validates that the path is actually a git repository.
    git_libgit2_init();
    git_repository* repo = nullptr;
    if (repo_open(&repo, r.path) != 0) {
        std::cerr << "Warning: could not open git repository at " << r.path
                  << " — make sure the path is correct and git init has been run.\n";
    } else {
        if (!remote_exists(repo, r.remote)) {
            std::cout << "Remote '" << r.remote << "' not found in this repository.\n";
            std::string url = prompt("Remote URL (e.g. https://github.com/user/repo.git)");
            if (!url.empty()) {
                if (remote_add(repo, r.remote, url) == 0) {
                    std::cout << "Remote '" << r.remote << "' added -> " << url << "\n";
                } else {
                    std::cerr << "Warning: failed to add remote. You can add it manually with:\n"
                              << "  git remote add " << r.remote << " <url>\n";
                }
            }
        } else {
            // Remote exists — show the user its current URL so they can confirm
            git_remote* remote = nullptr;
            if (git_remote_lookup(&remote, repo, r.remote.c_str()) == 0) {
                std::cout << "Remote '" << r.remote << "' -> "
                          << git_remote_url(remote) << "\n";
                git_remote_free(remote);
            }
        }
        git_repository_free(repo);
    }
    git_libgit2_shutdown();

    r.https_token        = prompt("HTTPS personal access token");
    r.author_name        = prompt("Commit author name", "Second Brain");
    r.author_email       = prompt("Commit author email", "sync@second-brain.local");
    std::string interval = prompt("Poll interval (seconds)", "30");
    try { r.poll_interval_seconds = static_cast<uint32_t>(std::stoul(interval)); }
    catch (...) { r.poll_interval_seconds = 30; }

    cfg.repos.push_back(r);
    save_config(cfg, config_path);
    std::cout << "Saved to " << config_path << "\n";
}

// ── remove repo ──────────────────────────────────────────────────────────────

// Canonical form for comparing paths typed in different styles.
static std::string normalize_path(std::string p) {
#ifdef _WIN32
    for (char& ch : p) {
        if (ch == '\\') ch = '/';
        else ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
#endif
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

bool remove_repo(AppConfig& cfg, const std::string& config_path,
                 const std::string& target) {
    if (cfg.repos.empty()) {
        std::cout << "No repositories configured.\n";
        return false;
    }

    std::string choice = target;
    if (choice.empty()) {
        std::cout << "\n=== Remove Repository ===\n";
        for (size_t i = 0; i < cfg.repos.size(); ++i)
            std::cout << "  " << (i + 1) << ". " << cfg.repos[i].path
                      << "  [" << cfg.repos[i].remote << "/" << cfg.repos[i].branch << "]\n";
        choice = prompt("Number or path of repo to remove (blank to cancel)");
        if (choice.empty()) {
            std::cout << "Cancelled.\n";
            return false;
        }
    }

    // Match by path first, so a numeric-looking path is never misread as an index.
    size_t idx = cfg.repos.size();
    const std::string want = normalize_path(choice);
    for (size_t i = 0; i < cfg.repos.size(); ++i) {
        if (normalize_path(cfg.repos[i].path) == want) { idx = i; break; }
    }
    if (idx == cfg.repos.size()) {
        try {
            size_t used = 0;
            unsigned long n = std::stoul(choice, &used);
            if (used == choice.size() && n >= 1 && n <= cfg.repos.size()) idx = n - 1;
        } catch (...) {}
    }
    if (idx == cfg.repos.size()) {
        std::cerr << "No tracked repository matches '" << choice
                  << "'. Use --list-repos to see tracked paths.\n";
        return false;
    }

    const std::string removed = cfg.repos[idx].path;
    cfg.repos.erase(cfg.repos.begin() + static_cast<std::ptrdiff_t>(idx));
    if (!save_config(cfg, config_path)) {
        std::cerr << "Failed to save config.\n";
        return false;
    }
    std::cout << "Removed " << removed << " from tracking.\n"
              << "(The repository itself is untouched. A running daemon stops syncing it within ~10s.)\n";
    return true;
}
