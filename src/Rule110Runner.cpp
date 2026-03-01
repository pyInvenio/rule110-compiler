#include "Rule110Runner.hpp"
#include <algorithm>
#include <iostream>

namespace Rule110 {

// Packing: int vector -> uint64_t vector
// We map tape[0] to word[0] bit 0 (LSB). 
// tape[63] to word[0] bit 63 (MSB).
Rule110Runner::State Rule110Runner::pack(const std::vector<int>& tape) {
    size_t num_words = (tape.size() + 63) / 64;
    State packed(num_words, 0);
    
    for (size_t i = 0; i < tape.size(); ++i) {
        if (tape[i]) {
            size_t word_idx = i / 64;
            size_t bit_idx = i % 64;
            packed[word_idx] |= (1ULL << bit_idx);
        }
    }
    return packed;
}

std::vector<Rule110Runner::State> Rule110Runner::run(const State& initial_state, int generations, bool save_history) {
    std::vector<State> history;
    if (save_history) {
        history.reserve(generations + 1);
        history.push_back(initial_state);
    }

    State current = initial_state;
    for (int i = 0; i < generations; ++i) {
        current = next_generation(current);
        if (save_history) {
            history.push_back(current);
        }
    }
    
    if (!save_history) {
        history.push_back(current); // Return at least the final state
    }
    return history;
}

Rule110Runner::State Rule110Runner::next_generation(const State& current) {
    size_t n = current.size();
    State next(n);

    for (size_t i = 0; i < n; ++i) {
        uint64_t center = current[i];
        
        // Calculate Left neighbor bits (shift center UP 1)
        // The LSB (bit 0) needs the MSB (bit 63) of the PREVIOUS word.
        uint64_t prev_word = current[(i == 0) ? n - 1 : i - 1];
        uint64_t left = (center << 1) | (prev_word >> 63);

        // Calculate Right neighbor bits (shift center DOWN 1)
        // The MSB (bit 63) needs the LSB (bit 0) of the NEXT word.
        uint64_t next_word = current[(i == n - 1) ? 0 : i + 1];
        uint64_t right = (center >> 1) | (next_word << 63);

        // Rule 110 Boolean Formula:
        // Center' = (Center ^ Right) | (~Left & Right)
        // 
        // Proof:
        // 111 (0): (1^1)|(0&1) = 0|0 = 0
        // 110 (1): (1^0)|(0&0) = 1|0 = 1
        // 101 (1): (0^1)|(0&1) = 1|0 = 1
        // 100 (0): (0^0)|(0&0) = 0|0 = 0
        // 011 (1): (1^1)|(1&1) = 0|1 = 1
        // 010 (1): (1^0)|(1&0) = 1|0 = 1
        // 001 (1): (0^1)|(1&1) = 1|1 = 1
        // 000 (0): (0^0)|(1&0) = 0|0 = 0
        
        next[i] = (center ^ right) | (~left & right);
    }
    return next;
}

static inline int get_bit(const Rule110Runner::State& s, size_t idx) {
    return (s[idx / 64] >> (idx % 64)) & 1;
}

bool Rule110Runner::check_halt(const State& current, size_t start_bit, size_t end_bit) {
    // Halt pattern: 0,1,1,0,1,0,0,1,1,0,1,0,0,0 (14 bits)
    static const int pattern[] = {0,1,1,0,1,0,0,1,1,0,1,0,0,0};
    size_t total_bits = current.size() * 64;
    if (end_bit > total_bits) end_bit = total_bits;
    if (start_bit + 14 > end_bit) return false;

    for (size_t i = start_bit; i <= end_bit - 14; i++) {
        bool match = true;
        for (int j = 0; j < 14; j++) {
            if (get_bit(current, i + j) != pattern[j]) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

bool Rule110Runner::check_halt(const State& current) {
    return check_halt(current, 0, current.size() * 64);
}

void Rule110Runner::print_window(const State& state, int start_bit, int width) {
    for (int i = 0; i < width; ++i) {
        int global_idx = start_bit + i;
        size_t word_idx = (global_idx / 64) % state.size();
        size_t bit_idx = global_idx % 64;
        
        bool on = (state[word_idx] >> bit_idx) & 1;
        std::cout << (on ? "█" : " ");
    }
    std::cout << "\n";
}

void Rule110Runner::save_to_ppm(const std::vector<State>& history, const std::string& filename, int width_limit) {
    if (history.empty()) return;

    size_t total_bits = history[0].size() * 64;
    size_t width = (width_limit > 0 && width_limit < total_bits) ? width_limit : total_bits;
    size_t height = history.size();

    std::ofstream ofs(filename);
    ofs << "P3\n" << width << " " << height << "\n255\n";

    for (const auto& row : history) {
        for (size_t i = 0; i < width; ++i) {
            size_t word_idx = i / 64;
            size_t bit_idx = i % 64;
            bool on = (row[word_idx] >> bit_idx) & 1;
            
            if (on) ofs << "0 0 0 ";
            else ofs << "255 255 255 ";
        }
        ofs << "\n";
    }
}

} // namespace Rule110