// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace mh::ui {

/// A saved window layout.
///
/// `state` and `geometry` are `QMainWindow::saveState()` / `saveGeometry()`
/// blobs. They are opaque, versioned by Qt itself, and **not portable across Qt
/// major versions** -- which is exactly why `schemaVersion` exists separately:
/// this file's own shape can change without waiting for Qt to change its blob.
struct WorkspaceFile {
    /// Bumped when the JSON shape changes. A file from the future is refused
    /// rather than half-read, because a partially applied layout is harder to
    /// diagnose than a missing one.
    int schemaVersion{1};
    QString name;
    QByteArray state;
    QByteArray geometry;
};

/// The version this build writes and is willing to read.
inline constexpr int kWorkspaceSchemaVersion = 1;

[[nodiscard]] QJsonObject toJson(const WorkspaceFile& workspace);

/// @return nothing when the object is not a workspace this build understands:
///         a missing, non-numeric or newer `schemaVersion`; a missing name; a
///         `state` or `geometry` value that is present but not a string
///         (`null` included); either blob failing to decode as base64; or an
///         empty `state`, since `saveState()` never produces one and restoring
///         nothing would otherwise report success.
[[nodiscard]] std::optional<WorkspaceFile> workspaceFromJson(const QJsonObject& object);

/// The shipped layouts, in `⌘1`-`⌘4` order (`design.md` §6.4).
///
/// The first shows everything, so a category registered later is reachable by
/// construction rather than by a test noticing afterwards.
struct WorkspacePreset {
    QString name;

    /// The **categories** this preset shows -- not dock object names. Nothing
    /// means every registered category; an empty list means none. The first is
    /// the one the preset is about and gets the wider column.
    std::optional<QStringList> categories;
};

[[nodiscard]] const std::vector<WorkspacePreset>& workspacePresets();

/// Whether @p name may be used as a workspace file name.
///
/// The name goes straight into a path, so without this `../../Desktop/notes`
/// truncates and replaces a file the user never meant to touch -- and the
/// obvious caller is a free-text field in a Save dialog.
[[nodiscard]] bool isValidWorkspaceName(const QString& name);

}  // namespace mh::ui
