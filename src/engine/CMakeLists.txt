# Build the engine sources into object libraries,
# so we can re-use them in the tests
add_library(engine_common OBJECT
    contactid.cpp
    contactid_p.h
    contactstransientstore.cpp
    contactstransientstore.h
    conversion.cpp
    conversion_p.h
    defaultdlggenerator.cpp
    defaultdlggenerator.h
    memorytable.cpp
    memorytable_p.h
    semaphore_p.cpp
    semaphore_p.h
    ../extensions/contactmanagerengine.h
)

target_include_directories(engine_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(engine_common PUBLIC
    Qt::Core
    Qt::Contacts
    qtcontacts_sqlite_extensions
)


# Separate object library for the database. It needs some defines and mlite.
add_library(engine_database OBJECT
    contactsdatabase.cpp
    contactsdatabase.h
)

target_link_libraries(engine_database PUBLIC
    engine_common
    Qt::Sql
)

# Compile an absolute plugin search path into the code
if(IS_ABSOLUTE "${DLGG_PLUGINS_DIR}")
    set(DLGG_PLUGINS_FULL_DIR "${DLGG_PLUGINS_DIR}")
else()
    set(DLGG_PLUGINS_FULL_DIR "${CMAKE_INSTALL_PREFIX}/${DLGG_PLUGINS_DIR}")
endif()

target_compile_definitions(engine_database PRIVATE
    CONTACTS_DATABASE_PATH="${DLGG_PLUGINS_FULL_DIR}/"
    QTCONTACTS_SQLITE_PRIVILEGED_DIR="privileged"
    QTCONTACTS_SQLITE_DATABASE_DIR="Contacts/qtcontacts-sqlite"
    QTCONTACTS_SQLITE_DATABASE_NAME="contacts.db"
)

if(MLITE_FOUND)
    target_compile_definitions(engine_database PUBLIC HAS_MLITE)
    target_include_directories(engine_database PUBLIC ${MLITE_INCLUDE_DIRS})
    target_link_libraries(engine_database PUBLIC ${MLITE_LIBRARIES})
endif()

if(ENABLE_LOAD_ICU)
    target_compile_definitions(engine_database PRIVATE QTCONTACTS_SQLITE_LOAD_ICU)
    target_link_libraries(engine_database PUBLIC PkgConfig::SQLITE3)
endif()


# The actual contacts plugin
add_library(qtcontacts_sqlite MODULE
    contactsplugin.cpp
    contactnotifier.cpp
    contactreader.cpp
    contactsengine.cpp
    contactwriter.cpp
    contactnotifier.h
    contactreader.h
    contactsengine.h
    contactwriter.h
    trace_p.h
    plugin.json
)

target_link_libraries(qtcontacts_sqlite PRIVATE
    engine_common
    engine_database
    qtcontacts_sqlite_extensions
    Qt::Core
    Qt::Sql
    Qt::DBus
    Qt::Contacts
)

target_compile_definitions(qtcontacts_sqlite PRIVATE
    SORT_PRESENCE_BY_AVAILABILITY
)

# Shared libraries would otherwise allow undefined symbols
target_link_options(qtcontacts_sqlite PRIVATE "-Wl,--no-undefined")

# Needs to be placed in a "contacts" dir to be found while testing
set_target_properties(qtcontacts_sqlite PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/contacts"
)

install(TARGETS qtcontacts_sqlite LIBRARY DESTINATION
    "${CMAKE_INSTALL_LIBDIR}/qt${QT_VERSION_MAJOR}/plugins/contacts")
