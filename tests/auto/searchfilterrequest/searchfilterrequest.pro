TARGET = tst_searchfilterrequest
include (../../common.pri)

# We need access to the ContactManagerEngine header and moc output
INCLUDEPATH += ../../../src/extensions/
HEADERS += ../../../src/extensions/contactmanagerengine.h \
           ../../../src/extensions/qcontactsearchfilterrequest.h

SOURCES += tst_searchfilterrequest.cpp
