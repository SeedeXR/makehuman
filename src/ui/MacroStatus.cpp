// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/MacroStatus.h"

#include <QCoreApplication>

#include <cmath>

namespace mh::ui {
namespace {

QString tr_(const char* source) {
    return QCoreApplication::translate("", source);
}

}  // namespace

QString genderLabel(float gender) {
    // The endpoints are compared EXACTLY, exactly as the reference does
    // (guimodifier.py:155-163). A tolerance here would swallow 0.999, which the
    // reference reports as a split rather than as "male".
    if (gender == 0.0F) return tr_("female");
    if (gender == 1.0F) return tr_("male");
    if (std::fabs(gender - 0.5F) < 0.01F) return tr_("neutral");
    return tr_("%1 % female, %2 % male")
        .arg(static_cast<double>((1.0F - gender) * 100.0F), 0, 'f', 2)
        .arg(static_cast<double>(gender * 100.0F), 0, 'f', 2);
}

QString macroStatusLine(const MacroStats& stats) {
    // `%d` on the age, not `%.2f`: the reference truncates toward zero
    // (guimodifier.py:185), so 25.9 years reads "25".
    return tr_("Gender: %1  Age: %2  Muscle: %3 %  Weight: %4 %  Height: %5 cm")
        .arg(genderLabel(stats.gender))
        .arg(static_cast<int>(stats.ageYears))
        .arg(static_cast<double>(stats.muscle * 100.0F), 0, 'f', 2)
        // 50..150, not 0..100 -- see the header.
        .arg(static_cast<double>(50.0F + 100.0F * stats.weight), 0, 'f', 2)
        .arg(static_cast<double>(stats.heightCm), 0, 'f', 2);
}

}  // namespace mh::ui
