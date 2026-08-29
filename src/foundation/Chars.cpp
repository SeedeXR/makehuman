// SPDX-License-Identifier: Apache-2.0
#include "makehuman/foundation/Chars.h"

#include <cmath>
#include <cstdio>

#if MH_HAVE_FP_CHARCONV
#include <charconv>
#include <system_error>
#else
#include <locale.h>
#include <cstdlib>
#if __has_include(<xlocale.h>)
#include <xlocale.h>  // macOS/BSD put uselocale here
#endif
#endif

namespace mh::foundation {
namespace {

#if !MH_HAVE_FP_CHARCONV

/// A permanent "C" locale, so the fallback never reads the process locale.
///
/// Constructed once on first use and deliberately never freed: it lives for
/// the process, and freeing it at static-destruction time would race with any
/// other static destructor that formats a number.
locale_t cLocale() {
    static locale_t loc = newlocale(LC_ALL_MASK, "C", nullptr);
    return loc;
}

/// Swaps the CALLING THREAD's locale to "C" for the duration.
///
/// `uselocale` is thread-local (POSIX.1-2008), so this cannot disturb another
/// thread mid-format the way `setlocale` would. Preferred over the `_l`
/// function variants because `snprintf_l` is a BSD/macOS extension that glibc
/// does not provide, whereas `uselocale` exists on both.
class ScopedCLocale {
public:
    ScopedCLocale() : prev_(uselocale(cLocale())) {}

    ~ScopedCLocale() {
        if (prev_ != nullptr) uselocale(prev_);
    }

    ScopedCLocale(const ScopedCLocale&)            = delete;
    ScopedCLocale& operator=(const ScopedCLocale&) = delete;

private:
    locale_t prev_;
};

#endif

}  // namespace

bool parseFloat(std::string_view text, float& out) {
    if (text.empty()) return false;

    // std::from_chars rejects a leading '+'; the Python reference's float()
    // accepts it, and shipped assets use it. Strip it for both paths so the
    // two behave identically.
    std::string_view v = text;
    if (v.front() == '+') v.remove_prefix(1);
    if (v.empty()) return false;

#if MH_HAVE_FP_CHARCONV
    const auto r = std::from_chars(v.data(), v.data() + v.size(), out);
    if (r.ec != std::errc{} || r.ptr != v.data() + v.size()) return false;
#else
    // strtof_l needs a NUL-terminated string; string_view is not guaranteed to
    // be one, so copy. These are short numeric tokens.
    // strtod needs a NUL-terminated string; string_view is not guaranteed to be
    // one, so copy. These are short numeric tokens.
    const std::string buf(v);
    char* end = nullptr;
    double d  = 0.0;
    {
        const ScopedCLocale cloc;
        d = std::strtod(buf.c_str(), &end);
    }
    if (end != buf.c_str() + buf.size()) return false;
    out = static_cast<float>(d);
#endif

    // Non-finite values parse successfully ("nan", "inf") in both paths and
    // then poison every downstream consumer -- JSON has no literal for them,
    // so an exported file becomes unparseable. Reject at the boundary.
    return std::isfinite(out);
}

#if MH_HAVE_FP_CHARCONV

std::string formatFixed(float value, int decimals) {
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::fixed, decimals);
    return (r.ec == std::errc{}) ? std::string(buf, r.ptr) : std::string("0");
}

std::string formatGeneral(float value, int significant) {
    char buf[64];
    const auto r =
        std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::general, significant);
    return (r.ec == std::errc{}) ? std::string(buf, r.ptr) : std::string("0");
}

std::string formatShortest(float value) {
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof(buf), value);
    return (r.ec == std::errc{}) ? std::string(buf, r.ptr) : std::string("0");
}

#else

namespace {

/// -Wformat=2 rejects a non-literal format string, so the two formats are
/// separate literals rather than a parameter.
std::string clip(const char* buf, int n) {
    if (n <= 0) return std::string("0");
    const int cap = static_cast<int>(sizeof(char[64])) - 1;
    return std::string(buf, static_cast<size_t>(n < cap ? n : cap));
}

std::string fixedImpl(float value, int decimals) {
    char buf[64];
    const ScopedCLocale cloc;
    return clip(buf, std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(value)));
}

std::string generalImpl(float value, int significant) {
    char buf[64];
    const ScopedCLocale cloc;
    return clip(buf,
                std::snprintf(buf, sizeof(buf), "%.*g", significant, static_cast<double>(value)));
}

}  // namespace

std::string formatFixed(float value, int decimals) {
    return fixedImpl(value, decimals);
}

std::string formatGeneral(float value, int significant) {
    return generalImpl(value, significant);
}

std::string formatShortest(float value) {
    // to_chars gives the shortest string that round-trips; printf has no such
    // mode. Widen the precision until the result parses back to exactly the
    // same float -- 9 significant digits always suffices for binary32, so this
    // terminates, and it stops at the first (shortest) one that works.
    for (int p = 1; p <= 9; ++p) {
        std::string s = generalImpl(value, p);
        float back    = 0.0F;
        if (parseFloat(s, back) && back == value) return s;
    }
    return generalImpl(value, 9);
}

#endif

}  // namespace mh::foundation
