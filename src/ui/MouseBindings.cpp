// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/MouseBindings.h"

#include <QMetaType>
#include <QRegularExpression>
#include <QSettings>
#include <QVariant>

#include <algorithm>
#include <array>

namespace mh::ui {
namespace {

struct Named {
    const char* text;
    Qt::MouseButton button;
};

/// The buttons worth naming. Deliberately short: Qt defines 24 more, and a
/// binding to `ExtraButton19` is not something anyone can discover or type.
constexpr std::array<Named, 5> kButtons{{
    {"Left", Qt::LeftButton},
    {"Middle", Qt::MiddleButton},
    {"Right", Qt::RightButton},
    {"Back", Qt::BackButton},
    {"Forward", Qt::ForwardButton},
}};

struct NamedModifier {
    const char* text;
    Qt::KeyboardModifier modifier;
};

/// In the order they are written, so `toText` is stable and matches the order a
/// reader expects from a key sequence.
constexpr std::array<NamedModifier, 4> kModifiers{{
    {"Ctrl", Qt::ControlModifier},
    {"Alt", Qt::AltModifier},
    {"Shift", Qt::ShiftModifier},
    {"Meta", Qt::MetaModifier},
}};

const char* verbKey(NavVerb verb) {
    switch (verb) {
        case NavVerb::Orbit: return "orbit";
        case NavVerb::Pan: return "pan";
        case NavVerb::None: break;
    }
    return "";
}

}  // namespace

MouseBindings::MouseBindings() {
    reset();
}

void MouseBindings::reset() {
    // Pan FIRST: `verbFor` returns the first match, and the hardcoded handler
    // this replaces tested MIDDLE before falling through to orbit.
    entries_ = {
        Entry{NavVerb::Pan, Qt::MiddleButton, Qt::NoModifier},
        Entry{NavVerb::Orbit, Qt::LeftButton, Qt::NoModifier},
    };
}

NavVerb MouseBindings::verbFor(Qt::MouseButtons held, Qt::KeyboardModifiers modifiers) const {
    for (const Entry& e : entries_) {
        if (e.button == Qt::NoButton) continue;
        if ((held & e.button) == 0) continue;
        if (e.modifiers != modifiers) continue;
        return e.verb;
    }
    return NavVerb::None;
}

bool MouseBindings::bind(NavVerb verb, Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
    const bool taken = std::ranges::any_of(entries_, [&](const Entry& e) {
        return e.verb != verb && e.button == button && e.modifiers == modifiers &&
               button != Qt::NoButton;
    });
    if (taken) return false;

    for (Entry& e : entries_) {
        if (e.verb != verb) continue;
        e.button    = button;
        e.modifiers = modifiers;
        return true;
    }
    return false;
}

QString MouseBindings::toText(Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
    const auto named =
        std::ranges::find_if(kButtons, [button](const Named& n) { return n.button == button; });
    if (named == kButtons.end()) return {};

    QStringList parts;
    for (const NamedModifier& m : kModifiers) {
        if ((modifiers & m.modifier) != 0) parts << QString::fromLatin1(m.text);
    }
    parts << QString::fromLatin1(named->text);
    return parts.join(QLatin1Char('+'));
}

bool MouseBindings::fromText(const QString& text, Qt::MouseButton& button,
                             Qt::KeyboardModifiers& modifiers) {
    button    = Qt::NoButton;
    modifiers = Qt::NoModifier;
    if (text.isEmpty()) return false;

    const QStringList parts = text.split(QLatin1Char('+'), Qt::KeepEmptyParts);
    for (qsizetype i = 0; i < parts.size(); ++i) {
        const QString token = parts.at(i).trimmed();
        if (token.isEmpty()) return false;  // "Alt+" names no button

        if (i + 1 == parts.size()) {
            const auto named = std::ranges::find_if(kButtons, [&token](const Named& n) {
                return token.compare(QLatin1String(n.text), Qt::CaseInsensitive) == 0;
            });
            if (named == kButtons.end()) return false;
            button = named->button;
            return true;
        }

        const auto mod = std::ranges::find_if(kModifiers, [&token](const NamedModifier& m) {
            return token.compare(QLatin1String(m.text), Qt::CaseInsensitive) == 0;
        });
        if (mod == kModifiers.end()) return false;
        modifiers |= mod->modifier;
    }
    return false;
}

QStringList MouseBindings::apply(QSettings& settings) {
    QStringList problems;
    settings.beginGroup(QString::fromLatin1(kGroup));
    const QStringList keys = settings.childKeys();

    // What each verb WILL hold, seeded with the shipped gestures, so a
    // collision is judged on the finished table rather than on whichever
    // override happened to be read first.
    std::vector<Entry> wanted = entries_;
    const auto find           = [&wanted](NavVerb verb) {
        return std::ranges::find_if(wanted, [verb](const Entry& e) { return e.verb == verb; });
    };

    // All-digit strings are the reference's format. Guarded on the TEXT, not
    // just the QVariant type: QSettings only knows a value's type when it wrote
    // it itself in this process, and a migrated or hand-edited file arrives as
    // strings -- measured during the shortcut chunk, where a type-only check
    // passed its unit test and let `5` through in the application.
    static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));

    for (const QString& key : keys) {
        const auto verb = key == QLatin1String("orbit") ? NavVerb::Orbit
                          : key == QLatin1String("pan") ? NavVerb::Pan
                                                        : NavVerb::None;
        if (verb == NavVerb::None) {
            problems += QStringLiteral("%1: no camera action has that name, ignoring").arg(key);
            continue;
        }

        const QVariant raw = settings.value(key);
        const QString text = raw.toString();
        if (raw.typeId() == QMetaType::Int || raw.typeId() == QMetaType::LongLong ||
            digits.match(text).hasMatch()) {
            problems += QStringLiteral(
                            "%1: %2 is a raw Qt button mask, not a gesture -- "
                            "write it as text, e.g. \"Middle\"")
                            .arg(key, text);
            continue;
        }

        // Empty is a deliberate "no gesture for this", which the reference
        // cannot express: (0, 0) is a valid modifier-and-button pair there.
        if (text.isEmpty()) {
            find(verb)->button    = Qt::NoButton;
            find(verb)->modifiers = Qt::NoModifier;
            continue;
        }

        Qt::MouseButton button{};
        Qt::KeyboardModifiers mods{};
        if (!fromText(text, button, mods)) {
            problems += QStringLiteral("%1: \"%2\" is not a mouse gesture, keeping the default")
                            .arg(key, text);
            continue;
        }
        find(verb)->button    = button;
        find(verb)->modifiers = mods;
    }
    settings.endGroup();

    // Collisions last, on the finished table.
    for (size_t i = 0; i < wanted.size(); ++i) {
        for (size_t j = i + 1; j < wanted.size(); ++j) {
            if (wanted[i].button == Qt::NoButton) continue;
            if (wanted[i].button != wanted[j].button) continue;
            if (wanted[i].modifiers != wanted[j].modifiers) continue;
            QStringList pair{QString::fromLatin1(verbKey(wanted[i].verb)),
                             QString::fromLatin1(verbKey(wanted[j].verb))};
            pair.sort();
            problems +=
                QStringLiteral("%1 and %2 both want \"%3\"; keeping both defaults")
                    .arg(pair.at(0), pair.at(1), toText(wanted[i].button, wanted[i].modifiers));
            return problems;  // nothing applied: half a table is worse than none
        }
    }

    entries_ = std::move(wanted);
    return problems;
}

void MouseBindings::save(QSettings& settings) const {
    const MouseBindings shipped;
    settings.beginGroup(QString::fromLatin1(kGroup));
    for (const Entry& e : entries_) {
        const auto same   = std::ranges::find_if(shipped.entries_, [&e](const Entry& d) {
            return d.verb == e.verb && d.button == e.button && d.modifiers == e.modifiers;
        });
        const QString key = QString::fromLatin1(verbKey(e.verb));
        if (same != shipped.entries_.end()) {
            // Back on the shipped gesture: remove the override rather than
            // record it, so a later release changing it still reaches the user.
            settings.remove(key);
            continue;
        }
        settings.setValue(key, toText(e.button, e.modifiers));
    }
    settings.endGroup();
}

void MouseBindings::reset(QSettings& settings) {
    reset();
    settings.remove(QString::fromLatin1(kGroup));
}

}  // namespace mh::ui
