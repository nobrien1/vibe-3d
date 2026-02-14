cmake_minimum_required(VERSION 3.20)
project(vibe3d LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.3.9
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 0.9.9.8
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG v2.0.4
)
FetchContent_GetProperties(glad)
if (NOT glad_POPULATED)
  FetchContent_Populate(glad)
endif()
add_subdirectory(${glad_SOURCE_DIR}/cmake glad-cmake)

FetchContent_Declare(
  miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.21
)
FetchContent_GetProperties(miniaudio)
if (NOT miniaudio_POPULATED)
  FetchContent_Populate(miniaudio)
endif()

# Glad's CMake provides glad_add_library for on-demand loader generation.
glad_add_library(glad_gl STATIC API gl:core=3.3)

add_executable(vibe3d
  src/main.cpp
)

target_link_libraries(vibe3d PRIVATE glfw glad_gl glm)
target_include_directories(vibe3d PRIVATE ${miniaudio_SOURCE_DIR})
target_compile_definitions(vibe3d PRIVATE MINIAUDIO_IMPLEMENTATION)

if (APPLE)
  target_compile_definitions(vibe3d PRIVATE GL_SILENCE_DEPRECATION)
endif()

target_compile_definitions(vibe3d PRIVATE
  VIBE_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)
