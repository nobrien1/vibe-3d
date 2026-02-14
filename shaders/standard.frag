#version 330 core
in vec2 vUv;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2D uTexture;
uniform vec3 uTint;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uAmbient;
uniform vec3 uViewPos;
uniform vec3 uRimColor;
uniform float uRimPower;
uniform float uSpecPower;
uniform float uSpecIntensity;

out vec4 FragColor;

void main() {
  vec3 baseColor = texture(uTexture, vUv).rgb * uTint;
  vec3 N = normalize(vNormal);
  vec3 L = normalize(-uLightDir);
  vec3 V = normalize(uViewPos - vWorldPos);
  vec3 H = normalize(L + V);

  float diff = max(dot(N, L), 0.0);
  float spec = pow(max(dot(N, H), 0.0), uSpecPower) * uSpecIntensity;
  float rim = pow(1.0 - max(dot(N, V), 0.0), uRimPower);

  vec3 lit = baseColor * (uAmbient + diff * uLightColor) + (uLightColor * spec) + (uRimColor * rim);
  FragColor = vec4(lit, 1.0);
}
