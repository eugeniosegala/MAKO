set(COMMON_SOURCES
    "src/configuration/config.cpp"
    "src/configuration/detection.cpp"
    "src/helpers/errors.cpp"
    "src/helpers/paths.cpp"
    "src/vulkan/buffer.cpp"
    "src/vulkan/command_buffer.cpp"
    "src/vulkan/descriptor_pool.cpp"
    "src/vulkan/descriptor_set.cpp"
    "src/vulkan/fence.cpp"
    "src/vulkan/image.cpp"
    "src/vulkan/sampler.cpp"
    "src/vulkan/semaphore.cpp"
    "src/vulkan/shader.cpp"
    "src/vulkan/timeline_semaphore.cpp"
    "src/vulkan/vulkan.cpp")

add_library(mako-common STATIC ${COMMON_SOURCES})

target_include_directories(mako-common
    PUBLIC include)

target_include_directories(mako-common SYSTEM
    PRIVATE thirdparty/include)

if(BUILD_TESTING)
    add_executable(mako-config-tests
        "tests/config_tests.cpp")
    target_link_libraries(mako-config-tests
        PRIVATE mako-common)
    add_test(NAME watched-configuration
        COMMAND mako-config-tests)
endif()

if(MAKO_INSTALL_DEVELOP)
    install(TARGETS mako-common
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(DIRECTORY "include/mako-common/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mako-common"
        FILES_MATCHING PATTERN "*.hpp")
endif()
