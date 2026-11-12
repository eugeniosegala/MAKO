set(BACKEND_SOURCES
    "src/extraction/content_hash.cpp"
    "src/extraction/dll_inspection.cpp"
    "src/extraction/dll_reader.cpp"
    "src/extraction/dxbc_translation.cpp"
    "src/extraction/ls1_shader_set.cpp"
    "src/extraction/spirv_image_format.cpp"
    "src/extraction/spirv_output_split.cpp"
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
    "src/mako.cpp"
    "src/mako_fg.cpp")

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
    add_executable(mako-black-image-tests "tests/black_image_tests.cpp")
    target_include_directories(mako-black-image-tests PRIVATE src)
    target_link_libraries(mako-black-image-tests PRIVATE mako-common)
    add_test(NAME backend-black-image COMMAND mako-black-image-tests)
    set_tests_properties(backend-black-image PROPERTIES LABELS "portable-policy")

    add_executable(mako-backend-output-split-tests
        "tests/spirv_output_split_tests.cpp"
        "src/extraction/spirv_output_split.cpp"
        "src/extraction/model_resource_validation.cpp")
    target_include_directories(mako-backend-output-split-tests PRIVATE src include)
    target_link_libraries(mako-backend-output-split-tests PRIVATE mako-common)
    add_test(NAME backend-output-split COMMAND mako-backend-output-split-tests)
    set_tests_properties(backend-output-split PROPERTIES LABELS "portable-policy")

    add_executable(mako-frame-color-policy-tests "tests/frame_color_policy_tests.cpp")
    target_include_directories(mako-frame-color-policy-tests PRIVATE src include)
    add_test(NAME backend-frame-color-policy COMMAND mako-frame-color-policy-tests)
    if(MAKO_EXPERIMENTAL_LSFG_FP16)
        add_executable(mako-backend-fp16-conversion-tests "tests/spirv_fp16_tests.cpp")
        target_include_directories(mako-backend-fp16-conversion-tests PRIVATE src)
        target_link_libraries(mako-backend-fp16-conversion-tests PRIVATE mako-backend SPIRV-Tools-opt)
        add_test(NAME backend-experimental-lsfg-fp16 COMMAND mako-backend-fp16-conversion-tests)
    endif()
    add_executable(mako-independent-dispatch-tests "tests/independent_dispatch_tests.cpp")
    target_include_directories(mako-independent-dispatch-tests PRIVATE src)
    target_link_libraries(mako-independent-dispatch-tests PRIVATE mako-common)
    add_test(NAME backend-independent-dispatch COMMAND mako-independent-dispatch-tests)
    set_tests_properties(backend-independent-dispatch PROPERTIES LABELS "portable-policy")

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

