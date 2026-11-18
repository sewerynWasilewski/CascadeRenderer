#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;

void main() {
  outAlbedo = vec4(inUv, 0.0, 1.0);

  outNormal = vec4(normalize(inNormal), 1.0);
}
