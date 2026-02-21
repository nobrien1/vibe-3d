# Vibe 3D

Minimal 3D platformer demo in C++ using OpenGL, GLFW, GLAD, and GLM.

## Controls

- W/A/S/D: Move
- Space: Jump
- Hold right mouse button: Orbit camera
- Esc or P: Pause/Resume
- Pause menu: Adjust audio/camera settings, toggle debug HUD, reset player, quit

## Progression

- Level 1: Collect 10 cats and reach the car while avoiding the clown.
- Level 2: Collect 20 very cute dogs and reach the car while a mummy throws bombs at you.

## Build

This project uses CMake and downloads dependencies during configure. The glad loader generation requires Python 3 with the jinja2 package installed.

### Windows (MSVC)

1. Configure: `cmake -S . -B build`
2. Build: `cmake --build build`
3. Run: `build\Debug\vibe3d.exe`

### Linux/macOS

1. Configure: `cmake -S . -B build`
2. Build: `cmake --build build`
3. Run: `./build/vibe3d`

## Online Multiplayer (MVP)

This build now supports a simple 2-player online mode over UDP.

- Each player runs their own game instance.
- Gameplay state is still local, but player movement/pose is synced and rendered in-world.
- Useful for quick co-op testing over LAN or direct IP forwarding.

### Command-line options

- `--mp` enable multiplayer with defaults (`local=7777`, `peer=127.0.0.1:7778`)
- `--mp-local-port <port>` local UDP port to listen on
- `--mp-peer-ip <ip>` peer IPv4 address
- `--mp-peer-port <port>` peer UDP port

### Example (same PC, two windows)

Terminal A:

`build\Debug\vibe3d.exe --mp --mp-local-port 7777 --mp-peer-ip 127.0.0.1 --mp-peer-port 7778`

Terminal B:

`build\Debug\vibe3d.exe --mp --mp-local-port 7778 --mp-peer-ip 127.0.0.1 --mp-peer-port 7777`
