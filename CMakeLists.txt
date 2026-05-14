cmake_minimum_required(VERSION 3.24)
project(renderer VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/glfw)
add_subdirectory(vendor/glm)

set(VMA_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/vma/include)

find_package(Vulkan)

add_subdirectory(src)
add_subdirectory(sandbox)
