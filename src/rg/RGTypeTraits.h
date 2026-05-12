#pragma once
#include <concepts>
#include <type_traits>

struct IRHIBackend;

template<typename T>
concept Virtualizable =
  requires { typename T::Desc; } &&
  std::is_default_constructible_v<T> &&
  std::is_move_constructible_v<T> &&
  requires(const typename T::Desc& d, IRHIBackend* b, void* h) {
    { T::createGPU(d, b)     } -> std::same_as<void*>;
    { T::destroyGPU(d, b, h) } -> std::same_as<void>;
  };

#define VIRTUALIZABLE_RESOURCE(T) Virtualizable T
