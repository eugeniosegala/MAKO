set(COMMON_SOURCES
    "src/configuration/config.cpp"
    "src/configuration/detection.cpp"
    "src/configuration/launch.cpp"
    "src/helpers/errors.cpp"
    "src/helpers/paths.cpp"
    "src/quality/image_quality.cpp"
    "src/vulkan/buffer.cpp"
    "src/vulkan/command_buffer.cpp"
    "src/vulkan/descriptor_pool.cpp"
    "src/vulkan/descriptor_set.cpp"
    "src/vulkan/device_memory_accounting.cpp"
    "src/vulkan/fence.cpp"
    "src/vulkan/image.cpp"
    "src/vulkan/image_memory_pool.cpp"
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

    add_executable(mako-launch-config-tests
        "tests/launch_config_tests.cpp")
    target_link_libraries(mako-launch-config-tests
        PRIVATE mako-common)
    add_test(NAME standalone-launch-configuration
        COMMAND mako-launch-config-tests)

    add_executable(mako-device-feature-tests
        "tests/device_feature_tests.cpp")
    target_link_libraries(mako-device-feature-tests
        PRIVATE mako-common)
    add_test(NAME optional-device-features
        COMMAND mako-device-feature-tests)

    add_executable(mako-device-memory-accounting-tests
        "tests/device_memory_accounting_tests.cpp")
    target_link_libraries(mako-device-memory-accounting-tests
        PRIVATE mako-common)
    add_test(NAME device-memory-accounting
        COMMAND mako-device-memory-accounting-tests)

    add_executable(mako-image-memory-pool-tests
        "tests/image_memory_pool_tests.cpp")
    target_link_libraries(mako-image-memory-pool-tests
        PRIVATE mako-common)
    add_test(NAME image-memory-pool
        COMMAND mako-image-memory-pool-tests)

    add_executable(mako-command-buffer-submit-storage-tests
        "tests/command_buffer_submit_storage_tests.cpp")
    target_include_directories(mako-command-buffer-submit-storage-tests
        PRIVATE src)
    add_test(NAME command-buffer-submit-storage
        COMMAND mako-command-buffer-submit-storage-tests)

    add_executable(mako-image-quality-tests
        "tests/image_quality_tests.cpp")
    target_link_libraries(mako-image-quality-tests
        PRIVATE mako-common)
    add_test(NAME procedural-image-quality-scene
        COMMAND mako-image-quality-tests)
endif()

if(MAKO_INSTALL_DEVELOP)
    install(TARGETS mako-common
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(DIRECTORY "include/mako-common/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mako-common"
        FILES_MATCHING PATTERN "*.hpp")
endif()
