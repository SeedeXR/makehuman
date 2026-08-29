// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>

namespace mh::foundation {

/// Locale-independent float parsing and formatting.
///
/// Two constraints meet here, and neither can be dropped:
///
/// 1. **Locale independence.** Every format this project reads and writes is
///    ASCII with a `.` decimal point. `strtof`, `snprintf` and iostreams all
///    honour `LC_NUMERIC`, so under a locale like `de_DE.UTF-8` a coordinate
///    reads back as `0` and writes out as `0,5` -- which for OBJ is corruption
///    and for glTF silently turns a 3-element array into 5.
///
/// 2. **Toolchain reach.** `std::from_chars`/`std::to_chars` solve (1) by
///    definition, but their **floating-point** overloads are not in every
///    libc++ we must build on. Apple clang 21 has them; the Xcode 16.4 libc++
///    on the `macos-15` GitHub runner does not, where the call instead
///    resolves to the deleted `bool` overload:
///        error: call to deleted function 'from_chars'
///    That is why every CI build failed while local builds passed.
///
/// So: use `std::from_chars`/`to_chars` where the standard library really has
/// them (detected at configure time by CMake, not guessed from a feature
/// macro), and fall back to the POSIX `_l` functions with a permanent C locale
/// otherwise. Both paths are locale-independent; only the mechanism differs.
///
/// Every float parsed or written by this project goes through here. Calling
/// `std::from_chars` on a float directly is what broke the build.

/// Parses a complete float. Returns false unless the ENTIRE view is consumed,
/// so trailing junk is an error rather than a silently truncated read.
///
/// Accepts a leading `+` (which `std::from_chars` rejects but the Python
/// reference's `float()` accepts) and rejects non-finite results.
[[nodiscard]] bool parseFloat(std::string_view text, float& out);

/// The same, at double precision.
///
/// `.mhm` camera values come from Python floats, which are binary64; narrowing
/// them to float loses digits the format actually carries.
[[nodiscard]] bool parseFloat(std::string_view text, double& out);

/// Integer parsing, for the same reason of containment rather than necessity:
/// the INTEGRAL from_chars overloads exist everywhere. Routing them here too
/// means `<charconv>` appears in exactly one translation unit, which CI can
/// enforce with a grep -- and enforcement is the point. The float bug reached
/// CI twice: once because five call sites each had their own parser, and again
/// because a sixth (`Mhm.cpp`) used a different helper name and was missed.
/// A rule that says "don't call std::from_chars on a float" cannot be checked;
/// "only foundation includes <charconv>" can.
///
/// All of these require the ENTIRE view to be consumed.
[[nodiscard]] bool parseInteger(std::string_view text, int& out);
[[nodiscard]] bool parseInteger(std::string_view text, unsigned& out);
[[nodiscard]] bool parseInteger(std::string_view text, long& out);

/// Fixed-point, @p decimals after the point. Used by the OBJ writer.
[[nodiscard]] std::string formatFixed(float value, int decimals);

/// @p significant significant digits, `%g` style.
[[nodiscard]] std::string formatGeneral(float value, int significant);

/// The shortest representation that parses back to exactly @p value.
///
/// This is what Python's `repr` gives, and what the `.mhmat` writer wants: the
/// file stays readable instead of turning `0.1` into `0.100000001`.
[[nodiscard]] std::string formatShortest(float value);
[[nodiscard]] std::string formatShortest(double value);

}  // namespace mh::foundation
