#include "git_ops.h"
#include "logger.h"
#include <git2.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <filesystem>

// ── Helpers ───────────────────────────────────────────────────────────────────

std::string git_last_error_str() {
    const git_error* e = git_error_last();
    return e ? e->message : "(no detail)";
}

static std::string utc_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S UTC");
    return ss.str();
}

// ── SSL setup ─────────────────────────────────────────────────────────────────
// Called once per thread after git_libgit2_init(). Tries to point libgit2 at a
// known CA bundle so HTTPS certificate validation works out of the box.
// If no bundle is found the certificate_check_cb below acts as the fallback.
void setup_ssl() {
    static const char* candidates[] = {
#ifdef _WIN32
        "C:/msys64/ucrt64/etc/ssl/certs/ca-bundle.crt",
        "C:/msys64/usr/ssl/certs/ca-bundle.crt",
        "C:/Program Files/Git/mingw64/etc/ssl/certs/ca-bundle.crt",
#else
        "/etc/ssl/certs/ca-certificates.crt",   // Ubuntu/Debian
        "/etc/pki/tls/certs/ca-bundle.crt",     // RHEL/Fedora
        "/etc/ssl/cert.pem",                     // macOS / Alpine
#endif
        nullptr
    };
    for (const char** p = candidates; *p; ++p) {
        if (std::filesystem::exists(*p)) {
            git_libgit2_opts(GIT_OPT_SET_SSL_CERT_LOCATIONS, *p, nullptr);
            return;
        }
    }
    // No CA bundle found; certificate_check_cb will accept the cert anyway,
    // but log a warning so the user knows validation is not active.
    log_warn("No CA bundle found — SSL certificate verification is disabled. "
             "Install ca-certificates (Linux) or ensure MSYS2 ssl certs are present (Windows).");
}

// ── Certificate check callback ────────────────────────────────────────────────
// Fallback: accept the certificate unconditionally. Only reached when libgit2
// cannot validate the cert against a CA bundle (e.g. missing bundle on Windows).
static int certificate_check_cb(git_cert* /*cert*/,
                                 int       valid,
                                 const char* /*host*/,
                                 void*     /*payload*/)
{
    if (!valid) {
        log_warn("SSL certificate could not be verified — accepting anyway. "
                 "To fix this properly, ensure a CA bundle is present.");
    }
    return 0;  // 0 = GIT_OK = accept
}

// ── Credential callback ───────────────────────────────────────────────────────

int credential_cb(git_credential** out,
                  const char* /*url*/,
                  const char* /*username_from_url*/,
                  unsigned int allowed_types,
                  void* payload)
{
    auto* cred = static_cast<CredPayload*>(payload);
    if (allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT) {
        return git_credential_userpass_plaintext_new(
            out, cred->username.c_str(), cred->token.c_str());
    }
    return GIT_EAUTH;
}

git_remote_callbacks make_remote_callbacks(CredPayload* payload) {
    git_remote_callbacks cb = GIT_REMOTE_CALLBACKS_INIT;
    cb.credentials       = credential_cb;
    cb.certificate_check = certificate_check_cb;
    cb.payload           = payload;
    return cb;
}

// ── Repository operations ─────────────────────────────────────────────────────

int repo_open(git_repository** out, const std::string& path) {
    int err = git_repository_open(out, path.c_str());
    if (err != 0) log_git_error("repo_open(" + path + ")", err);
    return err;
}

bool repo_is_dirty(git_repository* repo) {
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show  = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED
               | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
               | GIT_STATUS_OPT_EXCLUDE_SUBMODULES;

    git_status_list* list = nullptr;
    if (git_status_list_new(&list, repo, &opts) != 0) return false;

    bool dirty = false;
    size_t count = git_status_list_entrycount(list);
    for (size_t i = 0; i < count; ++i) {
        const git_status_entry* e = git_status_byindex(list, i);
        if (e->status != GIT_STATUS_CURRENT &&
            !(e->status & GIT_STATUS_IGNORED)) {
            dirty = true;
            break;
        }
    }
    git_status_list_free(list);
    return dirty;
}

