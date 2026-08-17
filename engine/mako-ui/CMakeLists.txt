find_package(Qt6 REQUIRED COMPONENTS Quick)

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
    "rsc/io.github.eugeniosegala.mako.png"
    "rsc/UI.qml")

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
    PRIVATE Qt6::Quick)

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
