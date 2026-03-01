#pragma once
#include <string>

// Returns the platform-specific config directory:
//   Windows: %APPDATA%\second-brain
//   Linux:   $XDG_CONFIG_HOME/second-brain  (or ~/.config/second-brain)
std::string get_config_dir();

// Returns the full path to the config JSON file.
std::string get_config_path();

// Creates a directory and all required parents.
// Returns true on success (or if the directory already exists).
bool make_dirs(const std::string& path);
