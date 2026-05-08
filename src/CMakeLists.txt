add_library(renderer STATIC
    renderer/renderer.cpp
    rg/rg.cpp
    rhi/vulkan/VkGPUAllocator.cpp
)

target_include_directories(renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(renderer PUBLIC
    ${VMA_INCLUDE_DIR}
)

target_link_libraries(renderer PUBLIC
    glm::glm
)
