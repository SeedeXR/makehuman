// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Proxy.h"
#include "makehuman/foundation/Chars.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace mh::core {
namespace {

/// Upper bound on a `delete_verts` index.
///
/// These indices come straight from the file and are used to *size* a vector.
/// The reference indexes a fixed-size array allocated to the body's vertex
/// count (proxy.py:115), so an out-of-range index simply raises there; growing
/// on demand is our divergence, and it turns a two-line file into an unbounded
/// allocation -- or worse: `resize(v + 1)` in uint32 wraps to 0 at UINT32_MAX
/// and the following write lands out of bounds (confirmed under ASan: "BUS ...
/// WRITE memory access" at loadProxy). Loading an asset is a trust boundary,
/// so the index is bounded before it is used for anything.
///
/// The base mesh has 19,158 vertices; 2^24 leaves four orders of magnitude of
/// headroom and caps the allocation at 16 MB.
constexpr uint32_t kMaxDeleteVertIndex = 1U << 24;

std::vector<std::string> splitWs(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream ss(line);
    std::string tok;
    while (ss >> tok)
        out.push_back(tok);
    return out;
}

/// Delegates to the shared shim: floating-point std::from_chars does not exist
/// in every libc++ we build on (the macos-15 runner's Xcode 16.4 lacks it), and
/// strtof would honour LC_NUMERIC. See foundation/Chars.h.
bool parseFloat(const std::string& s, float& out) {
    return foundation::parseFloat(s, out);
}

bool parseUint(const std::string& s, uint32_t& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

bool parseInt(const std::string& s, int32_t& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

std::string joinFrom(const std::vector<std::string>& tok, size_t first) {
    std::string out;
    for (size_t i = first; i < tok.size(); ++i) {
        if (i != first) out.push_back(' ');
        out += tok[i];
    }
    return out;
}

std::string toLower(std::string s) {
    std::ranges::transform(s, s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

ProxyType typeFromExtension(const std::filesystem::path& p) {
    // .proxy is an alternate body topology; .mhclo is everything else
    // (proxy.py:986-991). The finer type comes from where the asset lives,
    // which the asset library supplies; default to Clothes.
    return p.extension() == ".proxy" ? ProxyType::Proxymeshes : ProxyType::Clothes;
}

/// Which block of data lines we are inside.
enum class Block { None, Verts, Weights, DeleteVerts };

}  // namespace

Vec3 TMatrix::diagonal(std::span<const Vec3> humanCoords) const {
    Vec3 d{1.0F, 1.0F, 1.0F};
    if (isIdentity()) return d;

    // matrix[n][n] = |co1[n] - co2[n]| / den   (proxy.py:902-909)
    float* comp[3] = {&d.x, &d.y, &d.z};
    for (size_t n = 0; n < 3; ++n) {
        if (!scale[n]) continue;
        const Scale& s = *scale[n];
        if (s.v1 >= humanCoords.size() || s.v2 >= humanCoords.size() || s.den == 0.0F) continue;
        const Vec3& a     = humanCoords[s.v1];
        const Vec3& b     = humanCoords[s.v2];
        const float av[3] = {a.x, a.y, a.z};
        const float bv[3] = {b.x, b.y, b.z};
        *comp[n]          = std::abs(av[n] - bv[n]) / s.den;
    }
    return d;
}

std::string ProxyError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case ProxyErrorKind::NotFound: k = "file not found"; break;
        case ProxyErrorKind::Unreadable: k = "file unreadable"; break;
        case ProxyErrorKind::MalformedLine: k = "malformed line"; break;
        case ProxyErrorKind::IndexOutOfRange: k = "vertex index out of range"; break;
    }
    std::string m = file;
    if (line > 0) m += ':' + std::to_string(line);
    m += ": ";
    m += k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

std::expected<Proxy, ProxyError> loadProxy(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(ProxyError{ProxyErrorKind::NotFound, path.string(), 0, {}});
    }
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(ProxyError{ProxyErrorKind::Unreadable, path.string(), 0, {}});
    }

    Proxy p;
    p.type = typeFromExtension(path);
    p.name = path.stem().string();

    const auto dir = path.parent_path();
    Block block    = Block::None;
    bool sawZDepth = false;
    bool anyTriple = false;

    // `-` ranges carry across lines in the reference: `v0` is a function-level
    // local there (proxy.py:516-529), so `10` on one line and `- 14` on the
    // next deletes 10..14. Keeping this state per-line, as it was, deleted only
    // two vertices instead of five.
    bool deleteRange        = false;
    bool sawDeleteVert      = false;
    uint32_t lastDeleteVert = 0;

    std::string line;
    uint32_t lineNo = 0;

    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '#') continue;  // comment / licence header

