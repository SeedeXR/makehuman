// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The product version, generated from /VERSION.
//
// It was compiled in and NEVER verified: `MH_VERSION_STRING` was defined
// PRIVATE to mh_core, so nothing outside that library could read it and no test
// asserted on it. `makehuman --version` did not work while the version sat in
// the binary the whole time.
//
// These do not hardcode a version -- that would be a second declaration, which
// is the whole thing `tools/audit_version.py` forbids. They check the
// PROPERTIES a version must have, so they keep working across every bump.
#include "makehuman/foundation/Version.h"

#include <catch2/catch_test_macros.hpp>

#include <charconv>
#include <string>
#include <string_view>
#include <vector>

using namespace mh;

namespace {

/// The dot-separated components of @p v, or empty when any of them is not a
/// plain unsigned number.
std::vector<uint32_t> components(std::string_view v) {
    std::vector<uint32_t> out;
    size_t at = 0;
    while (at <= v.size()) {
        const size_t dot   = v.find('.', at);
        const size_t end   = dot == std::string_view::npos ? v.size() : dot;
        const auto* first  = v.data() + at;
        const auto* last   = v.data() + end;
        uint32_t n         = 0;
        const auto [p, ec] = std::from_chars(first, last, n);
        if (ec != std::errc{} || p != last) return {};
        out.push_back(n);
        if (dot == std::string_view::npos) break;
        at = dot + 1;
    }
    return out;
}

}  // namespace

TEST_CASE("the version is three numbers, generated not guessed", "[foundation][version]") {
    CHECK_FALSE(foundation::kVersion.empty());

    const auto parts = components(foundation::kVersion);
    INFO("kVersion = " << foundation::kVersion);
    REQUIRE(parts.size() == 3);

    // An unsubstituted template would leave `@PROJECT_VERSION@` here, and it
    // would still COMPILE, because it is a string literal.
    //
    // This assertion cannot be mutation-killed, and that is the good news:
    // tried on 2026-09-07 with `configure_file(... COPYONLY)`, and the build
    // failed instead -- `kVersionPatch = @PROJECT_VERSION_PATCH@` is not an
    // expression. The numeric constants below are the compile-time canary for
    // the substitution, and this line is the belt for a template that ever
    // holds the string alone.
    CHECK(foundation::kVersion.find('@') == std::string_view::npos);
    // ...and 0.0.0 is what an EMPTY PROJECT_VERSION configures to, which is
    // what happens when the header is generated before project() has run.
    CHECK_FALSE(foundation::kVersion == "0.0.0");
}

TEST_CASE("the numeric constants agree with the string", "[foundation][version]") {
    // Two spellings of one fact, so they can disagree: the template
    // interpolates four separate CMake variables, and PROJECT_VERSION_PATCH is
    // EMPTY when the version has only two components -- which configures to a
    // header that does not compile, or worse, to `0`.
    //
    // **Mutation-testing this needs three DISTINCT numbers.** At 2.0.0, minor
    // and patch are both 0, so interpolating PROJECT_VERSION_MINOR into the
    // patch constant is a no-op and the mutation survives -- measured. It was
    // killed at 9.8.7 (`"9.8.8" == "9.8.7"`). Bump /VERSION to something like
    // 9.8.7 before judging a change to this case; the surviving mutation was
    // the fixture's fault, not the code's.
    const auto parts = components(foundation::kVersion);
    REQUIRE(parts.size() == 3);
    CHECK(parts[0] == foundation::kVersionMajor);
    CHECK(parts[1] == foundation::kVersionMinor);
    CHECK(parts[2] == foundation::kVersionPatch);

    const std::string rebuilt = std::to_string(foundation::kVersionMajor) + "." +
                                std::to_string(foundation::kVersionMinor) + "." +
                                std::to_string(foundation::kVersionPatch);
    CHECK(rebuilt == foundation::kVersion);
}

TEST_CASE("the version is reachable from a module that is not mh_core", "[foundation][version]") {
    // The point of moving it out of mh_core's PRIVATE compile definitions. This
    // test lives in the unit binary, which links foundation, core, rig and io;
    // that it compiles at all is the assertion. The runtime check is here so
    // the case is not empty.
    static_assert(!foundation::kVersion.empty(), "kVersion must be a non-empty literal");
    CHECK(foundation::kVersionMajor + foundation::kVersionMinor + foundation::kVersionPatch > 0);
}
