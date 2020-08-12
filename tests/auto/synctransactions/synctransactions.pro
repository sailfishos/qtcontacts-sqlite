TARGET = tst_synctransactions
include(../../common.pri)

# We need access to the ContactManagerEngine header and moc output
INCLUDEPATH += \
    ../../../src/engine/ \
    ../../../src/extensions/

HEADERS += \
    ../../../src/engine/contactid_p.h \
    ../../../src/extensions/contactmanagerengine.h \
    ../../../src/extensions/qcontactcollectionchangesfetchrequest.h \
    ../../../src/extensions/qcontactchangesfetchrequest.h \
    ../../../src/extensions/qcontactchangessaverequest.h \
    ../../../src/extensions/qcontactclearchangeflagsrequest.h \
    ../../util.h \
    testsyncadaptor.h

SOURCES += \
    ../../../src/engine/contactid.cpp \
    tst_synctransactions.cpp \
    testsyncadaptor.cpp

