#pragma once

#include "CyclicTagSystem.hpp"
#include <vector>
#include <string>

namespace Rule110 {

struct Rule110State {
    std::vector<int> left_periodic;
    std::vector<int> central_part;
    std::vector<int> right_periodic;
};

class Rule110Compiler {
public:
    static Rule110State compile(const CyclicTagSystem& cts);
};

} // namespace Rule110
