// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace mh::core {

class Human;

/// A parsed `.mhm` saved model.
///
/// Line-oriented UTF-8, `#` comments, whitespace-split
/// (legacy/python/apps/human.py:1459-1643). The keys below are the ones the
/// core writes; plugin-contributed lines (skeleton, pose, proxies, materials)
/// are preserved verbatim in @ref unhandled so a round trip does not lose them,
/// and so a later milestone can consume them without a format change.
struct MhmFile {
    /// The `# Written by MakeHuman <x>` header, kept so a file that is loaded
    /// and saved again comes back byte for byte. Other comments are dropped --
    /// the reference does not preserve them either.
    std::string writtenBy;

    std::string version;  ///< e.g. "v1.3.0" -- major.minor only, no patch
    std::string uuid;
    std::string name;

    /// Lower-cased, truncated to 25 characters and de-duplicated on load, which
    /// is what `addTag` does (`human.py:131`) -- the loader routes every tag
    /// through it. The writer sorts them (`human.py:1605`).
    std::vector<std::string> tags;

    /// rotX rotY transX transY transZ zoom.
    ///
    /// **double, not float.** The reference writes Python floats with `'%s'`,
    /// which is up to 17 significant digits; storing them as float turns
    /// `-13.399999999999999` into `-13.4` and breaks a byte round trip on any
    /// file a real session saved.
    std::array<double, 6> camera{};
    bool hasCamera{false};

    /// Modifier full name -> value, in file order.
    std::vector<std::pair<std::string, float>> modifiers;

    bool subdivide{false};

    /// Lines this parser does not interpret, kept verbatim.
    std::vector<std::string> unhandled;

    /// Compares only the major and minor version, as the reference does with
    /// the regex `v(\d)\.(\d)` (human.py:1461-1466).
    [[nodiscard]] bool versionMatches(std::string_view other) const;
};

enum class MhmErrorKind { NotFound, Unreadable, Malformed };

struct MhmError {
    MhmErrorKind kind{};
    std::string file;
    uint32_t line{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

[[nodiscard]] std::expected<MhmFile, MhmError> loadMhm(const std::filesystem::path& path);

/// Applies every modifier line to @p human, in file order.
///
/// @param unknown optionally receives the count of modifier names the human
///        does not have. The reference warns and continues unless `strict`
///        (human.py:1543-1547); the same tolerance is kept, since a model saved
///        with community plugins names modifiers this build has never heard of.
/// @return the number of modifiers actually applied.
uint32_t applyMhm(const MhmFile& mhm, Human& human, uint32_t* unknown = nullptr);

/// Writes @p mhm in the reference's own format (`human.py:1590-1613`).
///
/// Field order, the `%f` six-decimal modifier values, `tags` sorted and
/// `;`-joined, and the omission of empty `uuid`/`name`/`tags` lines all follow
/// the reference, because a `.mhm` this writes must load in MakeHuman 1.x.
/// Lines it did not interpret are written back verbatim, after the modifiers,
/// which is where the reference's plugin handlers put them.
[[nodiscard]] std::expected<void, MhmError> saveMhm(const std::filesystem::path& path,
                                                    const MhmFile& mhm);

/// Snapshots @p human into a savable file.
///
/// Only modifiers that are non-zero **or macro** are written, exactly as the
/// reference decides (`human.py:1612`): a macro at its default still matters,
/// because the default is 0.5 rather than 0 and omitting it would load as 0.
[[nodiscard]] MhmFile mhmFromHuman(const Human& human, std::string name);

/// Distance matching the reference's default `zoomFactor` of 1.0.
///
/// The format stores a magnification (`lib/camera.py:454-457`: 0.25..15,
/// default 1.0); this renderer orbits at a distance. The two relate by
/// `zoom = kDefaultOrbitDistance / distance` -- a convention anchored on the
/// defaults, not a measurement: the cameras do not share a projection, so a
/// file written here opens in MakeHuman 1.x at a sensible zoom, not the
/// identical framing.
inline constexpr float kDefaultOrbitDistance = 45.0F;

/// The orbit view a `.mhm` camera line describes.
struct OrbitView {
    float pitchDegrees{0.0F};
    float yawDegrees{0.0F};
    float distance{kDefaultOrbitDistance};
};

/// Packs an orbit view into the six numbers the format stores.
///
/// Translation is written as zeros: this renderer has no camera pan, and
/// inventing values would be worse than recording that there is none.
///
/// A non-finite or non-positive distance falls back to the default rather than
/// writing `inf`, which neither this loader nor MakeHuman 1.x can read back.
[[nodiscard]] std::array<double, 6> mhmCameraFrom(const OrbitView& view);

/// The inverse.
///
/// The format holds doubles; these fields are float. A value that is finite in
/// a `.mhm` but out of float range -- `1e300` is a legal camera rotation as far
/// as the parser is concerned -- would otherwise narrow to `inf`, get written
/// back as the unparseable `inf.0`, and break the file for every reader. Values
/// that do not survive the narrowing are replaced by the defaults.

[[nodiscard]] OrbitView orbitFromMhmCamera(const std::array<double, 6>& camera);

}  // namespace mh::core
