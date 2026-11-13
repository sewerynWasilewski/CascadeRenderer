add_executable(sandbox
    main.cpp
    app/debug_ui.cpp
)

target_link_libraries(sandbox PRIVATE
    renderer
    glfw
)

target_compile_definitions(sandbox PRIVATE
    SHADER_DIR="${SHADER_OUTPUT_DIR}"
    GLFW_INCLUDE_NONE
)

if(TARGET shaders)
    add_dependencies(sandbox shaders)
endif()
