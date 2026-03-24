cmake_minimum_required(VERSION 3.25.0 FATAL_ERROR)

project(qtcontacts-sqlite
  VERSION 6.5
  DESCRIPTION "Qt Contacts backend with SQLite"
  HOMEPAGE_URL "https://github.com/sailfishos/qtcontacts-sqlite"
  LANGUAGES C CXX
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

include(CTest)

set(QT_VERSION 6)
find_package(Qt6 COMPONENTS Core DBus Sql Core5Compat)
find_package(QtPIM6 REQUIRED)

add_subdirectory(src)
