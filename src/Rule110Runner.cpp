#include "Rule110Runner.hpp"
#include <algorithm>
#include <iostream>

namespace Rule110 {

Rule110Runner::State Rule110Runner::pack(const std::vector<int>& tape) {
    size_t num_words = (tape.size() + 63) / 64;
    State packed(num_words, 0);

    for (size_t i = 0; i < tape.size(); ++i) {
        if (tape[i]) {
            packed[i / 64] |= (1ULL << (i % 64));
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
        history.push_back(current);
    }
    return history;
}

// Rule 110: Center' = (Center ^ Right) | (~Left & Right)
// Core implementation: writes next generation of src into dst.
void Rule110Runner::next_generation(const State& current, State& next) {
    size_t n = current.size();
    const uint64_t* __restrict__ s = current.data();
    uint64_t* __restrict__ d = next.data();

    if (n == 0) return;
    if (n == 1) {
        uint64_t c = s[0];
        uint64_t l = (c << 1) | (c >> 63);
        uint64_t r = (c >> 1) | (c << 63);
        d[0] = (c ^ r) | (~l & r);
        return;
    }

    // First word: left neighbor wraps from last word
    {
        uint64_t c = s[0];
        uint64_t l = (c << 1) | (s[n - 1] >> 63);
        uint64_t r = (c >> 1) | (s[1] << 63);
        d[0] = (c ^ r) | (~l & r);
    }

    // Inner loop: no boundary checks, unrolled 4x
    size_t i = 1;
    for (; i + 4 < n; i += 4) {
        uint64_t c0 = s[i], c1 = s[i+1], c2 = s[i+2], c3 = s[i+3];
        uint64_t p = s[i - 1];
        uint64_t nx = s[i + 4];

        uint64_t l0 = (c0 << 1) | (p  >> 63);
        uint64_t l1 = (c1 << 1) | (c0 >> 63);
        uint64_t l2 = (c2 << 1) | (c1 >> 63);
        uint64_t l3 = (c3 << 1) | (c2 >> 63);

        uint64_t r0 = (c0 >> 1) | (c1 << 63);
        uint64_t r1 = (c1 >> 1) | (c2 << 63);
        uint64_t r2 = (c2 >> 1) | (c3 << 63);
        uint64_t r3 = (c3 >> 1) | (nx << 63);

        d[i]   = (c0 ^ r0) | (~l0 & r0);
        d[i+1] = (c1 ^ r1) | (~l1 & r1);
        d[i+2] = (c2 ^ r2) | (~l2 & r2);
        d[i+3] = (c3 ^ r3) | (~l3 & r3);
    }

    // Remaining inner words
    for (; i < n - 1; i++) {
        uint64_t c = s[i];
        uint64_t l = (c << 1) | (s[i - 1] >> 63);
        uint64_t r = (c >> 1) | (s[i + 1] << 63);
        d[i] = (c ^ r) | (~l & r);
    }

    // Last word: right neighbor wraps to first word
    {
        uint64_t c = s[n - 1];
        uint64_t l = (c << 1) | (s[n - 2] >> 63);
        uint64_t r = (c >> 1) | (s[0] << 63);
        d[n - 1] = (c ^ r) | (~l & r);
    }
}

Rule110Runner::State Rule110Runner::next_generation(const State& current) {
    State next(current.size());
    next_generation(current, next);
    return next;
}

static inline int get_bit(const Rule110Runner::State& s, size_t idx) {
    return (s[idx / 64] >> (idx % 64)) & 1;
}

bool Rule110Runner::check_halt(const State& current, size_t start_bit, size_t end_bit) {
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
        std::cout << (on ? "\xe2\x96\x88" : " ");
    }
    std::cout << "\n";
}

void Rule110Runner::save_to_ppm(const std::vector<State>& history, const std::string& filename, int width_limit) {
    if (history.empty()) return;

    size_t total_bits = history[0].size() * 64;
    size_t width = (width_limit > 0 && (size_t)width_limit < total_bits) ? width_limit : total_bits;
    size_t height = history.size();

    std::ofstream ofs(filename);
    ofs << "P3\n" << width << " " << height << "\n255\n";

    for (const auto& row : history) {
        for (size_t i = 0; i < width; ++i) {
            bool on = (row[i / 64] >> (i % 64)) & 1;
            if (on) ofs << "0 0 0 ";
            else ofs << "255 255 255 ";
        }
        ofs << "\n";
    }
}

} // namespace Rule110
