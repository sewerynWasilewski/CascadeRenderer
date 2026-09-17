add_executable(rg_tests
    test_compile.cpp
    test_model_loader.cpp
    test_renderer_graph.cpp
)

target_link_libraries(rg_tests PRIVATE renderer)
target_include_directories(rg_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor
)
target_compile_definitions(rg_tests PRIVATE RG_ENABLE_TESTS)

add_test(NAME rg_tests COMMAND rg_tests)
