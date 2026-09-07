// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/TaskRegistry.h"

namespace mh::ui {

bool TaskRegistry::add(QString category, QString title) {
    if (category.isEmpty()) return false;
    if (categories_.contains(category, Qt::CaseInsensitive)) return false;
    if (!title.isEmpty()) titles_.insert(category, std::move(title));
    categories_ << std::move(category);
    return true;
}

QStringList TaskRegistry::categories() const {
    return categories_;
}

QString TaskRegistry::title(const QString& category) const {
    return titles_.value(category, category);
}

}  // namespace mh::ui
