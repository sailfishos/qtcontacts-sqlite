TARGET = tst_memorytable

include(../../common.pri)

HEADERS += \
    ../../util.h
SOURCES += \
    tst_memorytable.cpp \
    ../../../src/engine/conversion.cpp \
    ../../../src/engine/memorytable.cpp
