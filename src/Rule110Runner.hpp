#pragma once

#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <cstdint>

namespace Rule110 {

class Rule110Runner {
public:
    // Bit-packed state: each uint64_t holds 64 cells.
    // Bit 0 is Left, Bit 63 is Right (within the word).
    using State = std::vector<uint64_t>;

    // Convert integer vector (0/1) to bit-packed state
    static State pack(const std::vector<int>& tape);
    
    // Run the simulation (returns history if requested, but careful with RAM)
    // If save_history is false, returns only the final state in a vector of size 1.
    static std::vector<State> run(const State& initial_state, int generations, bool save_history = false);

    // Evolve a single generation using 64-bit bitwise ops
    static State next_generation(const State& current_state);

    // In-place: write next generation of src into dst (must be same size, pre-allocated)
    static void next_generation(const State& src, State& dst);

    // Check if the halt pattern (01101001101000) exists in a region of the tape
    static bool check_halt(const State& current_state, size_t start_bit, size_t end_bit);
    static bool check_halt(const State& current_state); // full tape

    // Save to PPM image file
    static void save_to_ppm(const std::vector<State>& history, const std::string& filename, int width_limit = 0);
    
    // Helper to print a specific window
    static void print_window(const State& state, int start_bit, int width);
};

} // namespace Rule110
