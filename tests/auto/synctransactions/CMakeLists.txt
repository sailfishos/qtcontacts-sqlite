add_qtcontacts_sqlite_test(tst_synctransactions
    tst_synctransactions.cpp
    testsyncadaptor.cpp
    testsyncadaptor.h
    "${PROJECT_SOURCE_DIR}/src/extensions/qcontactcollectionchangesfetchrequest.h"
    "${PROJECT_SOURCE_DIR}/src/extensions/qcontactchangesfetchrequest.h"
    "${PROJECT_SOURCE_DIR}/src/extensions/qcontactchangessaverequest.h"
    "${PROJECT_SOURCE_DIR}/src/extensions/qcontactclearchangeflagsrequest.h"
    "${PROJECT_SOURCE_DIR}/tests/util.h"
)

target_link_libraries(tst_synctransactions PRIVATE engine_common)
