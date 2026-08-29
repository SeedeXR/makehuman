// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/ObjReader.h"

#include <charconv>
#include <cstdio>
#include <fstream>
#include <string_view>
#include <vector>

namespace mh::core {
namespace {

constexpr std::string_view kWhitespace = " \t\r\n";

/// Splits on runs of whitespace, writing views into `out`. Returns the count.
size_t tokenize(std::string_view line, std::vector<std::string_view>& out) {
    out.clear();
    size_t pos = 0;
    while (pos < line.size()) {
        const size_t start = line.find_first_not_of(kWhitespace, pos);
        if (start == std::string_view::npos) break;
        const size_t end = line.find_first_of(kWhitespace, start);
        out.push_back(line.substr(start, (end == std::string_view::npos)
                                             ? std::string_view::npos
                                             : end - start));
        if (end == std::string_view::npos) break;
        pos = end;
    }
    return out.size();
}

/// OBJ floats include forms like ".001" and "-.5"; from_chars handles both.
bool parseFloat(std::string_view s, float& out) {
    const auto res = std::from_chars(s.data(), s.data() + s.size(), out);
    return res.ec == std::errc{} && res.ptr == s.data() + s.size();
}

/// Parses one corner of an `f` statement: "v", "v/vt", "v//vn", or "v/vt/vn".
/// Returns false on a malformed vertex index; a missing UV yields `hasUv=false`.
bool parseCorner(std::string_view tok, long& vIdx, long& tIdx, bool& hasUv) {
    hasUv = false;
    tIdx  = 0;

    const size_t firstSlash = tok.find('/');
    const std::string_view vPart =
        (firstSlash == std::string_view::npos) ? tok : tok.substr(0, firstSlash);
    if (vPart.empty()) return false;
    if (std::from_chars(vPart.data(), vPart.data() + vPart.size(), vIdx).ec != std::errc{}) {
        return false;
    }

    if (firstSlash == std::string_view::npos) return true;

    const std::string_view rest = tok.substr(firstSlash + 1);
    const size_t secondSlash = rest.find('/');
    const std::string_view tPart =
        (secondSlash == std::string_view::npos) ? rest : rest.substr(0, secondSlash);
    if (!tPart.empty() &&
        std::from_chars(tPart.data(), tPart.data() + tPart.size(), tIdx).ec == std::errc{}) {
        hasUv = true;
    }
    return true;
}

/// Resolves a 1-based (or negative, end-relative) OBJ index to 0-based.
bool resolveIndex(long raw, size_t count, uint32_t& out) {
    long resolved;
    if (raw > 0) {
        resolved = raw - 1;
    } else if (raw < 0) {
        resolved = static_cast<long>(count) + raw;
    } else {
        return false;                      // 0 is not a valid OBJ index
    }
    if (resolved < 0 || static_cast<size_t>(resolved) >= count) return false;
    out = static_cast<uint32_t>(resolved);
    return true;
}

} // namespace

std::string ObjError::message() const {
    const char* kindStr = "unknown error";
    switch (kind) {
        case ObjErrorKind::NotFound:        kindStr = "file not found";             break;
        case ObjErrorKind::Unreadable:      kindStr = "file could not be read";     break;
        case ObjErrorKind::LooseVertex:     kindStr = "vertex referenced by no face"; break;
        case ObjErrorKind::BadIndex:        kindStr = "face index out of range";    break;
        case ObjErrorKind::MixedPrimitives: kindStr = "mixed primitive sizes";      break;
        case ObjErrorKind::EmptyMesh:       kindStr = "mesh contains no faces";     break;
    }
    std::string msg = file;
    if (line > 0) {
        msg += ':';
        msg += std::to_string(line);
    }
    msg += ": ";
    msg += kindStr;
    if (!detail.empty()) {
        msg += " (";
        msg += detail;
        msg += ')';
    }
    return msg;
}

std::expected<Mesh, ObjError> loadObj(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(ObjError{ObjErrorKind::NotFound, path.string(), 0, {}});
    }

    std::ifstream in(path);
    if (!in) {
        return std::unexpected(ObjError{ObjErrorKind::Unreadable, path.string(), 0, {}});
    }

    std::vector<Vec3>     coords;
    std::vector<Vec2>     uvs;
    std::vector<uint32_t> faceVerts;
    std::vector<uint32_t> faceUVs;
    std::vector<uint16_t> faceGroup;

