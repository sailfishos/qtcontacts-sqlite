TEMPLATE = subdirs
SUBDIRS = \
    auto \
    benchmarks

tests_xml.path = /opt/tests/qtcontacts-sqlite-qt5/
tests_xml.files = tests.xml
INSTALLS += tests_xml
