// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Proxy.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace mh::core {
namespace {

std::vector<std::string> splitWs(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream ss(line);
    std::string tok;
    while (ss >> tok)
        out.push_back(tok);
    return out;
}

bool parseFloat(const std::string& s, float& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
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
            }
            continue;
        }

        if (block == Block::DeleteVerts) {
            // Integers, with '-' marking an inclusive range (proxy.py:516-529).
            std::vector<uint32_t> nums;
            bool range = false;
            for (const std::string& t : tok) {
                if (t == "-") {
                    range = true;
                    continue;
                }
                uint32_t v{};
                if (!parseUint(t, v)) continue;
                if (range && !nums.empty()) {
                    for (uint32_t i = nums.back() + 1; i <= v; ++i)
                        nums.push_back(i);
                    range = false;
                } else {
                    nums.push_back(v);
                }
            }
            for (const uint32_t v : nums) {
                if (v >= p.deleteVerts.size()) p.deleteVerts.resize(v + 1, 0);
                p.deleteVerts[v] = 1;
            }
            continue;
        }

        // weights blocks and every deprecated key fall through, matching the
        // reference's warn-and-continue (proxy.py:449-499).
    }

    if (!sawZDepth || p.zDepth < 0) p.zDepth = 50;  // proxy.py:535-537
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

}  // namespace mh::core