    Mesh mesh(path.stem().string(), 4);
    uint16_t currentGroup = 0;
    bool     groupOpened  = false;
    bool     sawAnyUv     = false;

    std::string line;
    std::vector<std::string_view> tok;
    uint32_t lineNo = 0;

    while (std::getline(in, line)) {
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;
        if (tokenize(line, tok) == 0) continue;

        const std::string_view key = tok[0];

        if (key == "v" && tok.size() >= 4) {
            Vec3 v{};
            if (parseFloat(tok[1], v.x) && parseFloat(tok[2], v.y) && parseFloat(tok[3], v.z)) {
                coords.push_back(v);
            }
        } else if (key == "vt" && tok.size() >= 3) {
            Vec2 t{};
            if (parseFloat(tok[1], t.x) && parseFloat(tok[2], t.y)) {
                uvs.push_back(t);
            }
        } else if (key == "g" || key == "o") {
            std::string gname = (tok.size() >= 2) ? std::string{tok[1]} : std::string{"default"};
            currentGroup = mesh.addFaceGroup(std::move(gname));
            groupOpened  = true;
        } else if (key == "f" && tok.size() >= 4) {
            if (!groupOpened) {
                currentGroup = mesh.addFaceGroup("default");
                groupOpened  = true;
            }

            const size_t corners = tok.size() - 1;
            if (corners != 3 && corners != 4) {
                return std::unexpected(ObjError{
                    ObjErrorKind::MixedPrimitives, path.string(), lineNo,
                    std::to_string(corners) + " corners; only triangles and quads are supported"});
            }

            uint32_t vIdx[4]{};
            uint32_t tIdx[4]{};
            bool     cornerHasUv[4]{};

            for (size_t c = 0; c < corners; ++c) {
                long rawV = 0;
                long rawT = 0;
                bool hasUv = false;
                if (!parseCorner(tok[c + 1], rawV, rawT, hasUv)) {
                    return std::unexpected(ObjError{ObjErrorKind::BadIndex, path.string(), lineNo,
                                                    std::string{"malformed corner '"} +
                                                        std::string{tok[c + 1]} + "'"});
                }
                if (!resolveIndex(rawV, coords.size(), vIdx[c])) {
                    return std::unexpected(ObjError{ObjErrorKind::BadIndex, path.string(), lineNo,
                                                    "vertex index " + std::to_string(rawV)});
                }
                cornerHasUv[c] = hasUv;
                if (hasUv) {
                    if (!resolveIndex(rawT, uvs.size(), tIdx[c])) {
                        return std::unexpected(ObjError{ObjErrorKind::BadIndex, path.string(),
                                                        lineNo,
                                                        "uv index " + std::to_string(rawT)});
                    }
                    sawAnyUv = true;
                }
            }

            // Store a triangle as a degenerate quad by repeating corner 0,
            // matching wavefront.py:105-106 and :110-111.
            if (corners == 3) {
                vIdx[3] = vIdx[0];
                tIdx[3] = tIdx[0];
                cornerHasUv[3] = cornerHasUv[0];
            }

            for (size_t c = 0; c < 4; ++c) {
                faceVerts.push_back(vIdx[c]);
                faceUVs.push_back(cornerHasUv[c] ? tIdx[c] : 0);
            }
            faceGroup.push_back(currentGroup);
        }
        // vn, usemtl, mtllib, s: intentionally ignored (see the header).
    }

    if (faceGroup.empty()) {
        return std::unexpected(ObjError{ObjErrorKind::EmptyMesh, path.string(), 0, {}});
    }

    // A vertex referenced by no face is an error in the reference
    // (wavefront.py:132-142) because it breaks the adjacency invariants.
    std::vector<bool> used(coords.size(), false);
    for (const uint32_t v : faceVerts) used[v] = true;
    for (size_t i = 0; i < used.size(); ++i) {
        if (!used[i]) {
            return std::unexpected(ObjError{ObjErrorKind::LooseVertex, path.string(), 0,
                                            "vertex " + std::to_string(i)});
        }
    }

    mesh.setCoords(std::move(coords));
    if (sawAnyUv) {
        mesh.setUVs(std::move(uvs));
        mesh.setFaces(std::move(faceVerts), std::move(faceUVs), std::move(faceGroup));
    } else {
        mesh.setFaces(std::move(faceVerts), {}, std::move(faceGroup));
    }
    mesh.buildAdjacency();
    mesh.calcNormals();
    return mesh;
}

} // namespace mh::core
