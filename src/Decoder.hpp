#pragma once

#include "Verifier.hpp"
#include <vector>

namespace Rule110 {

struct TMConfig {
    int state = -1;
    std::vector<int> tape;
    int head_pos = 0;
    bool valid = false;
};

class Decoder {
public:
    // CTS tape → tag word. Returns empty vector if invalid.
    static std::vector<int> cts_to_tag(const std::vector<int>& cts_tape, int phi_size);

    // Clean tag word → TM configuration.
    // m = num_states, t = num_symbols, s = t + 2 (deletion number).
    // Returns invalid config if word isn't a clean [H_γ]^a [L_γ]^b [R_γ]^c.
    static TMConfig tag_to_tm(const std::vector<int>& tag_word, int m, int t);

    // Full pipeline round-trip: TM → Tag → CTS → decode back → verify match.
    // Also runs CTS step-by-step, decoding at tag-step boundaries to
    // recover intermediate TM states and compare with TM execution history.
    static bool verify_decode(const TMTestCase& test, bool verbose);
    static int run_all_decode_tests(bool verbose = true);
};

} // namespace Rule110
