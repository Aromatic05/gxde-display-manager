# QML-facing lock facade shared by gxdm-lock-neo. This directory is added as a
# subdirectory of src/greeter-classic so it inherits the classic greeter's
# dependency checks, Qt porting guards and shared source variables.
#
# The legacy widget locker keeps compiling its own copies of these sources; the
# QML locker links the static libraries defined here so both frontends share
# exactly the same authentication, user enumeration and D-Bus behaviour.

set(GXDM_CLASSIC_DIR "${GXDM_REPO_ROOT}/src/greeter-classic")

# NEO_VENDOR_SOURCES/NEO_VENDOR_HEADERS are relative to src/greeter-classic.
# Make them absolute because this CMakeLists lives in a sibling directory.
set(GXDM_LOCK_VENDOR_SOURCES)
foreach(source ${NEO_VENDOR_SOURCES})
    list(APPEND GXDM_LOCK_VENDOR_SOURCES "${GXDM_CLASSIC_DIR}/${source}")
endforeach()
set(GXDM_LOCK_VENDOR_HEADERS)
foreach(header ${NEO_VENDOR_HEADERS})
    list(APPEND GXDM_LOCK_VENDOR_HEADERS "${GXDM_CLASSIC_DIR}/${header}")
endforeach()

set(GXDM_LOCK_BACKEND_SOURCES
    "${GXDM_CLASSIC_DIR}/session_widgets/authinterface.cpp"
    "${GXDM_CLASSIC_DIR}/session_widgets/sessionbasemodel.cpp"
    "${GXDM_CLASSIC_DIR}/session_widgets/userinfo.cpp"
    "${GXDM_CLASSIC_DIR}/lock/fingerprintunlock.cpp"
    "${GXDM_CLASSIC_DIR}/lock/lockworker.cpp"
    "${GXDM_CLASSIC_DIR}/lock/lockshortcutmanager.cpp"
    "${GXDM_CLASSIC_DIR}/lock/dbus/dbuslockagent.cpp"
    "${GXDM_CLASSIC_DIR}/lock/dbus/dbuslockfrontservice.cpp"
    "${GXDM_CLASSIC_DIR}/lock/freedesktopscreensaver.cpp"
    "${GXDM_CLASSIC_DIR}/lock/waylandsessionlock.cpp"
    "${GXDM_REPO_ROOT}/src/common/GreeterAppearance.cpp"
    "${GXDM_REPO_ROOT}/src/common/FingerprintProbe.cpp"
    "${GXDM_REPO_ROOT}/src/common/FingerprintSession.cpp"
    ${GXDM_LOCK_VENDOR_SOURCES}
    ${GXDM_AUTH_FILES}
    ${EXT_SESSION_LOCK_CLIENT_HEADER}
    ${EXT_SESSION_LOCK_PROTOCOL_CODE}
)

set(GXDM_LOCK_BACKEND_HEADERS
    "${GXDM_CLASSIC_DIR}/session_widgets/authinterface.h"
    "${GXDM_CLASSIC_DIR}/session_widgets/sessionbasemodel.h"
    "${GXDM_CLASSIC_DIR}/session_widgets/userinfo.h"
    "${GXDM_CLASSIC_DIR}/lock/fingerprintunlock.h"
    "${GXDM_CLASSIC_DIR}/lock/lockworker.h"
    "${GXDM_CLASSIC_DIR}/lock/lockshortcutmanager.h"
    "${GXDM_CLASSIC_DIR}/lock/dbus/dbuslockagent.h"
    "${GXDM_CLASSIC_DIR}/lock/dbus/dbuslockfrontservice.h"
    "${GXDM_CLASSIC_DIR}/lock/freedesktopscreensaver.h"
    "${GXDM_CLASSIC_DIR}/lock/waylandsessionlock.h"
    "${GXDM_REPO_ROOT}/src/common/GreeterAppearance.h"
    "${GXDM_REPO_ROOT}/src/common/FingerprintProbe.h"
    "${GXDM_REPO_ROOT}/src/common/FingerprintSession.h"
    ${GXDM_LOCK_VENDOR_HEADERS}
)

add_library(gxdm-lock-backend STATIC
    ${GXDM_LOCK_BACKEND_SOURCES}
    ${GXDM_LOCK_BACKEND_HEADERS}
)

target_include_directories(gxdm-lock-backend PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${GXDM_CLASSIC_DIR}"
    "${GXDM_CLASSIC_DIR}/global_util"
    "${GXDM_CLASSIC_DIR}/global_util/dbus"
    "${GXDM_CLASSIC_DIR}/session_widgets"
    "${GXDM_CLASSIC_DIR}/lock"
    "${GXDM_CLASSIC_DIR}/lock/dbus"
    "${GXDM_CLASSIC_DIR}/vendor"
    "${GXDM_REPO_ROOT}/src/auth"
    "${GXDM_REPO_ROOT}/src/common"
    "${GXDM_REPO_ROOT}/src/daemon"
    "${GXDM_CONSTANTS_INCLUDE_DIR}"
    "${GXDM_CLASSIC_BINARY_DIR}"
)

target_link_libraries(gxdm-lock-backend PUBLIC
    ${NEO_COMMON_LIBS}
    Qt6::Qml
    Qt6::WaylandClientPrivate
)

# QML-safe facade, user carousel and style resolution.
add_library(gxdm-lock-common STATIC
    lockbackend.cpp
    lockbackend.h
    userlistmodel.cpp
    userlistmodel.h
    lockstyle.cpp
    lockstyle.h
    lockstylemanager.cpp
    lockstylemanager.h
    mediacontroller.cpp
    mediacontroller.h
    notificationcounter.cpp
    notificationcounter.h
    systemvolume.cpp
    systemvolume.h
)

target_include_directories(gxdm-lock-common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(gxdm-lock-common PUBLIC
    gxdm-lock-backend
    Qt6::Core Qt6::Gui Qt6::DBus Qt6::Network
)

if(LOCK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
