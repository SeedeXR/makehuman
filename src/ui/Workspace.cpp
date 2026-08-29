// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/Workspace.h"

namespace mh::ui {
namespace {

constexpr auto kKeyVersion  = "schemaVersion";
constexpr auto kKeyName     = "name";
constexpr auto kKeyState    = "state";
constexpr auto kKeyGeometry = "geometry";

}  // namespace

QJsonObject toJson(const WorkspaceFile& workspace) {
    QJsonObject out;
    out[QLatin1String(kKeyVersion)] = workspace.schemaVersion;
    out[QLatin1String(kKeyName)]    = workspace.name;
    // Base64 because the blobs are binary and JSON strings are text. Qt's own
    // toBase64 defaults are fine; the decode below rejects anything else.
    out[QLatin1String(kKeyState)]    = QString::fromLatin1(workspace.state.toBase64());
    out[QLatin1String(kKeyGeometry)] = QString::fromLatin1(workspace.geometry.toBase64());
    return out;
}

std::optional<WorkspaceFile> workspaceFromJson(const QJsonObject& object) {
    const QJsonValue version = object.value(QLatin1String(kKeyVersion));
    if (!version.isDouble()) return std::nullopt;

    WorkspaceFile out;
    out.schemaVersion = version.toInt();
    // A file from a newer build is refused, not partially read: applying half a
    // layout is harder to diagnose than applying none.
    if (out.schemaVersion <= 0 || out.schemaVersion > kWorkspaceSchemaVersion) return std::nullopt;

    const QJsonValue name = object.value(QLatin1String(kKeyName));
    if (!name.isString() || name.toString().isEmpty()) return std::nullopt;
    out.name = name.toString();

    // AbortOnBase64DecodingErrors: a corrupt blob must not be handed to
    // restoreState, which would silently ignore it and leave the default layout
    // looking like a successful load.
    const auto decode = [&](const char* key, QByteArray& into) {
        const QJsonValue v = object.value(QLatin1String(key));
        if (!v.isString()) return v.isUndefined();  // absent is allowed; wrong type is not
        const auto decoded = QByteArray::fromBase64Encoding(
            v.toString().toLatin1(),
            QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) return false;
        into = *decoded;
        return true;
    };
    if (!decode(kKeyState, out.state)) return std::nullopt;
    if (!decode(kKeyGeometry, out.geometry)) return std::nullopt;
    // QMainWindow::saveState never returns an empty blob, so an empty one here
    // means a truncated or hand-edited file. Accepting it would restore nothing
    // and report success -- exactly what the base64 check above exists to stop.
    if (out.state.isEmpty()) return std::nullopt;
    return out;
}

bool isValidWorkspaceName(const QString& name) {
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) return false;
    // Anything that could change the directory the file lands in. `..` alone is
    // not enough: `a/../../b` contains no bare `..` component before splitting.
    return !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\')) &&
           !name.contains(QLatin1Char(':')) && !name.contains(QLatin1Char('\0'));
}

const std::vector<WorkspacePreset>& workspacePresets() {
    // design.md 6.4. Rigging and Export are defined here but have nothing of
    // their own to show yet -- the panels do not exist -- so they differ from
    // Modelling only in what they hide. They are shipped now so the switcher,
    // the shortcuts and the file format are exercised by four real entries
    // rather than one.
    static const std::vector<WorkspacePreset> presets{
        {QStringLiteral("Modelling"),
         {QStringLiteral("dock.modelling"), QStringLiteral("dock.materials")}},
        {QStringLiteral("Rigging"), {QStringLiteral("dock.modelling")}},
        {QStringLiteral("Materials"), {QStringLiteral("dock.materials")}},
        {QStringLiteral("Export"), {}},
    };
    return presets;
}

}  // namespace mh::ui
