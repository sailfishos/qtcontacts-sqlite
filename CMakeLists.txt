cmake_minimum_required(VERSION 3.18)
project(qtcontacts-sqlite VERSION 0.3.24 LANGUAGES CXX)

option(ENABLE_QT6 "Enable building with Qt6" OFF)
option(ENABLE_LOAD_ICU "Enable loading the ICU extension for SQLite" OFF)
option(BUILD_TESTING "Build the test suite" ON)

include(GNUInstallDirs)

if(ENABLE_QT6)
    set(QT_VERSION_MAJOR 6)
else()
    set(QT_VERSION_MAJOR 5)
endif()

# Find Qt
set(QT_COMPONENTS Core Sql DBus Contacts)
if(BUILD_TESTING)
    list(APPEND QT_COMPONENTS Test Versit)
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${QT_COMPONENTS})

# Create version-less aliases for Qt targets (missing below Qt 5.15)
foreach(component IN LISTS QT_COMPONENTS)
    if(NOT TARGET Qt::${component})
        add_library(Qt::${component} ALIAS Qt${QT_VERSION_MAJOR}::${component})
    endif()
endforeach()

# Optional dependency: mlite
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(MLITE mlite${QT_VERSION_MAJOR})
endif()

# Optional, for locale aware sorting of contacts
if(ENABLE_LOAD_ICU)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_AUTOMOC ON)

# Plugins directory for display label group generator plugins
set(DLGG_PLUGINS_DIR "${CMAKE_INSTALL_LIBDIR}/qtcontacts-sqlite-qt${QT_VERSION_MAJOR}")

add_subdirectory(src)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
