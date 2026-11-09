set(RENDERER_SOURCES
  renderer/renderer.cpp
  rg/RenderGraph.cpp
)

if(Vulkan_FOUND)
  list(APPEND RENDERER_SOURCES
    ../vendor/volk/volk.c
    rhi/vulkan/VkRHIAllocator.cpp
    rhi/vulkan/VkRHIBackend.cpp
    ../vendor/imgui/imgui.cpp
    ../vendor/imgui/imgui_draw.cpp
    ../vendor/imgui/imgui_tables.cpp
    ../vendor/imgui/imgui_widgets.cpp
    ../vendor/imgui/backends/imgui_impl_glfw.cpp
    ../vendor/imgui/backends/imgui_impl_vulkan.cpp
  )
endif()

add_library(renderer STATIC ${RENDERER_SOURCES})

target_compile_definitions(renderer PRIVATE GLFW_INCLUDE_NONE)

target_include_directories(renderer PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/imgui
  ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/imgui/backends
  ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/glfw/include
)

if(Vulkan_FOUND)
  target_include_directories(renderer PUBLIC
    ${VMA_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/volk
  )
  target_compile_definitions(renderer PUBLIC RENDERER_VULKAN IMGUI_IMPL_VULKAN_USE_VOLK)

  if(WIN32)
    target_compile_definitions(renderer PUBLIC VK_USE_PLATFORM_WIN32_KHR)
  elseif(APPLE)
    target_compile_definitions(renderer PUBLIC VK_USE_PLATFORM_METAL_EXT)
  elseif(UNIX)
    target_compile_definitions(renderer PUBLIC VK_USE_PLATFORM_XCB_KHR)
  endif()
endif()

target_link_libraries(renderer PUBLIC glfw glm::glm)
