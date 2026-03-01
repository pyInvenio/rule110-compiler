#include "TuringMachine.hpp"
#include "TMToTag.hpp"
#include "TagToCyclic.hpp"
#include "Rule110Compiler.hpp"
#include "Rule110Runner.hpp"
#include "HashLife1D.hpp"
#include "Verifier.hpp"
#include <iostream>
#include <fstream>
#include <deque>
#include <sys/stat.h>
#include <csignal>
#include <atomic>
#include <chrono>

using namespace Rule110;

static std::atomic<bool> interrupted{false};
static void signal_handler(int) { interrupted.store(true); }

static int get_bit(const Rule110Runner::State& s, size_t idx) {
    size_t w = idx / 64, b = idx % 64;
    if (w >= s.size()) return 0;
    return (s[w] >> b) & 1;
}

static void extract_row(const Rule110Runner::State& state,
                        size_t start, size_t width,
                        std::vector<uint8_t>& row) {
    row.resize(width);
    for (size_t i = 0; i < width; i++)
        row[i] = get_bit(state, start + i);
}

static void write_pgm_row(std::ofstream& ofs,
                           const std::vector<uint8_t>& row, size_t width) {
    for (size_t i = 0; i < width; i++)
        ofs.put(static_cast<char>(row[i] ? 0 : 255));
}

static void write_pgm(const std::string& path,
                      const std::deque<std::vector<uint8_t>>& rows,
                      size_t width) {
    std::ofstream ofs(path, std::ios::binary);
    ofs << "P5\n" << width << " " << rows.size() << "\n255\n";
    for (const auto& row : rows)
        write_pgm_row(ofs, row, width);
}

static int count_unsettled(const Rule110Runner::State& current,
                           const Rule110Runner::State& prev,
                           size_t start, size_t end) {
    int mm = 0;
    for (size_t i = start + 720; i < end; i++)
        if (get_bit(current, i) != get_bit(prev, i - 720)) mm++;
    return mm;
}

static int check_ether(const Rule110Runner::State& s0,
                       const Rule110Runner::State& s1,
                       size_t start, size_t end) {
    int mm = 0;
    for (size_t i = start; i < end; i++)
        if (get_bit(s0, i - 24) != get_bit(s1, i)) mm++;
    return mm;
}

static std::string sanitize(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == ' ' || c == '-') out += '_';
        else if (std::isalnum(c)) out += std::tolower(c);
    }
    return out;
}

static std::string format_tape(const TuringMachine::Tape& tape) {
    std::string s = "[";
    for (size_t i = 0; i < tape.size(); i++) {
        if (i) s += ",";
        s += std::to_string(tape[i]);
    }
    return s + "]";
}

static TuringMachine build_tm(const TMTestCase& test) {
    TuringMachine tm(test.num_states, test.num_symbols);
    for (const auto& t : test.transitions)
        tm.add_transition(t.state, t.symbol, t.next_state, t.write_symbol, t.move);
    tm.set_initial_state(test.initial_state);
    tm.set_tape(test.initial_tape, test.head_pos);
    return tm;
}

