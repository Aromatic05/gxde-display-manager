# Copyright (C) 2026 CharOfString <root@charofstring.cc>
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, version 3.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# this program. If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.21)

project(gxde-greeter VERSION 0.1 LANGUAGES CXX)

get_directory_property(GREETER_COMPILE_DEFINITIONS COMPILE_DEFINITIONS)
list(REMOVE_ITEM GREETER_COMPILE_DEFINITIONS
  QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
set_directory_properties(PROPERTIES
  COMPILE_DEFINITIONS "${GREETER_COMPILE_DEFINITIONS}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5
  REQUIRED COMPONENTS
    DBus
    Quick
    Network
)
find_package(X11 REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_resources(GREETER_RESOURCES
  res/resources.qrc
)

qt_add_executable(gxde-greeter
  ${GREETER_RESOURCES}
  src/session_list.cc
  src/session_list.h
  src/main.cc
  src/login_transition.cc
  src/login_transition.h
  src/greeter_backend.cc
  src/greeter_backend.h
  src/screen_manager.cc
  src/screen_manager.h
  src/utils/macros.h
  # Wallpaper and cursor setup, shared with gxdm-greeter-classic.
  ../common/GreeterAppearance.cpp
  ../common/GreeterAppearance.h
  ../common/GreeterCursor.cpp
  ../common/GreeterCursor.h
  ../common/GreeterScale.cpp
  ../common/GreeterScale.h
  ../common/SystemDefaults.cpp
  ../common/SystemDefaults.h
  ../common/FingerprintSession.cpp
  ../common/FingerprintSession.h
)

set_source_files_properties(qml/MainWindow.qml PROPERTIES
  QT_RESOURCE_ALIAS MainWindow.qml)
set_source_files_properties(qml/components/UserSelecter.qml PROPERTIES
  QT_RESOURCE_ALIAS UserSelecter.qml)
set_source_files_properties(qml/components/PasswordCard.qml PROPERTIES
  QT_RESOURCE_ALIAS PasswordCard.qml)
set_source_files_properties(qml/components/UserAvatar.qml PROPERTIES
  QT_RESOURCE_ALIAS UserAvatar.qml)
set_source_files_properties(qml/components/VirtualKeyboardPanel.qml PROPERTIES
  QT_RESOURCE_ALIAS VirtualKeyboardPanel.qml)
set_source_files_properties(qml/components/DtkFrame.qml PROPERTIES
  QT_RESOURCE_ALIAS DtkFrame.qml)
set_source_files_properties(qml/components/PowerMenu.qml PROPERTIES
  QT_RESOURCE_ALIAS PowerMenu.qml)

set_source_files_properties(qml/components/SessionMenu.qml PROPERTIES
  QT_RESOURCE_ALIAS SessionMenu.qml)

set(GREETER_QML_FILES
    qml/MainWindow.qml
    qml/components/UserSelecter.qml
    qml/components/PasswordCard.qml
    qml/components/UserAvatar.qml
    qml/components/VirtualKeyboardPanel.qml
    qml/components/DtkFrame.qml
    qml/components/SessionMenu.qml
    qml/components/PowerMenu.qml
)

qt_add_qml_module(gxde-greeter
  URI Greeter
  VERSION 1.0
  QML_FILES ${GREETER_QML_FILES}
)

set_target_properties(gxde-greeter PROPERTIES OUTPUT_NAME gxdm-greeter-neo)

target_include_directories(gxde-greeter PRIVATE ..)

target_link_libraries(gxde-greeter PRIVATE
  Qt6::DBus
  Qt6::Quick
  Qt6::Network
  X11::X11
  X11::Xcursor
  X11::Xfixes
  X11::Xrandr
)

include(GNUInstallDirs)

install(TARGETS gxde-greeter
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

option(GREETER_BUILD_TESTS "Build greeter backend tests" OFF)
if(GREETER_BUILD_TESTS)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)
  qt_add_executable(greeter-backend-test
    tests/greeter_backend_test.cc
    src/greeter_backend.cc
    src/greeter_backend.h
    ../common/SystemDefaults.cpp
    ../common/SystemDefaults.h
    ../common/FingerprintSession.cpp
    ../common/FingerprintSession.h
  )
  target_include_directories(greeter-backend-test PRIVATE ..)
  target_link_libraries(greeter-backend-test PRIVATE Qt6::Network Qt6::Test)
  add_test(NAME greeter-backend COMMAND greeter-backend-test)
  qt_add_executable(session-list-test
    tests/session_list_test.cc
    src/session_list.cc
    src/session_list.h
  )
  target_include_directories(session-list-test PRIVATE ..)
  target_link_libraries(session-list-test PRIVATE Qt6::Test)
  add_test(NAME session-list COMMAND session-list-test)
  qt_add_executable(screen-manager-test
    tests/screen_manager_test.cc
    src/screen_manager.cc
    src/screen_manager.h
  )
  target_include_directories(screen-manager-test PRIVATE ..)
  target_link_libraries(screen-manager-test PRIVATE
    Qt6::Quick Qt6::Test)
  add_test(NAME screen-manager COMMAND screen-manager-test)
  qt_add_executable(greeter-qml-test tests/qml_test.cc ${GREETER_RESOURCES})
  qt_add_resources(greeter-qml-test "transition-test-qml"
    PREFIX "/transition-test"
    FILES ${GREETER_QML_FILES})
  target_link_libraries(greeter-qml-test PRIVATE Qt6::QuickTest Qt6::Quick)
  target_compile_definitions(greeter-qml-test PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/qml")
  add_test(NAME greeter-qml COMMAND greeter-qml-test -platform offscreen)
endif()
