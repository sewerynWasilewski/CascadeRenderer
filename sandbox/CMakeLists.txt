add_executable(sandbox
    main.cpp
)

target_link_libraries(sandbox PRIVATE
    renderer
    glfw
)

target_compile_definitions(sandbox PRIVATE SHADER_DIR="${SHADER_OUTPUT_DIR}")

if(TARGET shaders)
    add_dependencies(sandbox shaders)
endif()
