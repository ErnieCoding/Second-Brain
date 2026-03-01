# Second-Brain

A lightweight background daemon that keeps your Obsidian vault (or any personal git repository) automatically synced across devices — no Obsidian Sync subscription required.

Every N seconds it:
1. Commits any local changes with an auto-generated timestamp message
2. Fetches the remote
3. Fast-forward merges any remote changes
4. Pushes to remote

If a real merge conflict is detected (shouldn't happen for solo use), a simple terminal UI lets you pick ours / theirs / skip.

---

## Prerequisites

### Windows

- [MSYS2](https://www.msys2.org/) — provides the compiler and libgit2
- [CMake](https://cmake.org/download/) ≥ 3.16 (add to PATH during install)
- [Git for Windows](https://git-scm.com/download/win)

Install the required MSYS2 packages by opening **MSYS2 UCRT64** and running:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-libgit2
```

> **Note:** Use the **UCRT64** environment, not MINGW64. The two are ABI-incompatible.

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install build-essential cmake libgit2-dev pkg-config
```

---

## Building

Run these commands from the repository root. On Windows, use the **UCRT64** MSYS2 shell.

```bash
# Configure
cmake -B build -G "MinGW Makefiles"   # Windows (MSYS2 UCRT64 shell)
cmake -B build                        # Linux

# Build
cmake --build build
```

The binary is placed at `build/second-brain.exe` (Windows) or `build/second-brain` (Linux).

---

## Configuration

The config file is stored at:
- **Windows:** `%APPDATA%\second-brain\config.json`
- **Linux:** `~/.config/second-brain/config.json`

### Adding a repository

```bash
./build/second-brain --add-repo       # Linux
./build/second-brain.exe --add-repo  # Windows
```

You will be prompted for:

| Field | Description | Default |
|---|---|---|
| Local repo path | Absolute path to the git working tree | *(required)* |
| Remote name | The git remote to sync with | `origin` |
| Branch | Branch to track | `main` |
| HTTPS token | Personal access token (PAT) for authentication | *(required)* |
| Author name | Name used in auto-commits | `Second Brain` |
| Author email | Email used in auto-commits | `sync@second-brain.local` |
| Poll interval | How often to check for changes (seconds) | `30` |

#### Generating a GitHub Personal Access Token

1. Go to **GitHub → Settings → Developer settings → Personal access tokens → Fine-grained tokens**
2. Create a token with **Contents: Read and write** permission for the target repository
3. Paste the token when prompted by `--add-repo`

The token is stored in plain text in the config JSON. Ensure the file has appropriate permissions (`chmod 600 ~/.config/second-brain/config.json` on Linux).

### Listing configured repositories

```bash
./build/second-brain --list-repos
```

### Config file format

```json
{
  "repos": [
    {
      "path": "/home/user/vaults/main-vault",
      "remote": "origin",
      "branch": "main",
      "https_token": "ghp_xxxxxxxxxxxx",
      "author_name": "Second Brain",
      "author_email": "sync@second-brain.local",
      "poll_interval_seconds": 30
    }
  ]
}
```

Multiple repositories can be added by running `--add-repo` multiple times or editing the JSON directly.

---

## Running

### Foreground (for testing)

```bash
./build/second-brain --run       # Linux
./build/second-brain.exe --run  # Windows
```

Press `Ctrl+C` to stop. Useful for verifying that sync works before installing as a service.

---

## Installing as a Background Service

### Windows — Windows Service

Open **Command Prompt or PowerShell as Administrator**, then run:

```cmd
build\second-brain.exe --install
```

This registers `second-brain` as a Windows Service set to start automatically at login. You can manage it like any other service:

```cmd
# Check status
sc query second-brain

# Start / stop manually
sc start second-brain
sc stop second-brain

# Open the Services GUI
services.msc
```

To remove the service:

```cmd
build\second-brain.exe --uninstall
```

> **Important:** `--install` and `--uninstall` must be run from an elevated (Administrator) prompt. The path embedded in the service points to the binary at the time of installation — if you move or rebuild the binary, uninstall and reinstall.

### Linux (Ubuntu) — systemd user service

```bash
./build/second-brain --install
```

This writes `~/.config/systemd/user/second-brain.service` and enables it immediately. No root required.

Useful systemctl commands:

```bash
# Check status and recent log output
systemctl --user status second-brain

# View live logs
journalctl --user -u second-brain -f

# Start / stop manually
systemctl --user start second-brain
systemctl --user stop second-brain

# Disable autostart (but keep the service file)
systemctl --user disable second-brain
```

To remove the service:

```bash
./build/second-brain --uninstall
```

> **Note:** On Ubuntu, user services only run while you are logged in. To make the service run even when you are logged out (e.g. on a headless server), enable lingering:
> ```bash
> loginctl enable-linger $USER
> ```

---

## Conflict Resolution

If a merge conflict is detected (two devices edited the same file simultaneously), the daemon pauses that repository's sync and opens an interactive prompt in the terminal / journal:

```
========================================
MERGE CONFLICT: Notes/daily/2026-03-01.md
========================================

--- OURS  (local version) ---
  ...

--- THEIRS (remote version) ---
  ...

Choose resolution:
  [o] Keep OURS  (local)
  [t] Keep THEIRS (remote)
  [s] Skip (abort this merge)
Choice:
```

After resolution, the daemon commits the result and resumes normal operation.

> When running as a background service the prompt won't be visible in the terminal. If you suspect a conflict is blocking a repo, check the logs (`journalctl --user -u second-brain -f` on Linux, or Event Viewer / the log output on Windows) and run `--run` in a terminal to interact with the prompt.

---

## Uninstalling

```bash
# 1. Remove the service
./build/second-brain --uninstall

# 2. Delete the config (optional)
rm ~/.config/second-brain/config.json          # Linux
rmdir /S %APPDATA%\second-brain                # Windows CMD
```
