set(LAYER_SOURCES
    "src/entrypoint.cpp"
    "src/gamescope_hdr_feedback.cpp"
    "src/instance.cpp"
    "src/present_diagnostics.cpp"
    "src/swapchain.cpp"
    "src/swapchain_present.cpp")

add_library(mako-adaptive STATIC
    "src/adaptive_scheduler.cpp")
target_include_directories(mako-adaptive
    PUBLIC "src")
set_target_properties(mako-adaptive PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(MAKO_BUILD_VK_LAYER)
    find_package(Threads REQUIRED)

    add_library(mako-color STATIC
        "src/color_pipeline.cpp")
    target_include_directories(mako-color
        PUBLIC "src")
    target_link_libraries(mako-color
        PUBLIC mako-backend)
    set_target_properties(mako-color PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)

    add_library(mako-render SHARED ${LAYER_SOURCES})

    target_link_libraries(mako-render
        PRIVATE mako-adaptive
        PRIVATE mako-color
        PRIVATE Threads::Threads
        PUBLIC mako-common
        PUBLIC mako-backend)

    target_compile_options(mako-render PRIVATE
        -Wno-unknown-warning-option
        -Wno-unsafe-buffer-usage) # Array indexing

    target_compile_definitions(mako-render PRIVATE
        MAKO_BUILD_VERSION="${MAKO_VERSION}")

    set_target_properties(mako-render PROPERTIES
        CXX_VISIBILITY_PRESET hidden)
endif()

if(BUILD_TESTING)
    add_executable(mako-device-selection-tests
        "tests/device_selection_tests.cpp")
    target_include_directories(mako-device-selection-tests
        PRIVATE "src")
    add_test(NAME device-selection
        COMMAND mako-device-selection-tests)

    add_executable(mako-profile-update-tests
        "tests/profile_update_tests.cpp")
    target_include_directories(mako-profile-update-tests
        PRIVATE "src"
        PRIVATE "../mako-common/include")
    add_test(NAME profile-update
        COMMAND mako-profile-update-tests)

    add_executable(mako-runtime-transition-tests
        "tests/runtime_transition_tests.cpp")
    target_include_directories(mako-runtime-transition-tests
        PRIVATE "src")
    add_test(NAME runtime-transition
        COMMAND mako-runtime-transition-tests)

    add_executable(mako-presentation-policy-tests
        "tests/presentation_policy_tests.cpp")
    target_include_directories(mako-presentation-policy-tests
        PRIVATE "src")
    add_test(NAME presentation-policy
        COMMAND mako-presentation-policy-tests)

    add_executable(mako-adaptive-tests
        "tests/adaptive_scheduler_tests.cpp")
    target_link_libraries(mako-adaptive-tests
        PRIVATE mako-adaptive)
    add_test(NAME adaptive-scheduler
        COMMAND mako-adaptive-tests)

    add_executable(mako-adaptive-matrix
        "tests/adaptive_scheduler_matrix.cpp")
    target_link_libraries(mako-adaptive-matrix
        PRIVATE mako-adaptive)
    add_test(NAME adaptive-scheduler-matrix
        COMMAND mako-adaptive-matrix)

    if(MAKO_BUILD_VK_LAYER)
        add_executable(mako-pnext-chain-tests
            "tests/pnext_chain_tests.cpp")
        target_include_directories(mako-pnext-chain-tests
            PRIVATE "src")
        add_test(NAME pnext-chain
            COMMAND mako-pnext-chain-tests)

        add_executable(mako-color-tests
            "tests/color_pipeline_tests.cpp")
        target_link_libraries(mako-color-tests
            PRIVATE mako-color)
        add_test(NAME color-pipeline
            COMMAND mako-color-tests)
    endif()

    add_executable(mako-adaptive-benchmark
        "tests/adaptive_scheduler_benchmark.cpp")
    target_link_libraries(mako-adaptive-benchmark
        PRIVATE mako-adaptive)

    add_executable(mako-hdr-color-math-tests
        "tests/hdr_color_math_tests.cpp")
    add_test(NAME hdr-color-math
        COMMAND mako-hdr-color-math-tests)
endif()

if(MAKO_BUILD_VK_LAYER)
    math(EXPR MAKO_LAYER_LIBRARY_ARCH "${CMAKE_SIZEOF_VOID_P} * 8")
    if(NOT MAKO_LAYER_LIBRARY_ARCH EQUAL 32 AND
            NOT MAKO_LAYER_LIBRARY_ARCH EQUAL 64)
        message(FATAL_ERROR
            "The Vulkan layer manifest supports only 32-bit or 64-bit libraries")
    endif()

    set(MAKO_LAYER_MANIFEST_FILE
        "VkLayer_MAKO_render${MAKO_LAYER_MANIFEST_SUFFIX}.json")
    configure_file(
        "VkLayer_MAKO_render.json.in"
        "${MAKO_LAYER_MANIFEST_FILE}"
        @ONLY)

    # MAKO-managed SDR launches use a private manifest directory, matching the
    # proven v2 Decky boundary. Relative paths containing a directory need one
    # more parent traversal; a bare loader name remains valid as-is.
    set(MAKO_PUBLIC_LAYER_LIBRARY_PATH "${MAKO_LAYER_LIBRARY_PATH}")
    if(MAKO_PUBLIC_LAYER_LIBRARY_PATH MATCHES "/")
        set(MAKO_LAYER_LIBRARY_PATH "../${MAKO_PUBLIC_LAYER_LIBRARY_PATH}")
    else()
        set(MAKO_LAYER_LIBRARY_PATH "${MAKO_PUBLIC_LAYER_LIBRARY_PATH}")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/private-manifest")
    configure_file(
        "VkLayer_MAKO_render.json.in"
        "${CMAKE_CURRENT_BINARY_DIR}/private-manifest/${MAKO_LAYER_MANIFEST_FILE}"
        @ONLY)
    set(MAKO_LAYER_LIBRARY_PATH "${MAKO_PUBLIC_LAYER_LIBRARY_PATH}")

    install(TARGETS mako-render
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${MAKO_LAYER_MANIFEST_FILE}"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/vulkan/implicit_layer.d")
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/private-manifest/${MAKO_LAYER_MANIFEST_FILE}"
        DESTINATION
            "${CMAKE_INSTALL_DATAROOTDIR}/mako-render/vulkan/implicit_layer.d")
endif()
