include(../config.pri)

QT = \
    core \
    testlib

TEMPLATE = app
CONFIG -= app_bundle

INCLUDEPATH += $$PWD/../src/extensions

target.path = /opt/tests/qtcontacts-sqlite-qt5
INSTALLS += target

check.commands = "$${PWD}/run_test.sh $$shadowed($${PWD})/.. ./$${TARGET}"
check.depends = $${TARGET}
QMAKE_EXTRA_TARGETS += check
