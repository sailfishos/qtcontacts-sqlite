include(CMakePackageConfigHelpers)

set(EXTENSIONS_NAME "qtcontacts-sqlite-qt${QT_VERSION_MAJOR}-extensions")
set(EXTENSIONS_CMAKE_NAME "QtContactsSqliteQt${QT_VERSION_MAJOR}Extensions")
set(EXTENSIONS_INCLUDE_DIR "${CMAKE_INSTALL_INCLUDEDIR}/${EXTENSIONS_NAME}")
set(EXTENSIONS_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/${EXTENSIONS_CMAKE_NAME}")

# Header-only library / plugin interface
add_library(qtcontacts_sqlite_extensions INTERFACE)
set_target_properties(qtcontacts_sqlite_extensions PROPERTIES EXPORT_NAME Extensions)
target_include_directories(qtcontacts_sqlite_extensions INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/extensions>"
    "$<INSTALL_INTERFACE:${EXTENSIONS_INCLUDE_DIR}>")
target_link_libraries(qtcontacts_sqlite_extensions INTERFACE
    Qt${QT_VERSION_MAJOR}::Contacts)
install(DIRECTORY extensions/ DESTINATION "${EXTENSIONS_INCLUDE_DIR}")

# CMake package config
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/extensions-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${EXTENSIONS_CMAKE_NAME}Config.cmake"
    INSTALL_DESTINATION "${EXTENSIONS_CMAKE_DIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${EXTENSIONS_CMAKE_NAME}Config.cmake"
    DESTINATION "${EXTENSIONS_CMAKE_DIR}")
install(TARGETS qtcontacts_sqlite_extensions
    EXPORT ${EXTENSIONS_CMAKE_NAME}Targets)
install(EXPORT ${EXTENSIONS_CMAKE_NAME}Targets
    NAMESPACE QtContactsSqlite::
    DESTINATION "${EXTENSIONS_CMAKE_DIR}")

# pkg-config
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/qtcontacts-sqlite-extensions.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${EXTENSIONS_NAME}.pc" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${EXTENSIONS_NAME}.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

add_subdirectory(engine)
