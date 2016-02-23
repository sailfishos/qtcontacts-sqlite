include(../../../config.pri)

TEMPLATE = app
TARGET = deltadetection

SOURCES = main.cpp deltasyncadapter.cpp
HEADERS = deltasyncadapter.h ../../../src/extensions/contactmanagerengine.h

INCLUDEPATH += ../../../src/extensions ../../../src/engine/

QT += contacts-private

target.path = /opt/tests/qtcontacts-sqlite-qt5
INSTALLS += target