        const auto tok = splitWs(line);
        if (tok.empty()) continue;
        const std::string& key = tok[0];

        // ---- block switches ------------------------------------------------
        if (key == "verts") {
            block = Block::Verts;  // the count that follows is ignored (proxy.py:425)
            continue;
        }
        if (key == "weights") {
            block = Block::Weights;
            continue;
        }
        if (key == "delete_verts") {
            block = Block::DeleteVerts;
            continue;
        }

        // ---- keyed metadata ------------------------------------------------
        if (key == "name" && tok.size() > 1) {
            p.name = joinFrom(tok, 1);
            block  = Block::None;
            continue;
        }
        if (key == "uuid" && tok.size() > 1) {
            p.uuid = tok[1];
            block  = Block::None;
            continue;
        }
        if (key == "description" && tok.size() > 1) {
            p.description = joinFrom(tok, 1);
            block         = Block::None;
            continue;
        }
        if (key == "tag" && tok.size() > 1) {
            p.tags.push_back(toLower(joinFrom(tok, 1)));
            block = Block::None;
            continue;
        }
        if (key == "basemesh" && tok.size() > 1) {
            p.basemesh = tok[1];
            block      = Block::None;
            continue;
        }
        if (key == "version" && tok.size() > 1) {
            (void)parseInt(tok[1], p.version);
            block = Block::None;
            continue;
        }
        if (key == "z_depth" && tok.size() > 1) {
            (void)parseInt(tok[1], p.zDepth);
            sawZDepth = true;
            block     = Block::None;
            continue;
        }
        if (key == "max_pole" && tok.size() > 1) {
            uint32_t mp{};
            if (parseUint(tok[1], mp)) p.maxPole = mp * 2;  // doubled, proxy.py:540
            block = Block::None;
            continue;
        }
        if (key == "obj_file" && tok.size() > 1) {
            std::filesystem::path f = tok[1];
            if (!f.has_extension()) f += ".obj";
            p.objFile = dir / f;
            block     = Block::None;
            continue;
        }
        if (key == "material" && tok.size() > 1) {
            std::filesystem::path f = tok[1];
            if (!f.has_extension()) f += ".mhmat";
            p.materialFile = dir / f;
            block          = Block::None;
            continue;
        }
        if ((key == "x_scale" || key == "y_scale" || key == "z_scale") && tok.size() >= 4) {
            const size_t axis = (key[0] == 'x') ? 0 : (key[0] == 'y') ? 1 : 2;
            TMatrix::Scale s;
            if (parseUint(tok[1], s.v1) && parseUint(tok[2], s.v2) && parseFloat(tok[3], s.den)) {
                p.tmatrix.scale[axis] = s;
            }
            block = Block::None;
            continue;
        }

        // ---- block payloads -------------------------------------------------
        if (block == Block::Verts) {
            if (tok.size() == 1) {
                // Exact fit: bound to one base vertex (proxy.py:710-717).
                uint32_t v0{};
                if (!parseUint(tok[0], v0)) {
                    return std::unexpected(ProxyError{ProxyErrorKind::MalformedLine, path.string(),
                                                      lineNo, "expected a vertex index"});
                }
                p.refVerts.push_back({v0, 0U, 1U});
                p.weights.push_back({1.0F, 0.0F, 0.0F});
                p.offsets.push_back(Vec3{});
                p.maxRefIndex_ = std::max(p.maxRefIndex_, v0);
            } else if (tok.size() >= 6) {
                // Barycentric fit, with an optional offset (proxy.py:719-740).
                std::array<uint32_t, 3> v{};
                std::array<float, 3> w{};
                bool ok = true;
                for (size_t i = 0; i < 3; ++i)
                    ok = ok && parseUint(tok[i], v[i]);
                for (size_t i = 0; i < 3; ++i)
                    ok = ok && parseFloat(tok[i + 3], w[i]);
                Vec3 d{};
                if (ok && tok.size() >= 9) {
                    ok = parseFloat(tok[6], d.x) && parseFloat(tok[7], d.y) &&
                         parseFloat(tok[8], d.z);
                }
                if (!ok) {
                    return std::unexpected(ProxyError{ProxyErrorKind::MalformedLine, path.string(),
                                                      lineNo,
                                                      "expected 'v0 v1 v2 w0 w1 w2 [dx dy dz]'"});
                }
                p.refVerts.push_back(v);
                p.weights.push_back(w);
                p.offsets.push_back(d);
                p.maxRefIndex_ = std::max({p.maxRefIndex_, v[0], v[1], v[2]});
                if (w[1] != 0.0F || w[2] != 0.0F) anyTriple = true;
            } else {
                // 2..5 tokens used to fall through silently. Proxy vertex n
                // binds to vertex n of the sibling .obj, so dropping a line
                // shifts every vertex after it onto the wrong reference
                // triangle -- plausible-looking, wrong geometry, no
                // diagnostic. The reference calls fromTriple and raises
                // IndexError.
                return std::unexpected(ProxyError{
                    ProxyErrorKind::MalformedLine, path.string(), lineNo,
                    "expected 1 or 6+ fields in a verts line, got " + std::to_string(tok.size())});
            }
            continue;
        }

