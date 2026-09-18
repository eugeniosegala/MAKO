add_library(mako-cli-support STATIC
    "src/arguments.cpp"
    "src/i18n.cpp")
target_include_directories(mako-cli-support
    PUBLIC "src")

set(CLI_SOURCES
    "src/tools/image_transfer.cpp"
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
    add_executable(mako-temporal-sequence-tests "tests/temporal_sequence_tests.cpp")
    target_include_directories(mako-temporal-sequence-tests PRIVATE src)
    add_test(NAME temporal-sequence-contract COMMAND mako-temporal-sequence-tests)

    add_executable(mako-cli-dll-inspection-tests
        "src/tools/inspect_dll.cpp"
        "tests/inspect_dll_tests.cpp")
    target_include_directories(mako-cli-dll-inspection-tests PRIVATE
        "src" "../mako-backend/include")
    add_test(NAME cli-dll-inspection COMMAND mako-cli-dll-inspection-tests)

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

    # Exercise the production parser and option defaults without a GPU or DLL.
    add_executable(mako-cli-precision-probe
        "src/main.cpp"
        "tests/precision_cli_stubs.cpp")
    target_link_libraries(mako-cli-precision-probe PRIVATE mako-cli-support)
    target_include_directories(mako-cli-precision-probe PRIVATE
        "../mako-backend/include")
    add_test(NAME cli-precision-contract
        COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test-precision-cli.sh"
            "$<TARGET_FILE:mako-cli-precision-probe>")

endif()

install(TARGETS mako-cli
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
