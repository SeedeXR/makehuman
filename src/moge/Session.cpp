// SPDX-License-Identifier: Apache-2.0
#include "makehuman/moge/Session.h"

#include "makehuman/foundation/FocalShift.h"

#include <onnxruntime_cxx_api.h>

#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <span>
#include <vector>

namespace mh::moge {

namespace {

/// The ViT patch size. Both input dimensions must be a multiple of it, so the
/// resize below rounds to it rather than to anything prettier.
constexpr int kPatch = 14;

/// The longest side we feed. 518 is 37 patches, the resolution MoGe's own
/// examples use, and large enough that a mask traced from it is still accurate
/// after scaling back to a 4000-pixel photograph.
constexpr int kLongSide = 518;

/// Above this the model's sigmoid is taken to mean "subject".
///
/// 0.5 and not something tuned: the output is a calibrated confidence, and a
/// threshold picked to flatter one test image is how a segmentation starts
/// lying on the next one.
constexpr float kMaskThreshold = 0.5F;

int roundToPatch(int v) {
    return std::max(kPatch, (v / kPatch) * kPatch);
}

}  // namespace

struct Session::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "mh_moge"};
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> session;
};

Session::Session() : impl_(std::make_unique<Impl>()) {}

Session::~Session()                             = default;
Session::Session(Session&&) noexcept            = default;
Session& Session::operator=(Session&&) noexcept = default;

std::expected<Session, std::string> Session::open(const std::filesystem::path& model) {
    std::error_code ec;
    if (!std::filesystem::exists(model, ec)) {
        return std::unexpected("no MoGe model at " + model.string() +
                               " -- run tools/fetch_moge.sh");
    }
    Session s;
    try {
        // Single-threaded on purpose. The fit calls this once per reference,
        // not per probe, so throughput is not the constraint; a model quietly
        // spawning a thread pool inside a test harness is a nuisance.
        s.impl_->options.SetIntraOpNumThreads(1);
        s.impl_->session =
            std::make_unique<Ort::Session>(s.impl_->env, model.c_str(), s.impl_->options);
    } catch (const Ort::Exception& e) {
        return std::unexpected(std::string("cannot load MoGe model: ") + e.what());
    }
    return s;
}

