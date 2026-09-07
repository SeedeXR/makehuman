// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Which mouse drag orbits and which pans, and how that is stored.
//
// Same shape as `ui::shortcuts`, and for the same reason: the reference writes
// `'%d %d %s\n' % (modifier, buttonMask, methodName)` into `mouse.ini`
// (`legacy/python/core/mhmain.py:1027`) -- raw Qt enum ints again, unreadable
// and only meaningful to the build that wrote them. We store `"Middle"` and
// `"Alt+Left"`.
//
// The dispatch was hardcoded in `ViewportWidget::mouseMoveEvent`: middle drag
// pans, anything else orbits. That is not configurable and, more quietly, it is
// not testable either -- the rule lived inside an event handler.
#include "makehuman/ui/MouseBindings.h"

#include <catch2/catch_test_macros.hpp>

#include <QSettings>
#include <QTemporaryDir>

using namespace mh::ui;

namespace {

QString iniIn(const QTemporaryDir& dir) {
    return dir.filePath(QStringLiteral("m.ini"));
}

}  // namespace

TEST_CASE("the shipped defaults are left-orbit and middle-pan", "[ui][mouse]") {
    const MouseBindings b;
    CHECK(b.verbFor(Qt::LeftButton, Qt::NoModifier) == NavVerb::Orbit);
    CHECK(b.verbFor(Qt::MiddleButton, Qt::NoModifier) == NavVerb::Pan);
    // Right is deliberately unbound: the reference zooms with it
    // (`mhmain.py:198`), we zoom on the wheel, and binding it to nothing is
    // honest about that rather than inventing a third gesture.
    CHECK(b.verbFor(Qt::RightButton, Qt::NoModifier) == NavVerb::None);
    CHECK(b.verbFor(Qt::NoButton, Qt::NoModifier) == NavVerb::None);
}

TEST_CASE("a held modifier must match exactly", "[ui][mouse]") {
    // Exact, not "at least". A plain-Left binding firing while the user holds
    // Alt+Left would steal a gesture a DCC user expects to mean something else,
    // and there would be no way to bind Alt+Left separately afterwards.
    const MouseBindings b;
    CHECK(b.verbFor(Qt::LeftButton, Qt::AltModifier) == NavVerb::None);
    CHECK(b.verbFor(Qt::MiddleButton, Qt::ShiftModifier) == NavVerb::None);
}

TEST_CASE("pan wins when both its button and orbit's are held", "[ui][mouse]") {
    // Deterministic, and it preserves what the hardcoded handler did: it tested
    // MIDDLE first and fell through to orbit. Without a fixed order the result
    // would depend on map iteration.
    const MouseBindings b;
    CHECK(b.verbFor(Qt::LeftButton | Qt::MiddleButton, Qt::NoModifier) == NavVerb::Pan);
}

TEST_CASE("a gesture round-trips through symbolic text", "[ui][mouse]") {
    CHECK(MouseBindings::toText(Qt::MiddleButton, Qt::NoModifier) == QStringLiteral("Middle"));
    CHECK(MouseBindings::toText(Qt::LeftButton, Qt::AltModifier) == QStringLiteral("Alt+Left"));

    Qt::MouseButton button{};
    Qt::KeyboardModifiers mods{};
    REQUIRE(MouseBindings::fromText(QStringLiteral("Alt+Left"), button, mods));
    CHECK(button == Qt::LeftButton);
    CHECK(mods == Qt::AltModifier);

    REQUIRE(MouseBindings::fromText(QStringLiteral("Right"), button, mods));
    CHECK(button == Qt::RightButton);
    CHECK(mods == Qt::NoModifier);

    // Names no button at all.
    CHECK_FALSE(MouseBindings::fromText(QStringLiteral("Alt+"), button, mods));
    CHECK_FALSE(MouseBindings::fromText(QStringLiteral("Sideways"), button, mods));
    CHECK_FALSE(MouseBindings::fromText(QString(), button, mods));
}

TEST_CASE("an override is applied and saved symbolically", "[ui][mouse]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
        QSettings s(iniIn(dir), QSettings::IniFormat);
        s.setValue(QStringLiteral("mouse/pan"), QStringLiteral("Right"));
    }
    MouseBindings b;
    QSettings s(iniIn(dir), QSettings::IniFormat);
    CHECK(b.apply(s).isEmpty());
    CHECK(b.verbFor(Qt::RightButton, Qt::NoModifier) == NavVerb::Pan);
    // The old gesture stops panning -- a rebind moves it, it does not add one.
    CHECK(b.verbFor(Qt::MiddleButton, Qt::NoModifier) == NavVerb::None);
    // ...and orbit is untouched.
    CHECK(b.verbFor(Qt::LeftButton, Qt::NoModifier) == NavVerb::Orbit);

    b.save(s);
    s.sync();
    CHECK(s.value(QStringLiteral("mouse/pan")).toString() == QStringLiteral("Right"));
    // Only the override is written, so a changed default still reaches the user
    // -- the same rule the shortcuts use, and the reason neither needs the
    // reference's version sentinel.
    CHECK_FALSE(s.contains(QStringLiteral("mouse/orbit")));
}

