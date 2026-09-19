// SPDX-License-Identifier: Apache-2.0
//
// The KTX2 container, checked against files an INDEPENDENT encoder produced.
//
// Written from the Khronos specification, but the specification is not what
// this test trusts. A spec summary read during the investigation said a DFD
// sample's `bitLength` was a 16-bit field; the reference bytes say it is 8 bits
// stored MINUS ONE, and the 16-bit reading yields a nonsensical 3904. So the
// oracle here is three real `basisu` 2.50 files, and the assertion is byte
// equality: given a reference file's own payload and its own `KTXwriter`
// string, our writer must reproduce it exactly.
//
// The three cover every shape the container takes -- see golden/ktx2/MANIFEST.json:
//   brown_eye           2 DFD samples (alpha), sRGB
//   skin_normal         1 DFD sample,          sRGB
//   normal_map_linear   1 DFD sample,          LINEAR (`basisu -normal_map`)
//
// TWO THINGS THE REFERENCES CANNOT TEST, both found by a mutation that
// SURVIVED them, and both covered by the synthetic cases below:
//   * the mandatory 8-byte pad before the supercompression global data --
//     all three references happen to end their key/value data at 184 or 200,
//     which are already 8-aligned, so aligning to 4 instead passed everything;
//   * the refusal path -- deleting the multiple-of-4 guard outright changed
//     nothing any check could see.
//
// THERE IS DELIBERATELY NO `basisu -unpack` TEST HERE, though the oracle exists
// and was run. Byte-equality against a file basisu itself produced already
// implies basisu accepts it, so such a test would only add information for a
// container basisu did NOT write -- and the synthetic case below covers exactly
// that, since a 15-character writer string shifts every downstream offset. CI
// installs ninja/assimp/qt and has no basisu, so a guarded test would be a
// second `test_draco.cpp`: present, reassuring, and never actually run.
//
// The manual check, run 2026-09-19 and worth repeating when the payload
// changes -- it returned `Success`, exit 0, 61 files, transcoding to ASTC 4x4,
// BC1/BC3/BC4/BC5/BC7, ATC, ETC1/ETC2, PVRTC1/2 and FXT1 on a 55,007-byte
// container carrying our own KTXwriter string:
//     basisu -unpack ours.ktx2

#include "makehuman/io/Ktx2Writer.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mh::io;

namespace {

std::filesystem::path fixtureDir() {
    return std::filesystem::path(MH_GOLDEN_DIR) / "ktx2";
}

std::vector<uint8_t> readFixture(std::string_view name) {
    const auto path = fixtureDir() / name;
    std::ifstream in(path, std::ios::binary);
    // Loud on purpose. A fixture that silently fails to load leaves a test
    // that measures nothing while reporting green.
    REQUIRE(in);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

uint32_t u32(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + at, sizeof v);
    return v;
}

uint64_t u64(const std::vector<uint8_t>& b, size_t at) {
    uint64_t v = 0;
    std::memcpy(&v, b.data() + at, sizeof v);
    return v;
}

/// Reports WHERE two byte strings first differ.
///
/// `REQUIRE(a == b)` on 55 kB of binary says only that they are not equal,
/// which is the least useful thing to know when a container field moves.
void requireSameBytes(const std::vector<uint8_t>& got, const std::vector<uint8_t>& want) {
    REQUIRE(got.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i) {
        if (got[i] != want[i]) {
            INFO("first difference at byte " << i << ": got 0x" << std::hex
                                             << static_cast<int>(got[i]) << ", reference 0x"
                                             << static_cast<int>(want[i]));
            REQUIRE(got[i] == want[i]);
        }
    }
}

}  // namespace

