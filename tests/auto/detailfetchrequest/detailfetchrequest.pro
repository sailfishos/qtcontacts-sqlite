TARGET = tst_detailfetchrequest
include (../../common.pri)

# We need access to the ContactManagerEngine header and moc output
INCLUDEPATH += ../../../src/extensions/
HEADERS += ../../../src/extensions/contactmanagerengine.h \
           ../../../src/extensions/qcontactdetailfetchrequest.h

SOURCES += tst_detailfetchrequest.cpp
