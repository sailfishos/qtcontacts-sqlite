include(../../../config.pri)

TEMPLATE = app
TARGET = deltadetection

QT += testlib

SOURCES = main.cpp deltasyncadapter.cpp
HEADERS = deltasyncadapter.h ../../../src/extensions/contactmanagerengine.h

INCLUDEPATH += ../../../src/extensions ../../../src/engine/

target.path = /opt/tests/qtcontacts-sqlite-qt5
INSTALLS += target
