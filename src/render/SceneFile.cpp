// SPDX-License-Identifier: Apache-2.0

#include "makehuman/render/SceneFile.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <algorithm>
#include <array>

namespace mh::render {

namespace {

/// Rec.709 luminance. The particular weights matter less than using ONE set
/// consistently: this is only ever compared against itself, to put two scenes
/// on the same exposure.
float luminance(const std::array<float, 3>& rgb) {
    return 0.2126F * rgb[0] + 0.7152F * rgb[1] + 0.0722F * rgb[2];
}

/// The default rig's total light luminance, summed over its three lights with
/// their intensities.
///
/// DERIVED from `Lighting{}` rather than written down, so that retuning the
/// default rig retunes every scene with it instead of silently changing what a
/// scene's exposure is matched against. Measured today it is 5.29; nothing
/// depends on that number staying put.
float defaultRigLuminance() {
    const Lighting rig{};
    float total = 0.0F;
    for (const Light& light : rig.lights) {
        total += luminance(light.colour) * light.intensity;
    }
    return total;
}

std::expected<std::array<float, 3>, RenderError> readRgb(const QJsonValue& value,
                                                         const char* what) {
    const QJsonArray array = value.toArray();
    if (array.size() != 3) {
        return std::unexpected(
            RenderError{RenderErrorKind::Malformed, std::string(what) + " must be three numbers"});
    }
    std::array<float, 3> out{};
    for (int i = 0; i < 3; ++i) {
        if (!array.at(i).isDouble()) {
            return std::unexpected(RenderError{RenderErrorKind::Malformed,
                                               std::string(what) + " has a non-numeric component"});
        }
        out[static_cast<size_t>(i)] = static_cast<float>(array.at(i).toDouble());
    }
    return out;
}

}  // namespace

std::expected<Lighting, RenderError> loadLighting(const std::filesystem::path& path) {
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(RenderError{RenderErrorKind::FileMissing, path.string()});
    }
    const QByteArray bytes = file.readAll();

    // The refusal this file exists for. 0x80 is a pickle's PROTO opcode, and
    // every `.mhscene` upstream ships begins with it. Saying so beats "illegal
    // value at offset 0", which is what the JSON parser would report.
    if (!bytes.isEmpty() && static_cast<unsigned char>(bytes.at(0)) == 0x80) {
        return std::unexpected(RenderError{
            RenderErrorKind::Malformed,
            path.string() + " is a Python pickle, not JSON; this port never unpickles a scene "
                            "-- convert it with tools/convert_mhscene.py"});
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (document.isNull()) {
        return std::unexpected(
            RenderError{RenderErrorKind::Malformed,
                        path.string() + ": " + parseError.errorString().toStdString()});
    }
    const QJsonObject root = document.object();

    Lighting lighting{};
    auto ambient = readRgb(root.value(QStringLiteral("ambient")), "ambient");
    if (!ambient) return std::unexpected(ambient.error());
    // One ambient colour becomes both halves of the hemisphere. The reference
    // has a single `ambience` and no ground term at all, so inventing a
    // different bounce colour would be inventing content, not converting it.
    lighting.sky    = *ambient;
    lighting.ground = *ambient;

    const QJsonArray lights = root.value(QStringLiteral("lights")).toArray();
    if (lights.isEmpty()) {
        return std::unexpected(
            RenderError{RenderErrorKind::Malformed, path.string() + ": no lights"});
    }
    if (lights.size() > static_cast<int>(lighting.lights.size())) {
        // Refuse rather than silently drop the fourth: a scene that asked for
        // more light than we can give should say so.
        return std::unexpected(RenderError{RenderErrorKind::Malformed,
                                           path.string() + ": " + std::to_string(lights.size()) +
                                               " lights, the rig holds " +
                                               std::to_string(lighting.lights.size())});
    }

    // Read colours and directions first, because the intensity scale depends on
    // the colours of ALL of them.
    std::vector<std::array<float, 3>> directions;
    std::vector<std::array<float, 3>> colours;
    float sceneLuminance = 0.0F;
    for (const QJsonValue& entry : lights) {
        const QJsonObject light = entry.toObject();
        auto direction          = readRgb(light.value(QStringLiteral("direction")), "direction");
        if (!direction) return std::unexpected(direction.error());
        auto colour = readRgb(light.value(QStringLiteral("color")), "color");
        if (!colour) return std::unexpected(colour.error());
        sceneLuminance += luminance(*colour);
        directions.push_back(*direction);
        colours.push_back(*colour);
    }
    if (sceneLuminance <= 0.0F) {
        return std::unexpected(
            RenderError{RenderErrorKind::Malformed, path.string() + ": every light is black"});
    }

    // The exposure match described in the header: one scale for the whole
    // scene, so the RATIO between its lights -- which is what the author chose
    // -- survives untouched.
    const float scale = defaultRigLuminance() / sceneLuminance;

    // Any slot the scene does not fill keeps `Light{}`, whose intensity is 0
    // and which therefore contributes nothing. That is why the rig needs no
    // light count.
    lighting.lights = {};
    for (size_t i = 0; i < directions.size(); ++i) {
        lighting.lights[i].direction = directions[i];
        lighting.lights[i].colour    = colours[i];
        lighting.lights[i].intensity = scale;
    }
    return lighting;
}

std::vector<std::string> availableScenes(const std::filesystem::path& sceneDir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(sceneDir, ec)) {
        if (entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    // The built-in leads, because it is the default and a list whose first
    // entry is not the default reads as though the default were missing.
    names.insert(names.begin(), kBuiltinSceneName);
    return names;
}

}  // namespace mh::render
