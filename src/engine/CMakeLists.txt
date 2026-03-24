qt_add_plugin(
    qtcontacts_sqlite
    CLASS_NAME QContactMemoryEngineFactory
    defaultdlggenerator.cpp
    memorytable.cpp
    semaphore_p.cpp
    conversion.cpp
    contactid.cpp
    contactsdatabase.cpp
    contactsengine.cpp
    contactstransientstore.cpp
    contactsplugin.cpp
    contactnotifier.cpp
    contactreader.cpp
    contactwriter.cpp
)
target_link_libraries(
    qtcontacts_sqlite
    PUBLIC
        Qt6::Core
        Qt6::Core5Compat
        Qt6::DBus
        Qt6::Sql
        QtPIM6::Contacts
)
target_include_directories(
    qtcontacts_sqlite
    PRIVATE
        ../extensions/
)
target_compile_definitions(
    qtcontacts_sqlite
    PRIVATE
        SORT_PRESENCE_BY_AVAILABILITY
        QTCONTACTS_SQLITE_DATABASE_NAME="contacts.db"
        QTCONTACTS_SQLITE_PRIVILEGED_DIR="privileged"
        QTCONTACTS_SQLITE_DATABASE_DIR="Contacts/qtcontacts-sqlite"
        CONTACTS_DATABASE_PATH="${CMAKE_INSTALL_LIBDIR}/qtcontacts-sqlite-qt6/"
)

set_target_properties(
    qtcontacts_sqlite
    PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/contacts
        OUTPUT_NAME libqtcontacts_sqlite
)

install(
    TARGETS
        qtcontacts_sqlite
    LIBRARY DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/qt${QT_VERSION}/plugins/contacts
)
