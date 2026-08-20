add_qtcontacts_sqlite_test(tst_database
    tst_database.cpp
    stub_contactsengine.cpp
)

target_link_libraries(tst_database PRIVATE engine_common engine_database)
