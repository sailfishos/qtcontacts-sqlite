TEMPLATE = subdirs
SUBDIRS = \
        src \
        tests
OTHER_FILES += rpm/qtcontacts-sqlite-qt5.spec

packagesExist(gsettings-qt) {
    schemas.path = $${PREFIX}/share/glib-2.0/schemas
    schemas.files = schemas/org.nemomobile.contacts.gschema.xml
    
    INSTALLS += schemas
}

tests.depends = src
