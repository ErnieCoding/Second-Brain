#include "sync_loop.h"
#include "git_ops.h"
#include "conflict_ui.h"
#include "logger.h"
#include <git2.h>
#include <chrono>
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

SyncLoop::SyncLoop(const AppConfig& cfg) : cfg_(cfg) {}

SyncLoop::~SyncLoop() {
    stop();
    for (auto& t : threads_) if (t.joinable()) t.join();
}

void SyncLoop::run() {
    running_ = true;
    for (const auto& repo_cfg : cfg_.repos) {
        threads_.emplace_back(&SyncLoop::sync_repo, this, repo_cfg);
    }
    for (auto& t : threads_) t.join();
}

void SyncLoop::stop() {
    running_ = false;
}

void SyncLoop::sync_repo(const RepoConfig& cfg) {
    git_libgit2_init();
    setup_ssl();
    git_repository* repo = nullptr;
    if (repo_open(&repo, cfg.path) != 0) {
        log_error("Failed to open repo: " + cfg.path);
        git_libgit2_shutdown();
        return;
    }
    log_info("Monitoring: " + cfg.path + " (poll every " +
             std::to_string(cfg.poll_interval_seconds) + "s)");

    while (running_.load()) {
        try {
            perform_sync_cycle(repo, cfg);
        } catch (const std::exception& ex) {
            log_error(std::string("Sync cycle exception: ") + ex.what());
        } catch (...) {
            log_error("Sync cycle unknown exception — continuing.");
        }
        for (uint32_t i = 0; i < cfg.poll_interval_seconds && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    repo_cleanup(repo);
}

void run_sync_loop(const AppConfig& cfg) {
    SyncLoop loop(cfg);
    loop.run();
}
