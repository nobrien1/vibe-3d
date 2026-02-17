#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "miniaudio.h"

#ifndef VIBE_SHADER_DIR
#define VIBE_SHADER_DIR "shaders"
#endif

static std::string ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file) {
    std::cerr << "Failed to open file: " << path << "\n";
    return "";
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

struct Sound {
  std::vector<float> samples;
  ma_audio_buffer buffer{};
  ma_sound sound{};
};

struct AudioState {
  ma_engine engine{};
  bool ready = false;
  Sound footstep;
  Sound jump;
  Sound land;
  Sound ambient;
  Sound chase;
};

static float RandomFloat(unsigned int& seed) {
  seed = seed * 1664525u + 1013904223u;
  return static_cast<float>((seed >> 8) & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

static std::vector<float> GenerateFootstep(int sampleRate) {
  const int frames = static_cast<int>(sampleRate * 0.08f);
  std::vector<float> data(frames);
  unsigned int seed = 1337u;
  for (int i = 0; i < frames; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(frames);
    const float env = std::exp(-t * 8.0f);
    const float noise = (RandomFloat(seed) * 2.0f - 1.0f) * 0.4f;
    const float tone = std::sin(2.0f * 3.14159f * 110.0f * t) * 0.25f;
    data[i] = (noise + tone) * env;
  }
  return data;
}

static std::vector<float> GenerateJump(int sampleRate) {
  const int frames = static_cast<int>(sampleRate * 0.2f);
  std::vector<float> data(frames);
  for (int i = 0; i < frames; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(frames);
    const float freq = 240.0f + t * 420.0f;
    const float env = std::exp(-t * 4.0f);
    data[i] = std::sin(2.0f * 3.14159f * freq * t) * env * 0.35f;
  }
  return data;
}

static std::vector<float> GenerateLand(int sampleRate) {
  const int frames = static_cast<int>(sampleRate * 0.12f);
  std::vector<float> data(frames);
  unsigned int seed = 999u;
  for (int i = 0; i < frames; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(frames);
    const float env = std::exp(-t * 10.0f);
    const float noise = (RandomFloat(seed) * 2.0f - 1.0f) * 0.25f;
    const float tone = std::sin(2.0f * 3.14159f * 80.0f * t) * 0.4f;
    data[i] = (noise + tone) * env;
  }
  return data;
}

static std::vector<float> GenerateAmbient(int sampleRate) {
  const int frames = static_cast<int>(sampleRate * 4.0f);
  std::vector<float> data(frames);
  unsigned int seed = 4242u;
  for (int i = 0; i < frames; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
    const float hum = std::sin(2.0f * 3.14159f * 55.0f * t) * 0.12f +
                      std::sin(2.0f * 3.14159f * 110.0f * t) * 0.07f;
    const float noise = (RandomFloat(seed) * 2.0f - 1.0f) * 0.02f;
    data[i] = hum + noise;
  }
  return data;
}

static std::vector<float> GenerateChase(int sampleRate) {
  const int frames = static_cast<int>(sampleRate * 0.45f);
  std::vector<float> data(frames);
  for (int i = 0; i < frames; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(frames);
    const float freq = 160.0f + t * 260.0f;
    const float env = std::exp(-t * 3.5f);
    data[i] = std::sin(2.0f * 3.14159f * freq * t) * env * 0.5f;
  }
  return data;
}

static bool CreateSound(ma_engine& engine, Sound& sound, std::vector<float>&& samples, bool loop) {
  sound.samples = std::move(samples);
  ma_audio_buffer_config config = ma_audio_buffer_config_init(
      ma_format_f32, 1, static_cast<ma_uint32>(sound.samples.size()), sound.samples.data(), nullptr);
  if (ma_audio_buffer_init(&config, &sound.buffer) != MA_SUCCESS) {
    return false;
  }
  if (ma_sound_init_from_data_source(&engine, &sound.buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &sound.sound) != MA_SUCCESS) {
    return false;
  }
  ma_sound_set_looping(&sound.sound, loop ? MA_TRUE : MA_FALSE);
  return true;
}

static void PlaySound(Sound& sound) {
  ma_sound_seek_to_pcm_frame(&sound.sound, 0);
  ma_sound_start(&sound.sound);
}

struct Shader {
  GLuint id = 0;

  bool Load(const std::string& vertPath, const std::string& fragPath) {
    const std::string vertSrc = ReadFile(vertPath);
    const std::string fragSrc = ReadFile(fragPath);
    if (vertSrc.empty() || fragSrc.empty()) {
      return false;
    }

    const char* vertCStr = vertSrc.c_str();
    const char* fragCStr = fragSrc.c_str();

    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vertCStr, nullptr);
    glCompileShader(vert);

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fragCStr, nullptr);
    glCompileShader(frag);

    GLint success = 0;
    glGetShaderiv(vert, GL_COMPILE_STATUS, &success);
    if (!success) {
      char log[512] = {};
      glGetShaderInfoLog(vert, sizeof(log), nullptr, log);
      std::cerr << "Vertex shader error: " << log << "\n";
      return false;
    }

    glGetShaderiv(frag, GL_COMPILE_STATUS, &success);
    if (!success) {
      char log[512] = {};
      glGetShaderInfoLog(frag, sizeof(log), nullptr, log);
      std::cerr << "Fragment shader error: " << log << "\n";
      return false;
    }

    id = glCreateProgram();
    glAttachShader(id, vert);
    glAttachShader(id, frag);
    glLinkProgram(id);

    glGetProgramiv(id, GL_LINK_STATUS, &success);
    if (!success) {
      char log[512] = {};
      glGetProgramInfoLog(id, sizeof(log), nullptr, log);
      std::cerr << "Shader link error: " << log << "\n";
      return false;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return true;
  }

  void Use() const {
    glUseProgram(id);
  }

  void SetMat4(const char* name, const glm::mat4& value) const {
    glUniformMatrix4fv(glGetUniformLocation(id, name), 1, GL_FALSE, glm::value_ptr(value));
  }

  void SetVec3(const char* name, const glm::vec3& value) const {
    glUniform3fv(glGetUniformLocation(id, name), 1, glm::value_ptr(value));
  }

  void SetMat3(const char* name, const glm::mat3& value) const {
    glUniformMatrix3fv(glGetUniformLocation(id, name), 1, GL_FALSE, glm::value_ptr(value));
  }

  void SetInt(const char* name, int value) const {
    glUniform1i(glGetUniformLocation(id, name), value);
  }
};

struct Player {
  glm::vec3 position{0.0f, 2.0f, 0.0f};
  glm::vec3 velocity{0.0f};
  float halfSize = 0.5f;
  bool onGround = false;
};

struct Enemy {
  glm::vec3 position{4.0f, 0.0f, -4.0f};
  glm::vec3 velocity{0.0f};
  float halfSize = 0.45f;
  float speed = 3.2f;
  bool onGround = false;
  float jumpCooldown = 0.0f;
};

struct Platform {
  glm::vec3 position;
  glm::vec3 halfExtents;
  glm::vec3 tint;
};

struct CloudPuff {
  glm::vec3 offset;
  glm::vec3 scale;
};

struct CloudCluster {
  glm::vec3 basePosition;
  glm::vec2 driftDir;
  float driftSpeed = 0.0f;
  float hueOffset = 0.0f;
  std::vector<CloudPuff> puffs;
};

struct Cat {
  glm::vec3 position;
  glm::vec3 velocity = glm::vec3(0.0f);
  bool collected = false;
  
  // AI state
  enum class Behavior { Idle, Wandering, Following };
  Behavior behavior = Behavior::Idle;
  float behaviorTimer = 0.0f;
  glm::vec3 wanderTarget = glm::vec3(0.0f);
  enum class IdleAnim { None, Groom, Loaf, Roll, Groomed };
  IdleAnim idleAnim = IdleAnim::None;
  float idleAnimTimer = 0.0f;
  float idleAnimPhase = 0.0f;
  int groomTarget = -1;
  float rollHold = 0.0f;
  
  // Personality
  float moveSpeed = 3.0f;
  float turnSpeed = 5.0f;
  float facing = 0.0f;
  float walkCycle = 0.0f;
  unsigned int seed = 0;
};

struct Dog {
  glm::vec3 position;
  bool collected = false;
  float bobOffset = 0.0f;
  glm::vec3 velocity{0.0f};
  bool onGround = false;
  enum class Behavior { Idle, Wandering, Following };
  Behavior behavior = Behavior::Idle;
  float behaviorTimer = 0.0f;
  glm::vec3 wanderTarget{0.0f};
  float facing = 0.0f;
  float walkCycle = 0.0f;
  float moveSpeed = 2.8f;
  float turnSpeed = 5.0f;
  unsigned int seed = 0;
};

struct Bomb {
  glm::vec3 position{0.0f};
  glm::vec3 velocity{0.0f};
  float timer = 0.0f;
  bool active = false;
};

static void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
  (void)window;
  glViewport(0, 0, width, height);
}

static GLuint BuildCheckerTexture() {
  const int size = 64;
  std::vector<unsigned char> pixels(size * size * 3);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const int idx = (y * size + x) * 3;
      const bool odd = ((x / 8) + (y / 8)) % 2 == 1;
      const unsigned char color = odd ? 220 : 40;
      pixels[idx + 0] = color;
      pixels[idx + 1] = color;
      pixels[idx + 2] = color;
    }
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

static GLuint BuildStripeTexture(unsigned char a, unsigned char b) {
  const int size = 64;
  std::vector<unsigned char> pixels(size * size * 3);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const int idx = (y * size + x) * 3;
      const bool odd = ((x / 6) % 2) == 1;
      const unsigned char color = odd ? a : b;
      pixels[idx + 0] = color;
      pixels[idx + 1] = color;
      pixels[idx + 2] = color;
    }
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

static GLuint BuildDotsTexture(unsigned char base, unsigned char dot) {
  const int size = 64;
  std::vector<unsigned char> pixels(size * size * 3, base);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const int cellX = x % 16;
      const int cellY = y % 16;
      const int dx = cellX - 8;
      const int dy = cellY - 8;
      if (dx * dx + dy * dy <= 16) {
        const int idx = (y * size + x) * 3;
        pixels[idx + 0] = dot;
        pixels[idx + 1] = dot;
        pixels[idx + 2] = dot;
      }
    }
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

static GLuint BuildCatTexture() {
  const int size = 128;
  std::vector<unsigned char> pixels(size * size * 3);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const float fx = static_cast<float>(x) / static_cast<float>(size - 1);
      const float fy = static_cast<float>(y) / static_cast<float>(size - 1);
      const float stripe = 0.04f * std::sin(fx * 10.0f + fy * 6.0f) +
                           0.03f * std::sin(fx * 22.0f - fy * 4.0f);
      const int cellX = (x + 8) % 32;
      const int cellY = (y + 12) % 32;
      const int dx = cellX - 16;
      const int dy = cellY - 16;
      const float spot = (dx * dx + dy * dy <= 40) ? -0.12f : 0.0f;
      float base = 0.86f + stripe + spot + 0.04f * (0.5f - fy);
      base = glm::clamp(base, 0.65f, 1.0f);
      const int idx = (y * size + x) * 3;
      pixels[idx + 0] = static_cast<unsigned char>(base * 240.0f + 10.0f);
      pixels[idx + 1] = static_cast<unsigned char>(base * 220.0f + 8.0f);
      pixels[idx + 2] = static_cast<unsigned char>(base * 210.0f + 6.0f);
    }
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

static GLuint BuildPlankTexture() {
  const int size = 128;
  std::vector<unsigned char> pixels(size * size * 3);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const float fx = static_cast<float>(x) / static_cast<float>(size);
      const float fy = static_cast<float>(y) / static_cast<float>(size);
      const int plank = (x / 16) % 2;
      const float grain = 0.12f * std::sin(fy * 40.0f + fx * 6.0f);
      const float edge = (x % 16 == 0 || x % 16 == 15) ? -0.25f : 0.0f;
      float base = 0.55f + grain + edge + (plank ? 0.05f : -0.03f);
      base = glm::clamp(base, 0.2f, 0.9f);
      const unsigned char r = static_cast<unsigned char>(base * 140.0f + 60.0f);
      const unsigned char g = static_cast<unsigned char>(base * 110.0f + 50.0f);
      const unsigned char b = static_cast<unsigned char>(base * 80.0f + 40.0f);
      const int idx = (y * size + x) * 3;
      pixels[idx + 0] = r;
      pixels[idx + 1] = g;
      pixels[idx + 2] = b;
    }
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

static GLuint BuildFabricTexture(unsigned char base, unsigned char stripe) {
  const int size = 128;
  std::vector<unsigned char> pixels(size * size * 3);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const int idx = (y * size + x) * 3;
      const bool band = ((x / 10) % 2) == 0;
      const int weave = ((x % 4) == 0 || (y % 4) == 0) ? -8 : 0;
      const unsigned char c = static_cast<unsigned char>(glm::clamp<int>(base + (band ? stripe : 0) + weave, 20, 230));
      pixels[idx + 0] = c;
      pixels[idx + 1] = static_cast<unsigned char>(glm::clamp<int>(c - 10, 0, 255));
      pixels[idx + 2] = static_cast<unsigned char>(glm::clamp<int>(c - 20, 0, 255));
    }
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

static GLuint BuildSkinTexture() {
  const int size = 64;
  std::vector<unsigned char> pixels(size * size * 3);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const float fx = static_cast<float>(x) / static_cast<float>(size);
      const float fy = static_cast<float>(y) / static_cast<float>(size);
      const float vignette = 0.85f + 0.2f * (1.0f - std::abs(fx - 0.5f) * 2.0f);
      const float freckles = ((x + y * 7) % 17 == 0) ? -0.08f : 0.0f;
      float base = 0.86f * vignette + freckles;
      base = glm::clamp(base, 0.6f, 0.95f);
      const int idx = (y * size + x) * 3;
      pixels[idx + 0] = static_cast<unsigned char>(base * 230.0f + 10.0f);
      pixels[idx + 1] = static_cast<unsigned char>(base * 190.0f + 15.0f);
      pixels[idx + 2] = static_cast<unsigned char>(base * 160.0f + 20.0f);
    }
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

static GLuint BuildMetalTexture() {
  const int size = 64;
  std::vector<unsigned char> pixels(size * size * 3);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const float fx = static_cast<float>(x) / static_cast<float>(size);
      const float streak = 0.1f * std::sin(fx * 24.0f) + 0.05f * std::sin(fx * 60.0f);
      float base = 0.75f + streak;
      base = glm::clamp(base, 0.55f, 0.9f);
      const int idx = (y * size + x) * 3;
      pixels[idx + 0] = static_cast<unsigned char>(base * 220.0f);
      pixels[idx + 1] = static_cast<unsigned char>(base * 220.0f);
      pixels[idx + 2] = static_cast<unsigned char>(base * 235.0f);
    }
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

static GLuint BuildCloudTexture() {
  const int size = 128;
  std::vector<unsigned char> pixels(size * size * 3);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const float fx = static_cast<float>(x) / static_cast<float>(size - 1);
      const float fy = static_cast<float>(y) / static_cast<float>(size - 1);
      const float dx = (fx - 0.5f) * 2.0f;
      const float dy = (fy - 0.5f) * 2.0f;
      const float puff = std::exp(-(dx * dx * 1.6f + dy * dy * 3.0f));
      const float wisps = 0.08f * std::sin(fx * 20.0f) + 0.06f * std::sin((fx + fy) * 16.0f);
      float shade = 0.72f + puff * 0.24f + wisps;
      shade = glm::clamp(shade, 0.62f, 1.0f);
      const int idx = (y * size + x) * 3;
      pixels[idx + 0] = static_cast<unsigned char>(shade * 245.0f);
      pixels[idx + 1] = static_cast<unsigned char>(shade * 242.0f);
      pixels[idx + 2] = static_cast<unsigned char>(shade * 236.0f);
    }
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

int main() {
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW\n";
    return 1;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow* window = glfwCreateWindow(1280, 720, "Vibe 3D", nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create window\n";
    glfwTerminate();
    return 1;
  }

  glfwMakeContextCurrent(window);
  glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
  glfwSwapInterval(1);

  if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
    std::cerr << "Failed to load OpenGL\n";
    glfwTerminate();
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  float uiScale = 1.15f;
  ImGuiStyle baseImGuiStyle = ImGui::GetStyle();
  auto ApplyUiScale = [&](float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = baseImGuiStyle;
    style.ScaleAllSizes(scale);
    ImGui::GetIO().FontGlobalScale = scale;
  };
  ApplyUiScale(uiScale);
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  glEnable(GL_DEPTH_TEST);

  Shader shader;
  const std::string shaderDir = std::string(VIBE_SHADER_DIR);
  if (!shader.Load(shaderDir + "/standard.vert", shaderDir + "/standard.frag")) {
    glfwTerminate();
    return 1;
  }

    const float cubeVertices[] = {
      // positions          // normals           // uvs
      -0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
       0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,  1.0f, 0.0f,
       0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
       0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
      -0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,  0.0f, 1.0f,
      -0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,  0.0f, 0.0f,

      -0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,  0.0f, 0.0f,
       0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,  1.0f, 0.0f,
       0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
       0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
      -0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,  0.0f, 1.0f,
      -0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,  0.0f, 0.0f,

      -0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
      -0.5f,  0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
      -0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
      -0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
      -0.5f, -0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
      -0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

       0.5f,  0.5f,  0.5f,   1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
       0.5f,  0.5f, -0.5f,   1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
       0.5f, -0.5f, -0.5f,   1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
       0.5f, -0.5f, -0.5f,   1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
       0.5f, -0.5f,  0.5f,   1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
       0.5f,  0.5f,  0.5f,   1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

      -0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,  0.0f, 1.0f,
       0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,  1.0f, 1.0f,
       0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
       0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
      -0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,  0.0f, 0.0f,
      -0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,  0.0f, 1.0f,

      -0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
       0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,  1.0f, 1.0f,
       0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
       0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
      -0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,  0.0f, 0.0f,
      -0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
    };

  GLuint vao = 0;
  GLuint vbo = 0;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
  glBindVertexArray(0);

  const GLuint platformTexture = BuildPlankTexture();
  const GLuint playerTexture = BuildFabricTexture(90, 70);
  const GLuint playerSkinTexture = BuildSkinTexture();
  const GLuint clownTexture = BuildFabricTexture(160, 40);
  const GLuint clownSkinTexture = BuildSkinTexture();
  const GLuint clownAccentTexture = BuildDotsTexture(220, 60);
  const GLuint knifeTexture = BuildMetalTexture();
  const GLuint catTexture = BuildCatTexture();
  const GLuint carTexture = BuildMetalTexture();
  const GLuint cloudTexture = BuildCloudTexture();

  AudioState audio;
  if (ma_engine_init(nullptr, &audio.engine) == MA_SUCCESS) {
    audio.ready = true;
    const int sampleRate = 48000;
    CreateSound(audio.engine, audio.footstep, GenerateFootstep(sampleRate), false);
    CreateSound(audio.engine, audio.jump, GenerateJump(sampleRate), false);
    CreateSound(audio.engine, audio.land, GenerateLand(sampleRate), false);
    CreateSound(audio.engine, audio.ambient, GenerateAmbient(sampleRate), true);
    CreateSound(audio.engine, audio.chase, GenerateChase(sampleRate), false);
    ma_sound_set_volume(&audio.ambient.sound, 0.3f);
    ma_sound_set_volume(&audio.footstep.sound, 0.45f);
    ma_sound_set_volume(&audio.jump.sound, 0.5f);
    ma_sound_set_volume(&audio.land.sound, 0.5f);
    ma_sound_set_volume(&audio.chase.sound, 0.6f);
    ma_sound_start(&audio.ambient.sound);
  }

  Player player;
  const glm::vec3 playerSpawn{0.0f, 2.0f, 0.0f};
  Enemy clown;
  const float gravity = -18.0f;
  const float moveSpeed = 5.0f;
  const float sprintMultiplier = 1.6f;
  const float accelGround = 24.0f;
  const float accelAir = 10.0f;
  const float jumpSpeed = 7.0f;
  const float coyoteTimeMax = 0.12f;
  const float jumpBufferMax = 0.12f;
  float coyoteTimer = 0.0f;
  float jumpBufferTimer = 0.0f;
  float stamina = 1.0f;

  std::vector<Platform> platforms = {
      {{0.0f, -1.0f, 0.0f}, {22.0f, 0.5f, 22.0f}, {0.6f, 0.7f, 0.8f}},
      {{4.0f, 1.0f, 0.0f}, {1.8f, 0.3f, 1.8f}, {0.9f, 0.7f, 0.4f}},
      {{-3.0f, 2.2f, -2.5f}, {1.2f, 0.3f, 1.2f}, {0.5f, 0.9f, 0.6f}},
      {{7.0f, 3.2f, 2.5f}, {1.2f, 0.3f, 1.2f}, {0.6f, 0.8f, 0.9f}},
      {{-8.0f, 1.4f, 4.0f}, {2.0f, 0.3f, 1.0f}, {0.8f, 0.6f, 0.7f}},
      {{-11.0f, 3.0f, 6.0f}, {1.4f, 0.3f, 1.4f}, {0.7f, 0.8f, 0.5f}},
      {{10.0f, 1.8f, -6.0f}, {1.6f, 0.3f, 1.2f}, {0.6f, 0.9f, 0.7f}},
      {{14.0f, 3.0f, -8.0f}, {1.2f, 0.3f, 1.2f}, {0.9f, 0.8f, 0.5f}},
      {{-12.0f, 1.0f, -4.0f}, {1.2f, 0.3f, 1.2f}, {0.7f, 0.8f, 0.7f}},
      {{-14.0f, 1.6f, -6.0f}, {1.2f, 0.3f, 1.2f}, {0.7f, 0.6f, 0.9f}},
      {{-18.0f, 2.4f, -9.0f}, {1.1f, 0.3f, 1.1f}, {0.9f, 0.6f, 0.6f}},
      {{5.0f, 3.6f, 8.0f}, {1.2f, 0.3f, 1.2f}, {0.6f, 0.8f, 0.6f}},
      {{-2.0f, 4.2f, 8.5f}, {1.0f, 0.3f, 1.0f}, {0.8f, 0.7f, 0.6f}},
      {{-6.0f, 4.6f, 9.0f}, {1.0f, 0.3f, 1.0f}, {0.7f, 0.7f, 0.9f}},
  };

  std::vector<Cat> cats = {
      {{2.5f, 0.0f, -2.0f}},
      {{-4.0f, 0.0f, 3.0f}},
      {{6.0f, 2.0f, 1.5f}},
      {{-9.0f, 2.4f, 4.0f}},
      {{-12.0f, 3.8f, 6.0f}},
      {{10.0f, 2.2f, -5.5f}},
      {{14.0f, 3.4f, -8.0f}},
      {{-14.0f, 2.2f, -6.0f}},
      {{-18.0f, 2.8f, -9.0f}},
      {{-6.0f, 5.0f, 9.0f}},
  };
  
  // Initialize cat personalities
  unsigned int catSeed = 42u;
  for (Cat& cat : cats) {
    cat.seed = catSeed;
    catSeed = catSeed * 1664525u + 1013904223u;
    cat.moveSpeed = 2.5f + RandomFloat(cat.seed) * 2.0f; // 2.5-4.5 speed
    cat.turnSpeed = 4.0f + RandomFloat(cat.seed) * 3.0f; // 4-7 turn rate
    cat.facing = RandomFloat(cat.seed) * 6.28318f;
    cat.behaviorTimer = RandomFloat(cat.seed) * 3.0f;
    cat.behavior = Cat::Behavior::Idle;
    cat.idleAnimTimer = 0.5f + RandomFloat(cat.seed) * 2.0f;
  }
  std::vector<Dog> dogs = {
      {{-14.0f, 0.35f, -16.0f}, false, 0.1f},
      {{-8.0f, 0.35f, -18.0f}, false, 0.4f},
      {{-2.0f, 0.35f, -15.0f}, false, 1.0f},
      {{4.0f, 0.35f, -17.0f}, false, 1.7f},
      {{11.0f, 0.35f, -14.0f}, false, 2.1f},
      {{16.0f, 0.35f, -8.0f}, false, 2.6f},
      {{14.0f, 0.35f, -1.0f}, false, 3.0f},
      {{9.0f, 0.35f, 4.0f}, false, 3.4f},
      {{2.0f, 0.35f, 7.0f}, false, 3.9f},
      {{-5.0f, 0.35f, 9.0f}, false, 4.3f},
      {{-11.0f, 0.35f, 12.0f}, false, 4.8f},
      {{-17.0f, 0.35f, 9.0f}, false, 5.2f},
      {{-19.0f, 0.35f, 2.0f}, false, 5.7f},
      {{-18.0f, 0.35f, -5.0f}, false, 6.1f},
      {{-12.0f, 0.35f, -8.0f}, false, 0.7f},
      {{-6.0f, 0.35f, -6.0f}, false, 1.4f},
      {{0.0f, 0.35f, -2.0f}, false, 2.9f},
      {{7.0f, 0.35f, -4.0f}, false, 3.7f},
      {{12.0f, 0.35f, 10.0f}, false, 4.9f},
      {{-2.0f, 0.35f, 15.0f}, false, 5.9f},
  };
  std::vector<Bomb> bombs(12);
  unsigned int dogSeed = 9001u;
  for (Dog& dog : dogs) {
    dog.seed = dogSeed;
    dogSeed = dogSeed * 1664525u + 1013904223u;
    dog.moveSpeed = 2.4f + RandomFloat(dog.seed) * 1.6f;
    dog.turnSpeed = 4.0f + RandomFloat(dog.seed) * 2.5f;
    dog.behaviorTimer = 0.6f + RandomFloat(dog.seed) * 2.2f;
    dog.facing = RandomFloat(dog.seed) * 6.28318f;
    dog.wanderTarget = dog.position;
  }
  Enemy mummy;
  mummy.position = glm::vec3(-2.0f, 0.45f, -10.0f);
  mummy.speed = 2.2f;
  float mummyFacing = 0.0f;
  float mummyWalkCycle = 0.0f;
  float mummyThrowCooldown = 1.4f;
  const glm::vec3 levelOneSpawn = playerSpawn;
  const glm::vec3 levelTwoSpawn(-16.0f, 2.0f, -16.0f);
  const glm::vec3 carPositionLevel1(18.0f, 0.0f, 16.0f);
  const glm::vec3 carPositionLevel2(-18.0f, 0.0f, 18.0f);
  enum class GameLevel { Level1Cats, Level2Dogs };
  GameLevel currentLevel = GameLevel::Level1Cats;
  bool levelOneAnnounced = false;
  const std::vector<CloudCluster> clouds = {
      {glm::vec3(-16.0f, 14.0f, -18.0f), glm::normalize(glm::vec2(1.0f, 0.2f)), 0.55f, 0.05f,
       {{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(5.2f, 1.1f, 2.6f)},
        {glm::vec3(3.2f, 0.25f, 0.5f), glm::vec3(3.6f, 0.95f, 2.1f)},
        {glm::vec3(-3.0f, 0.2f, -0.6f), glm::vec3(3.4f, 0.9f, 1.9f)},
        {glm::vec3(1.0f, 0.45f, -1.1f), glm::vec3(2.8f, 0.85f, 1.6f)}}},
      {glm::vec3(4.0f, 16.5f, -24.0f), glm::normalize(glm::vec2(0.9f, -0.3f)), 0.42f, 0.12f,
       {{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(6.0f, 1.25f, 2.9f)},
        {glm::vec3(3.8f, 0.35f, -0.8f), glm::vec3(4.1f, 1.0f, 2.2f)},
        {glm::vec3(-3.6f, 0.25f, 0.7f), glm::vec3(4.0f, 1.0f, 2.15f)},
        {glm::vec3(0.6f, 0.55f, 1.3f), glm::vec3(3.2f, 0.95f, 1.75f)}}},
      {glm::vec3(20.0f, 15.0f, -10.0f), glm::normalize(glm::vec2(0.8f, 0.55f)), 0.38f, 0.18f,
       {{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(5.0f, 1.0f, 2.4f)},
        {glm::vec3(2.7f, 0.28f, 0.9f), glm::vec3(3.3f, 0.82f, 1.8f)},
        {glm::vec3(-2.9f, 0.22f, -0.7f), glm::vec3(3.1f, 0.8f, 1.7f)},
        {glm::vec3(0.2f, 0.45f, -1.2f), glm::vec3(2.6f, 0.75f, 1.45f)}}},
      {glm::vec3(-24.0f, 13.8f, 10.0f), glm::normalize(glm::vec2(1.0f, -0.45f)), 0.5f, 0.09f,
       {{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(4.8f, 0.95f, 2.3f)},
        {glm::vec3(2.9f, 0.2f, 0.7f), glm::vec3(3.2f, 0.8f, 1.7f)},
        {glm::vec3(-2.5f, 0.18f, -0.6f), glm::vec3(3.0f, 0.78f, 1.65f)},
        {glm::vec3(0.1f, 0.42f, 1.15f), glm::vec3(2.4f, 0.72f, 1.35f)}}},
      {glm::vec3(10.0f, 17.2f, 22.0f), glm::normalize(glm::vec2(0.7f, 0.5f)), 0.34f, 0.22f,
       {{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(5.6f, 1.18f, 2.7f)},
        {glm::vec3(3.4f, 0.32f, -0.9f), glm::vec3(3.9f, 0.96f, 2.1f)},
        {glm::vec3(-3.2f, 0.26f, 0.8f), glm::vec3(3.7f, 0.92f, 2.0f)},
        {glm::vec3(0.8f, 0.52f, 1.4f), glm::vec3(3.0f, 0.88f, 1.65f)}}},
  };
  bool hasWon = false;
  bool winAnnounced = false;

  float lastTime = static_cast<float>(glfwGetTime());
  float yaw = glm::radians(45.0f);
  float pitch = glm::radians(-20.0f);
  float cameraDistance = 6.0f;
  float playerFacing = 0.0f;
  float clownFacing = 0.0f;
  glm::vec3 cameraPosSmooth(0.0f);
  glm::vec3 cameraTargetSmooth(0.0f);
  bool cameraInitialized = false;
  float playerWalkCycle = 0.0f;
  float clownWalkCycle = 0.0f;
  bool wasPlayerOnGround = false;
  bool wasClownOnGround = false;
  float footstepTimer = 0.0f;
  float chaseTimer = 0.0f;
  bool isPaused = false;
  bool wasEscapeDown = false;
  bool wasPDown = false;
  bool invertLookY = false;
  bool showDebugHud = false;
  float lastAppliedUiScale = uiScale;
  float mouseSensitivity = 0.005f;
  float musicVolume = 0.3f;
  float sfxVolume = 1.0f;
  int collectedCount = 0;

  shader.Use();
  shader.SetInt("uTexture", 0);

  while (!glfwWindowShouldClose(window)) {
    const float currentTime = static_cast<float>(glfwGetTime());
    const float deltaTime = currentTime - lastTime;
    lastTime = currentTime;

    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const bool escapeDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    const bool pDown = glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS;
    if ((escapeDown && !wasEscapeDown) || (pDown && !wasPDown)) {
      isPaused = !isPaused;
    }
    wasEscapeDown = escapeDown;
    wasPDown = pDown;

    if (std::abs(uiScale - lastAppliedUiScale) > 0.001f) {
      ApplyUiScale(uiScale);
      lastAppliedUiScale = uiScale;
    }

    if (isPaused) {
      player.velocity = glm::vec3(0.0f);
      clown.velocity.x = 0.0f;
      clown.velocity.z = 0.0f;
      mummy.velocity.x = 0.0f;
      mummy.velocity.z = 0.0f;
    }

    glm::vec3 cameraForward = glm::normalize(glm::vec3(
        std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::cos(yaw) * std::cos(pitch)));

    static double lastX = 0.0;
    static double lastY = 0.0;
    static bool first = true;
    if (!isPaused && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
      double x = 0.0;
      double y = 0.0;
      glfwGetCursorPos(window, &x, &y);
      if (first) {
        lastX = x;
        lastY = y;
        first = false;
      }
      const float dx = static_cast<float>(x - lastX) * mouseSensitivity;
      const float dySign = invertLookY ? -1.0f : 1.0f;
      const float dy = static_cast<float>(y - lastY) * mouseSensitivity * dySign;
      yaw -= dx;
      pitch -= dy;
      pitch = glm::clamp(pitch, glm::radians(-70.0f), glm::radians(20.0f));
      lastX = x;
      lastY = y;
    } else {
      first = true;
    }

    if (!isPaused) {
      glm::vec3 forwardXZ = glm::normalize(glm::vec3(cameraForward.x, 0.0f, cameraForward.z));
      glm::vec3 rightXZ = glm::normalize(glm::cross(forwardXZ, glm::vec3(0.0f, 1.0f, 0.0f)));

    glm::vec3 inputDir(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
      inputDir += forwardXZ;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
      inputDir -= forwardXZ;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
      inputDir += rightXZ;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
      inputDir -= rightXZ;
    }
    if (glm::length(inputDir) > 0.001f) {
      inputDir = glm::normalize(inputDir);
    }
    if (hasWon) {
      inputDir = glm::vec3(0.0f);
    }

    const bool wantsSprint = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS && stamina > 0.05f;
    const float targetSpeed = moveSpeed * (wantsSprint ? sprintMultiplier : 1.0f);
    const float accel = player.onGround ? accelGround : accelAir;
    const glm::vec3 targetVel = inputDir * targetSpeed;
    player.velocity.x = glm::mix(player.velocity.x, targetVel.x, glm::clamp(accel * deltaTime, 0.0f, 1.0f));
    player.velocity.z = glm::mix(player.velocity.z, targetVel.z, glm::clamp(accel * deltaTime, 0.0f, 1.0f));
    player.velocity.y += gravity * deltaTime;

    if (wantsSprint && glm::length(inputDir) > 0.1f) {
      stamina = glm::max(0.0f, stamina - deltaTime * 0.45f);
    } else {
      stamina = glm::min(1.0f, stamina + deltaTime * 0.35f);
    }

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
      jumpBufferTimer = jumpBufferMax;
    }

    player.position += player.velocity * deltaTime;

    player.onGround = false;
    const float groundTop = platforms[0].position.y + platforms[0].halfExtents.y;
    if (player.position.y - player.halfSize < groundTop) {
      player.position.y = groundTop + player.halfSize;
      player.velocity.y = 0.0f;
      player.onGround = true;
    }

    for (size_t i = 1; i < platforms.size(); ++i) {
      const Platform& platform = platforms[i];
      const float platformTop = platform.position.y + platform.halfExtents.y;
      const bool withinX = std::abs(player.position.x - platform.position.x) <= (platform.halfExtents.x + player.halfSize);
      const bool withinZ = std::abs(player.position.z - platform.position.z) <= (platform.halfExtents.z + player.halfSize);
      const bool falling = player.velocity.y <= 0.0f;
      if (withinX && withinZ && falling) {
        const float playerBottom = player.position.y - player.halfSize;
        if (playerBottom < platformTop && player.position.y > platformTop - 0.6f) {
          player.position.y = platformTop + player.halfSize;
          player.velocity.y = 0.0f;
          player.onGround = true;
        }
      }
    }

    if (player.onGround) {
      coyoteTimer = coyoteTimeMax;
    } else {
      coyoteTimer = glm::max(0.0f, coyoteTimer - deltaTime);
    }
    jumpBufferTimer = glm::max(0.0f, jumpBufferTimer - deltaTime);

    if (jumpBufferTimer > 0.0f && coyoteTimer > 0.0f) {
      player.velocity.y = jumpSpeed;
      player.onGround = false;
      coyoteTimer = 0.0f;
      jumpBufferTimer = 0.0f;
      if (audio.ready) {
        PlaySound(audio.jump);
      }
    }

    if (currentLevel == GameLevel::Level1Cats) {
    const float enemyGround = platforms[0].position.y + platforms[0].halfExtents.y + clown.halfSize;
    const float playerDistance = glm::length(player.position - clown.position);
    const float aggroRange = 18.0f;
    const bool hasLineOfSight = playerDistance < aggroRange;
    glm::vec3 aiTarget = player.position;

    if (hasLineOfSight && player.position.y > clown.position.y + 0.6f) {
      float bestScore = 1e9f;
      for (const Platform& platform : platforms) {
        const float platformTop = platform.position.y + platform.halfExtents.y + clown.halfSize;
        if (platformTop > clown.position.y + 0.4f && platformTop <= player.position.y + 0.3f) {
          const float distToPlayer = glm::length(glm::vec2(platform.position.x - player.position.x,
                                                           platform.position.z - player.position.z));
          const float distToClown = glm::length(glm::vec2(platform.position.x - clown.position.x,
                                                          platform.position.z - clown.position.z));
          const float score = distToPlayer + distToClown * 0.4f + std::abs(platformTop - player.position.y);
          if (score < bestScore) {
            bestScore = score;
            aiTarget = glm::vec3(platform.position.x, platformTop, platform.position.z);
          }
        }
      }
    }

    glm::vec3 chaseDir = aiTarget - clown.position;
    chaseDir.y = 0.0f;
    if (glm::length(chaseDir) > 0.001f) {
      chaseDir = glm::normalize(chaseDir);
    }

    const float closeRange = 4.5f;
    const float speedRamp = 1.0f + glm::clamp((closeRange - playerDistance) / closeRange, 0.0f, 1.0f) * 0.6f;
    const float clownChaseSpeed = clown.speed * speedRamp * 1.15f;
    clown.velocity.x = chaseDir.x * clownChaseSpeed;
    clown.velocity.z = chaseDir.z * clownChaseSpeed;
    clown.velocity.y += gravity * deltaTime;

    if (clown.jumpCooldown > 0.0f) {
      clown.jumpCooldown -= deltaTime;
    }

    const float playerHeightGap = player.position.y - clown.position.y;
    const float playerHorizDist = glm::length(glm::vec2(player.position.x - clown.position.x,
                                                       player.position.z - clown.position.z));
    if (!hasWon && clown.onGround && clown.jumpCooldown <= 0.0f && playerHeightGap > 0.2f && playerHorizDist < 6.5f) {
      const float jumpHeight = glm::clamp(playerHeightGap + 0.4f, 0.8f, 2.4f);
      const float jumpVelocity = std::sqrt(2.0f * -gravity * jumpHeight);
      clown.velocity.y = jumpVelocity;
      clown.jumpCooldown = 0.45f;
    }

    if (audio.ready) {
      footstepTimer -= deltaTime;
      const float playerSpeed = glm::length(glm::vec2(player.velocity.x, player.velocity.z));
      if (player.onGround && playerSpeed > 0.2f && footstepTimer <= 0.0f) {
        PlaySound(audio.footstep);
        footstepTimer = 0.35f - glm::clamp(playerSpeed / (moveSpeed * sprintMultiplier), 0.0f, 1.0f) * 0.15f;
      }

      if (!wasPlayerOnGround && player.onGround) {
        PlaySound(audio.land);
      }
      wasPlayerOnGround = player.onGround;

      if (!wasClownOnGround && clown.onGround) {
        PlaySound(audio.land);
      }
      wasClownOnGround = clown.onGround;

      chaseTimer -= deltaTime;
      if (playerDistance < 5.5f && chaseTimer <= 0.0f) {
        PlaySound(audio.chase);
        chaseTimer = 2.5f;
      }
    }

    if (hasWon) {
      clown.velocity.x = 0.0f;
      clown.velocity.z = 0.0f;
    }

    clown.position += clown.velocity * deltaTime;

    clown.onGround = false;
    if (clown.position.y < enemyGround) {
      clown.position.y = enemyGround;
      clown.velocity.y = 0.0f;
      clown.onGround = true;
    }

    for (size_t i = 1; i < platforms.size(); ++i) {
      const Platform& platform = platforms[i];
      const float platformTop = platform.position.y + platform.halfExtents.y;
      const bool withinX = std::abs(clown.position.x - platform.position.x) <= (platform.halfExtents.x + clown.halfSize);
      const bool withinZ = std::abs(clown.position.z - platform.position.z) <= (platform.halfExtents.z + clown.halfSize);
      const bool falling = clown.velocity.y <= 0.0f;
      if (withinX && withinZ && falling) {
        const float clownBottom = clown.position.y - clown.halfSize;
        if (clownBottom < platformTop && clown.position.y > platformTop - 0.6f) {
          clown.position.y = platformTop + clown.halfSize;
          clown.velocity.y = 0.0f;
          clown.onGround = true;
        }
      }
    }

    const float hitDistance = player.halfSize + clown.halfSize + 0.1f;
    if (glm::distance(player.position, clown.position) < hitDistance) {
      player.position = levelOneSpawn;
      player.velocity = glm::vec3(0.0f);
      clown.position = glm::vec3(4.0f, enemyGround, -4.0f);
      clown.velocity = glm::vec3(0.0f);
      clown.onGround = true;
    }

      collectedCount = 0;
    for (Cat& cat : cats) {
      if (!cat.collected && glm::distance(player.position, cat.position) < 1.2f) {
        cat.collected = true;
        cat.behavior = Cat::Behavior::Following;
        cat.behaviorTimer = 0.0f;
      }
      if (cat.collected) {
        collectedCount++;
      }
    }

    // Update cat AI and physics
    for (size_t catIdx = 0; catIdx < cats.size(); ++catIdx) {
      Cat& cat = cats[catIdx];
      cat.behaviorTimer -= deltaTime;
      cat.idleAnimTimer -= deltaTime;
      if (cat.idleAnim != Cat::IdleAnim::None) {
        cat.idleAnimPhase += deltaTime;
        if (cat.idleAnimTimer <= 0.0f) {
          cat.idleAnim = Cat::IdleAnim::None;
          cat.idleAnimPhase = 0.0f;
          cat.groomTarget = -1;
          cat.rollHold = 0.0f;
        }
      }
      
      // Apply gravity
      const float catGravity = -18.0f;
      cat.velocity.y += catGravity * deltaTime;
      
      // Ground collision
      const float catRadius = 0.3f;
      if (cat.position.y - catRadius < 0.0f) {
        cat.position.y = catRadius;
        cat.velocity.y = 0.0f;
      }
      
      // Platform collision
      for (size_t i = 1; i < platforms.size(); ++i) {
        const Platform& platform = platforms[i];
        const float platformTop = platform.position.y + platform.halfExtents.y;
        const bool withinX = std::abs(cat.position.x - platform.position.x) <= (platform.halfExtents.x + catRadius);
        const bool withinZ = std::abs(cat.position.z - platform.position.z) <= (platform.halfExtents.z + catRadius);
        const bool falling = cat.velocity.y <= 0.0f;
        if (withinX && withinZ && falling) {
          const float catBottom = cat.position.y - catRadius;
          if (catBottom < platformTop && cat.position.y > platformTop - 0.6f) {
            cat.position.y = platformTop + catRadius;
            cat.velocity.y = 0.0f;
          }
        }
      }
      
      glm::vec3 desiredVelocity(0.0f);
      float distToTarget = 999.0f;
      const float playerDist2D = glm::length(glm::vec2(player.position.x - cat.position.x,
                                                       player.position.z - cat.position.z));
      
      if (cat.collected) {
        // Following AI - move toward area around player
        if (cat.behaviorTimer <= 0.0f || playerDist2D > 5.5f) {
          // Pick new target near player
          const float angle = RandomFloat(cat.seed) * 6.28318f;
          const float radius = (playerDist2D > 5.5f) ? 0.6f : (1.6f + RandomFloat(cat.seed) * 1.8f);
          cat.wanderTarget = player.position + glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
          cat.behaviorTimer = (playerDist2D > 5.5f) ? 0.5f : (1.4f + RandomFloat(cat.seed) * 1.6f);
        }
        
        const glm::vec3 toTarget = cat.wanderTarget - cat.position;
        distToTarget = glm::length(glm::vec2(toTarget.x, toTarget.z));
        
        if (distToTarget > 0.35f) {
          const glm::vec3 dir = glm::normalize(glm::vec3(toTarget.x, 0.0f, toTarget.z));
          const float catchup = glm::clamp((playerDist2D - 2.5f) / 6.0f, 0.0f, 1.0f);
          const float targetSpeed = cat.moveSpeed * (1.0f + catchup * 1.2f);
          desiredVelocity = dir * targetSpeed;
          
          // Update facing
          const float targetFacing = std::atan2(dir.x, dir.z);
          float facingDiff = targetFacing - cat.facing;
          while (facingDiff > 3.14159f) facingDiff -= 6.28318f;
          while (facingDiff < -3.14159f) facingDiff += 6.28318f;
          cat.facing += facingDiff * cat.turnSpeed * deltaTime;
        }
      } else {
        // Wandering AI - explore randomly
        if (cat.behaviorTimer <= 0.0f) {
          const float roll = RandomFloat(cat.seed);
          if (roll < 0.4f) {
            // Idle
            cat.behavior = Cat::Behavior::Idle;
            cat.behaviorTimer = 1.0f + RandomFloat(cat.seed) * 2.0f;
          } else {
            // Wander
            cat.behavior = Cat::Behavior::Wandering;
            const float angle = RandomFloat(cat.seed) * 6.28318f;
            const float dist = 2.0f + RandomFloat(cat.seed) * 4.0f;
            cat.wanderTarget = cat.position + glm::vec3(std::cos(angle) * dist, 0.0f, std::sin(angle) * dist);
            cat.behaviorTimer = 2.0f + RandomFloat(cat.seed) * 3.0f;
          }
        }
        
        if (cat.behavior == Cat::Behavior::Wandering) {
          const glm::vec3 toTarget = cat.wanderTarget - cat.position;
          distToTarget = glm::length(glm::vec2(toTarget.x, toTarget.z));
          
          if (distToTarget > 0.5f) {
            const glm::vec3 dir = glm::normalize(glm::vec3(toTarget.x, 0.0f, toTarget.z));
            desiredVelocity = dir * (cat.moveSpeed * 0.5f); // Slower when wandering
            
            const float targetFacing = std::atan2(dir.x, dir.z);
            float facingDiff = targetFacing - cat.facing;
            while (facingDiff > 3.14159f) facingDiff -= 6.28318f;
            while (facingDiff < -3.14159f) facingDiff += 6.28318f;
            cat.facing += facingDiff * cat.turnSpeed * deltaTime;
          } else {
            cat.behavior = Cat::Behavior::Idle;
            cat.behaviorTimer = 1.0f + RandomFloat(cat.seed) * 2.0f;
          }
        }
      }

      const float speed2D = glm::length(glm::vec2(cat.velocity.x, cat.velocity.z));
      const bool canIdle = speed2D < 0.15f && cat.velocity.y == 0.0f &&
                           ((cat.collected && playerDist2D < 2.8f && distToTarget < 0.6f) ||
                            (!cat.collected && cat.behavior == Cat::Behavior::Idle));
      if (glm::length(glm::vec2(desiredVelocity.x, desiredVelocity.z)) > 0.2f) {
        cat.idleAnim = Cat::IdleAnim::None;
        cat.idleAnimTimer = 0.2f;
        cat.idleAnimPhase = 0.0f;
        cat.groomTarget = -1;
        cat.rollHold = 0.0f;
      } else if (canIdle && cat.idleAnim == Cat::IdleAnim::None && cat.idleAnimTimer <= 0.0f) {
        const float roll = RandomFloat(cat.seed);
        if (roll < 0.28f) {
          cat.idleAnim = Cat::IdleAnim::Groom;
          cat.idleAnimTimer = 12.0f + RandomFloat(cat.seed) * 18.0f;
          cat.groomTarget = -1;
          float nearestDist = 999.0f;
          for (size_t otherIdx = 0; otherIdx < cats.size(); ++otherIdx) {
            if (otherIdx == catIdx) {
              continue;
            }
            const Cat& other = cats[otherIdx];
            const float otherSpeed = glm::length(glm::vec2(other.velocity.x, other.velocity.z));
            const float dist = glm::length(glm::vec2(other.position.x - cat.position.x,
                                                      other.position.z - cat.position.z));
            if (otherSpeed < 0.2f && dist < 1.4f && dist < nearestDist) {
              nearestDist = dist;
              cat.groomTarget = static_cast<int>(otherIdx);
            }
          }
        } else if (roll < 0.72f) {
          cat.idleAnim = Cat::IdleAnim::Loaf;
          cat.idleAnimTimer = 20.0f + RandomFloat(cat.seed) * 220.0f;
        } else if (roll < 0.86f) {
          cat.idleAnim = Cat::IdleAnim::Roll;
          cat.rollHold = 4.0f + RandomFloat(cat.seed) * 4.0f;
          cat.idleAnimTimer = 2.0f + cat.rollHold + 2.0f;
        } else {
          cat.idleAnimTimer = 1.0f + RandomFloat(cat.seed) * 1.5f;
        }
        cat.idleAnimPhase = 0.0f;
      }

      if (cat.idleAnim == Cat::IdleAnim::Groom && cat.groomTarget >= 0) {
        const Cat& other = cats[static_cast<size_t>(cat.groomTarget)];
        const glm::vec3 toOther = other.position - cat.position;
        const float dist = glm::length(glm::vec2(toOther.x, toOther.z));
        if (dist < 1.8f) {
          const glm::vec3 dir = glm::normalize(glm::vec3(toOther.x, 0.0f, toOther.z));
          const float targetFacing = std::atan2(dir.x, dir.z);
          float facingDiff = targetFacing - cat.facing;
          while (facingDiff > 3.14159f) facingDiff -= 6.28318f;
          while (facingDiff < -3.14159f) facingDiff += 6.28318f;
          cat.facing += facingDiff * cat.turnSpeed * deltaTime;
          Cat& otherCat = cats[static_cast<size_t>(cat.groomTarget)];
          const float otherSpeed = glm::length(glm::vec2(otherCat.velocity.x, otherCat.velocity.z));
          if (otherSpeed < 0.2f && otherCat.velocity.y == 0.0f) {
            if (otherCat.idleAnim != Cat::IdleAnim::Groomed) {
              otherCat.idleAnimPhase = 0.0f;
            }
            otherCat.idleAnim = Cat::IdleAnim::Groomed;
            otherCat.idleAnimTimer = 1.2f;
            const float otherFacing = std::atan2(-dir.x, -dir.z);
            float otherDiff = otherFacing - otherCat.facing;
            while (otherDiff > 3.14159f) otherDiff -= 6.28318f;
            while (otherDiff < -3.14159f) otherDiff += 6.28318f;
            otherCat.facing += otherDiff * otherCat.turnSpeed * deltaTime;
          }
        } else {
          cat.groomTarget = -1;
        }
      }
      
      // Apply horizontal movement with smooth acceleration
      const float accel = (cat.collected ? 18.0f : 12.0f) * deltaTime;
      cat.velocity.x = glm::mix(cat.velocity.x, desiredVelocity.x, accel);
      cat.velocity.z = glm::mix(cat.velocity.z, desiredVelocity.z, accel);
      
      // Update walk cycle
      const float speed = glm::length(glm::vec2(cat.velocity.x, cat.velocity.z));
      cat.walkCycle += speed * deltaTime * 3.0f;
      
      // Apply velocity
      cat.position += cat.velocity * deltaTime;
    }

      if (!levelOneAnnounced && collectedCount >= 10 && glm::distance(player.position, carPositionLevel1) < 2.2f) {
        levelOneAnnounced = true;
        currentLevel = GameLevel::Level2Dogs;
        player.position = levelTwoSpawn;
        player.velocity = glm::vec3(0.0f);
        clown.velocity = glm::vec3(0.0f);
        mummy.position = glm::vec3(-2.0f, 0.45f, -10.0f);
        mummy.velocity = glm::vec3(0.0f);
        mummyThrowCooldown = 1.25f;
        for (Bomb& bomb : bombs) {
          bomb.active = false;
        }
        glfwSetWindowTitle(window, "Vibe 3D - Level 2: Rescue the Dogs");
        std::cout << "Level 2 unlocked! Collect 20 dogs and escape the mummy.\n";
      }
    } else {
      const float enemyGround = platforms[0].position.y + platforms[0].halfExtents.y + mummy.halfSize;
      const glm::vec3 toPlayer = player.position - mummy.position;
      glm::vec3 moveDir(toPlayer.x, 0.0f, toPlayer.z);
      if (glm::length(moveDir) > 0.001f) {
        moveDir = glm::normalize(moveDir);
      }

      const float desiredDistance = 8.0f;
      const float dist2D = glm::length(glm::vec2(toPlayer.x, toPlayer.z));
      const float approach = glm::clamp((dist2D - desiredDistance) / 6.0f, -1.0f, 1.0f);
      mummy.velocity.x = moveDir.x * mummy.speed * approach;
      mummy.velocity.z = moveDir.z * mummy.speed * approach;
      mummy.velocity.y += gravity * deltaTime;
      mummy.position += mummy.velocity * deltaTime;

      mummy.onGround = false;
      if (mummy.position.y < enemyGround) {
        mummy.position.y = enemyGround;
        mummy.velocity.y = 0.0f;
        mummy.onGround = true;
      }

      for (size_t i = 1; i < platforms.size(); ++i) {
        const Platform& platform = platforms[i];
        const float platformTop = platform.position.y + platform.halfExtents.y;
        const bool withinX = std::abs(mummy.position.x - platform.position.x) <= (platform.halfExtents.x + mummy.halfSize);
        const bool withinZ = std::abs(mummy.position.z - platform.position.z) <= (platform.halfExtents.z + mummy.halfSize);
        const bool falling = mummy.velocity.y <= 0.0f;
        if (withinX && withinZ && falling) {
          const float mummyBottom = mummy.position.y - mummy.halfSize;
          if (mummyBottom < platformTop && mummy.position.y > platformTop - 0.6f) {
            mummy.position.y = platformTop + mummy.halfSize;
            mummy.velocity.y = 0.0f;
            mummy.onGround = true;
          }
        }
      }

      mummyThrowCooldown -= deltaTime;
      if (mummyThrowCooldown <= 0.0f && dist2D < 26.0f) {
        for (Bomb& bomb : bombs) {
          if (!bomb.active) {
            bomb.active = true;
            bomb.timer = 3.5f;
            bomb.position = mummy.position + glm::vec3(0.0f, mummy.halfSize + 0.6f, 0.0f);
            glm::vec3 throwDir = player.position - bomb.position;
            throwDir.y = 0.0f;
            if (glm::length(throwDir) > 0.001f) {
              throwDir = glm::normalize(throwDir);
            }
            bomb.velocity = throwDir * (7.5f + glm::clamp(dist2D / 16.0f, 0.0f, 1.2f));
            bomb.velocity.y = 6.2f;
            break;
          }
        }
        mummyThrowCooldown = 1.1f;
      }

      const float bombGravity = -16.0f;
      const float blastRadius = 2.1f;
      const float groundTop = platforms[0].position.y + platforms[0].halfExtents.y;
      for (Bomb& bomb : bombs) {
        if (!bomb.active) {
          continue;
        }
        bomb.timer -= deltaTime;
        bomb.velocity.y += bombGravity * deltaTime;
        bomb.position += bomb.velocity * deltaTime;

        bool exploded = false;
        if (bomb.position.y <= groundTop + 0.25f) {
          bomb.position.y = groundTop + 0.25f;
          exploded = true;
        }
        if (bomb.timer <= 0.0f) {
          exploded = true;
        }

        if (exploded) {
          if (glm::distance(player.position, bomb.position) < blastRadius) {
            player.position = levelTwoSpawn;
            player.velocity = glm::vec3(0.0f);
          }
          bomb.active = false;
        }
      }

      const float dogRadius = 0.28f;
      const float dogGround = platforms[0].position.y + platforms[0].halfExtents.y + dogRadius;
      for (Dog& dog : dogs) {
        dog.behaviorTimer -= deltaTime;
        dog.velocity.y += gravity * deltaTime;

        if (!dog.collected && glm::distance(player.position, dog.position) < 1.15f) {
          dog.collected = true;
          dog.behavior = Dog::Behavior::Following;
          dog.behaviorTimer = 0.0f;
        }

        glm::vec3 desiredVelocity(0.0f);
        const float distToPlayer = glm::length(glm::vec2(player.position.x - dog.position.x,
                                                          player.position.z - dog.position.z));

        if (dog.collected) {
          if (dog.behaviorTimer <= 0.0f || distToPlayer > 4.8f) {
            const float angle = RandomFloat(dog.seed) * 6.28318f;
            const float radius = (distToPlayer > 4.8f) ? 0.45f : (1.2f + RandomFloat(dog.seed) * 1.5f);
            dog.wanderTarget = player.position + glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
            dog.behaviorTimer = (distToPlayer > 4.8f) ? 0.35f : (0.9f + RandomFloat(dog.seed) * 1.4f);
          }
          const glm::vec3 toTarget = dog.wanderTarget - dog.position;
          const float distToTarget = glm::length(glm::vec2(toTarget.x, toTarget.z));
          if (distToTarget > 0.25f && !hasWon) {
            const glm::vec3 dir = glm::normalize(glm::vec3(toTarget.x, 0.0f, toTarget.z));
            const float catchup = glm::clamp((distToPlayer - 2.0f) / 4.5f, 0.0f, 1.0f);
            desiredVelocity = dir * dog.moveSpeed * (1.0f + catchup * 1.1f);
            const float targetFacing = std::atan2(dir.x, dir.z);
            float facingDiff = targetFacing - dog.facing;
            while (facingDiff > 3.14159f) facingDiff -= 6.28318f;
            while (facingDiff < -3.14159f) facingDiff += 6.28318f;
            dog.facing += facingDiff * dog.turnSpeed * deltaTime;
          }
        } else {
          if (dog.behaviorTimer <= 0.0f) {
            if (RandomFloat(dog.seed) < 0.45f) {
              dog.behavior = Dog::Behavior::Idle;
              dog.behaviorTimer = 0.8f + RandomFloat(dog.seed) * 1.6f;
            } else {
              dog.behavior = Dog::Behavior::Wandering;
              const float angle = RandomFloat(dog.seed) * 6.28318f;
              const float dist = 1.2f + RandomFloat(dog.seed) * 2.5f;
              dog.wanderTarget = dog.position + glm::vec3(std::cos(angle) * dist, 0.0f, std::sin(angle) * dist);
              dog.behaviorTimer = 1.0f + RandomFloat(dog.seed) * 2.0f;
            }
          }

          if (dog.behavior == Dog::Behavior::Wandering) {
            const glm::vec3 toTarget = dog.wanderTarget - dog.position;
            const float distToTarget = glm::length(glm::vec2(toTarget.x, toTarget.z));
            if (distToTarget > 0.35f) {
              const glm::vec3 dir = glm::normalize(glm::vec3(toTarget.x, 0.0f, toTarget.z));
              desiredVelocity = dir * (dog.moveSpeed * 0.5f);
              const float targetFacing = std::atan2(dir.x, dir.z);
              float facingDiff = targetFacing - dog.facing;
              while (facingDiff > 3.14159f) facingDiff -= 6.28318f;
              while (facingDiff < -3.14159f) facingDiff += 6.28318f;
              dog.facing += facingDiff * dog.turnSpeed * deltaTime;
            }
          }
        }

        const float dogAccel = (dog.collected ? 16.0f : 10.0f) * deltaTime;
        dog.velocity.x = glm::mix(dog.velocity.x, desiredVelocity.x, glm::clamp(dogAccel, 0.0f, 1.0f));
        dog.velocity.z = glm::mix(dog.velocity.z, desiredVelocity.z, glm::clamp(dogAccel, 0.0f, 1.0f));

        dog.position += dog.velocity * deltaTime;

        dog.onGround = false;
        if (dog.position.y < dogGround) {
          dog.position.y = dogGround;
          dog.velocity.y = 0.0f;
          dog.onGround = true;
        }
        for (size_t i = 1; i < platforms.size(); ++i) {
          const Platform& platform = platforms[i];
          const float platformTop = platform.position.y + platform.halfExtents.y;
          const bool withinX = std::abs(dog.position.x - platform.position.x) <= (platform.halfExtents.x + dogRadius);
          const bool withinZ = std::abs(dog.position.z - platform.position.z) <= (platform.halfExtents.z + dogRadius);
          const bool falling = dog.velocity.y <= 0.0f;
          if (withinX && withinZ && falling) {
            const float dogBottom = dog.position.y - dogRadius;
            if (dogBottom < platformTop && dog.position.y > platformTop - 0.6f) {
              dog.position.y = platformTop + dogRadius;
              dog.velocity.y = 0.0f;
              dog.onGround = true;
            }
          }
        }

        const float speed = glm::length(glm::vec2(dog.velocity.x, dog.velocity.z));
        dog.walkCycle += speed * deltaTime * 3.2f;
      }

      collectedCount = 0;
      for (const Dog& dog : dogs) {
        if (dog.collected) {
          collectedCount++;
        }
      }

      if (!hasWon && collectedCount >= 20 && glm::distance(player.position, carPositionLevel2) < 2.2f) {
        hasWon = true;
        if (!winAnnounced) {
          winAnnounced = true;
          glfwSetWindowTitle(window, "Vibe 3D - You Win!");
          std::cout << "You rescued 20 dogs and escaped the mummy!\n";
        }
      }
    }
    }

    const glm::vec3 cameraOffset = cameraForward * -cameraDistance + glm::vec3(0.0f, 2.0f, 0.0f);
    const glm::vec3 cameraTarget = player.position + glm::vec3(0.0f, 0.9f, 0.0f);
    const glm::vec3 cameraPosTarget = player.position + cameraOffset;
    const float smoothStrength = 10.0f;
    const float smoothAlpha = 1.0f - std::exp(-smoothStrength * deltaTime);
    if (!cameraInitialized) {
      cameraPosSmooth = cameraPosTarget;
      cameraTargetSmooth = cameraTarget;
      cameraInitialized = true;
    }
    cameraPosSmooth = glm::mix(cameraPosSmooth, cameraPosTarget, smoothAlpha);
    cameraTargetSmooth = glm::mix(cameraTargetSmooth, cameraTarget, smoothAlpha);
    const glm::mat4 view = glm::lookAt(cameraPosSmooth, cameraTargetSmooth, glm::vec3(0.0f, 1.0f, 0.0f));

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    const float aspect = width > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

    const float sunsetPhase = 0.5f + 0.5f * std::sin(currentTime * 0.045f + 0.4f);
    const glm::vec3 skyCool(0.31f, 0.54f, 0.88f);
    const glm::vec3 skyWarm(0.96f, 0.56f, 0.36f);
    const glm::vec3 skyPurple(0.62f, 0.45f, 0.76f);
    const glm::vec3 clearColor = glm::mix(glm::mix(skyCool, skyPurple, 0.45f), skyWarm, 0.3f + sunsetPhase * 0.35f);
    const glm::vec3 lightColor = glm::mix(glm::vec3(1.0f, 0.9f, 0.78f), glm::vec3(1.0f, 0.62f, 0.44f), sunsetPhase);
    const glm::vec3 ambientColor = glm::mix(glm::vec3(0.24f, 0.31f, 0.42f), glm::vec3(0.38f, 0.3f, 0.36f), sunsetPhase);
    const glm::vec3 rimColor = glm::mix(glm::vec3(0.46f, 0.62f, 0.94f), glm::vec3(0.94f, 0.52f, 0.62f), sunsetPhase);

    glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader.Use();
    shader.SetMat4("uView", view);
    shader.SetMat4("uProj", proj);
    shader.SetVec3("uViewPos", cameraPosSmooth);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.2f)));
    shader.SetVec3("uLightColor", lightColor);
    shader.SetVec3("uAmbient", ambientColor);
    shader.SetVec3("uRimColor", rimColor);
    glUniform1f(glGetUniformLocation(shader.id, "uRimPower"), 2.0f);
    glUniform1f(glGetUniformLocation(shader.id, "uSpecPower"), 32.0f);
    glUniform1f(glGetUniformLocation(shader.id, "uSpecIntensity"), 0.35f);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);

    auto DrawCube = [&](const glm::vec3& position, const glm::vec3& scale, const glm::vec3& tint, GLuint tex) {
      glBindTexture(GL_TEXTURE_2D, tex);
      glm::mat4 model(1.0f);
      model = glm::translate(model, position);
      model = glm::scale(model, scale);
      shader.SetMat4("uModel", model);
      shader.SetMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
      shader.SetVec3("uTint", tint);
      glDrawArrays(GL_TRIANGLES, 0, 36);
    };

    for (const Platform& platform : platforms) {
      DrawCube(platform.position, platform.halfExtents * 2.0f, platform.tint, platformTexture);
    }

    auto WrapDrift = [](float value, float radius) {
      const float span = radius * 2.0f;
      float wrapped = std::fmod(value + radius, span);
      if (wrapped < 0.0f) {
        wrapped += span;
      }
      return wrapped - radius;
    };

    for (const CloudCluster& cloud : clouds) {
      const glm::vec2 drift = cloud.driftDir * cloud.driftSpeed * currentTime;
      const float wrappedX = WrapDrift(cloud.basePosition.x + drift.x, 54.0f);
      const float wrappedZ = WrapDrift(cloud.basePosition.z + drift.y, 54.0f);
      const glm::vec3 cloudCenter(wrappedX, cloud.basePosition.y, wrappedZ);
      const glm::vec3 cloudTint = glm::mix(glm::vec3(0.9f, 0.84f, 0.8f), glm::vec3(1.0f, 0.96f, 0.92f),
                                           0.35f + 0.65f * (0.5f + 0.5f * std::sin(currentTime * 0.03f + cloud.hueOffset)));
      for (const CloudPuff& puff : cloud.puffs) {
        DrawCube(cloudCenter + puff.offset, puff.scale, cloudTint, cloudTexture);
      }
    }

    if (currentLevel == GameLevel::Level1Cats) {
    int catIndex = 0;
    for (const Cat& cat : cats) {
      const float speed = glm::length(glm::vec2(cat.velocity.x, cat.velocity.z));
      const float walkAmount = glm::clamp(speed / 3.0f, 0.0f, 1.0f);
      float groom = 0.0f;
      float loaf = 0.0f;
      float roll = 0.0f;
      float groomed = 0.0f;
      const float catSeedOffset = static_cast<float>(catIndex) * 1.7f;
      if (cat.idleAnim == Cat::IdleAnim::Groom) {
        groom = 0.5f + 0.5f * std::sin(cat.idleAnimPhase * 1.6f);
      } else if (cat.idleAnim == Cat::IdleAnim::Loaf) {
        loaf = 1.0f;
      } else if (cat.idleAnim == Cat::IdleAnim::Roll) {
        const float rollIn = 1.0f;
        const float rollOut = 1.0f;
        const float hold = cat.rollHold;
        const float t = cat.idleAnimPhase;
        if (t < rollIn) {
          roll = glm::mix(0.0f, 1.6f, t / rollIn);
        } else if (t < rollIn + hold) {
          roll = 1.6f;
        } else if (t < rollIn + hold + rollOut) {
          roll = glm::mix(1.6f, 0.0f, (t - rollIn - hold) / rollOut);
        }
      } else if (cat.idleAnim == Cat::IdleAnim::Groomed) {
        groomed = 0.5f + 0.5f * std::sin(cat.idleAnimPhase * 1.4f);
      }

      float catBob = std::sin(cat.walkCycle * 2.0f) * walkAmount * 0.05f;
      catBob *= (1.0f - loaf * 0.9f) * (1.0f - groom * 0.4f) * (1.0f - groomed * 0.4f);
      float catWag = std::sin(cat.walkCycle * 1.6f) * 0.25f;
      catWag *= (1.0f - loaf * 0.7f) * (1.0f - groomed * 0.3f);
      const float legSwing = std::sin(cat.walkCycle) * walkAmount * 0.18f;
      const float earWiggle = (1.0f - walkAmount) * 0.18f * std::sin(currentTime * 2.3f + catSeedOffset) +
              groom * 0.22f * std::sin(cat.idleAnimPhase * 3.2f);
      const float headTilt = (1.0f - walkAmount) * 0.12f * std::sin(currentTime * 1.4f + catSeedOffset) +
                 groomed * 0.18f * std::sin(cat.idleAnimPhase * 2.0f);
      const float blinkPhase = std::sin(currentTime * 1.8f + catSeedOffset);
      const float blink = glm::clamp((blinkPhase - 0.92f) / 0.08f, 0.0f, 1.0f);
      
      glm::vec3 catPos = cat.position + glm::vec3(0.0f, catBob - loaf * 0.08f - groomed * 0.03f, 0.0f);
      glm::vec3 bodyScale(0.36f, 0.22f, 0.48f);
      glm::vec3 headScale(0.26f, 0.26f, 0.26f);
      const glm::vec3 earScale(0.085f, 0.13f, 0.065f);
      glm::vec3 legScale(0.055f, 0.16f, 0.055f);
      if (loaf > 0.0f) {
        bodyScale.y *= 0.7f;
        headScale.y *= 0.85f;
        legScale.y *= 0.4f;
      }

      const float groomLift = groom * 0.12f;
      const float groomBob = groom * 0.05f * std::sin(cat.idleAnimPhase * 2.4f);
      const float groomedBob = groomed * 0.035f * std::sin(cat.idleAnimPhase * 2.2f);
      const float eyeScaleY = 0.05f * (1.0f - blink) + 0.012f * blink;
      
      // Helper to draw oriented parts
      auto DrawCatPart = [&](const glm::vec3& localPos, const glm::vec3& scale, const glm::vec3& tint) {
        glBindTexture(GL_TEXTURE_2D, catTexture);
        glm::mat4 model(1.0f);
        model = glm::translate(model, catPos);
        model = glm::rotate(model, cat.facing, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, roll, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::translate(model, localPos);
        model = glm::scale(model, scale);
        shader.SetMat4("uModel", model);
        shader.SetMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
        shader.SetVec3("uTint", tint);
        glDrawArrays(GL_TRIANGLES, 0, 36);
      };

      auto DrawCatPartRot = [&](const glm::vec3& localPos, const glm::vec3& localRot,
                                const glm::vec3& scale, const glm::vec3& tint) {
        glBindTexture(GL_TEXTURE_2D, catTexture);
        glm::mat4 model(1.0f);
        model = glm::translate(model, catPos);
        model = glm::rotate(model, cat.facing, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, roll, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::translate(model, localPos);
        model = glm::rotate(model, localRot.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, localRot.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, localRot.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, scale);
        shader.SetMat4("uModel", model);
        shader.SetMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
        shader.SetVec3("uTint", tint);
        glDrawArrays(GL_TRIANGLES, 0, 36);
      };

      DrawCatPart(glm::vec3(0.0f, 0.28f, 0.0f), bodyScale, glm::vec3(1.0f, 0.85f, 0.95f));
      DrawCatPartRot(glm::vec3(0.0f, 0.52f + groomBob + groomedBob - loaf * 0.03f, 0.32f + groom * 0.06f),
                     glm::vec3(0.0f, 0.0f, headTilt), headScale, glm::vec3(1.0f, 0.92f, 0.98f));
      DrawCatPartRot(glm::vec3(0.09f, 0.62f, 0.38f), glm::vec3(0.0f, 0.0f, earWiggle), earScale, glm::vec3(0.95f, 0.75f, 0.85f));
      DrawCatPartRot(glm::vec3(-0.09f, 0.62f, 0.38f), glm::vec3(0.0f, 0.0f, -earWiggle), earScale, glm::vec3(0.95f, 0.75f, 0.85f));

      DrawCatPartRot(glm::vec3(0.0f, 0.48f + groomedBob * 0.5f, 0.42f), glm::vec3(0.0f, 0.0f, headTilt * 0.6f),
                     glm::vec3(0.12f, 0.08f, 0.08f), glm::vec3(1.0f, 0.95f, 0.98f));
      DrawCatPartRot(glm::vec3(0.0f, 0.47f, 0.47f), glm::vec3(0.0f, 0.0f, headTilt),
                     glm::vec3(0.04f, 0.03f, 0.04f), glm::vec3(0.9f, 0.55f, 0.6f));

      DrawCatPartRot(glm::vec3(0.075f, 0.51f, 0.48f), glm::vec3(0.0f, 0.0f, headTilt),
             glm::vec3(0.042f, eyeScaleY, 0.045f), glm::vec3(0.12f, 0.1f, 0.12f));
      DrawCatPartRot(glm::vec3(-0.075f, 0.51f, 0.48f), glm::vec3(0.0f, 0.0f, headTilt),
             glm::vec3(0.042f, eyeScaleY, 0.045f), glm::vec3(0.12f, 0.1f, 0.12f));
      DrawCatPartRot(glm::vec3(0.09f, 0.53f, 0.5f), glm::vec3(0.0f, 0.0f, headTilt),
             glm::vec3(0.012f, 0.012f, 0.012f), glm::vec3(0.98f, 0.98f, 1.0f));
      DrawCatPartRot(glm::vec3(-0.09f, 0.53f, 0.5f), glm::vec3(0.0f, 0.0f, headTilt),
             glm::vec3(0.012f, 0.012f, 0.012f), glm::vec3(0.98f, 0.98f, 1.0f));

      DrawCatPartRot(glm::vec3(0.13f, 0.48f, 0.45f), glm::vec3(0.0f, 0.0f, headTilt),
             glm::vec3(0.03f, 0.022f, 0.022f), glm::vec3(0.98f, 0.75f, 0.82f));
      DrawCatPartRot(glm::vec3(-0.13f, 0.48f, 0.45f), glm::vec3(0.0f, 0.0f, headTilt),
             glm::vec3(0.03f, 0.022f, 0.022f), glm::vec3(0.98f, 0.75f, 0.82f));

      if (cat.collected) {
        DrawCatPart(glm::vec3(0.0f, 0.43f, 0.26f), glm::vec3(0.24f, 0.03f, 0.26f), glm::vec3(0.2f, 0.55f, 0.95f));
        DrawCatPart(glm::vec3(0.0f, 0.39f, 0.49f), glm::vec3(0.05f, 0.05f, 0.02f), glm::vec3(0.3f, 0.7f, 1.0f));
      }

      DrawCatPart(glm::vec3(0.12f, 0.12f + groomLift, 0.18f + legSwing + groom * 0.06f), legScale, glm::vec3(0.95f, 0.8f, 0.9f));
      DrawCatPart(glm::vec3(-0.12f, 0.12f, 0.18f - legSwing), legScale, glm::vec3(0.95f, 0.8f, 0.9f));
      DrawCatPart(glm::vec3(0.12f, 0.12f, -0.18f - legSwing), legScale, glm::vec3(0.95f, 0.8f, 0.9f));
      DrawCatPart(glm::vec3(-0.12f, 0.12f, -0.18f + legSwing), legScale, glm::vec3(0.95f, 0.8f, 0.9f));

      DrawCatPart(glm::vec3(0.12f, 0.03f + groomLift * 0.4f, 0.18f + legSwing + groom * 0.06f),
          glm::vec3(0.045f, 0.02f, 0.045f), glm::vec3(0.98f, 0.72f, 0.82f));
      DrawCatPart(glm::vec3(-0.12f, 0.03f, 0.18f - legSwing),
          glm::vec3(0.045f, 0.02f, 0.045f), glm::vec3(0.98f, 0.72f, 0.82f));
      DrawCatPart(glm::vec3(0.12f, 0.03f, -0.18f - legSwing),
          glm::vec3(0.045f, 0.02f, 0.045f), glm::vec3(0.98f, 0.72f, 0.82f));
      DrawCatPart(glm::vec3(-0.12f, 0.03f, -0.18f + legSwing),
          glm::vec3(0.045f, 0.02f, 0.045f), glm::vec3(0.98f, 0.72f, 0.82f));

      // Tail with wag
      glm::mat4 tailModel(1.0f);
      tailModel = glm::translate(tailModel, catPos);
      tailModel = glm::rotate(tailModel, cat.facing, glm::vec3(0.0f, 1.0f, 0.0f));
      tailModel = glm::rotate(tailModel, roll, glm::vec3(0.0f, 0.0f, 1.0f));
      tailModel = glm::translate(tailModel, glm::vec3(0.0f, 0.34f, -0.32f));
      tailModel = glm::rotate(tailModel, catWag, glm::vec3(0.0f, 1.0f, 0.0f));
      tailModel = glm::scale(tailModel, glm::vec3(0.08f, 0.08f, 0.35f));
      shader.SetMat4("uModel", tailModel);
      shader.SetMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(tailModel))));
      shader.SetVec3("uTint", glm::vec3(1.0f, 0.8f, 0.9f));
      glBindTexture(GL_TEXTURE_2D, catTexture);
      glDrawArrays(GL_TRIANGLES, 0, 36);

      glm::mat4 tailTip = tailModel;
      tailTip = glm::translate(tailTip, glm::vec3(0.0f, 0.0f, 0.9f));
      tailTip = glm::scale(tailTip, glm::vec3(1.6f, 1.6f, 1.6f));
      shader.SetMat4("uModel", tailTip);
      shader.SetMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(tailTip))));
      shader.SetVec3("uTint", glm::vec3(1.0f, 0.9f, 0.95f));
      glDrawArrays(GL_TRIANGLES, 0, 36);

      catIndex++;
    }
    } else {
      for (const Dog& dog : dogs) {
        if (dog.collected) {
          // Collected dogs still render and follow the player.
        }
        const float speed = glm::length(glm::vec2(dog.velocity.x, dog.velocity.z));
        const float walk = glm::clamp(speed / 4.5f, 0.0f, 1.0f);
        const float bob = (0.03f + walk * 0.03f) * std::sin(dog.walkCycle * 2.0f + dog.bobOffset);
        const float legSwing = std::sin(dog.walkCycle) * walk * 0.11f;
        const float tailWag = (0.14f + walk * 0.2f) * std::sin(dog.walkCycle * 1.6f + 1.7f);
        const glm::vec3 dogPos = dog.position + glm::vec3(0.0f, bob, 0.0f);

        auto DrawDogPart = [&](const glm::vec3& localPos, const glm::vec3& scale, const glm::vec3& tint) {
          glBindTexture(GL_TEXTURE_2D, catTexture);
          glm::mat4 model(1.0f);
          model = glm::translate(model, dogPos);
          model = glm::rotate(model, dog.facing, glm::vec3(0.0f, 1.0f, 0.0f));
          model = glm::translate(model, localPos);
          model = glm::scale(model, scale);
          shader.SetMat4("uModel", model);
          shader.SetMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
          shader.SetVec3("uTint", tint);
          glDrawArrays(GL_TRIANGLES, 0, 36);
        };

        DrawDogPart(glm::vec3(0.0f, 0.26f, 0.0f), glm::vec3(0.46f, 0.23f, 0.74f), glm::vec3(0.93f, 0.76f, 0.56f));
        DrawDogPart(glm::vec3(0.0f, 0.4f, 0.46f), glm::vec3(0.32f, 0.24f, 0.32f), glm::vec3(0.96f, 0.82f, 0.62f));
        DrawDogPart(glm::vec3(0.18f, 0.52f, 0.42f), glm::vec3(0.08f, 0.14f, 0.07f), glm::vec3(0.76f, 0.54f, 0.38f));
        DrawDogPart(glm::vec3(-0.18f, 0.52f, 0.42f), glm::vec3(0.08f, 0.14f, 0.07f), glm::vec3(0.76f, 0.54f, 0.38f));
        DrawDogPart(glm::vec3(0.0f, 0.34f, 0.62f), glm::vec3(0.08f, 0.06f, 0.08f), glm::vec3(0.18f, 0.14f, 0.14f));
        DrawDogPart(glm::vec3(0.16f, 0.1f, 0.24f + legSwing), glm::vec3(0.09f, 0.2f, 0.09f), glm::vec3(0.9f, 0.72f, 0.52f));
        DrawDogPart(glm::vec3(-0.16f, 0.1f, 0.24f - legSwing), glm::vec3(0.09f, 0.2f, 0.09f), glm::vec3(0.9f, 0.72f, 0.52f));
        DrawDogPart(glm::vec3(0.16f, 0.1f, -0.22f - legSwing), glm::vec3(0.09f, 0.2f, 0.09f), glm::vec3(0.9f, 0.72f, 0.52f));
        DrawDogPart(glm::vec3(-0.16f, 0.1f, -0.22f + legSwing), glm::vec3(0.09f, 0.2f, 0.09f), glm::vec3(0.9f, 0.72f, 0.52f));

        glBindTexture(GL_TEXTURE_2D, catTexture);
        glm::mat4 tailModel(1.0f);
        tailModel = glm::translate(tailModel, dogPos);
        tailModel = glm::rotate(tailModel, dog.facing, glm::vec3(0.0f, 1.0f, 0.0f));
        tailModel = glm::translate(tailModel, glm::vec3(0.0f, 0.38f, -0.48f));
        tailModel = glm::rotate(tailModel, tailWag, glm::vec3(0.0f, 1.0f, 0.0f));
        tailModel = glm::scale(tailModel, glm::vec3(0.08f, 0.08f, 0.26f));
        shader.SetMat4("uModel", tailModel);
        shader.SetMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(tailModel))));
        shader.SetVec3("uTint", glm::vec3(0.84f, 0.64f, 0.48f));
        glDrawArrays(GL_TRIANGLES, 0, 36);
      }

      for (const Bomb& bomb : bombs) {
        if (!bomb.active) {
          continue;
        }
        DrawCube(bomb.position, glm::vec3(0.22f, 0.22f, 0.22f), glm::vec3(0.22f, 0.22f, 0.25f), knifeTexture);
      }
    }

    const glm::vec3 activeCarPos = (currentLevel == GameLevel::Level1Cats) ? carPositionLevel1 : carPositionLevel2;
    DrawCube(activeCarPos + glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(1.2f, 0.5f, 2.0f), glm::vec3(0.4f, 0.6f, 0.9f), carTexture);
    DrawCube(activeCarPos + glm::vec3(0.0f, 1.0f, -0.2f), glm::vec3(0.8f, 0.35f, 1.0f), glm::vec3(0.7f, 0.8f, 0.9f), carTexture);

    auto DrawHumanoid = [&](const glm::vec3& basePos, float size, const glm::vec3& bodyTint,
                            const glm::vec3& skinTint, const glm::vec3& accentTint,
                            GLuint bodyTex, GLuint skinTex, GLuint accentTex,
                            float walkPhase, float walkAmount, float faceYaw) {
      const float torsoHeight = size * 1.2f;
      const float torsoWidth = size * 0.75f;
      const float legHeight = size * 0.9f;
      const float legWidth = size * 0.28f;
      const float armHeight = size * 0.75f;
      const float armWidth = size * 0.22f;
      const float headSize = size * 0.55f;
      const float swing = std::sin(walkPhase) * walkAmount;
      const float legSwing = swing * size * 0.18f;
      const float armSwing = -swing * size * 0.22f;
      const float legRot = swing * 0.9f;
      const float armRot = -swing * 1.1f;
      const float torsoSway = swing * size * 0.08f;
      const float bob = std::sin(walkPhase * 2.0f) * walkAmount * size * 0.06f;
      const float idleBreath = std::sin(walkPhase * 0.6f) * (1.0f - walkAmount) * size * 0.03f;
      const glm::vec3 rootPos = basePos + glm::vec3(0.0f, bob + idleBreath, 0.0f);

      auto DrawPart = [&](const glm::vec3& localPos, const glm::vec3& scale, const glm::vec3& tint, GLuint tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glm::mat4 model(1.0f);
        model = glm::translate(model, rootPos);
        model = glm::rotate(model, faceYaw, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::translate(model, localPos);
        model = glm::scale(model, scale);
        shader.SetMat4("uModel", model);
        shader.SetMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
        shader.SetVec3("uTint", tint);
        glDrawArrays(GL_TRIANGLES, 0, 36);
      };

      auto DrawLimb = [&](const glm::vec3& jointPos, float length, float width, float depth,
                          const glm::vec3& tint, GLuint tex, float rotAngle) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glm::mat4 model(1.0f);
        model = glm::translate(model, rootPos);
        model = glm::rotate(model, faceYaw, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::translate(model, jointPos);
        model = glm::rotate(model, rotAngle, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::translate(model, glm::vec3(0.0f, -length * 0.5f, 0.0f));
        model = glm::scale(model, glm::vec3(width, length, depth));
        shader.SetMat4("uModel", model);
        shader.SetMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
        shader.SetVec3("uTint", tint);
        glDrawArrays(GL_TRIANGLES, 0, 36);
      };

      DrawLimb(glm::vec3(legWidth * 1.2f, legHeight, legSwing),
               legHeight, legWidth, legWidth * 0.9f,
               accentTint, accentTex, legRot);

      DrawLimb(glm::vec3(-legWidth * 1.2f, legHeight, -legSwing),
               legHeight, legWidth, legWidth * 0.9f,
               accentTint, accentTex, -legRot);

      DrawPart(glm::vec3(0.0f, legHeight + torsoHeight * 0.5f, torsoSway),
               glm::vec3(torsoWidth, torsoHeight, torsoWidth * 0.75f),
               bodyTint, bodyTex);

      DrawLimb(glm::vec3(torsoWidth * 0.85f, legHeight + torsoHeight * 0.95f, armSwing + torsoSway),
           armHeight, armWidth, armWidth * 0.9f,
           bodyTint, bodyTex, armRot);
      DrawLimb(glm::vec3(-torsoWidth * 0.85f, legHeight + torsoHeight * 0.95f, -armSwing + torsoSway),
           armHeight, armWidth, armWidth * 0.9f,
           bodyTint, bodyTex, -armRot);

      DrawPart(glm::vec3(0.0f, legHeight + torsoHeight + headSize * 0.55f, torsoSway * 1.4f),
               glm::vec3(headSize, headSize, headSize),
               skinTint, skinTex);
    };

    const float playerSize = player.halfSize * 2.0f;
    const float playerSpeed = glm::length(glm::vec2(player.velocity.x, player.velocity.z));
    const float playerWalk = glm::clamp(playerSpeed / moveSpeed, 0.0f, 1.0f);
    playerWalkCycle += playerWalk * (2.5f + playerWalk * 6.0f) * deltaTime;
    if (playerSpeed > 0.05f) {
      playerFacing = std::atan2(player.velocity.x, player.velocity.z);
    }
    DrawHumanoid(player.position, playerSize,
           glm::vec3(0.35f, 0.55f, 0.9f),
           glm::vec3(0.95f, 0.85f, 0.75f),
           glm::vec3(0.2f, 0.2f, 0.25f),
           playerTexture, playerSkinTexture, playerTexture,
                 playerWalkCycle, playerWalk, playerFacing);

    if (currentLevel == GameLevel::Level1Cats) {
      const float clownSize = clown.halfSize * 2.0f;
      const float clownSpeed = glm::length(glm::vec2(clown.velocity.x, clown.velocity.z));
      const float clownWalk = glm::clamp(clownSpeed / clown.speed, 0.0f, 1.0f);
      clownWalkCycle += clownWalk * (2.5f + clownWalk * 6.0f) * deltaTime;
      if (clownSpeed > 0.05f) {
        clownFacing = std::atan2(clown.velocity.x, clown.velocity.z);
      }
      DrawHumanoid(clown.position, clownSize,
             glm::vec3(0.95f, 0.2f, 0.2f),
             glm::vec3(1.0f, 0.9f, 0.85f),
             glm::vec3(0.2f, 0.2f, 0.2f),
             clownTexture, clownSkinTexture, clownAccentTexture,
                   clownWalkCycle, clownWalk, clownFacing);

      const float clownSwing = std::sin(clownWalkCycle) * clownWalk;
      const float clownArmRot = -clownSwing * 1.1f;
      const float clownTorsoHeight = clownSize * 1.2f;
      const float clownTorsoWidth = clownSize * 0.75f;
      const float clownLegHeight = clownSize * 0.9f;
      const float clownArmHeight = clownSize * 0.75f;
      const float clownArmSwing = -clownSwing * clownSize * 0.22f;
      const float clownTorsoSway = clownSwing * clownSize * 0.08f;
      const glm::vec3 clownRoot = clown.position;

      glm::mat4 handModel(1.0f);
      handModel = glm::translate(handModel, clownRoot);
      handModel = glm::rotate(handModel, clownFacing, glm::vec3(0.0f, 1.0f, 0.0f));
      handModel = glm::translate(handModel, glm::vec3(clownTorsoWidth * 0.85f,
                              clownLegHeight + clownTorsoHeight * 0.95f,
                              clownArmSwing + clownTorsoSway));
      handModel = glm::rotate(handModel, clownArmRot, glm::vec3(1.0f, 0.0f, 0.0f));
      handModel = glm::translate(handModel, glm::vec3(0.0f, -clownArmHeight * 0.9f, 0.0f));
      handModel = glm::scale(handModel, glm::vec3(clownSize * 0.15f, clownSize * 0.35f, clownSize * 0.6f));
      shader.SetMat4("uModel", handModel);
      shader.SetMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(handModel))));
      shader.SetVec3("uTint", glm::vec3(0.85f, 0.85f, 0.9f));
      glBindTexture(GL_TEXTURE_2D, knifeTexture);
      glDrawArrays(GL_TRIANGLES, 0, 36);
    } else {
      const float mummySize = mummy.halfSize * 2.0f;
      const float mummySpeed = glm::length(glm::vec2(mummy.velocity.x, mummy.velocity.z));
      const float mummyWalk = glm::clamp(mummySpeed / glm::max(mummy.speed, 0.001f), 0.0f, 1.0f);
      mummyWalkCycle += mummyWalk * (2.2f + mummyWalk * 4.0f) * deltaTime;
      if (mummySpeed > 0.05f) {
        mummyFacing = std::atan2(mummy.velocity.x, mummy.velocity.z);
      }
      DrawHumanoid(mummy.position, mummySize,
             glm::vec3(0.84f, 0.82f, 0.74f),
             glm::vec3(0.88f, 0.83f, 0.73f),
             glm::vec3(0.75f, 0.72f, 0.66f),
             platformTexture, playerSkinTexture, platformTexture,
                   mummyWalkCycle, mummyWalk, mummyFacing);

      glm::mat4 bombHand(1.0f);
      bombHand = glm::translate(bombHand, mummy.position);
      bombHand = glm::rotate(bombHand, mummyFacing, glm::vec3(0.0f, 1.0f, 0.0f));
      bombHand = glm::translate(bombHand, glm::vec3(0.42f, mummySize * 1.5f, 0.1f));
      bombHand = glm::scale(bombHand, glm::vec3(mummySize * 0.2f, mummySize * 0.2f, mummySize * 0.2f));
      shader.SetMat4("uModel", bombHand);
      shader.SetMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(bombHand))));
      shader.SetVec3("uTint", glm::vec3(0.22f, 0.22f, 0.24f));
      glBindTexture(GL_TEXTURE_2D, knifeTexture);
      glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    glBindVertexArray(0);

    if (audio.ready) {
      ma_sound_set_volume(&audio.ambient.sound, musicVolume);
      ma_sound_set_volume(&audio.footstep.sound, 0.45f * sfxVolume);
      ma_sound_set_volume(&audio.jump.sound, 0.5f * sfxVolume);
      ma_sound_set_volume(&audio.land.sound, 0.5f * sfxVolume);
      ma_sound_set_volume(&audio.chase.sound, 0.6f * sfxVolume);
    }

    ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoDecoration |
                                ImGuiWindowFlags_AlwaysAutoResize |
                                ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoFocusOnAppearing |
                                ImGuiWindowFlags_NoNav;
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.45f);
    ImGui::Begin("HUD", nullptr, hudFlags);
    if (currentLevel == GameLevel::Level1Cats) {
      ImGui::Text("Level 1 - Cats");
      ImGui::Text("Cats: %d / %zu", collectedCount, cats.size());
    } else {
      ImGui::Text("Level 2 - Dogs");
      ImGui::Text("Dogs: %d / %zu", collectedCount, dogs.size());
    }
    ImGui::ProgressBar(stamina, ImVec2(180.0f, 0.0f), "Stamina");
    if (currentLevel == GameLevel::Level1Cats) {
      ImGui::Text("Clown distance: %.1fm", glm::distance(player.position, clown.position));
    } else {
      ImGui::Text("Mummy distance: %.1fm", glm::distance(player.position, mummy.position));
    }
    if (!hasWon) {
      const glm::vec3 activeCarPos = (currentLevel == GameLevel::Level1Cats) ? carPositionLevel1 : carPositionLevel2;
      const float carDistance = glm::distance(player.position, activeCarPos);
      ImGui::Text("Car distance: %.1fm", carDistance);
      if (currentLevel == GameLevel::Level1Cats) {
        if (collectedCount < 10) {
          ImGui::Text("Objective: Find all cats");
        } else {
          ImGui::Text("Objective: Reach the car");
        }
      } else {
        if (collectedCount < 20) {
          ImGui::Text("Objective: Rescue 20 very cute dogs");
        } else {
          ImGui::Text("Objective: Reach the car");
        }
      }
    } else {
      ImGui::Text("You escaped! 🎉");
    }
    ImGui::Text("Esc/P: Pause");
    ImGui::End();

    if (showDebugHud) {
      ImGui::SetNextWindowPos(ImVec2(12.0f, 170.0f), ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(0.45f);
      ImGui::Begin("Debug", nullptr, hudFlags);
      ImGui::Text("Player: (%.2f, %.2f, %.2f)", player.position.x, player.position.y, player.position.z);
      if (currentLevel == GameLevel::Level1Cats) {
        ImGui::Text("Clown:  (%.2f, %.2f, %.2f)", clown.position.x, clown.position.y, clown.position.z);
      } else {
        ImGui::Text("Mummy:  (%.2f, %.2f, %.2f)", mummy.position.x, mummy.position.y, mummy.position.z);
      }
      ImGui::Text("Camera yaw/pitch: %.2f / %.2f", yaw, pitch);
      ImGui::End();
    }

    if (isPaused) {
      const ImVec2 viewport = ImGui::GetIO().DisplaySize;
      ImGui::SetNextWindowPos(ImVec2(viewport.x * 0.5f, viewport.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Always);
      ImGui::Begin("Pause Menu", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
      ImGui::Text("Game Paused");
      ImGui::Separator();
      ImGui::SliderFloat("Music Volume", &musicVolume, 0.0f, 1.0f);
      ImGui::SliderFloat("SFX Volume", &sfxVolume, 0.0f, 1.0f);
      ImGui::SliderFloat("GUI Scale", &uiScale, 0.85f, 2.8f, "%.2fx");
      ImGui::SliderFloat("Mouse Sensitivity", &mouseSensitivity, 0.0015f, 0.02f, "%.4f");
      ImGui::SliderFloat("Camera Distance", &cameraDistance, 3.0f, 10.0f);
      ImGui::Checkbox("Invert Look Y", &invertLookY);
      ImGui::Checkbox("Show Debug HUD", &showDebugHud);
      ImGui::Separator();
      if (ImGui::Button("Resume", ImVec2(-1.0f, 0.0f))) {
        isPaused = false;
      }
      if (ImGui::Button("Reset Player", ImVec2(-1.0f, 0.0f))) {
        player.position = (currentLevel == GameLevel::Level1Cats) ? levelOneSpawn : levelTwoSpawn;
        player.velocity = glm::vec3(0.0f);
      }
      if (ImGui::Button("Quit Game", ImVec2(-1.0f, 0.0f))) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
      ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
  }

  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(1, &vbo);
  glDeleteTextures(1, &platformTexture);
  glDeleteTextures(1, &playerTexture);
  glDeleteTextures(1, &playerSkinTexture);
  glDeleteTextures(1, &clownTexture);
  glDeleteTextures(1, &clownSkinTexture);
  glDeleteTextures(1, &clownAccentTexture);
  glDeleteTextures(1, &knifeTexture);
  glDeleteTextures(1, &cloudTexture);
  if (audio.ready) {
    ma_sound_uninit(&audio.footstep.sound);
    ma_sound_uninit(&audio.jump.sound);
    ma_sound_uninit(&audio.land.sound);
    ma_sound_uninit(&audio.ambient.sound);
    ma_sound_uninit(&audio.chase.sound);
    ma_audio_buffer_uninit(&audio.footstep.buffer);
    ma_audio_buffer_uninit(&audio.jump.buffer);
    ma_audio_buffer_uninit(&audio.land.buffer);
    ma_audio_buffer_uninit(&audio.ambient.buffer);
    ma_audio_buffer_uninit(&audio.chase.buffer);
    ma_engine_uninit(&audio.engine);
  }
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwTerminate();
  return 0;
}
