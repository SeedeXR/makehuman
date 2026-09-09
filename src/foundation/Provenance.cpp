// SPDX-License-Identifier: Apache-2.0
#include "makehuman/foundation/Provenance.h"

namespace mh::foundation {
namespace {

/// Lower-case hex, fixed 16 digits. Not `std::format`/`ostringstream`: this
/// string goes into files, and a locale-sensitive or width-varying spelling
/// would make the same topology read as two different ones.
std::string hex64(uint64_t v) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<size_t>(i)] = kDigits[v & 0xFULL];
        v >>= 4;
    }
    return out;
}

}  // namespace

std::string Provenance::stamp() const {
    if (application.empty()) return {};
    std::string out;
    out += ' ';
    out += application;
    out += " (content-format ";
    out += std::to_string(contentFormat);
    if (topologyHash != 0) {
        out += ", topology ";
        out += hex64(topologyHash);
    }
    out += ')';
    return out;
}

}  // namespace mh::foundation
