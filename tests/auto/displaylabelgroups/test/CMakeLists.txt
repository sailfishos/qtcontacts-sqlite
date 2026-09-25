add_qtcontacts_sqlite_test(tst_displaylabelgroups
    tst_displaylabelgroups.cpp
)

target_link_libraries(tst_displaylabelgroups PRIVATE engine_common)

add_dependencies(tst_displaylabelgroups testdlgg)

# Override the search path to find the test plugin
set_property(TEST tst_displaylabelgroups APPEND PROPERTY ENVIRONMENT
    "QTCONTACTS_SQLITE_PLUGIN_PATH=$<TARGET_FILE_DIR:testdlgg>/")
