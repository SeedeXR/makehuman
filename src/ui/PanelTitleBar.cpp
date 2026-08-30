// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/PanelTitleBar.h"

#include "makehuman/ui/Theme.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QToolButton>

namespace mh::ui {
namespace {

/// Object names are the handle tests and stylesheets reach these by; without
/// them a menu entry can only be found by its visible text, which is the one
/// thing that is meant to change.
QAction* addMenuAction(QMenu* menu, const QString& objectName, const QString& text,
                       const char* iconName) {
    QAction* a = menu->addAction(text);
    a->setObjectName(objectName);
    a->setIcon(theme::icon(iconName, theme::palette().textSecondary, 16));
    return a;
}

QToolButton* makeButton(const char* iconName, const QString& objectName, const QString& tip,
                        QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setObjectName(objectName);
    b->setIcon(theme::icon(iconName, theme::palette().textSecondary, 16));
    b->setAutoRaise(true);
    b->setToolTip(tip);
    // An icon-only button has no text for a screen reader to fall back on.
    b->setAccessibleName(tip);
    b->setFocusPolicy(Qt::TabFocus);  // reachable by keyboard; design.md §9
    return b;
}

}  // namespace

PanelTitleBar::PanelTitleBar(QDockWidget* dock, Qt::DockWidgetArea defaultArea)
    : QWidget(dock), defaultArea_(defaultArea) {
    setObjectName(QStringLiteral("panel.titlebar"));
    // Qt does NOT honour a stylesheet `background` on a plain QWidget subclass
    // without this -- the rule matches, parses, and paints nothing, so the bar
    // silently showed the window's base colour instead of the panel colour.
    setAttribute(Qt::WA_StyledBackground, true);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 4, 4, 4);
    row->setSpacing(2);

    auto* title = new QLabel(dock->windowTitle(), this);
    title->setObjectName(QStringLiteral("panel.title"));
    // The dock's title is the source of truth; mirroring it keeps a renamed
    // panel from showing a stale label.
    connect(dock, &QDockWidget::windowTitleChanged, title, &QLabel::setText);
    row->addWidget(title);
    row->addStretch(1);

    auto* grip =
        makeButton("grip-horizontal", QStringLiteral("panel.menu"), tr("Panel options"), this);
    connect(grip, &QToolButton::clicked, this, [this, grip] {
        // Released to the popup itself: WA_DeleteOnClose makes the menu delete
        // itself on either dismissal path, which is the standard Qt idiom for a
        // parentless popup.
        QMenu* menu = buildMenu().release();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(grip->mapToGlobal(QPoint(0, grip->height())));
    });
    row->addWidget(grip);

    auto* close = makeButton("x", QStringLiteral("panel.close"), tr("Close panel"), this);
    connect(close, &QToolButton::clicked, dock, &QDockWidget::close);
    row->addWidget(close);
}

std::unique_ptr<QMenu> PanelTitleBar::buildMenu() {
    // No QObject parent: the caller owns it. Giving it one and also handing back
    // a pointer means two owners, and which one wins is declaration order.
    auto menu = std::make_unique<QMenu>();

    auto* dock = qobject_cast<QDockWidget*>(parentWidget());
    if (dock == nullptr) return menu;
    auto* window = qobject_cast<QMainWindow*>(dock->parentWidget());

    QAction* floatA =
        addMenuAction(menu.get(), QStringLiteral("panel.float"), tr("Float"), "maximize-2");
    floatA->setCheckable(true);
    floatA->setChecked(dock->isFloating());
    connect(floatA, &QAction::triggered, dock, [dock](bool on) { dock->setFloating(on); });

    struct Side {
        const char* name;
        const char* label;
        const char* iconName;
        Qt::DockWidgetArea area;
    };

    // QT_TRANSLATE_NOOP so lupdate can extract these: tr() on a runtime
    // `const char*` compiles but is invisible to the string scanner.
    static constexpr Side kSides[] = {
        {"panel.left", QT_TRANSLATE_NOOP("mh::ui::PanelTitleBar", "Dock Left"), "panel-left",
         Qt::LeftDockWidgetArea},
        {"panel.right", QT_TRANSLATE_NOOP("mh::ui::PanelTitleBar", "Dock Right"), "panel-right",
         Qt::RightDockWidgetArea},
        {"panel.top", QT_TRANSLATE_NOOP("mh::ui::PanelTitleBar", "Dock Top"), "chevron-up",
         Qt::TopDockWidgetArea},
        {"panel.bottom", QT_TRANSLATE_NOOP("mh::ui::PanelTitleBar", "Dock Bottom"), "chevron-down",
         Qt::BottomDockWidgetArea},
    };
    // Only the areas this dock is allowed into: offering "Dock Top" on a panel
    // that refuses it gives a menu entry that silently does nothing.
    for (const Side& s : kSides) {
        if ((dock->allowedAreas() & s.area) == 0) continue;
        QAction* a =
            addMenuAction(menu.get(), QString::fromLatin1(s.name), tr(s.label), s.iconName);
        a->setCheckable(true);
        a->setChecked(!dock->isFloating() && window != nullptr &&
                      window->dockWidgetArea(dock) == s.area);
        connect(a, &QAction::triggered, dock, [dock, window, area = s.area] {
            if (window == nullptr) return;
            dock->setFloating(false);
            window->addDockWidget(area, dock);
        });
    }

    menu->addSeparator();

    QMenu* tabWith = menu->addMenu(tr("Tab with…"));
    tabWith->menuAction()->setObjectName(QStringLiteral("panel.tabwith"));
    if (window != nullptr) {
        for (QDockWidget* other : window->findChildren<QDockWidget*>()) {
            if (other == dock || other->isHidden()) continue;
            QAction* a = tabWith->addAction(other->windowTitle());
            a->setObjectName(QStringLiteral("panel.tabwith.") + other->objectName());
            // Context is `other`, not `dock`: the entry is meaningless once its
            // target is gone, and Qt disconnects on the context's destruction.
            // With `dock` as context the action outlived `other` and handed a
            // freed pointer to tabifyDockWidget.
            connect(a, &QAction::triggered, other,
                    [window, other, dock] { window->tabifyDockWidget(other, dock); });
        }
    }
    // An empty submenu is a dead end the user has to open to discover.
    tabWith->menuAction()->setEnabled(!tabWith->isEmpty());

    menu->addSeparator();

    QAction* reset = addMenuAction(menu.get(), QStringLiteral("panel.reset"),
                                   tr("Reset This Panel"), "rotate-ccw");
    connect(reset, &QAction::triggered, dock, [dock, window, area = defaultArea_] {
        if (window == nullptr) return;
        dock->setFloating(false);
        window->addDockWidget(area, dock);
        dock->show();
    });

    QAction* close =
        addMenuAction(menu.get(), QStringLiteral("panel.closeAction"), tr("Close"), "x");
    connect(close, &QAction::triggered, dock, &QDockWidget::close);

    return menu;
}

}  // namespace mh::ui
