// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QWidget>

#include <memory>

class QDockWidget;
class QMenu;

namespace mh::ui {

/// The dock title bar from `design.md` §6.3: title, then the `grip-horizontal`
/// six-dot button, then `x`.
///
/// Replacing QDockWidget's built-in title bar is the only way to add a button to
/// it -- Qt offers no hook for extra title-bar controls, only the float and
/// close buttons it draws itself.
class PanelTitleBar : public QWidget {
    Q_OBJECT

public:
    /// @param dock        the panel this bar belongs to. It becomes this
    ///                    widget's parent, so it always outlives it.
    /// @param defaultArea where "Reset This Panel" puts it back.
    PanelTitleBar(QDockWidget* dock, Qt::DockWidgetArea defaultArea);

    /// A freshly built panel menu, owned by the caller -- it has no QObject
    /// parent, so nothing else will delete it.
    ///
    /// Rebuilt per call rather than cached because "Tab with…" lists the *other*
    /// docks, and that set changes as panels are opened and closed.
    [[nodiscard]] std::unique_ptr<QMenu> buildMenu();

private:
    Qt::DockWidgetArea defaultArea_{};
};

}  // namespace mh::ui
