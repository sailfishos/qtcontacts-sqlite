CONFIG += \
    c++11 \
    link_pkgconfig
PKGCONFIG += Qt5Contacts

packagesExist(mlite5) {
    DEFINES += HAS_MLITE
}
