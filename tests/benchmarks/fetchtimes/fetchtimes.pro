include(../../../config.pri)

TEMPLATE = app
TARGET = fetchtimes

QT = core

INCLUDEPATH += ../../../src/extensions/
HEADERS += \
    $$PWD/../../../src/extensions/contactmanagerengine.h \
    $$PWD/../../../src/extensions/qcontactsearchfilterrequest.h
SOURCES = main.cpp

target.path = /opt/tests/qtcontacts-sqlite-qt5
INSTALLS += target
