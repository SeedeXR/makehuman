# SPDX-License-Identifier: Apache-2.0
#
# mh_moge is Apache-2.0: it takes a QImage and returns a QImage, never includes
# a core header, and so may be used from either side of the licence boundary.
#
# It is the ONLY target that links ONNX Runtime. That isolation is the reason
# it is a module rather than two more files in mh_ui: a 21 MB inference runtime
# has no business inside the widget library, and `ui::silhouetteFromMask` stays
# testable with no model and no ONNX at all.
#
# Built only when MH_WITH_MOGE is ON, which defaults OFF for the same reason
# MH_WITH_KTX2 does: nobody should pay for a dependency they did not ask for.
add_library(mh_moge STATIC
    Session.cpp
)

target_link_libraries(mh_moge
    PUBLIC  Qt6::Gui mh::foundation
    PRIVATE onnxruntime::onnxruntime mh::warnings mh::sanitize)

target_include_directories(mh_moge PUBLIC ${CMAKE_SOURCE_DIR}/include)

set_target_properties(mh_moge PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)

add_library(mh::moge ALIAS mh_moge)
