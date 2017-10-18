include(../../common.pri)

TARGET = tst_qcontactmanager

INCLUDEPATH += \
    ../../../src/engine/

HEADERS += \
    ../../../src/engine/contactid_p.h \
    ../../../src/extensions/contactmanagerengine.h \
    ../../util.h \
    ../../qcontactmanagerdataholder.h
SOURCES += \
    ../../../src/engine/contactid.cpp \
    tst_qcontactmanager.cpp