int repo_stage_all(git_repository* repo, git_oid* out_tree_oid) {
    git_index* index = nullptr;
    int err = git_repository_index(&index, repo);
    if (err != 0) { log_git_error("git_repository_index", err); return err; }

    git_strarray pathspec = {nullptr, 0};
    err = git_index_add_all(index, &pathspec,
                            GIT_INDEX_ADD_DEFAULT |
                            GIT_INDEX_ADD_FORCE,
                            nullptr, nullptr);
    if (err != 0) { log_git_error("git_index_add_all", err); git_index_free(index); return err; }

    err = git_index_write(index);
    if (err != 0) { log_git_error("git_index_write", err); git_index_free(index); return err; }

    err = git_index_write_tree(out_tree_oid, index);
    if (err != 0) log_git_error("git_index_write_tree", err);

    git_index_free(index);
    return err;
}

int repo_commit(git_repository* repo,
                const git_oid* tree_oid,
                const std::string& author_name,
                const std::string& author_email,
                const std::string& message,
                git_oid* out_commit_oid)
{
    // Signature: try repo config first, fall back to supplied name/email
    git_signature* sig = nullptr;
    if (git_signature_default(&sig, repo) != 0) {
        int err = git_signature_now(&sig, author_name.c_str(), author_email.c_str());
        if (err != 0) { log_git_error("git_signature_now", err); return err; }
    }

    git_tree* tree = nullptr;
    int err = git_tree_lookup(&tree, repo, tree_oid);
    if (err != 0) { log_git_error("git_tree_lookup", err); git_signature_free(sig); return err; }

    // Try to get the current HEAD commit as parent
    git_commit* parent = nullptr;
    git_reference* head_ref = nullptr;
    bool has_parent = false;
    if (git_repository_head(&head_ref, repo) == 0) {
        git_object* obj = nullptr;
        if (git_reference_peel(&obj, head_ref, GIT_OBJECT_COMMIT) == 0) {
            git_commit_lookup(&parent, repo, git_object_id(obj));
            has_parent = true;
        }
        git_object_free(obj);
        git_reference_free(head_ref);
    }

    const git_commit* parents[] = {parent};
    err = git_commit_create(
        out_commit_oid,
        repo,
        "HEAD",
        sig, sig,
        nullptr,
        message.c_str(),
        tree,
        has_parent ? 1 : 0,
        has_parent ? parents : nullptr
    );
    if (err != 0) log_git_error("git_commit_create", err);

    if (parent) git_commit_free(parent);
    git_tree_free(tree);
    git_signature_free(sig);
    return err;
}

int repo_push(git_repository* repo,
              const std::string& remote_name,
              const std::string& branch,
              const git_remote_callbacks& callbacks)
{
    git_remote* remote = nullptr;
    int err = git_remote_lookup(&remote, repo, remote_name.c_str());
    if (err != 0) { log_git_error("git_remote_lookup", err); return err; }

    // Use HEAD as the source so the push works regardless of what the local
    // branch is named (e.g. "master" vs "main").
    std::string refspec_str = "HEAD:refs/heads/" + branch;
    const char* refspec_cstr = refspec_str.c_str();
    git_strarray refspecs = {const_cast<char**>(&refspec_cstr), 1};

    git_push_options push_opts = GIT_PUSH_OPTIONS_INIT;
    push_opts.callbacks = callbacks;

    err = git_remote_push(remote, &refspecs, &push_opts);
    if (err != 0) log_git_error("git_remote_push", err);

    git_remote_free(remote);
    return err;
}

int repo_fetch(git_repository* repo,
               const std::string& remote_name,
               const git_remote_callbacks& callbacks)
{
    git_remote* remote = nullptr;
    int err = git_remote_lookup(&remote, repo, remote_name.c_str());
    if (err != 0) { log_git_error("git_remote_lookup", err); return err; }

    git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
    fetch_opts.callbacks = callbacks;

    err = git_remote_fetch(remote, nullptr, &fetch_opts, "second-brain auto-fetch");
    if (err != 0) log_git_error("git_remote_fetch", err);

    git_remote_free(remote);
    return err;
}

