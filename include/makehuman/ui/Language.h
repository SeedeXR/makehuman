// SPDX-License-Identifier: Apache-2.0
//
// Our own design over the shipped CC0 language data, not a translation of the
// reference's `lib/language.py`. What is borrowed is the FILE FORMAT -- a flat
// `source string -> translation` map plus an `__options__` block -- which is
// data, not code. See LICENSING.md section 4.
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QTranslator>

#include <filesystem>

namespace mh::ui {

/// A `QTranslator` backed by `data/languages/<name>.json`.
///
/// **Why a QTranslator rather than a lookup helper.** Qt already owns live
/// language switching: `installTranslator` posts `QEvent::LanguageChange` to
/// every top-level widget, and every `tr()` call in the application routes here
/// with no change at the call site. Writing our own lookup would mean touching
/// all 22 of them and reimplementing the event.
///
/// **Context is ignored on purpose.** Qt keys translations by (class, source),
/// the shipped files are one flat map, and the reference looks strings up by
/// source alone. Honouring context would make every entry miss.
///
/// That flatness is also what lets DATA strings be translated: a slider caption
/// comes from `data/modifiers/*.json`, never passes through `tr()`, and reaches
/// the same map through `QCoreApplication::translate("", caption)`.
class JsonTranslator : public QTranslator {
public:
    using QTranslator::QTranslator;

    /// Loads `<dataDir>/languages/<name>.json`.
    ///
    /// @return false if the file is missing or not a JSON object, leaving the
    ///         translator empty rather than half-loaded.
    [[nodiscard]] bool load(const std::filesystem::path& dataDir, const QString& name);

    /// True when the file's `__options__.rtl` is set -- Arabic, of the twenty
    /// shipped. The caller applies it with `QApplication::setLayoutDirection`;
    /// this type does not touch global state.
    [[nodiscard]] bool isRightToLeft() const noexcept { return rtl_; }

    [[nodiscard]] QString languageName() const { return name_; }

    /// Empty when nothing was loaded, which is what makes `tr()` fall through
    /// to the source string.
    [[nodiscard]] bool isEmpty() const override { return strings_.isEmpty(); }

    [[nodiscard]] QString translate(const char* context, const char* sourceText,
                                    const char* disambiguation = nullptr,
                                    int n                      = -1) const override;

private:
    QHash<QString, QString> strings_;
    QString name_;
    bool rtl_{false};
};

/// The language files present under `<dataDir>/languages`, by stem, sorted.
///
/// `master` is excluded: it is the English source list the other files are
/// generated against, so offering it as a language would present the untouched
/// source strings as a translation of themselves.
[[nodiscard]] QStringList availableLanguages(const std::filesystem::path& dataDir);

}  // namespace mh::ui
