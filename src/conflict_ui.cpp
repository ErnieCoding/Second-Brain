#include "conflict_ui.h"
#include <iostream>
#include <sstream>
#include <vector>

static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) lines.push_back(line);
    return lines;
}

static void print_section(const std::string& label, const std::string& content) {
    std::cout << "\n--- " << label << " ---\n";
    auto lines = split_lines(content);
    int n = 0;
    for (const auto& l : lines) {
        std::cout << "  " << l << "\n";
        if (++n >= 40) {          // cap at 40 lines to keep it readable
            std::cout << "  ... (" << (lines.size() - 40) << " more lines)\n";
            break;
        }
    }
}

ConflictChoice prompt_conflict(const std::string& path,
                               const std::string& ours,
                               const std::string& theirs)
{
    std::cout << "\n========================================\n";
    std::cout << "MERGE CONFLICT: " << path << "\n";
    std::cout << "========================================\n";

    print_section("OURS  (local version)", ours);
    print_section("THEIRS (remote version)", theirs);

    std::cout << "\nChoose resolution:\n"
              << "  [o] Keep OURS  (local)\n"
              << "  [t] Keep THEIRS (remote)\n"
              << "  [s] Skip (abort this merge)\n"
              << "Choice: ";

    std::string input;
    while (true) {
        if (!std::getline(std::cin, input)) {
            // stdin is closed (e.g. running as a service with no console) —
            // abort the merge rather than spinning forever on EOF.
            return ConflictChoice::Skip;
        }
        if (input == "o" || input == "O") return ConflictChoice::Ours;
        if (input == "t" || input == "T") return ConflictChoice::Theirs;
        if (input == "s" || input == "S") return ConflictChoice::Skip;
        std::cout << "Please enter 'o', 't', or 's': ";
    }
}

std::string apply_choice(const std::string& ours,
                         const std::string& theirs,
                         ConflictChoice choice)
{
    switch (choice) {
        case ConflictChoice::Ours:   return ours;
        case ConflictChoice::Theirs: return theirs;
        case ConflictChoice::Skip:   return ours;  // content unused; merge will be aborted
    }
    return ours;
}
