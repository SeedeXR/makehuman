# Asserts that the Ericsson-SLA code is NOT in the libktx we built.
#
# `external/etcdec/etcdec.cxx` is field-of-use restricted (LicenseRef-ETCSLA),
# which LICENSING.md 5.2 forbids outright and AGPL-3.0 distribution cannot
# carry. `src/io/CMakeLists.txt` passes KTX_FEATURE_ETC_UNPACK=OFF to keep it
# out -- but that option defaults ON upstream, so a version bump, a stray cache
# entry, or someone "tidying" the flag list would silently put it back.
#
# This checks the BUILT BINARY, not the documentation. A gate that greps the
# file recording the rule passes the moment the rule is written down, which is
# exactly how a forbidden-dependency check here once passed on a forbidden
# library.
#
# It reads SYMBOLS rather than archive members, because the artefact is not
# always an archive: this project builds libktx as a shared library
# (`libktx.<version>.dylib`), and `ar -t` fails outright on a Mach-O dylib --
# measured, that is how the first version of this gate failed. `nm` reads both.
# etcdec.cxx contributes non-static symbols (`setupAlphaTable`,
# `decompressBlockETC2c`, `alphaTableInitialized`), so their absence is the
# evidence and their presence is the alarm.
#
# Inputs: LIBRARY -- path to the built libktx (dylib or .a)
#         NM      -- path to the nm tool

if(NOT DEFINED LIBRARY OR NOT DEFINED NM)
    message(FATAL_ERROR "ktx_excludes_ericsson_sla: LIBRARY and NM are required")
endif()

if(NOT EXISTS "${LIBRARY}")
    message(FATAL_ERROR
        "ktx_excludes_ericsson_sla: nothing at ${LIBRARY}.\n"
        "The gate is registered only when MH_WITH_KTX2=ON, so a missing "
        "library means the build did not produce what it claimed to.")
endif()

execute_process(COMMAND "${NM}" "${LIBRARY}"
                OUTPUT_VARIABLE symbols
                ERROR_VARIABLE  nm_err
                RESULT_VARIABLE nm_rc
                OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT nm_rc EQUAL 0)
    message(FATAL_ERROR "ktx_excludes_ericsson_sla: nm failed (${nm_rc}): ${nm_err}")
endif()

# POSITIVE CONTROL FIRST. Without it the gate would pass just as happily on a
# stripped, empty or wrong file, and a pass that cannot fail means nothing. If
# the BasisLZ encoder is not in here, this is not the artefact we meant to
# check and the result is withheld rather than reported as success.
if(NOT symbols MATCHES "ktxTexture2_CompressBasis")
    message(FATAL_ERROR
        "ktx_excludes_ericsson_sla: no ktxTexture2_CompressBasis symbol in "
        "${LIBRARY}, so this is not a libktx with the encoder in it. Refusing "
        "to report a meaningless pass.")
endif()

# THE ASSERTION ITSELF.
if(symbols MATCHES "setupAlphaTable|decompressBlockETC2|alphaTableInitialized")
    message(FATAL_ERROR
        "ktx_excludes_ericsson_sla: ${LIBRARY} CONTAINS Ericsson ETC decoder "
        "symbols.\n"
        "That code is external/etcdec/etcdec.cxx, the Ericsson Texture "
        "Compression Codec SLA -- field-of-use restricted, forbidden by "
        "LICENSING.md 5.2, and not distributable under AGPL-3.0. Upstream says "
        "so itself: \"lib/etcdec.cxx is not open source\".\n"
        "Cause is almost certainly KTX_FEATURE_ETC_UNPACK drifting back to its "
        "upstream default of ON. See third_party/licenses/README.md.")
endif()

message(STATUS "ktx_excludes_ericsson_sla: encoder present, no Ericsson ETC symbols")
