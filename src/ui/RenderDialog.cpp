// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/RenderDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

namespace mh::ui {
namespace {

/// 64 to 8192. The floor keeps a stray keystroke from producing a 1x1 image
/// that still "succeeds"; the ceiling is not arbitrary either -- a render
/// allocates width*height*4 bytes for the readback plus an MSAA colour buffer,
/// so 8192 square is already ~1 GB of transient GPU memory.
constexpr int kMinPixels = 64;
constexpr int kMaxPixels = 8192;

}  // namespace

struct RenderDialog::Impl {
    QSpinBox* width{};
    QSpinBox* height{};
    QCheckBox* transparent{};
    QComboBox* shading{};
};

RenderDialog::RenderDialog(const RenderRequest& initial, QWidget* parent)
    : QDialog(parent), d_(std::make_unique<Impl>()) {
    setObjectName(QStringLiteral("dialog.render"));
    setWindowTitle(tr("Render"));

    auto* form = new QFormLayout;

    d_->width = new QSpinBox(this);
    d_->width->setObjectName(QStringLiteral("render.width"));
    d_->width->setRange(kMinPixels, kMaxPixels);
    d_->width->setValue(initial.width);
    form->addRow(tr("Width"), d_->width);

    d_->height = new QSpinBox(this);
    d_->height->setObjectName(QStringLiteral("render.height"));
    d_->height->setRange(kMinPixels, kMaxPixels);
    d_->height->setValue(initial.height);
    form->addRow(tr("Height"), d_->height);

    d_->transparent = new QCheckBox(tr("Transparent background"), this);
    d_->transparent->setObjectName(QStringLiteral("render.transparent"));
    d_->transparent->setChecked(initial.transparent);
    form->addRow(QString{}, d_->transparent);

    // Order matches the enum, so index and value map without a lookup table.
    d_->shading = new QComboBox(this);
    d_->shading->setObjectName(QStringLiteral("render.shading"));
    d_->shading->addItem(tr("Litsphere (matcap)"));
    d_->shading->addItem(tr("PBR (metallic-roughness)"));
    d_->shading->setCurrentIndex(static_cast<int>(initial.shading));
    form->addRow(tr("Shading"), d_->shading);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("render.buttons"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* column = new QVBoxLayout(this);
    column->addLayout(form);
    column->addWidget(buttons);
}

RenderDialog::~RenderDialog() = default;

RenderRequest RenderDialog::request() const {
    return RenderRequest{
        .width       = d_->width->value(),
        .height      = d_->height->value(),
        .transparent = d_->transparent->isChecked(),
        // The combo's order is the enum's order; see the constructor.
        .shading = static_cast<render::ShadingModel>(d_->shading->currentIndex()),
    };
}

}  // namespace mh::ui
