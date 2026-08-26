find_package(Qt6 6.2 REQUIRED COMPONENTS Quick)

set(UI_SOURCES
    "src/backend.cpp"
    "src/main.cpp"
    "src/utils.cpp")

set(UI_RESOURCES
    "rsc/dialogs/CenteredDialog.qml"
    "rsc/dialogs/LargeDialog.qml"
    "rsc/panes/Group.qml"
    "rsc/panes/GroupEntry.qml"
    "rsc/panes/Pane.qml"
    "rsc/widgets/FileEdit.qml"
    "rsc/widgets/FlowSlider.qml"
    "rsc/widgets/List.qml"
    "rsc/i18n/translations.json"
    "rsc/io.github.eugeniosegala.mako.png"
    "rsc/UI.qml")

add_library(mako-ui-localization STATIC
    "src/localization.cpp")
set_target_properties(mako-ui-localization PROPERTIES AUTOMOC ON)
target_include_directories(mako-ui-localization PUBLIC "src")
target_link_libraries(mako-ui-localization PUBLIC Qt6::Core)

qt_add_executable(mako-ui ${UI_SOURCES})

qt_add_resources(mako-ui RESOURCES
    PREFIX "/"
    FILES ${UI_RESOURCES})

set_target_properties(mako-ui PROPERTIES
    AUTOMOC ON
    AUTOUIC ON)

target_compile_options(mako-ui PRIVATE # QT-codegen warnings
    -Wno-unknown-warning-option
    -Wno-ctad-maybe-unsupported
    -Wno-unsafe-buffer-usage-in-libc-call
    -Wno-global-constructors
    -Wno-unsafe-buffer-usage)

target_link_libraries(mako-ui
    PRIVATE mako-common
    PRIVATE mako-backend
    PRIVATE mako-ui-localization
    PRIVATE Qt6::Quick)

if(BUILD_TESTING)
    add_executable(mako-ui-backend-contract-tests
        "tests/backend_contract_tests.cpp"
        "src/backend.cpp"
        "src/utils.cpp"
        "src/backend.hpp")
    set_target_properties(mako-ui-backend-contract-tests PROPERTIES AUTOMOC ON)
    target_include_directories(mako-ui-backend-contract-tests PRIVATE "src")
    target_link_libraries(mako-ui-backend-contract-tests
        PRIVATE mako-common
        PRIVATE mako-backend
        PRIVATE Qt6::Core)
    target_compile_definitions(mako-ui-backend-contract-tests PRIVATE
        MAKO_UI_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/rsc/UI.qml")
    add_test(NAME ui-backend-contract COMMAND mako-ui-backend-contract-tests)

    add_executable(mako-ui-localization-tests
        "tests/localization_tests.cpp")
    target_link_libraries(mako-ui-localization-tests
        PRIVATE mako-ui-localization
        PRIVATE Qt6::Core)
    target_compile_definitions(mako-ui-localization-tests PRIVATE
        MAKO_UI_TRANSLATIONS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/rsc/i18n/translations.json")
    add_test(NAME ui-localization-unit COMMAND mako-ui-localization-tests)
endif()

install(TARGETS mako-ui
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(MAKO_INSTALL_XDG_FILES)
    foreach(MAKO_ICON_SIZE IN ITEMS 16 24 32 48 64 128)
        install(FILES
            "rsc/icons/${MAKO_ICON_SIZE}x${MAKO_ICON_SIZE}/io.github.eugeniosegala.mako.png"
            DESTINATION
            "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${MAKO_ICON_SIZE}x${MAKO_ICON_SIZE}/apps")
    endforeach()
    install(FILES "rsc/io.github.eugeniosegala.mako.png"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps")
    install(FILES "rsc/io.github.eugeniosegala.mako.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
endif()
