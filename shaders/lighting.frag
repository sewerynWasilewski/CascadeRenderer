#version 450

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outHdr;

layout(set = 0, binding = 0) uniform sampler2D gbufferAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gbufferNormal;
layout(set = 0, binding = 2) uniform sampler2D gbufferDepth;

layout(push_constant) uniform PushConstants {
  mat4 invViewProj;
  vec4 lightDirToLight;
  vec4 lightColorIntensity;
  vec4 cameraPosition;
  vec4 ambient;
} pc;

const float kShininess = 64.0;

void main() {
  float depth = texture(gbufferDepth, inUv).r;

  if (depth >= 1.0) {
    outHdr = vec4(pc.ambient.rgb, 1.0);
    return;
  }

  vec3 albedo = texture(gbufferAlbedo, inUv).rgb;

  vec3 N = normalize(texture(gbufferNormal, inUv).rgb);

  vec4 clip = vec4(inUv.x * 2.0 - 1.0, 1.0 - inUv.y * 2.0, depth, 1.0);
  vec4 world = pc.invViewProj * clip;
  vec3 worldPos = world.xyz / world.w;

  vec3 L = normalize(pc.lightDirToLight.xyz);
  vec3 V = normalize(pc.cameraPosition.xyz - worldPos);
  vec3 H = normalize(L + V);

  float nDotL = max(dot(N, L), 0.0);

  float specular = (nDotL > 0.0) ? pow(max(dot(N, H), 0.0), kShininess) : 0.0;

  vec3 radiance = pc.lightColorIntensity.rgb * pc.lightColorIntensity.a;
  vec3 color = albedo * (pc.ambient.rgb + radiance * nDotL) + radiance * specular;

  outHdr = vec4(color, 1.0);
}
