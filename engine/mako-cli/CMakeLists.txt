set(CLI_SOURCES
    "src/tools/benchmark.cpp"
    "src/tools/debug.cpp"
    "src/tools/quality.cpp"
    "src/tools/validate.cpp"
    "src/main.cpp")

add_executable(mako-cli ${CLI_SOURCES})

target_link_libraries(mako-cli
    PUBLIC mako-common
    PUBLIC mako-backend)

target_compile_options(mako-cli PRIVATE
    -Wno-unknown-warning-option
    -Wno-unsafe-buffer-usage) # CLI parsing

if(BUILD_TESTING)
    add_test(NAME amd-image-quality-gpu
        COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/run-gpu-quality-regression.sh"
            "$<TARGET_FILE:mako-cli>"
            "${MAKO_GPU_QUALITY_OUTPUT}"
            "${MAKO_GPU_QUALITY_TEST}"
            77)
    set_tests_properties(amd-image-quality-gpu PROPERTIES
        LABELS "amd;gpu;image-quality"
        RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77
        TIMEOUT 300)
endif()

install(TARGETS mako-cli
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
