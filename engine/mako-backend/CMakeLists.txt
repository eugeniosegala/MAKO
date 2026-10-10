set(BACKEND_SOURCES
    "src/extraction/content_hash.cpp"
    "src/extraction/dll_inspection.cpp"
    "src/extraction/dll_reader.cpp"
    "src/extraction/dxbc_translation.cpp"
    "src/extraction/ls1_shader_set.cpp"
    "src/extraction/spirv_image_format.cpp"
    "src/extraction/spirv_fp16.cpp"
    "src/extraction/lsfg_shader_set.cpp"
    "src/extraction/model_resource_validation.cpp"
    "src/extraction/model_resources.cpp"
    "src/extraction/shader_registry.cpp"
    "src/helpers/limits.cpp"
    "src/helpers/managed_shader.cpp"
    "src/helpers/utils.cpp"
    "src/shaderchains/alpha0.cpp"
    "src/shaderchains/alpha1.cpp"
    "src/shaderchains/beta0.cpp"
    "src/shaderchains/beta1.cpp"
    "src/shaderchains/color_conversion.cpp"
    "src/shaderchains/delta0.cpp"
    "src/shaderchains/delta1.cpp"
    "src/shaderchains/gamma0.cpp"
    "src/shaderchains/gamma1.cpp"
    "src/shaderchains/generate.cpp"
    "src/shaderchains/mipmaps.cpp"
    "src/mako.cpp")

add_library(mako-backend STATIC ${BACKEND_SOURCES})

target_include_directories(mako-backend
    PUBLIC include)

target_link_libraries(mako-backend
    PUBLIC mako-common
    PRIVATE ${CMAKE_DL_LIBS})

set_target_properties(mako-backend PROPERTIES
    CXX_VISIBILITY_PRESET hidden)

if(MAKO_EXPERIMENTAL_LSFG_FP16)
    if(NOT EXISTS "${MAKO_LSFG_FP16_TOOLS_SOURCE}/include/spirv-tools/optimizer.hpp")
        message(FATAL_ERROR "Prepare SPIRV-Tools with scripts/prepare-lsfg-fp16-tools.sh and set MAKO_LSFG_FP16_TOOLS_SOURCE")
    endif()
    set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
    set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
    set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
    set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(SPIRV_TOOLS_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
    add_subdirectory("${MAKO_LSFG_FP16_TOOLS_SOURCE}" "spirv-tools" EXCLUDE_FROM_ALL)
    target_link_libraries(mako-backend PRIVATE SPIRV-Tools-opt)
    # A Vulkan layer must not export the statically included optimizer into
    # the application's symbol namespace or interpose on another layer.
    if(UNIX AND NOT APPLE)
        target_link_libraries(mako-backend PRIVATE
            "-Wl,--exclude-libs,libSPIRV-Tools-opt.a:libSPIRV-Tools.a")
    endif()
    target_compile_definitions(mako-backend PRIVATE MAKO_EXPERIMENTAL_LSFG_FP16=1)
    install(FILES "${MAKO_LSFG_FP16_TOOLS_SOURCE}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/mako-render/spirv-tools")
    install(FILES "${MAKO_LSFG_FP16_TOOLS_SOURCE}/external/spirv-headers/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/mako-render/spirv-headers")
endif()

if(BUILD_TESTING)
    add_executable(mako-frame-color-policy-tests "tests/frame_color_policy_tests.cpp")
    target_include_directories(mako-frame-color-policy-tests PRIVATE src include)
    add_test(NAME backend-frame-color-policy COMMAND mako-frame-color-policy-tests)
    if(MAKO_EXPERIMENTAL_LSFG_FP16)
        add_executable(mako-backend-fp16-conversion-tests "tests/spirv_fp16_tests.cpp")
        target_include_directories(mako-backend-fp16-conversion-tests PRIVATE src)
        target_link_libraries(mako-backend-fp16-conversion-tests PRIVATE mako-backend SPIRV-Tools-opt)
        add_test(NAME backend-experimental-lsfg-fp16 COMMAND mako-backend-fp16-conversion-tests)
    endif()
    add_executable(mako-backend-hot-path-tests
        "tests/hot_path_tests.cpp")
    target_include_directories(mako-backend-hot-path-tests
        PRIVATE src)
    add_test(NAME backend-hot-path
        COMMAND mako-backend-hot-path-tests)

    add_executable(mako-backend-ls1-spirv-patch-tests
        "tests/ls1_spirv_patch_tests.cpp"
        "src/extraction/spirv_image_format.cpp")
    target_include_directories(mako-backend-ls1-spirv-patch-tests
        PRIVATE src)
    target_link_libraries(mako-backend-ls1-spirv-patch-tests
        PRIVATE mako-common)
    add_test(NAME backend-ls1-spirv-patch
        COMMAND mako-backend-ls1-spirv-patch-tests)

    add_executable(mako-backend-dll-inspection-tests
        "tests/dll_inspection_tests.cpp")
    add_library(mako-test-dxbc-translator SHARED "tests/dxbc_translator_fixture.cpp")
    target_include_directories(mako-test-dxbc-translator PRIVATE src)
    add_dependencies(mako-backend-dll-inspection-tests mako-test-dxbc-translator)
    target_compile_definitions(mako-backend-dll-inspection-tests PRIVATE
        MAKO_TEST_TRANSLATOR_PATH="$<TARGET_FILE:mako-test-dxbc-translator>")
    target_include_directories(mako-backend-dll-inspection-tests
        PRIVATE src)
    target_link_libraries(mako-backend-dll-inspection-tests
        PRIVATE mako-backend)
    add_test(NAME backend-dll-inspection
        COMMAND mako-backend-dll-inspection-tests)

endif()

if(MAKO_INSTALL_DEVELOP)
    install(TARGETS mako-backend
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(DIRECTORY "include/mako-backend/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mako-backend"
        FILES_MATCHING PATTERN "*.hpp")
endif()
