# NVIDIA Replay Auto

A lightweight Windows background application written in C++ that automatically enables and disables NVIDIA Instant Replay based on running game processes.

No manual toggling, console windows, or system tray icons required.

## Requirements

- Windows 10 or Windows 11 (64-bit)
- NVIDIA GPU supporting Instant Replay
- NVIDIA App with In-Game Overlay enabled
- NVIDIA App version supporting the MCP interface
- NVIDIA App MCP server enabled and accessible

## Installation

1. Download the latest `AutoInstantReplay.exe` from the [Releases](../../releases) page.
2. Create a folder for the application.
3. Place `AutoInstantReplay.exe` and `games.txt` in the same folder.
4. Configure the games you want to monitor.
5. Run `AutoInstantReplay.exe`.

The application runs silently without opening a console window.

## Configuration

Edit `games.txt`:

```ini
poll_ms=2000

cs2.exe
```

`poll_ms` determines how frequently the application checks running processes, in milliseconds.

Each subsequent line specifies a game executable to monitor.

When any configured game is running, Instant Replay is enabled. When none of the configured games are running, Instant Replay is disabled.

## Task Scheduler

You can have the application run at start-up (which is the intended use) by creating a new task or adding the program to start-up programs in Task Manager.

## Build From Source

Clone the repository:

```powershell
git clone https://github.com/RequiemB/auto-instant-replay.git
cd auto-instant-replay
```

Compile using MinGW-w64:

```powershell
g++ -std=c++20 -O2 -s -mwindows main.cpp -o AutoInstantReplay.exe -lwinhttp
```

## Disclaimer

This project is an independent utility and is not affiliated with, endorsed by, or sponsored by NVIDIA Corporation.

NVIDIA, GeForce, and their respective trademarks belong to NVIDIA Corporation.
