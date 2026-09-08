# SPDX-License-Identifier: AGPL-3.0-or-later
add_executable(mh_bench bench_core.cpp)
# mh::core is NOT listed: mh::rig PUBLIC-links it since PosedMesh, so naming it
# here too makes the linker see libmh_core.a twice and warn -- the same trap the
# comment above records for libmh_io.a.
target_link_libraries(mh_bench PRIVATE mh::rig mh::io mh::warnings)
target_compile_definitions(mh_bench PRIVATE MH_DATA_DIR="${MH_DATA_DIR}")