TEST_CASE("the KTX2 container reproduces a reference encoder byte for byte", "[ktx2]") {
    for (const std::string_view name :
         {"brown_eye.ktx2", "skin_normal.ktx2", "normal_map_linear.ktx2"}) {
        INFO("fixture " << name);
        const std::vector<uint8_t> ref = readFixture(name);

        // Read the payload and the writer string back out of the reference, so
        // the only thing under test is the container around them.
        const size_t dfdOffset = u32(ref, 48);
        const size_t kvdOffset = u32(ref, 56);
        const size_t sgdOffset = static_cast<size_t>(u64(ref, 64));
        const size_t sgdLength = static_cast<size_t>(u64(ref, 72));
        const size_t lvlOffset = static_cast<size_t>(u64(ref, 80));
        const size_t lvlLength = static_cast<size_t>(u64(ref, 88));
        const uint32_t block   = u32(ref, dfdOffset + 8) >> 16;  // descriptorBlockSize

        Ktx2Etc1sImage image;
        image.width      = u32(ref, 20);
        image.height     = u32(ref, 24);
        image.hasAlpha   = (block - 24) / 16 == 2;
        image.transfer   = static_cast<Ktx2Transfer>((u32(ref, dfdOffset + 12) >> 16) & 0xFF);
        image.globalData = {ref.data() + sgdOffset, sgdLength};
        image.levelData  = {ref.data() + lvlOffset, lvlLength};

        // The key/value entry is `key\0value\0`; the writer string is the value.
        const auto* entry        = reinterpret_cast<const char*>(ref.data()) + kvdOffset + 4;
        const std::string writer = entry + std::strlen(entry) + 1;

        const auto got = ktx2Write(image, writer);
        REQUIRE(got.has_value());
        requireSameBytes(*got, ref);
    }
}

TEST_CASE("the global data is padded to an 8-byte boundary", "[ktx2]") {
    // No reference file reaches this: they all end their key/value data at 184
    // or 200. A 15-character writer string gives a 32-byte key/value section
    // ending at 180, which needs four bytes of padding.
    const std::vector<uint8_t> sgd(64, 0xAA);
    const std::vector<uint8_t> lvl(100, 0xBB);
    Ktx2Etc1sImage image;
    image.width      = 64;
    image.height     = 64;
    image.globalData = sgd;
    image.levelData  = lvl;

    const auto got = ktx2Write(image, std::string(15, 'w'));
    REQUIRE(got.has_value());

    const size_t kvdEnd    = u32(*got, 56) + size_t{u32(*got, 60)};
    const size_t sgdOffset = static_cast<size_t>(u64(*got, 64));
    CHECK(kvdEnd == 180);
    CHECK(sgdOffset == 184);
    CHECK(sgdOffset % 8 == 0);
    for (size_t i = kvdEnd; i < sgdOffset; ++i) {
        INFO("pad byte " << i);
        CHECK((*got)[i] == 0);
    }

    // And both payloads really landed where the index claims.
    REQUIRE(std::memcmp(got->data() + sgdOffset, sgd.data(), sgd.size()) == 0);
    const size_t lvlOffset = static_cast<size_t>(u64(*got, 80));
    CHECK(lvlOffset == sgdOffset + sgd.size());  // level alignment is 1 when supercompressed
    REQUIRE(std::memcmp(got->data() + lvlOffset, lvl.data(), lvl.size()) == 0);
    CHECK(got->size() == lvlOffset + lvl.size());
}

TEST_CASE("the container writer refuses what it cannot describe", "[ktx2]") {
    const std::vector<uint8_t> sgd(64, 0xAA);
    const std::vector<uint8_t> lvl(100, 0xBB);
    const auto valid = [&] {
        Ktx2Etc1sImage i;
        i.width      = 64;
        i.height     = 64;
        i.globalData = sgd;
        i.levelData  = lvl;
        return i;
    };
    REQUIRE(ktx2Write(valid(), "w").has_value());  // the control: the base case is accepted

    SECTION("a width that is not a multiple of 4") {
        auto i  = valid();
        i.width = 63;
        CHECK_FALSE(ktx2Write(i, "w").has_value());
    }
    SECTION("a height that is not a multiple of 4") {
        auto i   = valid();
        i.height = 66;
        CHECK_FALSE(ktx2Write(i, "w").has_value());
    }
    SECTION("a zero dimension") {
        auto i  = valid();
        i.width = 0;
        CHECK_FALSE(ktx2Write(i, "w").has_value());
        auto j   = valid();
        j.height = 0;
        CHECK_FALSE(ktx2Write(j, "w").has_value());
    }
    SECTION("no global data, which supercompression scheme 1 requires") {
        auto i       = valid();
        i.globalData = {};
        CHECK_FALSE(ktx2Write(i, "w").has_value());
    }
    SECTION("no level data") {
        auto i      = valid();
        i.levelData = {};
        CHECK_FALSE(ktx2Write(i, "w").has_value());
    }
}
