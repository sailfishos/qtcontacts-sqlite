include(../config.pri)

QT += testlib
TEMPLATE = app
CONFIG -= app_bundle

INCLUDEPATH += $$PWD/../src/extensions

target.path = /opt/tests/qtcontacts-sqlite-qt5
INSTALLS += target

check.commands = "LC_ALL=C QT_PLUGIN_PATH=$$shadowed($${PWD})/../src/engine/ ./$${TARGET}"
check.depends = $${TARGET}
QMAKE_EXTRA_TARGETS += check
