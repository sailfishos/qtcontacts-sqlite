TARGET = tst_aggregation
include(../../common.pri)

INCLUDEPATH += \
    ../../../src/engine/

HEADERS += \
    ../../../src/engine/contactid_p.h \
    ../../../src/extensions/contactmanagerengine.h \
    ../../util.h \
    testsyncadapter.h

SOURCES += \
    ../../../src/engine/contactid.cpp \
    testsyncadapter.cpp \
    tst_aggregation.cpp