std::expected<Prediction, std::string> Session::run(const QImage& image, int tokens) {
    if (image.isNull()) return std::unexpected("null image");
    if (tokens < 1200 || tokens > 2500) {
        return std::unexpected("tokens must be 1200..2500, got " + std::to_string(tokens));
    }

    // Fit inside kLongSide, preserving aspect, then round each side to a whole
    // number of patches. Squashing to a square instead would hand the model a
    // distorted body and get back a distorted mask.
    const double fit = static_cast<double>(kLongSide) / std::max(image.width(), image.height());
    const int width  = roundToPatch(static_cast<int>(std::lround(image.width() * fit)));
    const int height = roundToPatch(static_cast<int>(std::lround(image.height() * fit)));

    const QImage scaled =
        image.convertToFormat(QImage::Format_RGB888)
            .scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // PLANAR CHW, AND RAW [0,1] -- NOT ImageNet-normalised.
    //
    // MEASURED, because the documentation does not say and a wrong guess here
    // produces confidently wrong output rather than an error. Both were run on
    // the same render and looked at: raw gives a cleanly segmented body with a
    // mask over 8.3% of the frame; ImageNet mean/std gives a smeared blob with
    // the background included and a mask calling 100% of the frame valid.
    const auto w       = static_cast<size_t>(width);
    const auto h       = static_cast<size_t>(height);
    const size_t plane = w * h;
    std::vector<float> input(3 * plane);
    for (size_t y = 0; y < h; ++y) {
        const uchar* row = scaled.constScanLine(static_cast<int>(y));
        for (size_t x = 0; x < w; ++x) {
            const size_t at       = y * w + x;
            input[at]             = row[x * 3 + 0] / 255.0F;
            input[plane + at]     = row[x * 3 + 1] / 255.0F;
            input[2 * plane + at] = row[x * 3 + 2] / 255.0F;
        }
    }

    try {
        auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 4> shape{1, 3, height, width};
        auto imageTensor   = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(),
                                                             shape.data(), shape.size());
        int64_t tokenCount = tokens;
        auto tokenTensor   = Ort::Value::CreateTensor<int64_t>(mem, &tokenCount, 1, nullptr, 0);

        const std::array<const char*, 2> inputNames{"image", "num_tokens"};
        // `points` and `normal` are requested but unused: see the header -- they
        // are only meaningful after a focal/shift recovery this does not do.
        const std::array<const char*, 4> outputNames{"points", "normal", "mask", "scale"};
        std::array<Ort::Value, 2> inputs{std::move(imageTensor), std::move(tokenTensor)};

        auto out = impl_->session->Run(Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(),
                                       inputs.size(), outputNames.data(), outputNames.size());

        const auto maskInfo = out[2].GetTensorTypeAndShapeInfo();
        const auto maskDims = maskInfo.GetShape();
        if (maskDims.size() != 3) {
            return std::unexpected("unexpected mask rank from the model");
        }
        const int mh            = static_cast<int>(maskDims[1]);
        const int mw            = static_cast<int>(maskDims[2]);
        const float* confidence = out[2].GetTensorData<float>();

        QImage mask(mw, mh, QImage::Format_Grayscale8);
        int64_t subject = 0;
        for (int y = 0; y < mh; ++y) {
            uchar* row = mask.scanLine(y);
            for (int x = 0; x < mw; ++x) {
                const size_t at =
                    static_cast<size_t>(y) * static_cast<size_t>(mw) + static_cast<size_t>(x);
                const bool on               = confidence[at] > kMaskThreshold;
                row[static_cast<size_t>(x)] = on ? 255 : 0;
                subject += on ? 1 : 0;
            }
        }

        // THE AFFINE AMBIGUITY, RESOLVED. `points` is only correct up to an
        // unknown focal and an unknown additive shift, so its z channel is not
        // depth -- it is a shape. The recovery lives in mh::foundation rather
        // than here precisely because it needs no model: it is tested against
        // cameras we chose, in CI, with MH_WITH_MOGE off.
        const auto pointsInfo = out[0].GetTensorTypeAndShapeInfo();
        const auto pointsDims = pointsInfo.GetShape();
        Prediction p;
        if (pointsDims.size() == 4 && pointsDims[3] == 3) {
            const int ph = static_cast<int>(pointsDims[1]);
            const int pw = static_cast<int>(pointsDims[2]);
            const std::span<const float> pts{out[0].GetTensorData<float>(),
                                             pointsInfo.GetElementCount()};

            // The MASK is passed in, so the fit follows the geometry the model
            // is confident about rather than whatever it guessed for the sky.
            std::vector<uint8_t> solveMask(static_cast<size_t>(pw) * static_cast<size_t>(ph));
            for (size_t i = 0; i < solveMask.size(); ++i) {
                solveMask[i] = confidence[i] > kMaskThreshold ? 1 : 0;
            }

            const auto found = foundation::recoverFocalShift(pts, pw, ph, solveMask);
            if (found.recovered) {
                p.metric      = true;
                p.focal       = found.focal;
                p.shift       = found.shift;
                p.fovDegrees  = foundation::verticalFovDegrees(found.focal, pw, ph);
                p.depthWidth  = pw;
                p.depthHeight = ph;
                p.depth.assign(static_cast<size_t>(pw) * static_cast<size_t>(ph), 0.0F);
                const float metricScale = out[3].GetTensorData<float>()[0];
                for (size_t i = 0; i < p.depth.size(); ++i) {
                    if (solveMask[i] == 0) continue;
                    // z + shift is the true relative depth; the graph's own
                    // scale output is what turns it into metres.
                    p.depth[i] = (pts[i * 3 + 2] + found.shift) * metricScale;
                }
            }
        }

        // Back to the CALLER's resolution, because the silhouette it feeds is
        // compared against images at that size. Nearest, not smooth: a smoothed
        // binary mask grows a grey fringe that the 128 threshold downstream
        // then has to guess about.
        p.mask     = mask.scaled(image.width(), image.height(), Qt::IgnoreAspectRatio,
                                 Qt::FastTransformation);
        p.coverage = static_cast<double>(subject) / (static_cast<double>(mw) * mh);
        p.scale    = out[3].GetTensorData<float>()[0];
        return p;
    } catch (const Ort::Exception& e) {
        return std::unexpected(std::string("MoGe inference failed: ") + e.what());
    }
}

std::filesystem::path defaultModelPath() {
    // The environment wins, so a test or a packager can point at its own copy
    // without touching the user's cache.
    if (const char* fromEnv = std::getenv("MH_MOGE_MODEL"); fromEnv != nullptr && *fromEnv != 0) {
        return std::filesystem::path(fromEnv);
    }
    // GENERIC cache, plus our own folder -- NOT QStandardPaths::CacheLocation.
    //
    // CacheLocation appends the Qt APPLICATION NAME, so the app and the test
    // binary resolve to different directories and `tools/fetch_moge.sh` can
    // only ever write to one of them. That is not hypothetical: the first run
    // of the model-backed test SKIPPED, silently, because the file it needed
    // was 20 characters away under a different name. A skip that looks like a
    // pass is the failure this project keeps relearning.
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (cache.isEmpty()) return {};
    return std::filesystem::path(cache.toStdString()) / "MakeHuman" / "models" /
           "moge-2-vits-normal.onnx";
}

bool modelAvailable() {
    const std::filesystem::path p = defaultModelPath();
    if (p.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(p, ec) && std::filesystem::file_size(p, ec) > 0;
}

}  // namespace mh::moge
