add_qtcontacts_sqlite_test(tst_memorytable
    tst_memorytable.cpp
    "${PROJECT_SOURCE_DIR}/tests/util.h"
)

target_link_libraries(tst_memorytable PRIVATE engine_common)
