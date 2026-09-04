// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/Language.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace mh::ui {

namespace {

constexpr auto kOptions = "__options__";

QString languageFile(const std::filesystem::path& dataDir, const QString& name) {
    return QString::fromStdString((dataDir / "languages").string()) + QLatin1Char('/') + name +
           QStringLiteral(".json");
}

}  // namespace

bool JsonTranslator::load(const std::filesystem::path& dataDir, const QString& name) {
    strings_.clear();
    name_.clear();
    rtl_ = false;

    QFile f(languageFile(dataDir, name));
    if (!f.open(QIODevice::ReadOnly)) return false;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;

    const QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key() == QLatin1String(kOptions)) {
            rtl_ = it.value().toObject().value(QStringLiteral("rtl")).toBool(false);
            continue;
        }
        // Only string values are translations. A nested object under any other
        // key would otherwise stringify into the UI as "QJsonValue(...)".
        if (it.value().isString()) strings_.insert(it.key(), it.value().toString());
    }
    name_ = name;
    return true;
}

QString JsonTranslator::translate(const char* context, const char* sourceText,
                                  const char* disambiguation, int n) const {
    // Context and plural form are not in this format; see the header.
    Q_UNUSED(context);
    Q_UNUSED(disambiguation);
    Q_UNUSED(n);
    if (sourceText == nullptr) return {};

    const auto it = strings_.constFind(QString::fromUtf8(sourceText));
    // A null QString means "no translation", which is what makes Qt fall back
    // to the source. Returning the source itself here would work too, but it
    // hides a missing entry from anything that checks.
    return it == strings_.constEnd() ? QString{} : *it;
}

QStringList availableLanguages(const std::filesystem::path& dataDir) {
    QDir dir(QString::fromStdString((dataDir / "languages").string()));
    QStringList out;
    for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        const QString stem = fi.completeBaseName();
        if (stem == QStringLiteral("master")) continue;  // the English source list
        out << stem;
    }
    out.sort();
    return out;
}

}  // namespace mh::ui