# MAKO FG, the default frame generation model, is a separate program: the Renderer starts mako-fg-host and drives it
# over a socket (src/mako_fg.cpp), and links no MAKO FG code. Packages install the program and its model from
# engine/models/mako-fg, the local copy of the active release that MAKO FG's release command writes, after checking
# each file against the release's MANIFEST.json. Every Renderer package sets MAKO_REQUIRE_FG, which also rejects a
# release that is not cleared for public distribution.
option(MAKO_FG "Support MAKO FG, the default frame generation model" ON)
option(MAKO_REQUIRE_FG "Fail unless this build installs the active MAKO FG release" OFF)
set(MAKO_FG_RELEASES "${CMAKE_CURRENT_SOURCE_DIR}/../models/mako-fg" CACHE PATH "MAKO FG releases to install")
if(MAKO_FG AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(mako-backend PRIVATE ${CMAKE_DL_LIBS})
    target_compile_definitions(mako-backend PRIVATE MAKO_FG=1)
    if(BUILD_TESTING)
        add_executable(mako-test-fake-fg-host tests/fake_mako_fg_host.cpp)
        target_include_directories(mako-test-fake-fg-host PRIVATE src)
        target_link_libraries(mako-test-fake-fg-host PRIVATE ${CMAKE_DL_LIBS})
        add_executable(mako-backend-fg-host-tests tests/mako_fg_host_tests.cpp)
        target_include_directories(mako-backend-fg-host-tests PRIVATE src)
        target_compile_definitions(mako-backend-fg-host-tests PRIVATE MAKO_FG=1)
        target_compile_options(mako-backend-fg-host-tests PRIVATE -UNDEBUG)
        target_link_libraries(mako-backend-fg-host-tests PRIVATE mako-backend)
        add_dependencies(mako-backend-fg-host-tests mako-test-fake-fg-host)
        add_test(NAME backend-fg-host
            COMMAND mako-backend-fg-host-tests $<TARGET_FILE:mako-test-fake-fg-host>)
        # The same checks in a process whose static thread-local storage outgrows a small thread stack, as a game's can.
        add_executable(mako-backend-fg-host-game-tls-tests tests/mako_fg_host_tests.cpp)
        target_include_directories(mako-backend-fg-host-game-tls-tests PRIVATE src)
        target_compile_definitions(mako-backend-fg-host-game-tls-tests PRIVATE MAKO_FG=1 MAKO_TEST_GAME_THREAD_STORAGE=1)
        target_compile_options(mako-backend-fg-host-game-tls-tests PRIVATE -UNDEBUG)
        target_link_libraries(mako-backend-fg-host-game-tls-tests PRIVATE mako-backend)
        add_dependencies(mako-backend-fg-host-game-tls-tests mako-test-fake-fg-host)
        add_test(NAME backend-fg-host-game-tls
            COMMAND mako-backend-fg-host-game-tls-tests $<TARGET_FILE:mako-test-fake-fg-host>)
    endif()
endif()
set(mako_fg_available OFF)
if(MAKO_FG AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$"
        AND EXISTS "${MAKO_FG_RELEASES}/versions.json")
    set(mako_fg_available ON)
endif()
if(MAKO_REQUIRE_FG AND NOT mako_fg_available)
    message(FATAL_ERROR "MAKO_REQUIRE_FG needs MAKO FG enabled on x86 Linux with its release in engine/models/mako-fg")
endif()
if(mako_fg_available)
    file(READ "${MAKO_FG_RELEASES}/versions.json" mako_fg_index)
    string(JSON mako_fg_active GET "${mako_fg_index}" active)
    string(JSON mako_fg_count LENGTH "${mako_fg_index}" versions)
    set(mako_fg_folder "")
    math(EXPR mako_fg_last "${mako_fg_count} - 1")
    foreach(i RANGE 0 ${mako_fg_last})
        string(JSON version GET "${mako_fg_index}" versions ${i} version)
        if(version STREQUAL mako_fg_active)
            string(JSON mako_fg_folder GET "${mako_fg_index}" versions ${i} folder)
        endif()
    endforeach()
    if(NOT mako_fg_folder OR mako_fg_folder MATCHES "/|^\\.")
        message(FATAL_ERROR "engine/models/mako-fg/versions.json has no valid active MAKO FG release")
    endif()
    set(mako_fg_release "${MAKO_FG_RELEASES}/${mako_fg_folder}")
    file(READ "${mako_fg_release}/MANIFEST.json" mako_fg_manifest)
    string(JSON mako_fg_format GET "${mako_fg_manifest}" format)
    if(NOT mako_fg_format EQUAL 2)
        message(FATAL_ERROR "MAKO FG release ${mako_fg_active} is not an open-weight release")
    endif()
    # Every file this build installs must match its MANIFEST.json hash; mixture.spv is optional.
    set(mako_fg_required model/weights.bin model/ops.spv model/transport.spv
        runtime/x64/bin/mako-fg-host)
    set(mako_fg_verified "")
    string(JSON mako_fg_listed LENGTH "${mako_fg_manifest}" files)
    math(EXPR mako_fg_last "${mako_fg_listed} - 1")
    foreach(i RANGE 0 ${mako_fg_last})
        string(JSON name GET "${mako_fg_manifest}" files ${i} name)
        list(FIND mako_fg_required "${name}" required)
        if(required GREATER_EQUAL 0 OR name STREQUAL "model/mixture.spv")
            string(JSON expected GET "${mako_fg_manifest}" files ${i} sha256)
            file(SHA256 "${mako_fg_release}/${name}" actual)
            if(NOT actual STREQUAL expected)
                message(FATAL_ERROR "MAKO FG release ${mako_fg_active}: ${name} differs from its MANIFEST.json")
            endif()
            list(APPEND mako_fg_verified "${name}")
        endif()
    endforeach()
    foreach(name IN LISTS mako_fg_required)
        if(NOT name IN_LIST mako_fg_verified)
            message(FATAL_ERROR "MAKO FG release ${mako_fg_active} lacks ${name}")
        endif()
    endforeach()
    if(MAKO_REQUIRE_FG)
        string(JSON mako_fg_cleared GET "${mako_fg_manifest}" public_release_cleared)
        if(NOT mako_fg_cleared STREQUAL "ON")
            message(FATAL_ERROR "MAKO FG release ${mako_fg_active} is not cleared for public distribution: "
                "packages install only a release whose MANIFEST.json sets public_release_cleared")
        endif()
    endif()
    list(TRANSFORM mako_fg_verified PREPEND "${mako_fg_release}/" OUTPUT_VARIABLE mako_fg_inputs)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${MAKO_FG_RELEASES}/versions.json" "${mako_fg_release}/MANIFEST.json" ${mako_fg_inputs})
    # One 64-bit program serves 32-bit and 64-bit games, so only the 64-bit build installs it. The Renderer finds it in
    # mako-fg/ beside the 64-bit library, and the program finds its model beside itself.
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        install(PROGRAMS "${mako_fg_release}/runtime/x64/bin/mako-fg-host"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/mako-fg")
        install(FILES "${mako_fg_release}/version.json" DESTINATION "${CMAKE_INSTALL_LIBDIR}/mako-fg")
        foreach(name IN LISTS mako_fg_verified)
            if(name MATCHES "^model/")
                install(FILES "${mako_fg_release}/${name}" DESTINATION "${CMAKE_INSTALL_LIBDIR}/mako-fg/model")
            endif()
        endforeach()
        # The model's licence and the program's ncnn and glslang notices ship with every package.
        install(FILES "${mako_fg_release}/LICENSE" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/mako-render/mako-fg")
        install(FILES "${mako_fg_release}/runtime/THIRD_PARTY_NOTICES" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/mako-render/mako-fg")
    endif()
    message(STATUS "MAKO FG: installing release ${mako_fg_active}")
endif()
