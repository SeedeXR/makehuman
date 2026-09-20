// SPDX-License-Identifier: Apache-2.0

#include "makehuman/io/Ktx2Encode.h"

#if defined(MH_HAVE_KTX2)
#include <ktx.h>
#endif

namespace mh::io {

#if !defined(MH_HAVE_KTX2)

bool ktx2Available() noexcept {
    return false;
}

std::optional<std::vector<uint8_t>> ktx2EncodeEtc1s(std::span<const uint8_t>, uint32_t, uint32_t,
                                                    Ktx2Transfer) {
    return std::nullopt;
}

#else

namespace {

/// Vulkan format enums, spelled out rather than pulled from a Vulkan header.
///
/// libktx takes a `vkFormat` as a plain integer and we do not otherwise depend
/// on Vulkan, so adding that dependency to name two constants would be a poor
/// trade. The values are from the Vulkan specification's `VkFormat` enum.
constexpr uint32_t kVkFormatR8G8B8A8Unorm = 37;
constexpr uint32_t kVkFormatR8G8B8A8Srgb  = 43;

/// ETC1S quality, on libktx's 1..255 scale.
///
/// 128 is the reference encoder's own default. Chosen deliberately rather than
/// tuned: the gate measures what this produces and records the figure, so a
/// change here is visible as a moved number rather than a silent drift.
constexpr ktx_uint32_t kQualityLevel = 128;

}  // namespace

bool ktx2Available() noexcept {
    return true;
}

std::optional<std::vector<uint8_t>> ktx2EncodeEtc1s(std::span<const uint8_t> rgba, uint32_t width,
                                                    uint32_t height, Ktx2Transfer transfer) {
    // `KHR_texture_basisu` requires multiples of four. REFUSE rather than pad:
    // a padded image is silently the wrong size to every consumer, whereas a
    // refusal is one loud failure at the point the mistake was made.
    if (width == 0 || height == 0 || width % 4 != 0 || height % 4 != 0) return std::nullopt;
    if (rgba.size() != size_t{width} * height * 4) return std::nullopt;

    ktxTextureCreateInfo info{};
    info.vkFormat = transfer == Ktx2Transfer::Srgb ? kVkFormatR8G8B8A8Srgb : kVkFormatR8G8B8A8Unorm;
    info.baseWidth       = width;
    info.baseHeight      = height;
    info.baseDepth       = 1;
    info.numDimensions   = 2;
    info.numLevels       = 1;  // see Ktx2Writer.h on why one level, not a pyramid
    info.numLayers       = 1;
    info.numFaces        = 1;
    info.isArray         = KTX_FALSE;
    info.generateMipmaps = KTX_FALSE;

    ktxTexture2* texture = nullptr;
    if (ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) != KTX_SUCCESS)
        return std::nullopt;

    // Every path below must destroy the texture, including the failures. The
    // function has several exits and libktx allocates, so the cleanup is
    // written once here rather than repeated at each `return`.
    struct Guard {
        ktxTexture2* t;

        ~Guard() {
            if (t != nullptr) ktxTexture_Destroy(ktxTexture(t));
        }
    } guard{texture};

    if (ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, rgba.data(), rgba.size()) !=
        KTX_SUCCESS)
        return std::nullopt;

    ktxBasisParams params{};
    params.structSize   = sizeof(params);
    params.uastc        = KTX_FALSE;  // ETC1S + BasisLZ, which is what the extension permits
    params.qualityLevel = kQualityLevel;
    if (ktxTexture2_CompressBasisEx(texture, &params) != KTX_SUCCESS) return std::nullopt;

    ktx_uint8_t* bytes = nullptr;
    ktx_size_t length  = 0;
    if (ktxTexture_WriteToMemory(ktxTexture(texture), &bytes, &length) != KTX_SUCCESS)
        return std::nullopt;

    std::vector<uint8_t> out(bytes, bytes + length);
    free(bytes);  // libktx allocates this with malloc and hands ownership over
    return out;
}

#endif

}  // namespace mh::io
