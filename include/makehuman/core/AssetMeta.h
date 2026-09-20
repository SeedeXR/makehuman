// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The `.meta` sidecar that travels beside a shipped asset.
//
// AGPL because it is a port of the reference's own metadata reader
// (`legacy/python/plugins/3_libraries_pose.py:111-135`), and because it is the
// same family as the `.mhclo`/`.mhskel`/`.mhmat` parsers LICENSING.md places in
// `mh-asset`. Rule 4 there is "when in doubt, it is AGPL"; there is no doubt
// here, this IS the reference's parser.
//
// It exists because a chooser built from filenames alone cannot tell a pose a
// user wants from a fixture that exists to break the rig. `data/poses/` ships
// both: `tpose.meta` is `tag Rest poses`, and `benchmark.meta` is
// `tag Developement` with the description "Benchmark pose used to test the
// rigging in extreme condition". Offering the second one as an ordinary choice
// hands the user a deliberately impossible pose -- MEASURED: its hands
// interpenetrate, because a BVH stores rotations and carries no collision.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mh::core {

/// What a `.meta` says about an asset, in the two respects anything reads:
/// what to call it, and what kind of thing it is.
struct AssetMeta {
    /// The `name` field, or EMPTY when the file gives none.
    ///
    /// The reference defaults this to the asset's stem
    /// (`3_libraries_pose.py:114`) and this deliberately does not, because the
    /// caller cannot then tell "the sidecar named it" from "there was no
    /// sidecar" -- and those want different labels here. A named asset uses its
    /// name; an unnamed one goes through `prettyAssetName` like every other
    /// chooser in this port, so `walk1.bvh` reads "Walk1" beside "High-poly"
    /// and "T-pose" rather than a bare stem. Defaulting here made that fallback
    /// unreachable and the Animation listing showed raw stems.
    std::string name;

    /// Lowercased and de-duplicated, as the reference lowercases them into a
    /// set. Order is the file's.
    std::vector<std::string> tags;

    /// Case-insensitive, because the tags are already lowercased on the way in
    /// and a caller should not have to know that.
    [[nodiscard]] bool hasTag(std::string_view tag) const;
};

/// Reads the `.meta` beside @p asset -- same stem, `.meta` extension.
///
/// A missing or unreadable sidecar is NOT an error: most assets ship without
/// one, and the reference treats its absence as "no metadata" rather than as a
/// failure. The result is then the asset's stem as `name` and nothing else, so
/// a caller can use the return value unconditionally.
///
/// Unknown keys are ignored rather than refused, or no file the reference has
/// ever written would load: `.meta` has no schema and authors add fields.
///
/// Only `name` and `tag` are kept. The reference also reads `description`,
/// `license`, `copyright` and `author`, and those are deliberately NOT stored:
/// nothing in this project reads them. The CI licence gate gets its answers
/// from SPDX headers in SOURCE files, not from asset sidecars. They are three
/// lines to add back the day something wants them, and until then they are
/// four fields that only their own tests read.
///
/// The reference falls back to parsing the ASSET file when no `.meta` exists
/// (`3_libraries_pose.py:105-109`). That is deliberately not ported here: for
/// a `.bvh` it reads a megabyte of motion data looking for lines that a BVH
/// cannot contain, and can only ever produce the same defaults this returns.
[[nodiscard]] AssetMeta loadAssetMeta(const std::filesystem::path& asset);

/// The name a `.mhanim` gives @p bvh, or EMPTY when none does.
///
/// A DIFFERENT shape from `.meta`, which is why it is a second function rather
/// than a flag on the first: `.meta` is one sidecar per asset and shares its
/// stem, while one `.mhanim` describes SEVERAL motion files in its directory,
/// one `# anim <Name> <file.bvh> [z_is_up]` line each. There is no path to
/// guess -- the directory has to be read and the lines matched by filename.
///
/// Empty rather than a default, for the reason `AssetMeta::name` is empty:
/// only the caller can tell "the author named it" from "no one did", and the
/// two deserve different labels. Everything unnamed keeps going through
/// `prettyAssetName` like every other chooser.
///
/// MEASURED, and it is why this exists: the shipped files are authored
/// "Walk1", "Dance1" and "zombieWalk1". Title-casing the stem agrees for the
/// first two and gets the third wrong -- the chooser read "ZombieWalk1",
/// capitalising a letter the author deliberately left lower.
///
/// Only the NAME is read here. `.mhanim` also carries author, licence,
/// homepage, uuid, `# tag` lines, `# rig` and `z_is_up`; the axis declaration
/// already has a consumer in `tests/regression/test_animation_upaxis.cpp`,
/// which cross-checks it against `io::readBvh`'s independent detection and
/// should keep its own reader for exactly that independence.
[[nodiscard]] std::string animationName(const std::filesystem::path& bvh);

}  // namespace mh::core
