set(RENDERER_SOURCES
  renderer/renderer.cpp
  rg/RenderGraph.cpp
)

if(Vulkan_FOUND)
  list(APPEND RENDERER_SOURCES 
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
  target_include_directories(renderer PUBLIC ${VMA_INCLUDE_DIR})
  target_link_libraries(renderer PUBLIC Vulkan::Vulkan)
  target_compile_definitions(renderer PUBLIC RENDERER_VULKAN)
endif()

target_link_libraries(renderer PUBLIC glfw glm::glm)
