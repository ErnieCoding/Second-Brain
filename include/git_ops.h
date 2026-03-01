#pragma once
#include <git2.h>
#include <string>
#include <vector>

std::string git_last_error_str();

// HTTPS credential callback
struct CredPayload {
    std::string username;
    std::string token;
};

// Signature matches git_credential_acquire_cb
int credential_cb(git_credential** out,
                  const char* url,
                  const char* username_from_url,
                  unsigned int allowed_types,
                  void* payload);

git_remote_callbacks make_remote_callbacks(CredPayload* payload);


// Repository operations

// Configure SSL certificate locations for this thread. Call once after
// git_libgit2_init(). Tries common system CA bundle paths; falls back to
// accepting all certs with a warning if none are found
void setup_ssl();

// Open an existing repo. Returns 0 on success
int repo_open(git_repository** out, const std::string& path);

// Returns true if there are uncommitted local changes (modified, new, deleted)
bool repo_is_dirty(git_repository* repo);

// Stage all changes (git add -A). Writes tree OID to out_tree_oid. Returns 0 on success
int repo_stage_all(git_repository* repo, git_oid* out_tree_oid);

// Create an auto-commit using the given tree OID. Returns 0 on success
int repo_commit(git_repository* repo,
                const git_oid* tree_oid,
                const std::string& author_name,
                const std::string& author_email,
                const std::string& message,
                git_oid* out_commit_oid);

// Push local branch to remote. Returns 0 on success.
int repo_push(git_repository* repo,
              const std::string& remote_name,
              const std::string& branch,
              const git_remote_callbacks& callbacks);

// Fetch from remote (populates FETCH_HEAD). Returns 0 on success.
int repo_fetch(git_repository* repo,
               const std::string& remote_name,
               const git_remote_callbacks& callbacks);

enum class MergeResult { UpToDate, FastForwarded, Conflict, Error };

// Attempt to merge the fetched remote tracking branch into the local branch.
MergeResult repo_merge(git_repository* repo,
                       const std::string& remote_name,
                       const std::string& branch);

// ── Conflict resolution helpers ───────────────────────────────────────────────

std::vector<std::string> get_conflict_paths(git_repository* repo);

struct ConflictBlobs {
    std::string path;
    std::string ours;
    std::string theirs;
};

ConflictBlobs read_conflict_blobs(git_repository* repo, const std::string& path);

// Write resolved content to the file, stage it, and clear the conflict entry.
int resolve_conflict(git_repository* repo,
                     const std::string& path,
                     const std::string& resolved_content);

// Abort an in-progress merge (git merge --abort equivalent).
int repo_merge_abort(git_repository* repo);

// Free repo and shut down libgit2.
void repo_cleanup(git_repository* repo);

// ── Remote management ─────────────────────────────────────────────────────────

// Returns true if a remote with the given name exists in the repo config.
bool remote_exists(git_repository* repo, const std::string& name);

// Adds a new remote (equivalent to git remote add <name> <url>).
// Returns 0 on success.
int remote_add(git_repository* repo,
               const std::string& name,
               const std::string& url);
