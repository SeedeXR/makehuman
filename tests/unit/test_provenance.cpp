// SPDX-License-Identifier: Apache-2.0
//
// The stamp every export carries (owner directive 12.4).

#include "makehuman/foundation/Provenance.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace mh;

TEST_CASE("the stamp carries both numbers and the topology", "[foundation][provenance]") {
    const foundation::Provenance p{
        .application = "2.0.0", .contentFormat = 1, .topologyHash = 0xE38C060123B5D0DBULL};
    CHECK(p.stamp() == " 2.0.0 (content-format 1, topology e38c060123b5d0db)");
}

TEST_CASE("the content-format version is NOT the product version", "[foundation][provenance]") {
    // The whole reason for the second number: a rebuild changes the first and
    // must not change the second. Asserted as a shape rather than a value --
    // "2.0.0" is three components, the content format is one integer, and
    // nothing may quietly start deriving one from the other.
    const foundation::Provenance p{.application = "9.8.7"};
    CHECK(p.contentFormat == foundation::kContentFormatVersion);
    CHECK(p.stamp().find("9.8.7") != std::string::npos);
    CHECK(p.stamp().find("content-format " + std::to_string(foundation::kContentFormatVersion)) !=
          std::string::npos);
}

TEST_CASE("no application version means no stamp at all", "[foundation][provenance]") {
    // A writer used from a test has no product version to hand and should not
    // have to invent one; writing "(content-format 1)" with no version beside
    // it would be a half-truth in a file.
    CHECK(foundation::Provenance{}.stamp().empty());
}

TEST_CASE("a zero topology hash is omitted, not written as zeros", "[foundation][provenance]") {
    // 0 means "not supplied". Writing sixteen zeros would be a topology
    // identity that no mesh has, and a consumer comparing hashes would reject
    // every such file instead of ignoring the field.
    const foundation::Provenance p{.application = "2.0.0"};
    CHECK(p.stamp() == " 2.0.0 (content-format 1)");
}

TEST_CASE("the hash is spelled the same way every time", "[foundation][provenance]") {
    // Fixed 16 lower-case digits. A width-varying spelling would make the same
    // topology read as two different ones.
    const foundation::Provenance small{.application = "2.0.0", .topologyHash = 1};
    CHECK(small.stamp() == " 2.0.0 (content-format 1, topology 0000000000000001)");
    const foundation::Provenance big{.application = "2.0.0", .topologyHash = 0xFFFFFFFFFFFFFFFFULL};
    CHECK(big.stamp() == " 2.0.0 (content-format 1, topology ffffffffffffffff)");
}
