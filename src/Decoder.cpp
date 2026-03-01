#include "Decoder.hpp"
#include "TMToTag.hpp"
#include "TagToCyclic.hpp"
#include <iostream>

namespace Rule110 {

std::vector<int> Decoder::cts_to_tag(const std::vector<int>& cts_tape, int phi_size) {
    std::vector<int> tag_word;
    int len = (int)cts_tape.size();
    if (len % phi_size != 0) return {};

    for (int i = 0; i + phi_size <= len; i += phi_size) {
        int symbol = -1;
        for (int j = 0; j < phi_size; j++) {
            if (cts_tape[i + j] == 1) {
                if (symbol != -1) return {}; // multiple 1s in block
                symbol = j;
            }
        }
        if (symbol == -1) return {}; // no 1 in block
        tag_word.push_back(symbol);
    }
    return tag_word;
}

TMConfig Decoder::tag_to_tm(const std::vector<int>& tag_word, int m, int t) {
    if (tag_word.empty()) return {};

    int s = t + 2;

    // Symbol ranges: H(i)=[0,m), L(i)=[m,2m), R(i)=[2m,3m), R*(i)=[3m,4m)
    int gamma = -1;
    long long a = 0, b = 0, c = 0;
    bool has_rstar = false;
    int prev_type = -1;

    for (int sym : tag_word) {
        int state, type;
        if (sym < m)          { state = sym;       type = 0; }
        else if (sym < 2 * m) { state = sym - m;   type = 1; }
        else if (sym < 3 * m) { state = sym - 2*m; type = 2; }
        else if (sym < 4 * m) { state = sym - 3*m; type = 2; has_rstar = true; }
        else return {};

        if (gamma == -1) gamma = state;
        if (state != gamma) return {};
        if (type < prev_type) return {};
        prev_type = type;

        if (type == 0) a++;
        else if (type == 1) b++;
        else c++;
    }

    if (has_rstar) return {};

    int head_symbol = (int)(s - a);
    if (head_symbol < 0 || head_symbol >= s) return {};

    // Boundary symbols: head_symbol t = σ_{t+1} (left), t+1 = σ_{t+2} (right)
    // Both represent the head reading blank (0) at the tape boundary.
    bool at_right_boundary = (head_symbol == t + 1);
    bool at_left_boundary  = (head_symbol == t);
    int effective_head = (head_symbol >= t) ? 0 : head_symbol;

    // Decode left tape from b (base-s encoding)
    // expL = s^{x+1} + Σ_{k=1}^{x} (s-b_k)*s^k, digits[0]=0, digits[x+1]=1
    std::vector<int> left_tape;
    if (!at_left_boundary) {
        long long val = b;
        std::vector<int> digits;
        while (val > 0) {
            digits.push_back((int)(val % s));
            val /= s;
        }
        if (digits.empty() || digits[0] != 0) return {};
        if (digits.back() != 1) return {};

        int x = (int)digits.size() - 2;
        for (int k = x; k >= 1; k--) {
            int tape_val = s - digits[k] - 1;
            if (tape_val < 0 || tape_val >= t) return {};
            left_tape.push_back(tape_val);
        }
    }

    // Decode right tape from c (base-s encoding)
    // expR = Σ_{k=1}^{y} (s-d_k)*s^k, digits[0]=0
    // At right boundary, right tape is all blank — skip decoding.
    std::vector<int> right_tape;
    if (!at_right_boundary && c > 0) {
        long long val = c;
        std::vector<int> digits;
        while (val > 0) {
            digits.push_back((int)(val % s));
            val /= s;
        }
        if (digits.empty() || digits[0] != 0) return {};

        for (int k = 1; k < (int)digits.size(); k++) {
            int tape_val = s - digits[k] - 1;
            if (tape_val < 0 || tape_val >= t) return {};
            right_tape.push_back(tape_val);
        }
    }

    TMConfig cfg;
    cfg.state = gamma;
    cfg.tape = left_tape;
    cfg.head_pos = (int)left_tape.size();
    cfg.tape.push_back(effective_head);
    for (int v : right_tape) cfg.tape.push_back(v);
    cfg.valid = true;

    while (cfg.tape.size() > 1 && cfg.tape.back() == 0)
        cfg.tape.pop_back();

    return cfg;
}

static TuringMachine build_tm(const TMTestCase& test) {
    TuringMachine tm(test.num_states, test.num_symbols);
    for (const auto& t : test.transitions)
        tm.add_transition(t.state, t.symbol, t.next_state, t.write_symbol, t.move);
    tm.set_initial_state(test.initial_state);
    tm.set_tape(test.initial_tape, test.head_pos);
    return tm;
}

static std::string fmt_tape(const std::vector<int>& tape) {
    std::string s = "[";
    for (size_t i = 0; i < tape.size(); i++) {
        if (i) s += ",";
        s += std::to_string(tape[i]);
    }
    return s + "]";
}

static bool tapes_match(const std::vector<int>& a, const std::vector<int>& b) {
    auto effective_len = [](const std::vector<int>& t) -> int {
        for (int i = (int)t.size() - 1; i >= 0; i--)
            if (t[i] != 0) return i + 1;
        return 0;
    };
    int la = effective_len(a), lb = effective_len(b);
    if (la != lb) return false;
    for (int i = 0; i < la; i++)
        if (a[i] != b[i]) return false;
    return true;
}

bool Decoder::verify_decode(const TMTestCase& test, bool verbose) {
    if (verbose) std::cout << "--- Decode: " << test.name << " ---\n";

    // 1. Run TM, record configuration at each step
    TuringMachine tm = build_tm(test);
    struct Snapshot { int state; std::vector<int> tape; int head_pos; };
    std::vector<Snapshot> tm_history;
    tm_history.push_back({tm.get_current_state(), tm.get_tape(), tm.get_head_pos()});
    while (tm.step())
        tm_history.push_back({tm.get_current_state(), tm.get_tape(), tm.get_head_pos()});
    int tm_steps = (int)tm_history.size() - 1;

    if (verbose)
        std::cout << "  TM: " << tm_steps << " steps, final=" << fmt_tape(tm_history.back().tape) << "\n";

    // 2. Build Tag system from fresh TM copy, decode initial CTS tape (round-trip)
    TuringMachine tm2 = build_tm(test);
    TagSystem ts = TMToTagConverter::convert(tm2);
    CyclicTagSystem cts = TagToCyclicConverter::convert(ts);

    int phi_size = (int)cts.get_appendants().size() / ts.get_deletion_number();
    int m = test.num_states;
    int t = test.num_symbols;

    auto cts_tape = cts.get_tape();
    auto tag_word = cts_to_tag(cts_tape, phi_size);
    bool initial_ok = false;
    if (!tag_word.empty()) {
        TMConfig cfg = tag_to_tm(tag_word, m, t);
        if (cfg.valid) {
            initial_ok = (cfg.state == tm_history[0].state &&
                          cfg.head_pos == tm_history[0].head_pos &&
                          tapes_match(cfg.tape, tm_history[0].tape));
        }
    }
    if (verbose)
        std::cout << "  CTS round-trip t=0: " << (initial_ok ? "PASS" : "FAIL") << "\n";

    // 3. Run tag system directly, decode at each step to find TM state boundaries
    TuringMachine tm3 = build_tm(test);
    TagSystem ts2 = TMToTagConverter::convert(tm3);

    int next_tm_idx = 1;
    int total_decoded = 0;
    TMConfig last_decoded;
    int tag_steps = 0;

    while (ts2.step()) {
        tag_steps++;
        auto word = ts2.get_word();
        if (word.empty()) break;

        TMConfig cfg = tag_to_tm(word, m, t);
        if (!cfg.valid) continue;

        total_decoded++;
        last_decoded = cfg;

        // Scan forward in TM history for a match
        for (int idx = next_tm_idx; idx < (int)tm_history.size(); idx++) {
            const auto& expected = tm_history[idx];
            if (cfg.state == expected.state &&
                cfg.head_pos == expected.head_pos &&
                tapes_match(cfg.tape, expected.tape)) {
                next_tm_idx = idx + 1;
                break;
            }
        }
    }

    int matched = next_tm_idx - 1; // how many TM steps we matched

    if (verbose) {
        std::cout << "  Tag steps: " << tag_steps
                  << ", decoded " << total_decoded << " clean TM configs\n";
        std::cout << "  Matched " << matched << "/" << tm_steps << " TM steps\n";
        if (last_decoded.valid)
            std::cout << "  Last decoded: state=" << last_decoded.state
                      << " head=" << last_decoded.head_pos
                      << " tape=" << fmt_tape(last_decoded.tape) << "\n";
    }

    bool pass = initial_ok;
    if (tm_steps > 0)
        pass = pass && (matched == tm_steps);

    if (verbose)
        std::cout << "  " << (pass ? "PASS" : "FAIL") << "\n\n";

    return pass;
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

int Decoder::run_all_decode_tests(bool verbose) {
    return run_suite("Decode Verification", Verifier::get_tm_tests(), verify_decode, verbose);
}

} // namespace Rule110
