// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/Theme.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

#include <cmath>

namespace mh::ui::theme {
namespace {

/// Defaults to the shipped directory so a MainWindow built before setIconDir()
/// still gets icons. Without a default it silently produced two invisible
/// title-bar buttons -- a null QIcon renders as nothing at all.
std::filesystem::path g_iconDir{std::filesystem::path(MH_RESOURCE_DIR) / "icons" / "lucide"};

/// sRGB -> linear, the transfer function WCAG 2.1 specifies.
double linearise(int channel) {
    const double c = channel / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor& c) {
    return 0.2126 * linearise(c.red()) + 0.7152 * linearise(c.green()) +
           0.0722 * linearise(c.blue());
}

}  // namespace

const Palette& palette() {
    static const Palette p;
    return p;
}

double contrastRatio(const QColor& a, const QColor& b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double hi = std::max(la, lb);
    const double lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

void setIconDir(std::filesystem::path dir) {
    g_iconDir = std::move(dir);
}

const std::filesystem::path& iconDir() {
    return g_iconDir;
}

QString installFonts(const std::filesystem::path& fontDir) {
    const auto file = fontDir / "42dotSans-VariableFont_wght.ttf";
    const int id    = QFontDatabase::addApplicationFont(QString::fromStdString(file.string()));
    if (id < 0) return {};
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    return families.isEmpty() ? QString{} : families.front();
}

QIcon icon(std::string_view name, const QColor& colour, int px) {
    // A size computed from a not-yet-laid-out layout can arrive as 0; QPainter
    // guards every call but logs ~26 warnings on the way to the same null icon.
    if (px <= 0) return {};

    const auto path = g_iconDir / (std::string(name) + ".svg");
    QFile f(QString::fromStdString(path.string()));
    if (!f.open(QIODevice::ReadOnly)) return {};

    QByteArray svg = f.readAll();
    svg.replace("currentColor", colour.name().toUtf8());

    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) return {};

    // Rasterised at the device pixel ratio. Without this a 16 px title-bar icon
    // is drawn into a 16 px bitmap and stretched to 32 device pixels on a retina
    // display -- the 1.5 px stroke is exactly the detail that loses.
    const qreal dpr = qApp != nullptr ? qApp->devicePixelRatio() : qreal{1};
    QPixmap pm(qRound(px * dpr), qRound(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter);
    painter.end();
    return QIcon(pm);
}

QString styleSheet() {
    const Palette& p = palette();

    // NOT `QWidget { background: ... }`. A universal background rule matches
    // every child of every dock too, so it paints bgBase opaquely over the
    // QDockWidget rule below and the panel-vs-base separation the design rests
    // on never renders -- sampled from a real screenshot, the dock body came out
    // #212124 where design.md 3 says #2a2a2e. Text colour IS set universally,
    // because colour inherits without painting anything.
    return QStringLiteral(R"(
QWidget { color: %2; }

QMainWindow, QDialog {
    background: %1;
}
QMainWindow::separator {
    background: %3;
    width: 1px;
    height: 1px;
}
QDockWidget {
    background: %4;
    color: %2;
}
QDockWidget > QWidget {
    background: %4;
}
#panel\.titlebar {
    border-bottom: 1px solid %3;
}
#panel\.title {
    font-weight: 600;
}
QStatusBar {
    background: %1;
    color: %5;
    border-top: 1px solid %3;
}
QMenu, QToolTip {
    background: %6;
    color: %2;
    border: 1px solid %7;
    padding: 4px;
}
QMenu::item {
    padding: 4px 24px 4px 8px;
}
QMenu::item:selected {
    background: %8;
}
QMenu::item:disabled {
    color: %12;
}
QMenu::separator {
    height: 1px;
    background: %3;
    margin: 4px 2px;
}
QLineEdit, QSpinBox, QDoubleSpinBox {
    background: %9;
    color: %2;
    border: 1px solid %7;
    border-radius: 4px;
    padding: 3px 6px;
    selection-background-color: %10;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border: 2px solid %10;
}
QToolButton {
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 2px;
}
QToolButton:hover  { background: %8; }
QToolButton:pressed { background: %11; }
QPushButton {
    background: %6;
    color: %2;
    border: 1px solid %7;
    border-radius: 4px;
    padding: 4px 12px;
}
QPushButton:hover  { background: %8; }
QPushButton:pressed { background: %11; }
QPushButton:disabled { color: %12; }
QPushButton:default {
    background: %10;
    border-color: %10;
    color: %1;
}
QPushButton:default:hover  { background: %13; border-color: %13; }
QPushButton:default:pressed { background: %14; border-color: %14; }
QSlider::groove:horizontal {
    background: %9;
    height: 4px;
    border-radius: 2px;
}
QSlider::sub-page:horizontal {
    background: %10;
    height: 4px;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background: %2;
    width: 12px;
    margin: -5px 0;
    border-radius: 6px;
}
QScrollBar:vertical, QScrollBar:horizontal {
    background: transparent;
    width: 10px;
    height: 10px;
}
QScrollBar::handle {
    background: %7;
    border-radius: 5px;
    min-height: 24px;
}
QScrollBar::handle:hover { background: %8; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QTabBar::tab {
    background: %1;
    color: %5;
    padding: 5px 12px;
    border: none;
}
QTabBar::tab:selected {
    color: %2;
    border-bottom: 2px solid %10;
}
)")
        .arg(p.bgBase.name(),         // 1
             p.textPrimary.name(),    // 2
             p.borderSubtle.name(),   // 3
             p.bgPanel.name(),        // 4
             p.textSecondary.name(),  // 5
             p.bgElevated.name(),     // 6
             p.borderStrong.name(),   // 7
             p.bgHover.name(),        // 8
             p.bgInput.name())        // 9
        .arg(p.accent.name(),         // 10
             p.bgActive.name(),       // 11
             p.textDisabled.name(),   // 12
             p.accentHover.name(),    // 13
             p.accentPress.name());   // 14
}

}  // namespace mh::ui::theme
