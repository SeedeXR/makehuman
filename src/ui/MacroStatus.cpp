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

QString macroStatusLine(const MacroStats& stats, Units units, Weight weight) {
    const bool imperial = units == Units::Imperial;
    const float height  = imperial ? stats.heightCm * kCentimetresToInches : stats.heightCm;

    // A percentage has no units, so `units` must not touch it. The reference
    // converts inside the real-weight branch only (`guimodifier.py:176-183`),
    // and multiplying a percentage by 2.20462 is the obvious way to get this
    // wrong.
    const bool real       = weight == Weight::Real;
    const float massUnits = imperial ? stats.weightKg * kKilogramsToPounds : stats.weightKg;
    const double shownWeight =
        static_cast<double>(real ? massUnits : 50.0F + (100.0F * stats.weight));
    const QString weightUnits = real ? (imperial ? tr_("lb.") : tr_("kg")) : QStringLiteral("%");

    // `%d` on the age, not `%.2f`: the reference truncates toward zero
    // (guimodifier.py:185), so 25.9 years reads "25". The unit suffix is "in."
    // WITH the full stop, which is what the reference writes (:179) -- it reads
    // as an abbreviation rather than as the word "in".
    return tr_("Gender: %1  Age: %2  Muscle: %3 %  Weight: %4 %5  Height: %6 %7")
        .arg(genderLabel(stats.gender))
        .arg(static_cast<int>(stats.ageYears))
        .arg(static_cast<double>(stats.muscle * 100.0F), 0, 'f', 2)
        // 50..150 as a percentage -- see the header -- or kilograms.
        .arg(shownWeight, 0, 'f', 2)
        .arg(weightUnits)
        .arg(static_cast<double>(height), 0, 'f', 2)
        .arg(imperial ? tr_("in.") : tr_("cm"));
}

}  // namespace mh::ui
