add_qtcontacts_sqlite_test(tst_qcontactmanagerfiltering
    tst_qcontactmanagerfiltering.cpp
    "${PROJECT_SOURCE_DIR}/tests/util.h"
    "${PROJECT_SOURCE_DIR}/tests/qcontactmanagerdataholder.h"
)

target_link_libraries(tst_qcontactmanagerfiltering PRIVATE engine_common)
