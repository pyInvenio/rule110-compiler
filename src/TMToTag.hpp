#pragma once

#include "TuringMachine.hpp"
#include "TagSystem.hpp"

namespace Rule110 {

class TMToTagConverter {
public:
    static TagSystem convert(const TuringMachine& tm);
};

} // namespace Rule110
