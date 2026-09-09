// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mh::foundation {

/// The CONTENT-FORMAT version, which is NOT the product version.
///
/// Owner directive 12.4: "Two version numbers in every export: application
/// version and content-format version. You need the second one the moment
/// correctives ship."
///
/// The product version answers "which build wrote this". This answers "what do
/// the bytes MEAN" -- it changes when the meaning of exported content changes
/// (a delta encoding, a new channel, a renamed convention) and NOT when the
/// application is merely rebuilt. A consumer that understands version N can
/// read every file stamped N whatever build produced it.
///
/// Bump it deliberately, in the commit that changes the meaning, and say so.
inline constexpr uint32_t kContentFormatVersion = 1;

/// What every writer stamps into what it writes.
///
/// One struct and one `stamp()` so the four formats cannot drift into four
/// spellings of the same three facts -- which is what "MakeHuman C++ glTF
/// writer", "MakeHuman C++ FBX writer", "MakeHuman C++ USD writer" and
/// "Wavefront OBJ written by MakeHuman" already were.
struct Provenance {
    /// The product version, `MAJOR.MINOR.PATCH`. Empty means "not supplied",
    /// and then nothing about versions is written -- a writer used from a test
    /// should not have to invent one.
    std::string_view application;

    uint32_t contentFormat{kContentFormatVersion};

    /// `core::topologyHash` of the BASE mesh the content is indexed against --
    /// not of the mesh being written, which may be a reduced LOD. 0 omits it.
    uint64_t topologyHash{};

    /// The canonical suffix, e.g.
    /// ` 2.0.0 (content-format 1, topology e38c060123b5d0db)`.
    ///
    /// Leading space and no trailing one, so a caller appends it to its own
    /// name: the per-format prefixes stay what they were and every existing
    /// consumer that greps for "MakeHuman" still finds it.
    [[nodiscard]] std::string stamp() const;
};

}  // namespace mh::foundation
