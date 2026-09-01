add_executable(fetchtimes main.cpp)

target_link_libraries(fetchtimes PRIVATE
    Qt::Core
    Qt::Contacts
    qtcontacts_sqlite_extensions
)

install(TARGETS fetchtimes RUNTIME DESTINATION "${TESTS_INSTALL_DIR}")
