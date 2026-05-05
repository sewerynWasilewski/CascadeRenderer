#pragma once
#include <concepts>

template<typename T>
concept Virtualizable = requires(T t) {
  requires std::conjunction_v<std::is_default_constructible<T>, std::is_move_constructible<T>>;
  typename T::Desc;
  { t.create(typename T::Desc{}, (void*)nullptr) } -> std::same_as<void>;
  { t.destroy(typename T::Desc{}, (void*)nullptr) } -> std::same_as<void>;
};

#define VIRTUALIZABLE_RESOURCE(T) Virtualizable T

template<typename T>
concept has_preRead = requires(T t) {
  { t.preRead(typename T::Desc{}, (void*)nullptr) } -> std::same_as<void>;
};

template<typename T>
concept has_preWrite = requires(T t) {
  { t.preWrite(typename T::Desc{}, (void*)nullptr) } -> std::same_as<void>;
};
