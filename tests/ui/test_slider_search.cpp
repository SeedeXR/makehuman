// SPDX-License-Identifier: Apache-2.0
//
// Searching the sliders by the word a user actually has.
//
// This exists because of a measured failure rather than a hypothesis: the
// project's OWN OWNER did not know several of these sliders were there. 291
// sliders sit behind 7 tabs, and the search matched the LABEL only, so
// "chubby", "fat", "abs", "six pack" and "toned" found nothing at all -- the
// shipped labels read Weight, Stomach tone and Muscle.
//
// The panel is Apache-2.0 and knows nothing about modifiers, so the synonyms
// arrive on `SliderSpec::keywords` exactly as the labels do. That is the seam
// these tests drive: specs in, visibility out.
#include "makehuman/foundation/SliderSpec.h"
#include "makehuman/ui/ModifierPanel.h"

#include <catch2/catch_test_macros.hpp>

#include <QComboBox>
#include <QSlider>
#include <QWidget>

#include <span>
#include <string>
#include <vector>

using namespace mh;

namespace {

/// Three sliders whose LABELS are exactly the words that failed to be found.
std::vector<foundation::TaskViewSpec> shipped() {
    foundation::SliderSpec weight;
    weight.id       = "macrodetails-universal/Weight";
    weight.label    = "Weight";
    weight.keywords = "fat chubby heavy thin skinny slim overweight";

    foundation::SliderSpec tone;
    tone.id       = "stomach/stomach-tone-decr|incr";
    tone.label    = "Stomach tone";
    tone.keywords = "abs sixpack six-pack belly tummy gut core";

    foundation::SliderSpec nose;
    nose.id    = "nose/nose-scale-depth-decr|incr";
    nose.label = "Nose depth";

    foundation::SliderSection body;
    body.name    = "Body";
    body.sliders = {weight, tone, nose};

    foundation::TaskViewSpec view;
    view.name     = "Main";
    view.sections = {body};
    return {view};
}

/// How many sliders the panel is showing.
int visible(ui::ModifierPanel& panel) {
    int n = 0;
    for (const QSlider* s : panel.findChildren<QSlider*>()) {
        // A row is hidden by hiding its CONTAINER, so the slider itself
        // reports hidden only once the panel is shown; walk up to the row.
        const QWidget* row = s->parentWidget();
        if (row != nullptr && !row->isHidden()) ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("a search finds a slider by a word that is not its label", "[sliders][search]") {
    const auto views = shipped();
    ui::ModifierPanel panel(views);

    // The control first: with no needle everything is showing, so a later
    // count of 1 means something was HIDDEN rather than never built.
    panel.filter(QString{});
    REQUIRE(visible(panel) == 3);

    // The five words the owner actually typed. None of them appears in any
    // label, and before keywords every one of these returned an empty panel.
    for (const char* needle : {"chubby", "fat", "overweight"}) {
        INFO("searching " << needle);
        panel.filter(QString::fromLatin1(needle));
        CHECK(visible(panel) == 1);
    }
    for (const char* needle : {"abs", "six-pack", "tummy"}) {
        INFO("searching " << needle);
        panel.filter(QString::fromLatin1(needle));
        CHECK(visible(panel) == 1);
    }
}

TEST_CASE("keywords do not make the search match everything", "[sliders][search]") {
    const auto views = shipped();
    ui::ModifierPanel panel(views);

    // A synonym list that grows into a thesaurus matches every slider, which
    // is the same as matching none. The nose has no keywords and must stay
    // hidden for a body search.
    panel.filter(QStringLiteral("chubby"));
    CHECK(visible(panel) == 1);

    // And a word in nobody's label, id or keywords still finds nothing.
    panel.filter(QStringLiteral("zzzz"));
    CHECK(visible(panel) == 0);

    // The label still works -- keywords are an addition, not a replacement.
    panel.filter(QStringLiteral("Nose"));
    CHECK(visible(panel) == 1);
}

// The preset chooser: the "one click" half of combination presets.
//
// `--preset` already applies a recipe from the command line, and that is not
// what the complaint was about -- it was that finding "six-pack" meant knowing
// Muscle, Weight and Stomach tone are the three sliders involved.
TEST_CASE("the preset chooser offers the recipes and reports the pick", "[sliders][preset]") {
    const auto views = shipped();
    ui::ModifierPanel panel(views);

    auto* box = panel.findChild<QComboBox*>(QStringLiteral("modifiers.presets"));
    REQUIRE(box != nullptr);
    // Nothing offered yet, so no empty control is shown: a chooser with no
    // choices is a question the user cannot answer.
    CHECK_FALSE(box->isVisibleTo(&panel));

    foundation::SliderPreset six;
    six.name   = "Six-pack";
    six.values = {{"macrodetails-universal/Muscle", 0.85F},
                  {"stomach/stomach-tone-decr|incr", 0.9F}};
    foundation::SliderPreset slim;
    slim.name   = "Slim";
    slim.values = {{"macrodetails-universal/Weight", 0.22F}};
    const std::vector<foundation::SliderPreset> presets{six, slim};
    panel.setPresets(presets);

    CHECK(box->isVisibleTo(&panel));
    // Two recipes plus the "Preset…" label that heads the list.
    REQUIRE(box->count() == 3);
    CHECK(box->itemText(1) == QStringLiteral("Six-pack"));

    QStringList picked;
    QObject::connect(&panel, &ui::ModifierPanel::presetChosen, &panel,
                     [&picked](const QString& name) { picked.append(name); });

    // Index 0 is the label and must do NOTHING -- it is the resting state the
    // combo returns to, so firing on it would re-apply a preset every time.
    box->activated(0);
    CHECK(picked.isEmpty());

    box->activated(1);
    REQUIRE(picked.size() == 1);
    CHECK(picked.first() == QStringLiteral("Six-pack"));

    // ...and the combo goes back to the label rather than sitting on a name,
    // which would claim the body still matches a preset that the next slider
    // drag invalidates.
    CHECK(box->currentIndex() == 0);

    // The SAME preset again must fire again: after moving a slider by hand,
    // re-picking it is how a user puts the look back. `currentIndexChanged`
    // would have swallowed this.
    box->activated(1);
    CHECK(picked.size() == 2);
}
