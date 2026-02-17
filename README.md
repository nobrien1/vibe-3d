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
