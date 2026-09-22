#version 450

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrColor;

layout(push_constant) uniform PushConstants {
  float exposure;
} pc;

vec3 linearToSrgb(vec3 c) {
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

void main() {
  vec3 hdr = texture(hdrColor, inUv).rgb;
  vec3 mapped = hdr * pc.exposure;
  mapped = mapped / (mapped + vec3(1.0));

  outColor = vec4(linearToSrgb(mapped), 1.0);
}
