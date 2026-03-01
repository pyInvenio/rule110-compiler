#pragma once

#include "TuringMachine.hpp"
#include "TMToTag.hpp"
#include "TagToCyclic.hpp"
#include "Rule110Compiler.hpp"
#include "Rule110Runner.hpp"
#include <vector>
#include <string>
#include <iostream>

namespace Rule110 {

struct TMTestCase {
    std::string name;
    int num_states;
    int num_symbols;

    struct TransitionDef {
        int state, symbol, next_state, write_symbol;
        Move move;
    };
    std::vector<TransitionDef> transitions;

    int initial_state;
    TuringMachine::Tape initial_tape;
    int head_pos;

    TuringMachine::Tape expected_tape;
    int expected_steps;
};

class Verifier {
public:
    // TM Tests:
    static bool verify_tm(const TMTestCase& test, bool verbose = true);
    static int run_all_tm_tests(bool verbose = true);
    static std::vector<TMTestCase> get_tm_tests();

    // Tag Tests (reuses TM test cases):
    static bool verify_tag(const TMTestCase& test, bool verbose = true);
    static int run_all_tag_tests(bool verbose = true);

    // CTS Tests (reuses TM test cases):
    static bool verify_cts(const TMTestCase& test, bool verbose = true);
    static int run_all_cts_tests(bool verbose = true);

    // R110 Tests:
    static bool verify_r110(const TMTestCase& test, bool verbose = true);
    static int run_all_r110_tests(bool verbose = true);
};

} // namespace Rule110