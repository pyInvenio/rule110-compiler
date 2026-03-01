#pragma once

#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <map>

#include "Rule110Runner.hpp" // Include for Rule110Runner::State

namespace Rule110 {

struct GliderModel {
    std::string name;
    std::vector<int> base_pattern;
    std::vector<std::vector<int>> phases; // All valid phase variations
};

class GliderClassifier {
public:
    // Helper to get a specific bit from the packed state
    static int get_bit(const Rule110Runner::State& packed_state, size_t global_idx);

    // Helper to extract a window of bits from packed state
    static std::vector<int> extract_window(const Rule110Runner::State& packed_tape, size_t start_idx, size_t length);

    // Generate phases for a given block pattern
    static std::vector<std::vector<int>> generate_phases(const std::vector<int>& base_pattern, int generations = 40);

    // Decodes the packed tape using a registry of gliders
    static std::string classify_packed_multi(const Rule110Runner::State& packed_tape, 
                                             const std::vector<GliderModel>& known_gliders);

    // Legacy support: Single pattern decode
    static std::string classify(const std::vector<int>& tape, 
                                const std::vector<int>& pattern_zero, 
                                const std::vector<int>& pattern_one);
};

} // namespace Rule110