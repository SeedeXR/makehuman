// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/TaskRegistry.h"

namespace mh::ui {

bool TaskRegistry::add(QString category) {
    if (category.isEmpty()) return false;
    if (categories_.contains(category, Qt::CaseInsensitive)) return false;
    categories_ << std::move(category);
    return true;
}

QStringList TaskRegistry::categories() const {
    return categories_;
}

}  // namespace mh::ui
