set(RENDERER_SOURCES
    renderer/renderer.cpp
    rg/rg.cpp
)

if(Vulkan_FOUND)
    list(APPEND RENDERER_SOURCES rhi/vulkan/VkGPUAllocator.cpp)
endif()

add_library(renderer STATIC ${RENDERER_SOURCES})

target_include_directories(renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(Vulkan_FOUND)
    target_include_directories(renderer PUBLIC ${VMA_INCLUDE_DIR})
    target_link_libraries(renderer PUBLIC Vulkan::Vulkan)
    target_compile_definitions(renderer PUBLIC RENDERER_VULKAN)
endif()

target_link_libraries(renderer PUBLIC glm::glm)
