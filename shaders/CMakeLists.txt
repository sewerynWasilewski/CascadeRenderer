if(NOT Vulkan_FOUND OR NOT Vulkan_GLSLC_EXECUTABLE)
    message(STATUS "glslc not found - skipping shader compilation")
    return()
endif()

set(SHADER_SOURCES
    gbuffer.vert
    gbuffer.frag
)

set(SHADER_COMPILE_OPTIONS --target-env=vulkan1.1 -O)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND SHADER_COMPILE_OPTIONS -g)
endif()

set(SHADER_OUTPUTS)

foreach(SHADER_SOURCE ${SHADER_SOURCES})
    set(SHADER_INPUT  ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE})
    set(SHADER_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_SOURCE}.spv)

    add_custom_command(
        OUTPUT  ${SHADER_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${SHADER_COMPILE_OPTIONS}
                ${SHADER_INPUT} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER_INPUT}
        COMMENT "Compiling shader ${SHADER_SOURCE}"
        VERBATIM
    )

    list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_OUTPUTS})
