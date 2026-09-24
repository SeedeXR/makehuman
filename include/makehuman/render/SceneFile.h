// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/render/SceneResources.h"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace mh::render {

/// Reads a lighting scene from `data/scenes/<name>.json`.
///
/// **It refuses a Python pickle rather than reading one**, and that refusal is
/// the point of the file existing. The reference stores `.mhscene` as a pickle,
/// so opening a scene someone sent you executes whatever that file names;
/// `memory/project_context.md` lists it as a verified defect and CLAUDE.md
/// rule 3 forbids porting it. The three scenes that shipped with the asset
/// bootstrap really were pickles, and `tools/convert_mhscene.py` converted them
/// once, offline, with an unpickler that can build exactly one class. Nothing
/// in the application unpickles anything.
///
/// A pickle is recognised by its first byte, `0x80` -- the PROTO opcode -- and
/// reported as such rather than as "malformed JSON", because a user handed a
/// scene from upstream MakeHuman deserves to be told what the file actually is.
///
/// **Intensity is DERIVED, not stored.** The reference's format has no notion
/// of one: its lights carry a colour and nothing else. Reading those colours at
/// intensity 1.0 would render every converted scene far darker than the tuned
/// default rig, so a single scale is chosen per scene such that the scene's
/// total light luminance equals the default rig's. That keeps a scene a change
/// of COLOUR and DIRECTION -- which is all the reference ever varied -- rather
/// than a change of exposure. See `SceneFile.cpp` for the arithmetic.
[[nodiscard]] std::expected<Lighting, RenderError> loadLighting(const std::filesystem::path& path);

/// The scenes on disk, by stem, sorted, with the built-in first.
///
/// The built-in `studio` is NOT a file: it is `Lighting`'s own defaults, the
/// three-point rig that used to be constants in `pbr.frag`. It stays the
/// default because the reference's `default.mhscene` is a single white light
/// and adopting it would flatten the viewport.
[[nodiscard]] std::vector<std::string> availableScenes(const std::filesystem::path& sceneDir);

/// The name of the built-in rig, which `Lighting{}` already is.
inline constexpr const char* kBuiltinSceneName = "studio";

}  // namespace mh::render