TEST_CASE("a raw Qt button mask is refused", "[ui][mouse]") {
    // What a mouse.ini migrated from the reference holds: Qt::LeftButton is 1,
    // MiddleButton 4. Written as text by a hand edit or a converter, so the
    // guard has to read the STRING -- the lesson from the shortcut chunk, where
    // a type-only check passed its unit test and failed in the application.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = iniIn(dir);
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("[mouse]\norbit=1\npan=4\n");
    }
    MouseBindings b;
    QSettings s(path, QSettings::IniFormat);
    const QStringList problems = b.apply(s);
    CHECK(problems.size() == 2);
    INFO(problems.join(QStringLiteral(" | ")).toStdString());
    CHECK(problems.join(QString()).contains(QStringLiteral("raw Qt")));
    // Defaults survive.
    CHECK(b.verbFor(Qt::LeftButton, Qt::NoModifier) == NavVerb::Orbit);
    CHECK(b.verbFor(Qt::MiddleButton, Qt::NoModifier) == NavVerb::Pan);
}

TEST_CASE("unknown verbs and unparseable gestures are reported", "[ui][mouse]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
        QSettings s(iniIn(dir), QSettings::IniFormat);
        s.setValue(QStringLiteral("mouse/orbit"), QStringLiteral("Sideways"));
        s.setValue(QStringLiteral("mouse/teleport"), QStringLiteral("Left"));
    }
    MouseBindings b;
    QSettings s(iniIn(dir), QSettings::IniFormat);
    const QStringList problems = b.apply(s);
    CHECK(problems.size() == 2);
    const QString all = problems.join(QStringLiteral(" | "));
    INFO(all.toStdString());
    CHECK(all.contains(QStringLiteral("teleport")));
    CHECK(all.contains(QStringLiteral("Sideways")));
    CHECK(b.verbFor(Qt::LeftButton, Qt::NoModifier) == NavVerb::Orbit);
}

TEST_CASE("two verbs cannot share one gesture", "[ui][mouse]") {
    // One of them would be unreachable and nothing would say which. Same
    // refusal the shortcuts make for an ambiguous key sequence.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
        QSettings s(iniIn(dir), QSettings::IniFormat);
        s.setValue(QStringLiteral("mouse/pan"), QStringLiteral("Left"));
    }
    MouseBindings b;
    QSettings s(iniIn(dir), QSettings::IniFormat);
    const QStringList problems = b.apply(s);
    REQUIRE(problems.size() == 1);
    INFO(problems.at(0).toStdString());
    CHECK(problems.at(0).contains(QStringLiteral("orbit")));
    CHECK(problems.at(0).contains(QStringLiteral("pan")));
    // Both keep their defaults: the collision is refused, not half-applied.
    CHECK(b.verbFor(Qt::LeftButton, Qt::NoModifier) == NavVerb::Orbit);
    CHECK(b.verbFor(Qt::MiddleButton, Qt::NoModifier) == NavVerb::Pan);
}

TEST_CASE("an empty value unbinds a verb deliberately", "[ui][mouse]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
        QSettings s(iniIn(dir), QSettings::IniFormat);
        s.setValue(QStringLiteral("mouse/pan"), QString());
    }
    MouseBindings b;
    QSettings s(iniIn(dir), QSettings::IniFormat);
    CHECK(b.apply(s).isEmpty());
    CHECK(b.verbFor(Qt::MiddleButton, Qt::NoModifier) == NavVerb::None);
    CHECK(b.verbFor(Qt::LeftButton, Qt::NoModifier) == NavVerb::Orbit);
}

TEST_CASE("reset puts the shipped gestures back", "[ui][mouse]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    MouseBindings b;
    QSettings s(iniIn(dir), QSettings::IniFormat);
    Qt::MouseButton button{};
    Qt::KeyboardModifiers mods{};
    REQUIRE(MouseBindings::fromText(QStringLiteral("Right"), button, mods));
    REQUIRE(b.bind(NavVerb::Pan, button, mods));
    REQUIRE(b.verbFor(Qt::RightButton, Qt::NoModifier) == NavVerb::Pan);
    b.save(s);

    b.reset(s);
    CHECK(b.verbFor(Qt::MiddleButton, Qt::NoModifier) == NavVerb::Pan);
    CHECK(b.verbFor(Qt::RightButton, Qt::NoModifier) == NavVerb::None);
    s.sync();
    s.beginGroup(QString::fromLatin1(MouseBindings::kGroup));
    CHECK(s.childKeys().isEmpty());
    s.endGroup();
}
