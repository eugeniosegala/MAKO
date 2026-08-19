cmake_minimum_required(VERSION 3.10)
project(mako-render LANGUAGES CXX)
include(GNUInstallDirs)
include(CTest)

set(MAKO_REPOSITORY_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE.md")
if(NOT EXISTS "${MAKO_REPOSITORY_LICENSE}")
    message(FATAL_ERROR "The monorepo license is missing: ${MAKO_REPOSITORY_LICENSE}")
endif()
install(FILES "${MAKO_REPOSITORY_LICENSE}"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/mako-render")
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/mako-diagnostics"
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MAKO_VERSION LIMIT_COUNT 1)
string(STRIP "${MAKO_VERSION}" MAKO_VERSION)
if(MAKO_VERSION STREQUAL "")
    message(FATAL_ERROR "VERSION must contain a build identity")
endif()

# === READ HERE FOR BUILD OPTIONS ===
option(MAKO_BUILD_VK_LAYER     "Build the Vulkan layer"            ON)
option(MAKO_BUILD_UI           "Build the user interface"         OFF)
option(MAKO_BUILD_CLI          "Build the command line interface"  ON)
option(MAKO_INSTALL_DEVELOP    "Install development libraries and headers"      OFF)
option(MAKO_INSTALL_XDG_FILES  "Install the application icon and desktop files" OFF)
option(MAKO_ENABLE_SANITIZERS  "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
set(MAKO_LAYER_LIBRARY_PATH libmako-render.so CACHE STRING "Change where Vulkan searches for the layer library")
set(MAKO_LAYER_MANIFEST_SUFFIX "" CACHE STRING "Suffix for the installed Vulkan layer manifest filename")
option(MAKO_TESTING_RENDERDOC  "Enable RenderDoc integration for testing purposes" OFF)
set(MAKO_GPU_QUALITY_TEST AUTO CACHE STRING
    "Run the AMD hardware image-quality regression during CTest: AUTO (required on Steam Machine), REQUIRED, or OFF")
set_property(CACHE MAKO_GPU_QUALITY_TEST PROPERTY STRINGS AUTO REQUIRED OFF)
set(MAKO_GPU_QUALITY_OUTPUT "${CMAKE_BINARY_DIR}/quality-regression" CACHE PATH
    "Directory for AMD hardware image-quality comparison artifacts")
string(TOUPPER "${MAKO_GPU_QUALITY_TEST}" MAKO_GPU_QUALITY_TEST)
if(NOT MAKO_GPU_QUALITY_TEST STREQUAL "AUTO" AND
        NOT MAKO_GPU_QUALITY_TEST STREQUAL "REQUIRED" AND
        NOT MAKO_GPU_QUALITY_TEST STREQUAL "OFF")
    message(FATAL_ERROR "MAKO_GPU_QUALITY_TEST must be AUTO, REQUIRED, or OFF")
endif()
# === READ HERE FOR BUILD OPTIONS ===

if(MAKO_BUILD_VK_LAYER)
    install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mako-launch"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(BUILD_TESTING AND UNIX)
    add_test(NAME standalone-launcher
        COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-mako-launch.sh"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mako-launch")
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
if(MAKO_BUILD_VK_LAYER OR BUILD_TESTING)
    add_subdirectory(mako-render)
endif()
if(MAKO_BUILD_UI)
    add_subdirectory(mako-ui)
endif()
if(MAKO_BUILD_CLI)
    add_subdirectory(mako-cli)
endif()
