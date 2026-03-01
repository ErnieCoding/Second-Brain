#pragma once
#include <string>

enum class ConflictChoice { Ours, Theirs, Skip };

// Print a two-panel diff of ours vs theirs and prompt the user to choose.
ConflictChoice prompt_conflict(const std::string& path,
                               const std::string& ours,
                               const std::string& theirs);

// Return the resolved file content given the user's choice.
// ConflictChoice::Skip returns the original content unchanged.
std::string apply_choice(const std::string& ours,
                         const std::string& theirs,
                         ConflictChoice choice);
