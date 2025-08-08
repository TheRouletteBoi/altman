#pragma once

#include <string_view>
#include "../../components/data.h"

namespace AccountFilters {

inline bool IsBannedLikeStatus(std::string_view s) {
    return s == "Banned" || s == "Warned" || s == "Terminated";
}

inline bool IsAccountUsable(const AccountData &a) {
    return !IsBannedLikeStatus(a.status);
}

}
