find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)

set(TESTS_INSTALL_DIR "/opt/tests/qtcontacts-sqlite-qt${QT_VERSION_MAJOR}")

function(add_qtcontacts_sqlite_test TARGET)
    add_executable(${TARGET} ${ARGN})

    target_link_libraries(${TARGET} PRIVATE
        Qt::Core
        Qt::Contacts
        Qt::Test
        qtcontacts_sqlite_extensions
    )

    target_include_directories(${TARGET} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    # Each test gets its own home dir
    set(_test_home "${CMAKE_CURRENT_BINARY_DIR}/home-${TARGET}")
    file(MAKE_DIRECTORY "${_test_home}/.local/share")
    set(_test_env
        "HOME=${_test_home}"
        "XDG_DATA_HOME=${_test_home}/.local/share"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:qtcontacts_sqlite>/.."
        "LC_ALL=C")

    add_test(NAME ${TARGET}
        COMMAND "${DBUS_RUN_SESSION}" -- "$<TARGET_FILE:${TARGET}>")
    set_tests_properties(${TARGET} PROPERTIES ENVIRONMENT "${_test_env}")
    install(TARGETS ${TARGET} RUNTIME DESTINATION "${TESTS_INSTALL_DIR}")
endfunction()

install(FILES tests.xml DESTINATION "${TESTS_INSTALL_DIR}")

add_subdirectory(auto)
add_subdirectory(benchmarks)
