// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/render/SceneResources.h"

#include <QDialog>

#include <memory>

namespace mh::ui {

/// What a production render should be, as chosen by the user.
///
/// Deliberately not `render::RenderSettings`: that one also carries the camera,
/// the background colour and the litsphere path, none of which this dialog
/// asks about — the app fills those in from what the viewport is already
/// showing, so an on-screen framing and a rendered image cannot disagree.
struct RenderRequest {
    int width{1024};
    int height{1024};
    /// Clear to alpha 0 rather than opaque, for compositing.
    bool transparent{false};
    render::ShadingModel shading{render::ShadingModel::Litsphere};
};

/// Asks for the four things `--render` accepts.
///
/// This exists because the renderer had NO user interface at all:
/// `OffscreenRenderer` has been in the tree since M6 and `--render` has worked
/// for as long, and nothing in the window reached either. The reference's
/// equivalent is `OpenGLTaskView`, whose label is literally "Render"
/// (`legacy/python/plugins/4_rendering_opengl/__init__.py:53`) and which holds
/// exactly this: a resolution box, an anti-aliasing toggle and a button.
///
/// We have no AA toggle because MSAA is not optional here — `render::kSampleCount`
/// is requested for every target in the application and the backend may clamp
/// it, so a checkbox would be a control that sometimes silently does nothing.
/// The shading model takes its place, which is a real choice the reference
/// could not offer because it has no PBR path.
class RenderDialog : public QDialog {
    Q_OBJECT

public:
    explicit RenderDialog(const RenderRequest& initial, QWidget* parent = nullptr);
    ~RenderDialog() override;

    RenderDialog(const RenderDialog&)            = delete;
    RenderDialog& operator=(const RenderDialog&) = delete;

    /// What the controls currently say. Valid whether or not the dialog was
    /// accepted; the caller checks `exec()` for that.
    [[nodiscard]] RenderRequest request() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mh::ui
