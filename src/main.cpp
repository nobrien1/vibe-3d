#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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

  const GLuint texture = BuildCheckerTexture();

  Player player;
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
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(vao);

    for (const Platform& platform : platforms) {
      glm::mat4 model(1.0f);
      model = glm::translate(model, platform.position);
      model = glm::scale(model, platform.halfExtents * 2.0f);
      shader.SetMat4("uModel", model);
      shader.SetVec3("uTint", platform.tint);
      glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    glm::mat4 playerModel(1.0f);
    playerModel = glm::translate(playerModel, player.position);
    playerModel = glm::scale(playerModel, glm::vec3(player.halfSize * 2.0f));
    shader.SetMat4("uModel", playerModel);
    shader.SetVec3("uTint", glm::vec3(0.9f, 0.3f, 0.3f));
    glDrawArrays(GL_TRIANGLES, 0, 36);

    glBindVertexArray(0);

    glfwSwapBuffers(window);
  }

  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(1, &vbo);
  glDeleteTextures(1, &texture);
  glfwTerminate();
  return 0;
}
