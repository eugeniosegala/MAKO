set(BACKEND_SOURCES
    "src/extraction/dll_reader.cpp"
    "src/extraction/ls1_shader_set.cpp"
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

if(BUILD_TESTING)
    add_executable(mako-backend-hot-path-tests
        "tests/hot_path_tests.cpp")
    target_include_directories(mako-backend-hot-path-tests
        PRIVATE src)
    add_test(NAME backend-hot-path
        COMMAND mako-backend-hot-path-tests)

endif()

if(MAKO_INSTALL_DEVELOP)
    install(TARGETS mako-backend
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(DIRECTORY "include/mako-backend/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mako-backend"
        FILES_MATCHING PATTERN "*.hpp")
endif()
