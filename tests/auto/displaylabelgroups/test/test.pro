TARGET = tst_displaylabelgroups
include (../../../common.pri)

# We need access to QtContacts private headers
QT += contacts-private
# And we need access to the ContactManagerEngine header and moc output
INCLUDEPATH += ../../../../src/extensions/
HEADERS += ../../../../src/extensions/contactmanagerengine.h

SOURCES += tst_displaylabelgroups.cpp
