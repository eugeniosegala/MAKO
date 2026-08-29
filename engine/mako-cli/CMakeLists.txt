add_library(mako-cli-support STATIC
    "src/arguments.cpp"
    "src/i18n.cpp")
target_include_directories(mako-cli-support
    PUBLIC "src")

set(CLI_SOURCES
    "src/tools/benchmark.cpp"
    "src/tools/debug.cpp"
    "src/tools/inspect_dll.cpp"
    "src/tools/quality.cpp"
    "src/tools/validate.cpp"
    "src/main.cpp")

add_executable(mako-cli ${CLI_SOURCES})

target_link_libraries(mako-cli
    PRIVATE mako-cli-support
    PRIVATE mako-spatial-scaler
    PUBLIC mako-common
    PUBLIC mako-backend)

target_compile_options(mako-cli PRIVATE
    -Wno-unknown-warning-option
    -Wno-unsafe-buffer-usage) # CLI parsing

if(BUILD_TESTING)
    add_executable(mako-cli-i18n-tests
        "tests/i18n_tests.cpp")
    target_link_libraries(mako-cli-i18n-tests
        PRIVATE mako-cli-support)
    add_test(NAME cli-i18n-unit
        COMMAND mako-cli-i18n-tests)

    add_test(NAME cli-i18n-contract
        COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test-i18n-cli.sh"
            "$<TARGET_FILE:mako-cli>")

endif()

install(TARGETS mako-cli
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
