TARGET = tst_displaylabelgroups
include (../../../common.pri)

# We need access to the ContactManagerEngine header and moc output
INCLUDEPATH += ../../../../src/extensions/
HEADERS += ../../../../src/extensions/contactmanagerengine.h

SOURCES += tst_displaylabelgroups.cpp

# Override the test command to setup the environment
check.commands = "QTCONTACTS_SQLITE_PLUGIN_PATH=../testplugin/contacts_dlgg/ $${check.commands}"