        if (block == Block::DeleteVerts) {
            // Integers, with '-' marking an inclusive range (proxy.py:516-529).
            for (const std::string& t : tok) {
                if (t == "-") {
                    deleteRange = true;
                    continue;
                }
                uint32_t v{};
                if (!parseUint(t, v)) continue;
                if (v > kMaxDeleteVertIndex) {
                    return std::unexpected(ProxyError{
                        ProxyErrorKind::IndexOutOfRange, path.string(), lineNo,
                        "delete_verts index " + std::to_string(v) + " exceeds the maximum of " +
                            std::to_string(kMaxDeleteVertIndex)});
                }

                const uint32_t from = (deleteRange && sawDeleteVert) ? lastDeleteVert : v;
                deleteRange         = false;

                if (v >= p.deleteVerts.size()) p.deleteVerts.resize(size_t{v} + 1U, 0);
                for (uint32_t i = from; i <= v; ++i)
                    p.deleteVerts[i] = 1;

                lastDeleteVert = v;
                sawDeleteVert  = true;
            }
            continue;
        }

        // weights blocks and every deprecated key fall through, matching the
        // reference's warn-and-continue (proxy.py:449-499).
    }

    // The oracle tests `z_depth == -1` exactly, not `< 0`: `z_depth -5` stays
    // -5 there and became 50 here (proxy.py:535-537). -1 is also the sentinel
    // for "absent", which is why an unseen key takes the same branch.
    if (!sawZDepth || p.zDepth == -1) p.zDepth = 50;
    p.exactFitOnly = !anyTriple;
    return p;
}

bool fitProxy(const Proxy& proxy, std::span<const Vec3> humanCoords, std::vector<Vec3>& out) {
    if (proxy.maxRefIndex() >= humanCoords.size()) return false;

    const Vec3 m = proxy.tmatrix.diagonal(humanCoords);
    out.assign(proxy.vertexCount(), Vec3{});

    for (size_t i = 0; i < proxy.vertexCount(); ++i) {
        const auto& v = proxy.refVerts[i];
        const auto& w = proxy.weights[i];
        const Vec3& d = proxy.offsets[i];

        // P = SUM w_k * H[v_k] + M * d   (proxy.py:210-217)
        Vec3 p = humanCoords[v[0]] * w[0] + humanCoords[v[1]] * w[1] + humanCoords[v[2]] * w[2];
        p.x += m.x * d.x;
        p.y += m.y * d.y;
        p.z += m.z * d.z;
        out[i] = p;
    }
    return true;
}

std::vector<uint8_t> visibleVertexMask(std::span<const Proxy* const> proxies,
                                       size_t bodyVertexCount) {
    std::vector<uint8_t> visible(bodyVertexCount, 1U);
    for (const Proxy* pxy : proxies) {
        if (pxy == nullptr) continue;
        // A proxy authored against a different base mesh can declare more
        // vertices than this body has; the reference sizes deleteVerts to the
        // human it was loaded against (proxy.py:115) and would read past the
        // end here. Clamp instead.
        const size_t n = std::min(pxy->deleteVerts.size(), bodyVertexCount);
        for (size_t v = 0; v < n; ++v) {
            if (pxy->deleteVerts[v] != 0U) visible[v] = 0U;
        }
    }
    return visible;
}

}  // namespace mh::core
