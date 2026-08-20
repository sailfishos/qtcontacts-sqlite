add_library(testdlgg MODULE
    testdlggplugin.cpp
    testdlggplugin.h
)

target_link_libraries(testdlgg PRIVATE
    Qt::Core
    Qt::Contacts
    qtcontacts_sqlite_extensions
)

install(TARGETS testdlgg LIBRARY DESTINATION "${DLGG_PLUGINS_DIR}")
