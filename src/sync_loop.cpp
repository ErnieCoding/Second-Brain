#include "sync_loop.h"
#include "git_ops.h"
#include "conflict_ui.h"
#include "logger.h"
#include <git2.h>
#include <chrono>
#include <filesystem>
#include <set>
#include <thread>
#include <sstream>
#include <ctime>
#include <iomanip>


static std::string auto_commit_message() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::ostringstream ss;
    ss << "auto: sync " << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S UTC");
    return ss.str();
}

// Sync cycle

static void perform_sync_cycle(git_repository* repo, const RepoConfig& cfg) {
    CredPayload cred{"x-token-auth", cfg.https_token};
    auto callbacks = make_remote_callbacks(&cred);
    bool has_local_commit = false;

    // Step 1: commit local changes (no push yet).
    // Committing first protects dirty files from being overwritten by the
    // checkout that happens during fast-forward in step 3.
    if (repo_is_dirty(repo)) {
        log_info("[" + cfg.path + "] Local changes detected — committing...");
        git_oid tree_oid;
        if (repo_stage_all(repo, &tree_oid) == 0) {
            git_oid commit_oid;
            std::string msg = auto_commit_message();
            if (repo_commit(repo, &tree_oid,
                            cfg.author_name, cfg.author_email,
                            msg, &commit_oid) == 0) {
                log_info("[" + cfg.path + "] Committed: " + msg);
                has_local_commit = true;
            }
        }
    }

    // Step 2: fetch — must happen before push so we know whether remote has
    // diverged. Pushing before fetching would be rejected as a non-fast-forward
    // update if the remote has commits we don't have yet.
    if (repo_fetch(repo, cfg.remote, callbacks) != 0) {
        log_error("[" + cfg.path + "] Fetch failed.");
        return;
    }

    // Step 3: merge / fast-forward remote changes into local.
    // After this, local is a superset of remote, so the push in step 4 is
    // always a fast-forward (or up-to-date) push and will not be rejected.
    MergeResult result = repo_merge(repo, cfg.remote, cfg.branch);
    switch (result) {
        case MergeResult::UpToDate:
            break;
        case MergeResult::FastForwarded:
            log_info("[" + cfg.path + "] Fast-forwarded to remote.");
            break;
        case MergeResult::Error:
            log_error("[" + cfg.path + "] Merge error — skipping push this cycle.");
            return;
        case MergeResult::Conflict: {
            log_warn("[" + cfg.path + "] Merge conflicts detected — opening conflict UI...");
            auto conflict_paths = get_conflict_paths(repo);
            bool all_resolved = true;
            for (const auto& cpath : conflict_paths) {
                auto blobs  = read_conflict_blobs(repo, cpath);
                auto choice = prompt_conflict(cpath, blobs.ours, blobs.theirs);
                if (choice == ConflictChoice::Skip) {
                    log_warn("[" + cfg.path + "] Merge aborted by user.");
                    repo_merge_abort(repo);
                    all_resolved = false;
                    break;
                }
                auto resolved = apply_choice(blobs.ours, blobs.theirs, choice);
                if (resolve_conflict(repo, cpath, resolved) != 0) {
                    log_error("[" + cfg.path + "] Failed to resolve: " + cpath);
                    repo_merge_abort(repo);
                    all_resolved = false;
                    break;
                }
            }
            if (!all_resolved) return;
            git_oid tree_oid;
            if (repo_stage_all(repo, &tree_oid) == 0) {
                git_oid commit_oid;
                if (repo_commit(repo, &tree_oid,
                                cfg.author_name, cfg.author_email,
                                "merge: resolved conflicts",
                                &commit_oid) == 0) {
                    // Clear the MERGING state (MERGE_HEAD, MERGE_MSG, etc.)
                    // so the repo is clean before the next operation.
                    git_repository_state_cleanup(repo);
                    log_info("[" + cfg.path + "] Merge commit created.");
                    has_local_commit = true;
                }
            }
            break;
        }
    }

    // Step 4: push only if we have something ahead of remote.
    if (has_local_commit) {
        if (repo_push(repo, cfg.remote, cfg.branch, callbacks) == 0) {
            log_info("[" + cfg.path + "] Pushed to " + cfg.remote + "/" + cfg.branch);
        } else {
            log_error("[" + cfg.path + "] Push failed.");
        }
    }
}