MergeResult repo_merge(git_repository* repo,
                       const std::string& remote_name,
                       const std::string& branch)
{
    std::string tracking_ref = "refs/remotes/" + remote_name + "/" + branch;
    git_reference* remote_ref = nullptr;
    if (git_reference_lookup(&remote_ref, repo, tracking_ref.c_str()) != 0) {
        log_error("Cannot find remote tracking ref: " + tracking_ref);
        return MergeResult::Error;
    }

    git_annotated_commit* their_head = nullptr;
    if (git_annotated_commit_from_ref(&their_head, repo, remote_ref) != 0) {
        git_reference_free(remote_ref);
        return MergeResult::Error;
    }
    git_reference_free(remote_ref);

    git_merge_analysis_t   analysis;
    git_merge_preference_t preference;
    const git_annotated_commit* heads[] = {their_head};
    if (git_merge_analysis(&analysis, &preference, repo, heads, 1) != 0) {
        git_annotated_commit_free(their_head);
        return MergeResult::Error;
    }

    MergeResult result = MergeResult::Error;

    if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) {
        result = MergeResult::UpToDate;

    } else if (analysis & GIT_MERGE_ANALYSIS_FASTFORWARD) {
        const git_oid* ff_oid = git_annotated_commit_id(their_head);
        git_object* ff_obj = nullptr;
        int err = git_object_lookup(&ff_obj, repo, ff_oid, GIT_OBJECT_COMMIT);
        if (err == 0) {
            git_checkout_options co_opts = GIT_CHECKOUT_OPTIONS_INIT;
            co_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
            err = git_checkout_tree(repo, ff_obj, &co_opts);
            if (err == 0) {
                std::string head_ref_name = "refs/heads/" + branch;
                git_reference* head_ref = nullptr;
                git_reference* new_ref  = nullptr;
                if (git_reference_lookup(&head_ref, repo, head_ref_name.c_str()) == 0) {
                    git_reference_set_target(&new_ref, head_ref, ff_oid, "second-brain FF");
                    git_reference_free(new_ref);
                    git_reference_free(head_ref);
                }
                result = MergeResult::FastForwarded;
            } else {
                log_git_error("git_checkout_tree (FF)", err);
            }
            git_object_free(ff_obj);
        } else {
            log_git_error("git_object_lookup (FF)", err);
        }

    } else if (analysis & GIT_MERGE_ANALYSIS_NORMAL) {
        git_merge_options   merge_opts = GIT_MERGE_OPTIONS_INIT;
        git_checkout_options co_opts   = GIT_CHECKOUT_OPTIONS_INIT;
        co_opts.checkout_strategy      = GIT_CHECKOUT_FORCE;

        int err = git_merge(repo, heads, 1, &merge_opts, &co_opts);
        if (err != 0) {
            log_git_error("git_merge", err);
            result = MergeResult::Error;
        } else {
            git_index* idx = nullptr;
            git_repository_index(&idx, repo);
            bool conflicts = git_index_has_conflicts(idx) != 0;
            git_index_free(idx);
            result = conflicts ? MergeResult::Conflict : MergeResult::FastForwarded;
        }
    }

    git_annotated_commit_free(their_head);
    return result;
}

// ── Conflict helpers ──────────────────────────────────────────────────────────

std::vector<std::string> get_conflict_paths(git_repository* repo) {
    std::vector<std::string> paths;
    git_index* idx = nullptr;
    if (git_repository_index(&idx, repo) != 0) return paths;

    git_index_conflict_iterator* iter = nullptr;
    if (git_index_conflict_iterator_new(&iter, idx) != 0) {
        git_index_free(idx);
        return paths;
    }
    const git_index_entry *ancestor, *ours, *theirs;
    while (git_index_conflict_next(&ancestor, &ours, &theirs, iter) == 0) {
        const char* path = ours ? ours->path : (theirs ? theirs->path : nullptr);
        if (path) paths.emplace_back(path);
    }
    git_index_conflict_iterator_free(iter);
    git_index_free(idx);
    return paths;
}

