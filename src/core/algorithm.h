#pragma once

constexpr u64 FNV1A_BASIS = 14695981039346656037ull;
constexpr u64 FNV1A_PRIME = 1099511628211ull;

// seed defaults to the FNV-1a basis; pass a previous result to continue hashing
// across multiple calls (equivalent to hashing the concatenation of all inputs).
inline u64 fnv1a(const void* data, size_t len, u64 seed = FNV1A_BASIS) {
  u64 hash = seed;
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; i++)
    hash = (hash ^ p[i]) * FNV1A_PRIME;
  return hash;
}