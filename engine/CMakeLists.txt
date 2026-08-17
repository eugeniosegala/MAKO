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
set(MAKO_LAYER_LIBRARY_PATH libmako-render.so CACHE STRING "Change where Vulkan searches for the layer library")
set(MAKO_LAYER_MANIFEST_SUFFIX "" CACHE STRING "Suffix for the installed Vulkan layer manifest filename")
option(MAKO_TESTING_RENDERDOC  "Enable RenderDoc integration for testing purposes" OFF)
# === READ HERE FOR BUILD OPTIONS ===

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_SKIP_RPATH ON)

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
if(MAKO_BUILD_VK_LAYER OR MAKO_INSTALL_DEVELOP)
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
