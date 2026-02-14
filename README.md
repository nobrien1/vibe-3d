# Vibe 3D

Minimal 3D platformer demo in C++ using OpenGL, GLFW, GLAD, and GLM.

## Controls

- W/A/S/D: Move
- Space: Jump
- Hold right mouse button: Orbit camera
- Esc: Quit

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
