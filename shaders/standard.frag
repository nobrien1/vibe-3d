#version 330 core
in vec2 vUv;

uniform sampler2D uTexture;
uniform vec3 uTint;

out vec4 FragColor;

void main() {
  vec3 tex = texture(uTexture, vUv).rgb;
  FragColor = vec4(tex * uTint, 1.0);
}
