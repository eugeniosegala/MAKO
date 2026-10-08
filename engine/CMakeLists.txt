cmake_minimum_required(VERSION 3.10)
project(mako-render LANGUAGES CXX)
include(GNUInstallDirs)
include(CTest)

set(MAKO_REPOSITORY_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE.md")
set(MAKO_THIRD_PARTY_NOTICES "${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md")
set(MAKO_ASSET_PROVENANCE "${CMAKE_CURRENT_SOURCE_DIR}/../ASSET_PROVENANCE.md")
if(NOT EXISTS "${MAKO_REPOSITORY_LICENSE}")
    message(FATAL_ERROR "The monorepo license is missing: ${MAKO_REPOSITORY_LICENSE}")
endif()
if(NOT EXISTS "${MAKO_THIRD_PARTY_NOTICES}")
    message(FATAL_ERROR "The monorepo third-party notices are missing: ${MAKO_THIRD_PARTY_NOTICES}")
endif()
if(NOT EXISTS "${MAKO_ASSET_PROVENANCE}")
    message(FATAL_ERROR "The monorepo asset provenance record is missing: ${MAKO_ASSET_PROVENANCE}")
endif()
install(FILES "${MAKO_REPOSITORY_LICENSE}" "${MAKO_THIRD_PARTY_NOTICES}" "${MAKO_ASSET_PROVENANCE}"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/mako-render")
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/mako-diagnostics"
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mako-installer"
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mako-remote-play"
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mako_remote_play/"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/mako-render/mako_remote_play"
    FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MAKO_VERSION LIMIT_COUNT 1)
string(STRIP "${MAKO_VERSION}" MAKO_VERSION)
if(MAKO_VERSION STREQUAL "")
    message(FATAL_ERROR "VERSION must contain a build identity")
endif()

# Released source archives are identified by VERSION. Development checkouts
# also include the component commit and a short digest of tracked changes and
# untracked paths so two local binaries from the same release line cannot
# produce indistinguishable diagnostics. Keep this configure-time only: the
# Vulkan presentation path must never inspect Git or hash files at runtime.
set(MAKO_BUILD_FINGERPRINT "${MAKO_VERSION}")
find_program(MAKO_GIT_EXECUTABLE NAMES git)
if(MAKO_GIT_EXECUTABLE)
    execute_process(
        COMMAND "${MAKO_GIT_EXECUTABLE}" log -1 --format=%H -- .
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE MAKO_GIT_COMMIT_RESULT
        OUTPUT_VARIABLE MAKO_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(MAKO_GIT_COMMIT_RESULT EQUAL 0 AND NOT MAKO_GIT_COMMIT STREQUAL "")
        string(SUBSTRING "${MAKO_GIT_COMMIT}" 0 12 MAKO_GIT_SHORT_COMMIT)
        execute_process(
            COMMAND "${MAKO_GIT_EXECUTABLE}" diff --binary HEAD -- .
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE MAKO_GIT_DIFF
            ERROR_QUIET)
        execute_process(
            COMMAND "${MAKO_GIT_EXECUTABLE}" status --porcelain
                --untracked-files=normal -- .
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE MAKO_GIT_STATUS
            ERROR_QUIET)
        set(MAKO_BUILD_FINGERPRINT "${MAKO_GIT_SHORT_COMMIT}")
        if(NOT MAKO_GIT_STATUS STREQUAL "")
            string(SHA256 MAKO_GIT_DIRTY_DIGEST
                "${MAKO_GIT_DIFF}\n${MAKO_GIT_STATUS}")
            string(SUBSTRING "${MAKO_GIT_DIRTY_DIGEST}" 0 8
                MAKO_GIT_DIRTY_SHORT_DIGEST)
            string(APPEND MAKO_BUILD_FINGERPRINT
                ".dirty.${MAKO_GIT_DIRTY_SHORT_DIGEST}")
        endif()
    endif()
endif()

# === READ HERE FOR BUILD OPTIONS ===
option(MAKO_BUILD_VK_LAYER     "Build the Vulkan layer"            ON)
option(MAKO_BUILD_UI           "Build the user interface"         OFF)
option(MAKO_BUILD_CLI          "Build the command line interface"  ON)
option(MAKO_INSTALL_DEVELOP    "Install development libraries and headers"      OFF)
option(MAKO_INSTALL_XDG_FILES  "Install the application icon and desktop files" OFF)
option(MAKO_ENABLE_SANITIZERS  "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MAKO_EXPERIMENTAL_LSFG_FP16 "Convert FP32-only LSFG models when the existing FP16 setting is enabled (quality unqualified)" OFF)
set(MAKO_LSFG_FP16_TOOLS_SOURCE "" CACHE PATH "Pinned SPIRV-Tools source prepared by scripts/prepare-lsfg-fp16-tools.sh")
if(MAKO_EXPERIMENTAL_LSFG_FP16)
    string(APPEND MAKO_BUILD_FINGERPRINT ".experimental-lsfg-fp16")
endif()
option(MAKO_REQUIRE_NATIVE_PACKAGE_HEADERS "Require the shared package Vulkan header baseline" OFF)
set(MAKO_VULKAN_HEADERS_INCLUDE_DIR "" CACHE PATH "Optional Vulkan-Headers include directory for direct builds")
if(MAKO_VULKAN_HEADERS_INCLUDE_DIR)
    if(NOT EXISTS "${MAKO_VULKAN_HEADERS_INCLUDE_DIR}/vulkan/vulkan_core.h")
        message(FATAL_ERROR
            "MAKO_VULKAN_HEADERS_INCLUDE_DIR does not contain Vulkan-Headers: "
            "${MAKO_VULKAN_HEADERS_INCLUDE_DIR}")
    endif()
    include_directories(BEFORE SYSTEM "${MAKO_VULKAN_HEADERS_INCLUDE_DIR}")
endif()
set(MAKO_LAYER_LIBRARY_PATH libmako-render.so CACHE STRING "Change where Vulkan searches for the layer library")
set(MAKO_SCALING_LAYER_LIBRARY_PATH libmako-render-scaling.so CACHE STRING "Change where Vulkan searches for the isolated spatial-scaling layer library")
set(MAKO_LAYER_MANIFEST_SUFFIX "" CACHE STRING "Suffix for the installed Vulkan layer manifest filename")
option(MAKO_TESTING_RENDERDOC  "Enable RenderDoc integration for testing purposes" OFF)
# === READ HERE FOR BUILD OPTIONS ===

if(MAKO_BUILD_VK_LAYER)
    install(PROGRAMS
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mako-launch"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mako-vrr-lease"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(BUILD_TESTING AND UNIX)
    find_program(MAKO_PYTHON3_EXECUTABLE NAMES python3)
    if(NOT MAKO_PYTHON3_EXECUTABLE)
        message(FATAL_ERROR "Python 3 is required for Renderer source-contract tests")
    endif()

    add_test(NAME standalone-launcher
        COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-mako-launch.sh"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mako-launch")
    add_test(NAME remote-play-bindings
        COMMAND "${MAKO_PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-remote-play-bindings.py" --check)
    add_test(NAME remote-play-command
        COMMAND "${MAKO_PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-mako-remote-play.py")
    set_tests_properties(standalone-launcher PROPERTIES
        LABELS "portable-policy")

    add_test(NAME gamescope-vrr-lease
        COMMAND "${MAKO_PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-mako-vrr-lease.py")
    set_tests_properties(gamescope-vrr-lease PROPERTIES
        LABELS "portable-policy")

    add_test(NAME mako-gym-bridge-contract
        COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-run-mako-gym.sh"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-mako-gym.sh")
    set_tests_properties(mako-gym-bridge-contract PROPERTIES
        LABELS "portable-policy")

    add_test(NAME standalone-installer
        COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-mako-installer.sh"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mako-installer")
    set_tests_properties(standalone-installer PROPERTIES
        LABELS "portable-policy")

    add_test(NAME standalone-flatpak-installer
        COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-mako-flatpak-installer.sh"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mako-install-flatpaks")
    set_tests_properties(standalone-flatpak-installer PROPERTIES
        LABELS "portable-policy")

    add_test(NAME flatpak-runtime-freshness
        COMMAND "${MAKO_PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/generate-flatpak-runtimes.py"
            --check)
    set_tests_properties(flatpak-runtime-freshness PROPERTIES LABELS "portable-policy")

    add_test(NAME flatpak-vulkan-headers-freshness
        COMMAND "${MAKO_PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-flatpak-vulkan-headers.py"
            --check)
    add_test(NAME flatpak-vulkan-headers-contract
        COMMAND "${MAKO_PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-flatpak-vulkan-headers.py")
    set_tests_properties(flatpak-vulkan-headers-freshness
        flatpak-vulkan-headers-contract PROPERTIES LABELS "portable-policy")

    add_test(NAME vkbasalt-release-contract
        COMMAND "${MAKO_PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/manage-vkbasalt-release.py"
            --check
            --check-flatpak-module
            "${CMAKE_CURRENT_SOURCE_DIR}/dist/flatpak/mako-render/vkbasalt-module.json")
    add_test(NAME vkbasalt-release-validator
        COMMAND "${MAKO_PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-vkbasalt-release.py")
    set_tests_properties(vkbasalt-release-contract
        vkbasalt-release-validator PROPERTIES LABELS "portable-policy")

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME arch-package-release-pin
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/dist/arch/check-release-pin.sh")
        add_test(NAME arch-package-contract
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/dist/arch/test-package-contract.sh")
        set_tests_properties(arch-package-release-pin
            arch-package-contract PROPERTIES LABELS "portable-policy")
    endif()

    add_test(NAME embedded-color-conversion-spirv-freshness
        COMMAND "${MAKO_PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-color-conversion-spirv.py"
            --check)
    set_tests_properties(embedded-color-conversion-spirv-freshness PROPERTIES
        LABELS "portable-policy")

    add_test(NAME embedded-spatial-scaling-spirv-freshness
        COMMAND "${MAKO_PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-spatial-scaling-spirv.py"
            --check)
    set_tests_properties(embedded-spatial-scaling-spirv-freshness PROPERTIES
        LABELS "portable-policy")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_SKIP_RPATH ON)

if(MAKO_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR
            "MAKO_ENABLE_SANITIZERS requires GCC, Clang, or AppleClang")
    endif()
    add_compile_options(
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all)
    set(CMAKE_EXE_LINKER_FLAGS
        "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
    set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address,undefined")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(STATUS "Building with clang-specific diagnostics")

        set(CMAKE_CXX_CLANG_TIDY clang-tidy) # See .clang-tidy files

        add_compile_options(
            # By default, enable all warnings
            -Weverything
            # Some warnings are incompatible with each other
            -Wno-pre-c++20-compat-pedantic
            -Wno-c++98-compat-pedantic
            -Wno-switch-default
            # Then there's code-style things I don't care about
            -Wno-missing-designated-field-initializers
            -Wno-shadow
            # And functional warning I don't care about either
            -Wno-cast-function-type-strict
            -Wno-padded
        )
    endif()
endif()

if(MAKO_TESTING_RENDERDOC)
    add_compile_definitions(MAKO_TESTING_RENDERDOC)
endif()

if(MAKO_BUILD_VK_LAYER OR MAKO_BUILD_UI OR MAKO_BUILD_CLI OR
        MAKO_INSTALL_DEVELOP OR BUILD_TESTING)
    add_subdirectory(mako-common)
endif()
if(MAKO_BUILD_VK_LAYER OR MAKO_BUILD_CLI OR MAKO_INSTALL_DEVELOP)
    add_subdirectory(mako-backend)
endif()
if(MAKO_BUILD_VK_LAYER OR MAKO_BUILD_CLI OR BUILD_TESTING)
    add_subdirectory(mako-render)
endif()
if(MAKO_BUILD_UI)
    add_subdirectory(mako-ui)
endif()
if(MAKO_BUILD_CLI)
    add_subdirectory(mako-cli)
endif()
add_subdirectory(scripts/mako_remote_play)
if(BUILD_TESTING AND TARGET mako-remote-play-sdr)
    set(remote_play_test_environment "MAKO_TEST_SDR_NATIVE_HELPER=$<TARGET_FILE:mako-remote-play-sdr>;MAKO_TEST_SDR_CLIENT=$<TARGET_FILE:mako-remote-play-sdr-tests>;MAKO_TEST_SDL_FIXTURE_DIR=$<TARGET_FILE_DIR:mako-remote-play-sdl-fixture>")
    if(NOT MAKO_ENABLE_SANITIZERS)
        # Instrumented Clang DSOs need the executable's ASan runtime. The
        # SDL client below supplies it; /usr/bin/env environment fixtures do not.
        list(APPEND remote_play_test_environment "MAKO_TEST_SDR_HELPER=$<TARGET_FILE:mako-remote-play-sdr>")
    endif()
    set_tests_properties(remote-play-command PROPERTIES LABELS "portable-policy"
        ENVIRONMENT "${remote_play_test_environment}")
endif()
