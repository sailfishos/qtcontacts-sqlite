add_qtcontacts_sqlite_test(tst_detailfetchrequest
    tst_detailfetchrequest.cpp
    "${PROJECT_SOURCE_DIR}/src/extensions/qcontactdetailfetchrequest.h"
)

target_link_libraries(tst_detailfetchrequest PRIVATE engine_common)
