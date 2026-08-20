add_qtcontacts_sqlite_test(tst_qcontactmanager
    tst_qcontactmanager.cpp
    "${PROJECT_SOURCE_DIR}/tests/util.h"
    "${PROJECT_SOURCE_DIR}/tests/qcontactmanagerdataholder.h"
)

target_link_libraries(tst_qcontactmanager PRIVATE engine_common Qt::Versit)
