set(LAYER_SOURCES
    "src/entrypoint.cpp"
    "src/gamescope_hdr_feedback.cpp"
    "src/instance.cpp"
    "src/present_diagnostics.cpp"
    "src/runtime_status.cpp"
    "src/swapchain.cpp"
    "src/swapchain_present.cpp")

add_library(mako-adaptive STATIC
    "src/adaptive_scheduler.cpp")
target_include_directories(mako-adaptive
    PUBLIC "src"
    PUBLIC "../mako-common/include")
set_target_properties(mako-adaptive PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(MAKO_BUILD_VK_LAYER OR MAKO_BUILD_CLI)
    add_library(mako-spatial-scaler STATIC
        "src/spatial_scaler.cpp")
    target_include_directories(mako-spatial-scaler
        PUBLIC "src")
    target_link_libraries(mako-spatial-scaler
        PUBLIC mako-common
        PUBLIC mako-backend)
    set_target_properties(mako-spatial-scaler PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
endif()

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

    function(mako_configure_renderer_layer target)
        target_link_libraries(${target}
            PRIVATE mako-adaptive
            PRIVATE mako-color
            PRIVATE mako-spatial-scaler
            PRIVATE Threads::Threads
            PRIVATE ${CMAKE_DL_LIBS}
            PUBLIC mako-common
            PUBLIC mako-backend)

        target_compile_options(${target} PRIVATE
            -Wno-unknown-warning-option
            -Wno-unsafe-buffer-usage) # Array indexing

        target_compile_definitions(${target} PRIVATE
            MAKO_BUILD_VERSION="${MAKO_VERSION}"
            MAKO_BUILD_FINGERPRINT="${MAKO_BUILD_FINGERPRINT}")

        set_target_properties(${target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden)
    endfunction()

    add_library(mako-render SHARED ${LAYER_SOURCES})
    mako_configure_renderer_layer(mako-render)

    # Frame generation and scaling occupy opposite sides of Gamescope WSI.
    # Build a second isolated copy so the loader can form the only chain that
    # preserves both contracts: FG -> Gamescope WSI -> spatial scaling.
    add_library(mako-render-scaling SHARED ${LAYER_SOURCES})
    target_compile_definitions(mako-render-scaling PRIVATE
        MAKO_LAYER_ROLE_SPATIAL_SCALING=1)
    mako_configure_renderer_layer(mako-render-scaling)

    # The upper role consumes a same-process relay exported by the separately
    # compiled lower role. Keep targeted developer builds coherent: rebuilding
    # the public renderer target must never leave the spatial DSO stale.
    add_dependencies(mako-render mako-render-scaling)
endif()

if(BUILD_TESTING)
    # Keep portable build and CTest membership together so the shell runner
    # cannot drift when another policy test is added.
    add_custom_target(mako-portable-policy-test-binaries)
    function(mako_register_portable_policy_test target test_name)
        add_dependencies(mako-portable-policy-test-binaries ${target})
        set_tests_properties(${test_name} PROPERTIES
            LABELS "portable-policy")
    endfunction()

    add_executable(mako-device-selection-tests
        "tests/device_selection_tests.cpp")
    target_include_directories(mako-device-selection-tests
        PRIVATE "src")
    add_test(NAME device-selection
        COMMAND mako-device-selection-tests)
    mako_register_portable_policy_test(
        mako-device-selection-tests device-selection)

    add_executable(mako-profile-update-tests
        "tests/profile_update_tests.cpp")
    target_include_directories(mako-profile-update-tests
        PRIVATE "src"
        PRIVATE "../mako-common/include")
    add_test(NAME profile-update
        COMMAND mako-profile-update-tests)
    mako_register_portable_policy_test(
        mako-profile-update-tests profile-update)

    add_executable(mako-spatial-scaling-policy-tests
        "tests/spatial_scaling_policy_tests.cpp")
    target_include_directories(mako-spatial-scaling-policy-tests
        PRIVATE "src"
        PRIVATE "../mako-common/include")
    add_test(NAME spatial-scaling-policy
        COMMAND mako-spatial-scaling-policy-tests)
    mako_register_portable_policy_test(
        mako-spatial-scaling-policy-tests spatial-scaling-policy)

    add_executable(mako-swapchain-retirement-tests
        "tests/swapchain_retirement_tests.cpp")
    target_include_directories(mako-swapchain-retirement-tests
        PRIVATE "src")
    add_test(NAME swapchain-retirement
        COMMAND mako-swapchain-retirement-tests)
    mako_register_portable_policy_test(
        mako-swapchain-retirement-tests swapchain-retirement)

    add_executable(mako-generated-frame-plan-tests
        "tests/generated_frame_plan_tests.cpp")
    target_include_directories(mako-generated-frame-plan-tests
        PRIVATE "src")
    add_test(NAME generated-frame-plan
        COMMAND mako-generated-frame-plan-tests)
    mako_register_portable_policy_test(
        mako-generated-frame-plan-tests generated-frame-plan)

    add_executable(mako-runtime-transition-tests
        "tests/runtime_transition_tests.cpp")
    target_include_directories(mako-runtime-transition-tests
        PRIVATE "src")
    add_test(NAME runtime-transition
        COMMAND mako-runtime-transition-tests)
    mako_register_portable_policy_test(
        mako-runtime-transition-tests runtime-transition)

    add_executable(mako-runtime-status-tests
        "tests/runtime_status_tests.cpp"
        "src/runtime_status.cpp")
    target_include_directories(mako-runtime-status-tests
        PRIVATE "src"
        PRIVATE "../mako-common/include")
    target_link_libraries(mako-runtime-status-tests
        PRIVATE mako-common)
    add_test(NAME runtime-status
        COMMAND mako-runtime-status-tests)
    mako_register_portable_policy_test(
        mako-runtime-status-tests runtime-status)

    add_executable(mako-layer-role-tests
        "tests/layer_role_tests.cpp")
    target_include_directories(mako-layer-role-tests
        PRIVATE "src"
        PRIVATE "../mako-common/include")
    add_test(NAME layer-role
        COMMAND mako-layer-role-tests)
    mako_register_portable_policy_test(
        mako-layer-role-tests layer-role)

    add_executable(mako-spatial-layer-role-tests
        "tests/layer_role_tests.cpp")
    target_compile_definitions(mako-spatial-layer-role-tests PRIVATE
        MAKO_LAYER_ROLE_SPATIAL_SCALING=1)
    target_include_directories(mako-spatial-layer-role-tests
        PRIVATE "src"
        PRIVATE "../mako-common/include")
    add_test(NAME spatial-layer-role
        COMMAND mako-spatial-layer-role-tests)
    mako_register_portable_policy_test(
        mako-spatial-layer-role-tests spatial-layer-role)

    add_executable(mako-presentation-policy-tests
        "tests/presentation_policy_tests.cpp")
    target_include_directories(mako-presentation-policy-tests
        PRIVATE "src")
    add_test(NAME presentation-policy
        COMMAND mako-presentation-policy-tests)
    mako_register_portable_policy_test(
        mako-presentation-policy-tests presentation-policy)

    add_executable(mako-adaptive-tests
        "tests/adaptive_scheduler_tests.cpp")
    target_link_libraries(mako-adaptive-tests
        PRIVATE mako-adaptive)
    add_test(NAME adaptive-scheduler
        COMMAND mako-adaptive-tests)
    mako_register_portable_policy_test(
        mako-adaptive-tests adaptive-scheduler)

    add_executable(mako-adaptive-matrix
        "tests/adaptive_scheduler_matrix.cpp")
    target_link_libraries(mako-adaptive-matrix
        PRIVATE mako-adaptive)
    add_test(NAME adaptive-scheduler-matrix
        COMMAND mako-adaptive-matrix)
    mako_register_portable_policy_test(
        mako-adaptive-matrix adaptive-scheduler-matrix)

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
    mako_register_portable_policy_test(
        mako-hdr-color-math-tests hdr-color-math)
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
    set(MAKO_SCALING_LAYER_MANIFEST_FILE
        "VkLayer_MAKO_spatial_scaling${MAKO_LAYER_MANIFEST_SUFFIX}.json")
    configure_file(
        "VkLayer_MAKO_render.json.in"
        "${MAKO_LAYER_MANIFEST_FILE}"
        @ONLY)
    configure_file(
        "VkLayer_MAKO_spatial_scaling.json.in"
        "${MAKO_SCALING_LAYER_MANIFEST_FILE}"
        @ONLY)

    # MAKO-managed SDR launches use a private manifest directory, matching the
    # proven v2 Decky boundary. Relative paths containing a directory need one
    # more parent traversal; a bare loader name remains valid as-is.
    set(MAKO_PUBLIC_LAYER_LIBRARY_PATH "${MAKO_LAYER_LIBRARY_PATH}")
    set(MAKO_PUBLIC_SCALING_LAYER_LIBRARY_PATH
        "${MAKO_SCALING_LAYER_LIBRARY_PATH}")
    if(IS_ABSOLUTE "${MAKO_PUBLIC_LAYER_LIBRARY_PATH}")
        set(MAKO_LAYER_LIBRARY_PATH "${MAKO_PUBLIC_LAYER_LIBRARY_PATH}")
    elseif(MAKO_PUBLIC_LAYER_LIBRARY_PATH MATCHES "/")
        set(MAKO_LAYER_LIBRARY_PATH "../${MAKO_PUBLIC_LAYER_LIBRARY_PATH}")
    else()
        set(MAKO_LAYER_LIBRARY_PATH "${MAKO_PUBLIC_LAYER_LIBRARY_PATH}")
    endif()
    if(IS_ABSOLUTE "${MAKO_PUBLIC_SCALING_LAYER_LIBRARY_PATH}")
        set(MAKO_SCALING_LAYER_LIBRARY_PATH
            "${MAKO_PUBLIC_SCALING_LAYER_LIBRARY_PATH}")
    elseif(MAKO_PUBLIC_SCALING_LAYER_LIBRARY_PATH MATCHES "/")
        set(MAKO_SCALING_LAYER_LIBRARY_PATH
            "../${MAKO_PUBLIC_SCALING_LAYER_LIBRARY_PATH}")
    else()
        set(MAKO_SCALING_LAYER_LIBRARY_PATH
            "${MAKO_PUBLIC_SCALING_LAYER_LIBRARY_PATH}")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/private-manifest")
    configure_file(
        "VkLayer_MAKO_render.json.in"
        "${CMAKE_CURRENT_BINARY_DIR}/private-manifest/${MAKO_LAYER_MANIFEST_FILE}"
        @ONLY)
    file(MAKE_DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/private-scaling-manifest")
    configure_file(
        "VkLayer_MAKO_spatial_scaling.json.in"
        "${CMAKE_CURRENT_BINARY_DIR}/private-scaling-manifest/${MAKO_SCALING_LAYER_MANIFEST_FILE}"
        @ONLY)
    set(MAKO_LAYER_LIBRARY_PATH "${MAKO_PUBLIC_LAYER_LIBRARY_PATH}")
    set(MAKO_SCALING_LAYER_LIBRARY_PATH
        "${MAKO_PUBLIC_SCALING_LAYER_LIBRARY_PATH}")

    install(TARGETS mako-render mako-render-scaling
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${MAKO_LAYER_MANIFEST_FILE}"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/vulkan/implicit_layer.d")
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/private-manifest/${MAKO_LAYER_MANIFEST_FILE}"
        DESTINATION
            "${CMAKE_INSTALL_DATAROOTDIR}/mako-render/vulkan/implicit_layer.d")
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${MAKO_SCALING_LAYER_MANIFEST_FILE}"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/vulkan/implicit_layer.d")
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/private-scaling-manifest/${MAKO_SCALING_LAYER_MANIFEST_FILE}"
        DESTINATION
            "${CMAKE_INSTALL_DATAROOTDIR}/mako-render/vulkan/spatial_scaling.d")
endif()
