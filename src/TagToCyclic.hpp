#pragma once

#include "TagSystem.hpp"
#include "CyclicTagSystem.hpp"

namespace Rule110 {

class TagToCyclicConverter {
public:
    static CyclicTagSystem convert(const TagSystem& ts);
};

} // namespace Rule110
