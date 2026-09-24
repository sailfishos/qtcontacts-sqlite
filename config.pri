# Try to optimise for code size a bit
QMAKE_CXXFLAGS += -ffunction-sections -fdata-sections -Wl,--gc-sections -flto

CONFIG += \
    c++11 \
    link_pkgconfig
PKGCONFIG += Qt5Contacts

packagesExist(mlite5) {
    DEFINES += USE_MLITE
    PKGCONFIG += mlite5
} else:packagesExist(gsettings-qt) {
    PKGCONFIG += gsettings-qt
    DEFINES += USE_GSETTINGS_QT
}

DEFINES += CONTACTS_DATABASE_PATH=\"\\\"$$[QT_INSTALL_LIBS]/qtcontacts-sqlite-qt5/\\\"\"