static std::string blob_to_string(git_repository* repo, const git_oid* oid) {
    if (!oid || git_oid_is_zero(oid)) return "";
    git_blob* blob = nullptr;
    if (git_blob_lookup(&blob, repo, oid) != 0) return "";
    const char* raw = static_cast<const char*>(git_blob_rawcontent(blob));
    git_off_t   size = git_blob_rawsize(blob);
    std::string s(raw, static_cast<size_t>(size));
    git_blob_free(blob);
    return s;
}

ConflictBlobs read_conflict_blobs(git_repository* repo, const std::string& path) {
    ConflictBlobs result;
    result.path = path;
    git_index* idx = nullptr;
    if (git_repository_index(&idx, repo) != 0) return result;

    const git_index_entry *ancestor, *ours, *theirs;
    if (git_index_conflict_get(&ancestor, &ours, &theirs, idx, path.c_str()) == 0) {
        if (ours)   result.ours   = blob_to_string(repo, &ours->id);
        if (theirs) result.theirs = blob_to_string(repo, &theirs->id);
    }
    git_index_free(idx);
    return result;
}

int resolve_conflict(git_repository* repo,
                     const std::string& path,
                     const std::string& resolved_content)
{
    // Write resolved content to the working directory
    std::string workdir = git_repository_workdir(repo);
    std::string full_path = workdir + path;
    {
        std::ofstream f(full_path, std::ios::binary);
        if (!f.is_open()) {
            log_error("Cannot write resolved file: " + full_path);
            return -1;
        }
        f << resolved_content;
    }

    // Stage the resolved file
    git_index* idx = nullptr;
    int err = git_repository_index(&idx, repo);
    if (err != 0) return err;

    err = git_index_conflict_remove(idx, path.c_str());
    if (err != 0) { log_git_error("git_index_conflict_remove", err); git_index_free(idx); return err; }

    err = git_index_add_bypath(idx, path.c_str());
    if (err != 0) log_git_error("git_index_add_bypath", err);

    git_index_write(idx);
    git_index_free(idx);
    return err;
}

int repo_merge_abort(git_repository* repo) {
    // Clean up MERGE_HEAD, MERGE_MSG, CHERRY_PICK_HEAD, etc.
    // git_merge_cleanup was removed in libgit2 1.0; use git_repository_state_cleanup instead.
    int err = git_repository_state_cleanup(repo);
    if (err != 0) log_git_error("git_repository_state_cleanup", err);

    // Reset index and workdir to HEAD
    git_reference* head_ref = nullptr;
    if (git_repository_head(&head_ref, repo) == 0) {
        git_object* head_obj = nullptr;
        if (git_reference_peel(&head_obj, head_ref, GIT_OBJECT_COMMIT) == 0) {
            git_checkout_options co_opts = GIT_CHECKOUT_OPTIONS_INIT;
            co_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
            git_checkout_tree(repo, head_obj, &co_opts);
            git_object_free(head_obj);
        }
        git_reference_free(head_ref);
    }
    return err;
}

void repo_cleanup(git_repository* repo) {
    git_repository_free(repo);
    git_libgit2_shutdown();
}

// ── Remote management ─────────────────────────────────────────────────────────

bool remote_exists(git_repository* repo, const std::string& name) {
    git_remote* remote = nullptr;
    int err = git_remote_lookup(&remote, repo, name.c_str());
    if (err == 0) git_remote_free(remote);
    return err == 0;
}

int remote_add(git_repository* repo,
               const std::string& name,
               const std::string& url)
{
    git_remote* remote = nullptr;
    int err = git_remote_create(&remote, repo, name.c_str(), url.c_str());
    if (err != 0) log_git_error("git_remote_create", err);
    if (remote) git_remote_free(remote);
    return err;
}
