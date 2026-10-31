add_executable(LockStyleTest lock_style_test.cc)
target_link_libraries(LockStyleTest PRIVATE
    gxdm-lock-common
    Qt6::Test
)
add_test(NAME LockStyle COMMAND LockStyleTest)

add_executable(LockBackendTest lock_backend_test.cc)
target_link_libraries(LockBackendTest PRIVATE
    gxdm-lock-common
    Qt6::Test
)
add_test(NAME LockBackend COMMAND LockBackendTest)

# Runs on a private session bus so fake MPRIS players never reach the user's
# real session.
add_executable(LockMediaTest media_controller_test.cc)
target_link_libraries(LockMediaTest PRIVATE
    gxdm-lock-common
    Qt6::Test
)
find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
if(DBUS_RUN_SESSION_EXECUTABLE)
    add_test(NAME LockMedia COMMAND "${DBUS_RUN_SESSION_EXECUTABLE}"
        "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/dbus-test-session.conf"
        -- $<TARGET_FILE:LockMediaTest>)
endif()

add_executable(LockServicesTest lock_services_test.cc)
target_link_libraries(LockServicesTest PRIVATE
    gxdm-lock-common
    Qt6::Test
)
if(DBUS_RUN_SESSION_EXECUTABLE)
    add_test(NAME LockServices COMMAND "${DBUS_RUN_SESSION_EXECUTABLE}"
        "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/dbus-test-session.conf"
        -- $<TARGET_FILE:LockServicesTest>)
endif()

add_executable(LockFingerprintTest fingerprint_unlock_test.cc)
target_link_libraries(LockFingerprintTest PRIVATE
    gxdm-lock-common
    Qt6::Test
)
add_test(NAME LockFingerprint COMMAND LockFingerprintTest)
