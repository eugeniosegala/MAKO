set(CLI_SOURCES
    "src/tools/benchmark.cpp"
    "src/tools/debug.cpp"
    "src/tools/validate.cpp"
    "src/main.cpp")

add_executable(mako-cli ${CLI_SOURCES})

target_link_libraries(mako-cli
    PUBLIC mako-common
    PUBLIC mako-backend)

target_compile_options(mako-cli PRIVATE
    -Wno-unknown-warning-option
    -Wno-unsafe-buffer-usage) # CLI parsing

install(TARGETS mako-cli
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
