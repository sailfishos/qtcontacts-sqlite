add_qtcontacts_sqlite_test(tst_aggregation
    tst_aggregation.cpp
    "${PROJECT_SOURCE_DIR}/tests/util.h"
)

target_link_libraries(tst_aggregation PRIVATE engine_common)
