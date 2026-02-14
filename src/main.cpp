#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
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
      {{0.0f, -1.0f, 0.0f}, {10.0f, 0.5f, 10.0f}, {0.6f, 0.7f, 0.8f}},
      {{3.0f, 1.0f, 0.0f}, {1.5f, 0.3f, 1.5f}, {0.9f, 0.7f, 0.4f}},
      {{-2.5f, 2.2f, -1.5f}, {1.0f, 0.3f, 1.0f}, {0.5f, 0.9f, 0.6f}},
  };

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

  shader.Use();
  shader.SetInt("uTexture", 0);

  while (!glfwWindowShouldClose(window)) {
    const float currentTime = static_cast<float>(glfwGetTime());
    const float deltaTime = currentTime - lastTime;
    lastTime = currentTime;

    glfwPollEvents();
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    glm::vec3 cameraForward = glm::normalize(glm::vec3(
        std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::cos(yaw) * std::cos(pitch)));

    static double lastX = 0.0;
    static double lastY = 0.0;
    static bool first = true;
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
      double x = 0.0;
      double y = 0.0;
      glfwGetCursorPos(window, &x, &y);
      if (first) {
        lastX = x;
        lastY = y;
        first = false;
      }
      const float sensitivity = 0.005f;
      const float dx = static_cast<float>(x - lastX) * sensitivity;
      const float dy = static_cast<float>(y - lastY) * sensitivity;
      yaw -= dx;
      pitch -= dy;
      pitch = glm::clamp(pitch, glm::radians(-70.0f), glm::radians(20.0f));
      lastX = x;
      lastY = y;
    } else {
      first = true;
    }

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
    if (clown.onGround && clown.jumpCooldown <= 0.0f && playerHeightGap > 0.2f && playerHorizDist < 6.5f) {
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
      player.position = playerSpawn;
      player.velocity = glm::vec3(0.0f);
      clown.position = glm::vec3(4.0f, enemyGround, -4.0f);
      clown.velocity = glm::vec3(0.0f);
      clown.onGround = true;
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

    glClearColor(0.08f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader.Use();
    shader.SetMat4("uView", view);
    shader.SetMat4("uProj", proj);
    shader.SetVec3("uViewPos", cameraPosSmooth);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.2f)));
    shader.SetVec3("uLightColor", glm::vec3(1.0f, 0.98f, 0.95f));
    shader.SetVec3("uAmbient", glm::vec3(0.2f, 0.2f, 0.22f));
    shader.SetVec3("uRimColor", glm::vec3(0.2f, 0.35f, 0.6f));
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

    glBindVertexArray(0);

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
  glfwTerminate();
  return 0;
}
