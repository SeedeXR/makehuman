// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/rig/Facs.h"

#include <algorithm>
#include <array>

namespace mh::rig {
namespace {

/// The table. One row per Action Unit this unit set can express, ascending.
///
/// Read it as: FACS names the action and its muscle; the muscle picks the unit.
/// Where the unit's own name says the muscle -- `MouthLeftPlatysma` for AU20's
/// risorius-with-platysma, `NasolabialDeepener` for AU11 -- the row is the two
/// vocabularies meeting, not a judgement call.
constexpr std::array kTable{
    ActionUnitInfo{.code   = "AU1",
                   .name   = "Inner Brow Raiser",
                   .muscle = "frontalis, pars medialis",
                   .left   = {"LeftInnerBrowUp"},
                   .right  = {"RightInnerBrowUp"}},
    ActionUnitInfo{.code   = "AU2",
                   .name   = "Outer Brow Raiser",
                   .muscle = "frontalis, pars lateralis",
                   .left   = {"LeftOuterBrowUp"},
                   .right  = {"RightOuterBrowUp"}},
    ActionUnitInfo{.code   = "AU4",
                   .name   = "Brow Lowerer",
                   .muscle = "corrugator, depressor supercilii",
                   .left   = {"LeftBrowDown"},
                   .right  = {"RightBrowDown"}},
    ActionUnitInfo{.code   = "AU5",
                   .name   = "Upper Lid Raiser",
                   .muscle = "levator palpebrae superioris",
                   .left   = {"LeftUpperLidOpen"},
                   .right  = {"RightUpperLidOpen"}},
    ActionUnitInfo{.code   = "AU6",
                   .name   = "Cheek Raiser",
                   .muscle = "orbicularis oculi, pars orbitalis",
                   .left   = {"LeftCheekUp"},
                   .right  = {"RightCheekUp"}},
    ActionUnitInfo{.code   = "AU7",
                   .name   = "Lid Tightener",
                   .muscle = "orbicularis oculi, pars palpebralis",
                   .left   = {"LeftLowerLidUp"},
                   .right  = {"RightLowerLidUp"}},
    ActionUnitInfo{.code   = "AU9",
                   .name   = "Nose Wrinkler",
                   .muscle = "levator labii superioris alaeque nasi",
                   .left   = {"NoseWrinkler"}},
    ActionUnitInfo{.code   = "AU10",
                   .name   = "Upper Lip Raiser",
                   .muscle = "levator labii superioris",
                   .left   = {"UpperLipUp"}},
    ActionUnitInfo{.code   = "AU11",
                   .name   = "Nasolabial Deepener",
                   .muscle = "zygomaticus minor",
                   .left   = {"NasolabialDeepener"}},
    ActionUnitInfo{.code   = "AU12",
                   .name   = "Lip Corner Puller",
                   .muscle = "zygomaticus major",
                   .left   = {"MouthLeftPullUp"},
                   .right  = {"MouthRightPullUp"}},
    ActionUnitInfo{.code   = "AU14",
                   .name   = "Dimpler",
                   .muscle = "buccinator",
                   .left   = {"MouthLeftPullSide"},
                   .right  = {"MouthRightPullSide"}},
    ActionUnitInfo{.code   = "AU15",
                   .name   = "Lip Corner Depressor",
                   .muscle = "depressor anguli oris",
                   .left   = {"MouthLeftPullDown"},
                   .right  = {"MouthRightPullDown"}},
    ActionUnitInfo{.code   = "AU16",
                   .name   = "Lower Lip Depressor",
                   .muscle = "depressor labii inferioris",
                   .left   = {"lowerLipDown"}},
    // The chin raiser pushes the LOWER LIP up; the unit is named for what
    // moves, the AU for the muscle that moves it.
    ActionUnitInfo{
        .code = "AU17", .name = "Chin Raiser", .muscle = "mentalis", .left = {"lowerLipUp"}},
    ActionUnitInfo{.code   = "AU18",
                   .name   = "Lip Pucker",
                   .muscle = "incisivii labii superioris and inferioris",
                   .left   = {"LipsKiss"}},
    ActionUnitInfo{.code   = "AU19",
                   .name   = "Tongue Show",
                   .muscle = "genioglossus and friends",
                   .left   = {"TongueOut"}},
    ActionUnitInfo{.code   = "AU20",
                   .name   = "Lip Stretcher",
                   .muscle = "risorius with platysma",
                   .left   = {"MouthLeftPlatysma"},
                   .right  = {"MouthRightPlatysma"}},
    // One muscle, both lips: two units and ONE action, so it is unsided.
    ActionUnitInfo{.code   = "AU22",
                   .name   = "Lip Funneler",
                   .muscle = "orbicularis oris",
                   .left   = {"UpperLipForward", "lowerLipForward"}},
    ActionUnitInfo{.code   = "AU23",
                   .name   = "Lip Tightener",
                   .muscle = "orbicularis oris",
                   .left   = {"UpperLipBackward", "lowerLipBackward"}},
    ActionUnitInfo{.code   = "AU26",
                   .name   = "Jaw Drop",
                   .muscle = "masseter relaxed, temporalis, internal pterygoid",
                   .left   = {"JawDrop"}},
    ActionUnitInfo{.code   = "AU27",
                   .name   = "Mouth Stretch",
                   .muscle = "pterygoids, digastric",
                   .left   = {"JawDropStretched"}},
    ActionUnitInfo{
        .code = "AU29", .name = "Jaw Thrust", .muscle = "pterygoids", .left = {"ChinForward"}},
    ActionUnitInfo{.code   = "AU30",
                   .name   = "Jaw Sideways",
                   .muscle = "pterygoids",
                   .left   = {"ChinLeft"},
                   .right  = {"ChinRight"}},
    ActionUnitInfo{
        .code = "AU33", .name = "Cheek Blow", .muscle = "buccinator", .left = {"CheeksPump"}},
    ActionUnitInfo{
        .code = "AU35", .name = "Cheek Suck", .muscle = "buccinator", .left = {"CheeksSuck"}},
    ActionUnitInfo{.code   = "AU43",
                   .name   = "Eyes Closed",
                   .muscle = "relaxation of levator palpebrae superioris",
                   .left   = {"LeftUpperLidClosed"},
                   .right  = {"RightUpperLidClosed"}},
    // The eye-direction AUs move BOTH eyes together -- FACS codes a gaze, not
    // a per-eye action -- so each is one unsided row holding two units.
    ActionUnitInfo{.code   = "AU61",
                   .name   = "Eyes Turn Left",
                   .muscle = "lateral and medial rectus",
                   .left   = {"LeftEyeturnLeft", "RightEyeturnLeft"}},
    ActionUnitInfo{.code   = "AU62",
                   .name   = "Eyes Turn Right",
                   .muscle = "lateral and medial rectus",
                   .left   = {"LeftEyeturnRight", "RightEyeturnRight"}},
    ActionUnitInfo{.code   = "AU63",
                   .name   = "Eyes Up",
                   .muscle = "superior rectus",
                   .left   = {"LeftEyeUp", "RightEyeUp"}},
    ActionUnitInfo{.code   = "AU64",
                   .name   = "Eyes Down",
                   .muscle = "inferior rectus",
                   .left   = {"LeftEyeDown", "RightEyeDown"}},
};

/// Splits `AU12L` into `AU12` and 'L'. A trailing side letter is only a side
/// when what precedes it is a code we know -- there is no AU whose code ends in
/// a letter, but reading it that way keeps the rule local instead of relying on
/// that staying true.
struct Requested {
    std::string_view code;
    char side{};
};

Requested split(std::string_view code) {
    if (code.size() > 1) {
        const char last = code.back();
        if (last == 'L' || last == 'R') return {code.substr(0, code.size() - 1), last};
    }
    return {code, '\0'};
}

const ActionUnitInfo* find(std::string_view code) {
    const auto at =
        std::ranges::find_if(kTable, [code](const ActionUnitInfo& au) { return au.code == code; });
    return at == kTable.end() ? nullptr : &*at;
}

}  // namespace

std::string FacsError::message() const {
    switch (kind) {
        case FacsErrorKind::UnknownUnit:
            return "no such Action Unit \"" + detail + "\" in the face library";
        case FacsErrorKind::NotSided:
            return "Action Unit \"" + detail + "\" has one midline muscle, so it has no side";
        case FacsErrorKind::Empty: return "no Action Units given";
    }
    return "unknown FACS error";
}

std::span<const ActionUnitInfo> actionUnits() {
    return kTable;
}

std::expected<Expression, FacsError> facsExpression(std::span<const ActionUnit> requested) {
    if (requested.empty()) {
        return std::unexpected(FacsError{.kind = FacsErrorKind::Empty, .detail = {}});
    }

    Expression out;
    out.name = "FACS";
    for (const ActionUnit& want : requested) {
        // The whole spelling first, so a code that genuinely ends in L or R
        // would win over being read as a side. None does today; this is what
        // keeps that from becoming a rule to remember.
        const ActionUnitInfo* au = find(want.code);
        char side                = '\0';
        if (au == nullptr) {
            if (const Requested asked = split(want.code); asked.side != '\0') {
                au   = find(asked.code);
                side = asked.side;
            }
        }
        if (au == nullptr) {
            // Reported with the caller's own spelling: "AU99L" is more useful
            // to see than the "AU99" it was split into.
            return std::unexpected(
                FacsError{.kind = FacsErrorKind::UnknownUnit, .detail = want.code});
        }
        if (side != '\0' && !au->sided()) {
            return std::unexpected(
                FacsError{.kind = FacsErrorKind::NotSided, .detail = std::string(au->code)});
        }

        const auto add = [&out, &want](const std::array<std::string_view, 2>& half) {
            for (const std::string_view unit : half) {
                if (!unit.empty()) out.units.push_back({std::string(unit), want.weight});
            }
        };
        if (side != 'R') add(au->left);
        if (side != 'L' && au->sided()) add(au->right);
    }
    return out;
}

}  // namespace mh::rig