// SyncLoop

SyncLoop::SyncLoop(const std::string& config_path) : config_path_(config_path) {}

SyncLoop::~SyncLoop() {
    stop();
    if (watcher_thread_.joinable()) watcher_thread_.join();
    // Join threads that were removed from repos_ by the watcher but not yet joined
    for (auto& t : pending_join_) if (t.joinable()) t.join();
    // Join any remaining active threads (running flags already cleared by stop())
    for (auto& [path, entry] : repos_) if (entry.thread.joinable()) entry.thread.join();
}

void SyncLoop::run() {
    loop_running_ = true;

    try {
        AppConfig cfg = load_config(config_path_);
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& repo_cfg : cfg.repos) start_repo(repo_cfg);
    } catch (const std::exception& ex) {
        log_error(std::string("Failed to load initial config: ") + ex.what());
    }

    watcher_thread_ = std::thread(&SyncLoop::watch_config, this);

    while (loop_running_.load())
        std::this_thread::sleep_for(std::chrono::seconds(1));
}

void SyncLoop::stop() {
    loop_running_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [path, entry] : repos_) *entry.running = false;
}

void SyncLoop::start_repo(const RepoConfig& cfg) {
    auto running = std::make_shared<std::atomic<bool>>(true);
    repos_.emplace(cfg.path, RepoEntry{
        running,
        std::thread(&SyncLoop::sync_repo, this, cfg, running)
    });
}

void SyncLoop::watch_config() {
    namespace fs = std::filesystem;
    fs::file_time_type last_mtime{};

    while (loop_running_.load()) {
        // Sleep in 1 s increments so stop() is noticed quickly
        for (int i = 0; i < 10 && loop_running_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!loop_running_.load()) break;

        // Only reload when the file has actually changed
        std::error_code ec;
        auto mtime = fs::last_write_time(config_path_, ec);
        if (ec || mtime == last_mtime) continue;
        last_mtime = mtime;

        AppConfig new_cfg;
        try {
            new_cfg = load_config(config_path_);
        } catch (const std::exception& ex) {
            log_error(std::string("Config reload error: ") + ex.what());
            continue;
        }

        std::set<std::string> new_paths;
        for (const auto& r : new_cfg.repos) new_paths.insert(r.path);

        std::lock_guard<std::mutex> lock(mutex_);

        // Start threads for repos that appear in the new config but aren't running
        for (const auto& r : new_cfg.repos) {
            if (repos_.find(r.path) == repos_.end()) {
                log_info("Config changed: starting new repo " + r.path);
                start_repo(r);
            }
        }

        // Signal and evict repos that were removed from the config
        for (auto it = repos_.begin(); it != repos_.end(); ) {
            if (new_paths.find(it->first) == new_paths.end()) {
                log_info("Config changed: stopping removed repo " + it->first);
                *it->second.running = false;
                pending_join_.push_back(std::move(it->second.thread));
                it = repos_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void SyncLoop::sync_repo(RepoConfig cfg, std::shared_ptr<std::atomic<bool>> running) {
    git_libgit2_init();
    // Allow opening repos not owned by the current OS user (needed when running
    // as LocalSystem service — repos are owned by the interactive user account).
    git_libgit2_opts(GIT_OPT_SET_OWNER_VALIDATION, 0);
    setup_ssl();
    git_repository* repo = nullptr;
    if (repo_open(&repo, cfg.path) != 0) {
        log_error("Failed to open repo: " + cfg.path);
        git_libgit2_shutdown();
        return;
    }
    log_info("Monitoring: " + cfg.path + " (poll every " +
             std::to_string(cfg.poll_interval_seconds) + "s)");

    while (running->load()) {
        try {
            perform_sync_cycle(repo, cfg);
        } catch (const std::exception& ex) {
            log_error(std::string("Sync cycle exception: ") + ex.what());
        } catch (...) {
            log_error("Sync cycle unknown exception — continuing.");
        }
        for (uint32_t i = 0; i < cfg.poll_interval_seconds && running->load(); ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    repo_cleanup(repo);
}

void run_sync_loop(const std::string& config_path) {
    SyncLoop loop(config_path);
    loop.run();
}
