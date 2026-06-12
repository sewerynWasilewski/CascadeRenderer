#pragma once

inline u64 fnv1a(const void* data, size_t len) {
  constexpr u64 FNV_BASIS = 14695981039346656037ull;
  constexpr u64 FNV_PRIME = 1099511628211ull;
  u64 hash = FNV_BASIS;
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; i++)
    hash = (hash ^ p[i]) * FNV_PRIME;
  return hash;
}