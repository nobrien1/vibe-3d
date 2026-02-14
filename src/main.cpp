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
      // positions          // uvs
      -0.5f, -0.5f, -0.5f,   0.0f, 0.0f,
       0.5f, -0.5f, -0.5f,   1.0f, 0.0f,
       0.5f,  0.5f, -0.5f,   1.0f, 1.0f,
       0.5f,  0.5f, -0.5f,   1.0f, 1.0f,
      -0.5f,  0.5f, -0.5f,   0.0f, 1.0f,
      -0.5f, -0.5f, -0.5f,   0.0f, 0.0f,

      -0.5f, -0.5f,  0.5f,   0.0f, 0.0f,
       0.5f, -0.5f,  0.5f,   1.0f, 0.0f,
       0.5f,  0.5f,  0.5f,   1.0f, 1.0f,
       0.5f,  0.5f,  0.5f,   1.0f, 1.0f,
      -0.5f,  0.5f,  0.5f,   0.0f, 1.0f,
      -0.5f, -0.5f,  0.5f,   0.0f, 0.0f,

      -0.5f,  0.5f,  0.5f,   1.0f, 0.0f,
      -0.5f,  0.5f, -0.5f,   1.0f, 1.0f,
      -0.5f, -0.5f, -0.5f,   0.0f, 1.0f,
      -0.5f, -0.5f, -0.5f,   0.0f, 1.0f,
      -0.5f, -0.5f,  0.5f,   0.0f, 0.0f,
      -0.5f,  0.5f,  0.5f,   1.0f, 0.0f,

       0.5f,  0.5f,  0.5f,   1.0f, 0.0f,
       0.5f,  0.5f, -0.5f,   1.0f, 1.0f,
       0.5f, -0.5f, -0.5f,   0.0f, 1.0f,
       0.5f, -0.5f, -0.5f,   0.0f, 1.0f,
       0.5f, -0.5f,  0.5f,   0.0f, 0.0f,
       0.5f,  0.5f,  0.5f,   1.0f, 0.0f,

      -0.5f, -0.5f, -0.5f,   0.0f, 1.0f,
       0.5f, -0.5f, -0.5f,   1.0f, 1.0f,
       0.5f, -0.5f,  0.5f,   1.0f, 0.0f,
       0.5f, -0.5f,  0.5f,   1.0f, 0.0f,
      -0.5f, -0.5f,  0.5f,   0.0f, 0.0f,
      -0.5f, -0.5f, -0.5f,   0.0f, 1.0f,

      -0.5f,  0.5f, -0.5f,   0.0f, 1.0f,
       0.5f,  0.5f, -0.5f,   1.0f, 1.0f,
       0.5f,  0.5f,  0.5f,   1.0f, 0.0f,
       0.5f,  0.5f,  0.5f,   1.0f, 0.0f,
      -0.5f,  0.5f,  0.5f,   0.0f, 0.0f,
      -0.5f,  0.5f, -0.5f,   0.0f, 1.0f,
  };

  GLuint vao = 0;
  GLuint vbo = 0;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
  glBindVertexArray(0);

  const GLuint platformTexture = BuildPlankTexture();
  const GLuint playerTexture = BuildFabricTexture(90, 70);
  const GLuint playerSkinTexture = BuildSkinTexture();
  const GLuint clownTexture = BuildFabricTexture(160, 40);
  const GLuint clownSkinTexture = BuildSkinTexture();
  const GLuint clownAccentTexture = BuildDotsTexture(220, 60);
  const GLuint knifeTexture = BuildMetalTexture();

  Player player;
  const glm::vec3 playerSpawn{0.0f, 2.0f, 0.0f};
  Enemy clown;
  const float gravity = -18.0f;
  const float moveSpeed = 5.0f;
  const float jumpSpeed = 7.0f;

  std::vector<Platform> platforms = {
      {{0.0f, -1.0f, 0.0f}, {10.0f, 0.5f, 10.0f}, {0.6f, 0.7f, 0.8f}},
      {{3.0f, 1.0f, 0.0f}, {1.5f, 0.3f, 1.5f}, {0.9f, 0.7f, 0.4f}},
      {{-2.5f, 2.2f, -1.5f}, {1.0f, 0.3f, 1.0f}, {0.5f, 0.9f, 0.6f}},
  };

  float lastTime = static_cast<float>(glfwGetTime());
  float yaw = glm::radians(45.0f);
  float pitch = glm::radians(-20.0f);
  float cameraDistance = 6.0f;

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

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
      static double lastX = 0.0;
      static double lastY = 0.0;
      static bool first = true;
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
      yaw += dx;
      pitch -= dy;
      pitch = glm::clamp(pitch, glm::radians(-70.0f), glm::radians(20.0f));
      lastX = x;
      lastY = y;
    } else {
      static bool reset = true;
      if (reset) {
        reset = false;
      }
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

    player.velocity.x = inputDir.x * moveSpeed;
    player.velocity.z = inputDir.z * moveSpeed;
    player.velocity.y += gravity * deltaTime;

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && player.onGround) {
      player.velocity.y = jumpSpeed;
      player.onGround = false;
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

    const float enemyGround = platforms[0].position.y + platforms[0].halfExtents.y + clown.halfSize;
    glm::vec3 chaseDir = player.position - clown.position;
    chaseDir.y = 0.0f;
    if (glm::length(chaseDir) > 0.001f) {
      chaseDir = glm::normalize(chaseDir);
    }

    clown.velocity.x = chaseDir.x * clown.speed;
    clown.velocity.z = chaseDir.z * clown.speed;
    clown.velocity.y += gravity * deltaTime;

    if (clown.jumpCooldown > 0.0f) {
      clown.jumpCooldown -= deltaTime;
    }

    const float playerHeightGap = player.position.y - clown.position.y;
    const float playerHorizDist = glm::length(glm::vec2(player.position.x - clown.position.x,
                                                       player.position.z - clown.position.z));
    if (clown.onGround && clown.jumpCooldown <= 0.0f && playerHeightGap > 0.4f && playerHorizDist < 5.5f) {
      const float jumpHeight = glm::clamp(playerHeightGap + 0.4f, 0.8f, 2.4f);
      const float jumpVelocity = std::sqrt(2.0f * -gravity * jumpHeight);
      clown.velocity.y = jumpVelocity;
      clown.jumpCooldown = 0.6f;
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
    const glm::vec3 cameraPos = player.position + cameraOffset;
    const glm::mat4 view = glm::lookAt(cameraPos, player.position + glm::vec3(0.0f, 0.8f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

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

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);

    auto DrawCube = [&](const glm::vec3& position, const glm::vec3& scale, const glm::vec3& tint, GLuint tex) {
      glBindTexture(GL_TEXTURE_2D, tex);
      glm::mat4 model(1.0f);
      model = glm::translate(model, position);
      model = glm::scale(model, scale);
      shader.SetMat4("uModel", model);
      shader.SetVec3("uTint", tint);
      glDrawArrays(GL_TRIANGLES, 0, 36);
    };

    for (const Platform& platform : platforms) {
      DrawCube(platform.position, platform.halfExtents * 2.0f, platform.tint, platformTexture);
    }

    auto DrawHumanoid = [&](const glm::vec3& basePos, float size, const glm::vec3& bodyTint,
                const glm::vec3& skinTint, const glm::vec3& accentTint,
                GLuint bodyTex, GLuint skinTex, GLuint accentTex) {
      const float torsoHeight = size * 1.2f;
      const float torsoWidth = size * 0.75f;
      const float legHeight = size * 0.9f;
      const float legWidth = size * 0.28f;
      const float armHeight = size * 0.75f;
      const float armWidth = size * 0.22f;
      const float headSize = size * 0.55f;

      DrawCube(basePos + glm::vec3(0.0f, legHeight * 0.5f, 0.0f),
           glm::vec3(legWidth, legHeight, legWidth * 0.9f),
           accentTint, accentTex);
      DrawCube(basePos + glm::vec3(legWidth * 1.2f, legHeight * 0.5f, 0.0f),
           glm::vec3(legWidth, legHeight, legWidth * 0.9f),
           accentTint, accentTex);

      DrawCube(basePos + glm::vec3(-legWidth * 1.2f, legHeight * 0.5f, 0.0f),
           glm::vec3(legWidth, legHeight, legWidth * 0.9f),
           accentTint, accentTex);

      DrawCube(basePos + glm::vec3(0.0f, legHeight + torsoHeight * 0.5f, 0.0f),
           glm::vec3(torsoWidth, torsoHeight, torsoWidth * 0.75f),
           bodyTint, bodyTex);

      DrawCube(basePos + glm::vec3(torsoWidth * 0.85f, legHeight + torsoHeight * 0.7f, 0.0f),
           glm::vec3(armWidth, armHeight, armWidth * 0.9f),
           bodyTint, bodyTex);
      DrawCube(basePos + glm::vec3(-torsoWidth * 0.85f, legHeight + torsoHeight * 0.7f, 0.0f),
           glm::vec3(armWidth, armHeight, armWidth * 0.9f),
           bodyTint, bodyTex);

      DrawCube(basePos + glm::vec3(0.0f, legHeight + torsoHeight + headSize * 0.55f, 0.0f),
           glm::vec3(headSize, headSize, headSize),
           skinTint, skinTex);
    };

    const float playerSize = player.halfSize * 2.0f;
    DrawHumanoid(player.position, playerSize,
           glm::vec3(0.35f, 0.55f, 0.9f),
           glm::vec3(0.95f, 0.85f, 0.75f),
           glm::vec3(0.2f, 0.2f, 0.25f),
           playerTexture, playerSkinTexture, playerTexture);

    const float clownSize = clown.halfSize * 2.0f;
    DrawHumanoid(clown.position, clownSize,
           glm::vec3(0.95f, 0.2f, 0.2f),
           glm::vec3(1.0f, 0.9f, 0.85f),
           glm::vec3(0.2f, 0.2f, 0.2f),
           clownTexture, clownSkinTexture, clownAccentTexture);

    DrawCube(clown.position + glm::vec3(clownSize * 0.9f, clownSize * 1.1f, 0.0f),
         glm::vec3(clownSize * 0.15f, clownSize * 0.35f, clownSize * 0.6f),
         glm::vec3(0.85f, 0.85f, 0.9f), knifeTexture);

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
  glfwTerminate();
  return 0;
}
