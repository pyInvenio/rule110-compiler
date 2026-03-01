#include "Verifier.hpp"
#include "Rule110Compiler.hpp"
#include "Rule110Runner.hpp"

namespace Rule110 {

static bool tapes_equal(const TuringMachine::Tape& a, const TuringMachine::Tape& b) {
    auto last_nonzero = [](const TuringMachine::Tape& t) -> int {
        for (int i = t.size() - 1; i >= 0; i--)
            if (t[i] != 0) return i;
        return -1;
    };
    int la = last_nonzero(a), lb = last_nonzero(b);
    if (la != lb) return false;
    for (int i = 0; i <= la; i++)
        if (a[i] != b[i]) return false;
    return true;
}

static TuringMachine build_tm(const TMTestCase& test) {
    TuringMachine tm(test.num_states, test.num_symbols);
    for (const auto& t : test.transitions)
        tm.add_transition(t.state, t.symbol, t.next_state, t.write_symbol, t.move);
    tm.set_initial_state(test.initial_state);
    tm.set_tape(test.initial_tape, test.head_pos);
    return tm;
}

static int get_bit(const Rule110Runner::State& s, size_t idx) {
    size_t w = idx / 64, b = idx % 64;
    if (w >= s.size()) return 0;
    return (s[w] >> b) & 1;
}

template<typename Fn>
static int run_suite(const char* name, const std::vector<TMTestCase>& tests, Fn verify, bool verbose) {
    int failures = 0;
    if (verbose) std::cout << "=== " << name << " ===\n\n";
    for (const auto& test : tests)
        if (!verify(test, verbose)) failures++;
    if (verbose)
        std::cout << "=== " << (tests.size() - failures) << "/" << tests.size() << " passed ===\n";
    return failures;
}

// --- TM ---

bool Verifier::verify_tm(const TMTestCase& test, bool verbose) {
    if (verbose) std::cout << "--- " << test.name << " ---\n";
    TuringMachine tm = build_tm(test);
    if (verbose) tm.print_state();

    int steps = 0;
    while (tm.step()) {
        steps++;
        if (verbose) tm.print_state();
        if (steps > 10000) { if (verbose) std::cout << "  FAIL: exceeded step limit\n"; return false; }
    }
    if (verbose) { std::cout << "  (halted)\n"; tm.print_state(); }

    bool ok = tapes_equal(tm.get_tape(), test.expected_tape) && (steps == test.expected_steps);
    if (verbose) std::cout << "  Steps: " << steps << "  " << (ok ? "PASS" : "FAIL") << "\n\n";
    return ok;
}

int Verifier::run_all_tm_tests(bool verbose) {
    return run_suite("TM Verification", get_tm_tests(), verify_tm, verbose);
}

// --- Tag ---

bool Verifier::verify_tag(const TMTestCase& test, bool verbose) {
    if (verbose) std::cout << "--- Tag: " << test.name << " ---\n";
    TuringMachine tm = build_tm(test);
    TagSystem ts = TMToTagConverter::convert(tm);

    if (ts.get_deletion_number() != tm.get_num_symbols() + 2) {
        if (verbose) std::cout << "  FAIL: deletion number mismatch\n";
        return false;
    }

    int tag_steps = 0, limit = 1000000;
    while (ts.step()) { tag_steps++; if (tag_steps > limit) break; }

    bool ok = (test.expected_steps >= 0) == (tag_steps <= limit);
    if (verbose) std::cout << "  Tag steps: " << tag_steps << "  " << (ok ? "PASS" : "FAIL") << "\n\n";
    return ok;
}

int Verifier::run_all_tag_tests(bool verbose) {
    return run_suite("Tag System Verification", get_tm_tests(), verify_tag, verbose);
}

// --- CTS ---

bool Verifier::verify_cts(const TMTestCase& test, bool verbose) {
    if (verbose) std::cout << "--- CTS: " << test.name << " ---\n";
    TuringMachine tm = build_tm(test);
    TagSystem ts = TMToTagConverter::convert(tm);
    TagSystem::Word tag_word = ts.get_word();

    CyclicTagSystem cts = TagToCyclicConverter::convert(ts);
    int phi_size = (int)cts.get_appendants().size() / ts.get_deletion_number();

    if (verbose)
        std::cout << "  |Phi|: " << phi_size << "  Appendants: " << cts.get_appendants().size()
                  << "  CTS tape length: " << cts.tape_length() << "\n";

    // Round-trip decode
    auto cts_tape = cts.get_tape();
    bool roundtrip_ok = ((int)cts_tape.size() == (int)tag_word.size() * phi_size);
    if (roundtrip_ok) {
        for (int i = 0; i < (int)tag_word.size(); i++) {
            int sym = -1;
            for (int j = 0; j < phi_size; j++) {
                if (cts_tape[i * phi_size + j] == 1) {
                    if (sym != -1) { roundtrip_ok = false; break; }
                    sym = j;
                }
            }
            if (sym != tag_word[i]) { roundtrip_ok = false; break; }
        }
    }
    if (verbose) std::cout << "  Round-trip: " << (roundtrip_ok ? "OK" : "FAIL") << "\n";

    int cts_steps = 0, limit = 10000000;
    while (cts.step()) { cts_steps++; if (cts_steps > limit) break; }

    bool ok = roundtrip_ok && ((test.expected_steps >= 0) == (cts_steps <= limit));
    if (verbose) std::cout << "  CTS steps: " << cts_steps << "  " << (ok ? "PASS" : "FAIL") << "\n\n";
    return ok;
}

int Verifier::run_all_cts_tests(bool verbose) {
    return run_suite("CTS Verification", get_tm_tests(), verify_cts, verbose);
}

// --- R110 ---

bool Verifier::verify_r110(const TMTestCase& test, bool verbose) {
    if (verbose) std::cout << "--- R110: " << test.name << " ---\n";

    TuringMachine tm = build_tm(test);
    TagSystem ts = TMToTagConverter::convert(tm);
    CyclicTagSystem cts = TagToCyclicConverter::convert(ts);
    Rule110State r110 = Rule110Compiler::compile(cts);

    bool structure_ok = !r110.left_periodic.empty() &&
                        !r110.central_part.empty() &&
                        !r110.right_periodic.empty();

    if (verbose) {
        std::cout << "  Left: " << r110.left_periodic.size()
                  << "  Central: " << r110.central_part.size()
                  << "  Right: " << r110.right_periodic.size() << "\n";
    }
    if (!structure_ok) { if (verbose) std::cout << "  FAIL: empty region\n\n"; return false; }

    std::vector<int> tape_int;
    tape_int.insert(tape_int.end(), r110.left_periodic.begin(), r110.left_periodic.end());
    size_t center_start = tape_int.size();
    tape_int.insert(tape_int.end(), r110.central_part.begin(), r110.central_part.end());
    tape_int.insert(tape_int.end(), r110.right_periodic.begin(), r110.right_periodic.end());

    auto s0 = Rule110Runner::pack(tape_int);
    std::vector<int>().swap(tape_int);
    auto s1 = Rule110Runner::next_generation(s0);

    // Ether: period-1 shift-24
    size_t ck_start = 1000;
    size_t ck_end = std::min(center_start - 1000, ck_start + 100000UL);
    if (ck_end <= ck_start + 28) {
        if (verbose) std::cout << "  SKIP: left region too small\n\n";
        return structure_ok;
    }

    int mm = 0;
    for (size_t i = ck_start; i < ck_end; i++)
        if (get_bit(s0, i - 24) != get_bit(s1, i)) mm++;

    bool ok = structure_ok && (mm == 0);
    if (verbose)
        std::cout << "  Ether: " << mm << "/" << (ck_end - ck_start) << "  "
                  << (ok ? "PASS" : "FAIL") << "\n\n";
    return ok;
}

int Verifier::run_all_r110_tests(bool verbose) {
    return run_suite("R110 Verification", get_tm_tests(), verify_r110, verbose);
}

// --- Test cases ---

std::vector<TMTestCase> Verifier::get_tm_tests() {
    return {
        {"Immediate halt", 1, 2, {}, 0, {0}, 0, {0}, 0},
        {"Write-and-halt", 1, 2, {{0, 0, 0, 1, Move::HALT}}, 0, {0}, 0, {1}, 0},
        {"Binary increment", 3, 2,
            {{0, 0, 1, 0, Move::LEFT}, {0, 1, 0, 1, Move::RIGHT},
             {1, 0, 2, 1, Move::HALT}, {1, 1, 1, 0, Move::LEFT}},
            0, {1, 1}, 0, {1, 0, 0}, 5},
        {"Right-then-halt", 2, 2, {{0, 0, 1, 1, Move::RIGHT}}, 0, {0}, 0, {1}, 1},
        {"Left-then-halt", 2, 2, {{0, 0, 1, 1, Move::LEFT}}, 0, {0}, 0, {0, 1}, 1},
        {"Unary increment", 2, 2,
            {{0, 0, 0, 1, Move::HALT}, {0, 1, 0, 1, Move::RIGHT}},
            0, {1, 1, 0}, 0, {1, 1, 1}, 2},
    };
}

} // namespace Rule110
