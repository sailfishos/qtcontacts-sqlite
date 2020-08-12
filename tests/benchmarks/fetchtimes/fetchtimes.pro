include(../../../config.pri)

TEMPLATE = app
TARGET = fetchtimes

QT = core

SOURCES = main.cpp
INCLUDEPATH += $$PWD/../../../src/extensions/

target.path = /opt/tests/qtcontacts-sqlite-qt5
INSTALLS += target
