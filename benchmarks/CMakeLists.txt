# SPDX-License-Identifier: AGPL-3.0-or-later
add_executable(mh_bench bench_core.cpp)
target_link_libraries(mh_bench PRIVATE mh::core mh::warnings)
target_compile_definitions(mh_bench PRIVATE MH_DATA_DIR="${MH_DATA_DIR}")
