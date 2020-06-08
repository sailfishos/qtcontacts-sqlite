CONFIG += \
    c++11 \
    link_pkgconfig
PKGCONFIG += Qt5Contacts

packagesExist(mlite5) {
    DEFINES += HAS_MLITE
    PKGCONFIG += mlite5
}

DEFINES += CONTACTS_DATABASE_PATH=\"\\\"$$[QT_INSTALL_LIBS]/qtcontacts-sqlite-qt5/\\\"\"