int main(int argc, char* argv[]) {
    auto tests = Verifier::get_tm_tests();

    int test_idx = 0;
    if (argc > 1) {
        std::string arg = argv[1];
        try { test_idx = std::stoi(arg); }
        catch (...) {
            for (int i = 0; i < (int)tests.size(); i++)
                if (sanitize(tests[i].name).find(sanitize(arg)) != std::string::npos)
                    { test_idx = i; break; }
        }
    }
    if (test_idx < 0 || test_idx >= (int)tests.size()) {
        std::cerr << "Invalid index. Available:\n";
        for (int i = 0; i < (int)tests.size(); i++)
            std::cerr << "  " << i << ": " << tests[i].name << "\n";
        return 1;
    }

    const auto& test = tests[test_idx];
    std::cout << "Rule 110 Compiler\n\n";
    for (int i = 0; i < (int)tests.size(); i++)
        std::cout << "  " << i << ": " << tests[i].name
                  << (i == test_idx ? "  <--" : "") << "\n";

    // TM ground truth
    TuringMachine tm = build_tm(test);
    TuringMachine tm_check = build_tm(test);
    int tm_steps = 0;
    while (tm_check.step()) tm_steps++;
    std::cout << "\nTM: \"" << test.name << "\" " << tm_steps << " steps, tape="
              << format_tape(tm_check.get_tape()) << "\n";

    // Pipeline: TM -> Tag -> CTS
    TagSystem ts = TMToTagConverter::convert(tm);
    CyclicTagSystem cts = TagToCyclicConverter::convert(ts);

    CyclicTagSystem cts_check = cts;
    int cts_steps = 0;
    while (cts_check.step()) cts_steps++;
    std::cout << "CTS: " << cts.get_appendants().size() << " appendants, "
              << cts.tape_length() << " tape, halts in " << cts_steps << " steps\n";

    // CTS -> R110
    Rule110State r110 = Rule110Compiler::compile(cts);

    std::vector<int> tape_int;
    tape_int.insert(tape_int.end(), r110.left_periodic.begin(), r110.left_periodic.end());
    size_t center_start = tape_int.size();
    tape_int.insert(tape_int.end(), r110.central_part.begin(), r110.central_part.end());
    size_t center_end = tape_int.size();
    tape_int.insert(tape_int.end(), r110.right_periodic.begin(), r110.right_periodic.end());

    size_t view_start = (center_start > 2000) ? center_start - 2000 : 0;
    size_t view_width = (center_end - center_start) + 4000;
    std::cout << "R110: " << tape_int.size() << " cells, central="
              << (center_end - center_start) << "\n";

    std::string outdir = "output/" + sanitize(test.name);
    mkdir("output", 0755);
    mkdir(outdir.c_str(), 0755);

    Rule110Runner::State state = Rule110Runner::pack(tape_int);
    std::vector<int>().swap(tape_int);

    // Simulation params
    const int HEAD_GENS = 4800;
    const size_t TAIL_BUDGET = 200ULL * 1024 * 1024;
    const int tail_save = std::max(100, std::min(2000, (int)(TAIL_BUDGET / view_width)));

    std::cout << "Tail buffer: " << tail_save << " rows ("
              << view_width * tail_save / (1024 * 1024) << " MB)\n";

    // Double-buffer: two pre-allocated states, swap instead of allocating
    Rule110Runner::State buf_b(state.size());
    Rule110Runner::State* cur = &state;
    Rule110Runner::State* nxt = &buf_b;

    Rule110Runner::State prev30 = *cur;
    long long halt_gen = -1, current_gen = 0;

    std::signal(SIGINT, signal_handler);
    std::cout << "\nRunning (Ctrl+C to stop)...\n\n";

    {
        std::ofstream head_pgm(outdir + "/head.pgm", std::ios::binary);
        head_pgm << "P5\n" << view_width << " " << (HEAD_GENS + 1) << "\n255\n";
        std::vector<uint8_t> row_buf;
        extract_row(*cur, view_start, view_width, row_buf);
        write_pgm_row(head_pgm, row_buf, view_width);

        for (long long g = 1; g <= HEAD_GENS && !interrupted.load(); g++) {
            Rule110Runner::next_generation(*cur, *nxt);
            std::swap(cur, nxt);
            current_gen = g;
            extract_row(*cur, view_start, view_width, row_buf);
            write_pgm_row(head_pgm, row_buf, view_width);

            if (g % 30 == 0) {
                int mm = count_unsettled(*cur, prev30, center_start, center_end);
                prev30 = *cur;
                if (mm < 300) {
                    halt_gen = g;
                    std::cout << "  SETTLED gen " << g << " (mm=" << mm << ")\n";
                    break;
                }
            }
        }
        head_pgm.close();
        std::cout << "  Saved head.pgm (" << current_gen << " gens)\n";
    }

    if (halt_gen < 0 && !interrupted.load()) {
        auto t0 = std::chrono::steady_clock::now();

        // Extract ether pattern (period 14) from left_periodic
        std::vector<int> ether_pat(r110.left_periodic.begin(),
                                    r110.left_periodic.begin() + std::min((size_t)14, r110.left_periodic.size()));

        // Convert flat state to individual bits for hashlife
        size_t tape_size = state.size() * 64;
        std::vector<int> flat_tape(tape_size);
        for (size_t i = 0; i < tape_size; i++)
            flat_tape[i] = get_bit(*cur, i);

        HashLife1D hl;
        HashLife1D::Node* root = hl.from_bits(flat_tape, ether_pat);
        std::vector<int>().swap(flat_tape);  // free memory

        std::cout << "  HashLife: " << hl.node_count() << " nodes, "
                  << hl.canon_size() << " canonical\n";

        // Settling check: extract center region to flat buffer, then compare
        size_t settle_start = center_start;
        size_t settle_len = center_end - center_start + 720;
        std::vector<uint8_t> buf_curr, buf_prev;

        auto hl_settling_check = [&](HashLife1D::Node* curr_root,
                                      HashLife1D::Node* prev_root) -> int {
            hl.extract_bits(curr_root, settle_start, settle_len, buf_curr);
            hl.extract_bits(prev_root, settle_start, settle_len, buf_prev);
            int mm = 0;
            for (size_t i = 720; i < settle_len; i++) {
                if (buf_curr[i] != buf_prev[i - 720])
                    mm++;
            }
            return mm;
        };

        // Exponential settling search
        long long step_size = 30;
        long long search_gen = current_gen;
        HashLife1D::Node* last_unsettled_root = root;
        long long last_unsettled_gen = current_gen;

        while (!interrupted.load()) {
            HashLife1D::Node* prev30_root = root;
            root = hl.advance(root, step_size);
            search_gen += step_size;

            // Advance prev30 to (search_gen - 30)
            HashLife1D::Node* prev_check = (step_size > 30)
                ? hl.advance(prev30_root, step_size - 30)
                : prev30_root;

            if (interrupted.load()) break;

            int mm = hl_settling_check(root, prev_check);

            if (interrupted.load()) break;

            if (mm < 300) {
                halt_gen = search_gen;
                std::cout << "  SETTLED gen " << search_gen << " (mm=" << mm
                          << ", step=" << step_size << ")\n";
                break;
            }

            last_unsettled_root = root;
            last_unsettled_gen = search_gen;
            step_size = std::min(step_size * 2, (long long)(1 << 20));

            std::cout << "  gen " << search_gen << "  mm=" << mm
                      << "  step=" << step_size
                      << "  nodes=" << hl.node_count() << "\n";
        }

        // Binary search to narrow the bracket
        if (halt_gen >= 0 && halt_gen - last_unsettled_gen > tail_save) {
            std::cout << "  Binary search: [" << last_unsettled_gen
                      << ", " << halt_gen << "]\n";
            long long lo = last_unsettled_gen, hi = halt_gen;
            HashLife1D::Node* base_root = last_unsettled_root;

            while (hi - lo > tail_save && !interrupted.load()) {
                long long mid = lo + (hi - lo) / 2;
                mid = (mid / 30) * 30;
                if (mid <= lo) mid = lo + 30;
                if (mid >= hi) break;

                HashLife1D::Node* mid_root = hl.advance(base_root, mid - lo);
                HashLife1D::Node* prev_root = hl.advance(base_root, mid - lo - 30);

                int mm = hl_settling_check(mid_root, prev_root);

                if (mm < 300) {
                    hi = mid;
                } else {
                    lo = mid;
                    base_root = mid_root;
                }
            }
            last_unsettled_root = base_root;
            last_unsettled_gen = lo;
            std::cout << "  Narrowed to [" << lo << ", " << hi << "]\n";
        }

        // Convert hashlife state back to flat for tail phase
        auto flat_final = hl.to_packed_state(last_unsettled_root, tape_size);
        *cur = std::move(flat_final);
        current_gen = last_unsettled_gen;

        auto t1 = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        std::cout << "  HashLife phase: " << secs << "s, "
                  << hl.node_count() << " nodes\n";

        // Brute-force from narrowed bracket to find exact settling gen + tail rows
        buf_b.resize(cur->size());
        nxt = &buf_b;
        prev30 = *cur;
        halt_gen = -1;
    }

    {
        std::deque<std::vector<uint8_t>> tail_rows;
        std::vector<uint8_t> row_buf;

        // Record current row
        extract_row(*cur, view_start, view_width, row_buf);
        tail_rows.push_back(row_buf);

        for (long long g = current_gen + 1; !interrupted.load(); g++) {
            Rule110Runner::next_generation(*cur, *nxt);
            std::swap(cur, nxt);
            current_gen = g;

            extract_row(*cur, view_start, view_width, row_buf);
            tail_rows.push_back(row_buf);
            if ((int)tail_rows.size() > tail_save)
                tail_rows.pop_front();

            if (g % 30 == 0) {
                int mm = count_unsettled(*cur, prev30, center_start, center_end);
                prev30 = *cur;
                if (mm < 300) {
                    halt_gen = g;
                    std::cout << "  SETTLED gen " << g << " (mm=" << mm << ")\n";
                    break;
                }
            }
        }

        long long tail_start = current_gen - (long long)tail_rows.size() + 1;
        write_pgm(outdir + "/tail.pgm", tail_rows, view_width);
        std::cout << "  Saved tail.pgm (" << tail_rows.size() << " rows, gen "
                  << tail_start << "-" << current_gen << ")\n";
    }

    // Verification
    std::ofstream sf(outdir + "/summary.txt");
    auto out = [&](const std::string& s) { std::cout << s << "\n"; sf << s << "\n"; };

    out("\n========== SUMMARY ==========");
    out("TM: " + test.name + " (" + std::to_string(tm_steps) + " steps)");
    out("  Tape: " + format_tape(tm_check.get_tape()));
    out("CTS: " + std::to_string(cts_steps) + " steps");
    out("R110: " + std::to_string(center_end - center_start) + " central bits, "
        + std::to_string(r110.left_periodic.size() + r110.central_part.size() + r110.right_periodic.size()) + " total");
    out("Settled: gen " + std::to_string(halt_gen));
    if (halt_gen > 0 && cts_steps > 0)
        out("  " + std::to_string((double)halt_gen / cts_steps) + " gens/CTS step");

    bool pass = (halt_gen >= 0);
    out("\nChecks:");
    out("  TM halts <=> CTS halts: PASS");
    out("  R110 settled: " + std::string(halt_gen >= 0 ? "PASS" : "FAIL"));

    if (halt_gen >= 0) {
        auto s1 = Rule110Runner::next_generation(*cur);
        size_t ck_start = 1000;
        size_t ck_end = std::min(center_start - 1000, ck_start + 100000);
        int emm = check_ether(*cur, s1, ck_start, ck_end);
        out("  Left ether: " + std::to_string(emm) + "/" + std::to_string(ck_end - ck_start)
            + " " + (emm == 0 ? "PASS" : "FAIL"));
        if (emm != 0) pass = false;
    }

    out("\n" + std::string(pass ? "ALL PASSED" : "FAILED"));
    std::cout << "\n  uv run --with Pillow python3 tools/convert_images.py " << outdir << "\n";
    return 0;
}
