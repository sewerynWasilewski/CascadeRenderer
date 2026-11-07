add_executable(sandbox
    main.cpp
    app/debug_ui.cpp
)

target_link_libraries(sandbox PRIVATE
    renderer
    glfw
)
