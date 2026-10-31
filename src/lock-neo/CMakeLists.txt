# QML lock screen frontend. The authentication, user model, Wayland session
# lock and D-Bus surface come from gxdm-lock-common / gxdm-lock-backend; this
# target only owns the Qt Quick UI and the per-screen window lifecycle.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Qml)
qt_standard_project_setup(REQUIRES 6.5)

set(LOCK_NEO_QML_FILES
    qml/LockWindow.qml
    qml/LockScreen.qml
    qml/components/Clock.qml
    qml/components/LoginCard.qml
    qml/components/LockButton.qml
    qml/components/IconButton.qml
    qml/components/MediaIndicator.qml
    qml/components/NotificationPill.qml
    qml/components/ShadowText.qml
    qml/components/PowerMenu.qml
    qml/components/UserAvatar.qml
    qml/components/UserSwitcher.qml
    qml/components/VirtualKeyboardPanel.qml
)

# Flatten the QML module so LockWindow.qml can reference the components by
# their type names without a subdirectory import.
foreach(qml_file ${LOCK_NEO_QML_FILES})
    get_filename_component(qml_alias "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES
        QT_RESOURCE_ALIAS "${qml_alias}")
endforeach()

qt_add_resources(LOCK_NEO_RESOURCES
    "${GXDM_REPO_ROOT}/src/greeter-neo/res/resources.qrc"
    lock-neo.qrc
)

qt_add_executable(gxdm-lock-neo
    ${LOCK_NEO_RESOURCES}
    src/main.cc
    "${GXDM_REPO_ROOT}/src/common/GreeterCursor.cpp"
    "${GXDM_REPO_ROOT}/src/common/GreeterCursor.h"
    "${GXDM_REPO_ROOT}/src/common/GreeterScale.cpp"
    "${GXDM_REPO_ROOT}/src/common/GreeterScale.h"
)

qt_add_qml_module(gxdm-lock-neo
    URI Lock
    VERSION 1.0
    QML_FILES ${LOCK_NEO_QML_FILES}
)

set_target_properties(gxdm-lock-neo PROPERTIES
    OUTPUT_NAME gxdm-lock-neo
    # Keep the build-tree binary next to gxdm-lock-classic so the classic
    # locker's style handoff works without installing first.
    RUNTIME_OUTPUT_DIRECTORY "${GXDM_CLASSIC_BINARY_DIR}"
)

target_link_libraries(gxdm-lock-neo PRIVATE
    gxdm-lock-common
    Qt6::Quick
    Qt6::Qml
    LayerShellQt::Interface
    Qt6::WaylandClientPrivate
    X11
    Xcursor
    Xfixes
    Xrandr
)

install(TARGETS gxdm-lock-neo
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
