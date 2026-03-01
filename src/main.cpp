#include "TuringMachine.hpp"
#include "TMToTag.hpp"
#include "TagToCyclic.hpp"
#include "Rule110Compiler.hpp"
#include "Rule110Runner.hpp"
#include "Verifier.hpp"
#include <iostream>
#include <fstream>
#include <deque>
#include <sys/stat.h>
#include <csignal>
#include <atomic>

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

    // Stream head to disk
    std::ofstream head_pgm(outdir + "/head.pgm", std::ios::binary);
    head_pgm << "P5\n" << view_width << " " << (HEAD_GENS + 1) << "\n255\n";
    std::vector<uint8_t> row_buf;
    extract_row(state, view_start, view_width, row_buf);
    write_pgm_row(head_pgm, row_buf, view_width);
    bool head_done = false;

    std::deque<std::vector<uint8_t>> tail_rows;
    tail_rows.push_back(row_buf);

    Rule110Runner::State prev30 = state;
    long long halt_gen = -1, current_gen = 0;

    std::signal(SIGINT, signal_handler);
    std::cout << "\nRunning (Ctrl+C to stop)...\n\n";

    for (long long g = 1; ; g++) {
        if (interrupted.load()) {
            std::cout << "\n  Interrupted at gen " << current_gen << "\n";
            break;
        }

        state = Rule110Runner::next_generation(state);
        current_gen = g;
        extract_row(state, view_start, view_width, row_buf);

        if (!head_done) {
            write_pgm_row(head_pgm, row_buf, view_width);
            if (g == HEAD_GENS) {
                head_pgm.close();
                head_done = true;
                std::cout << "  Saved head.pgm\n";
            }
        }

        tail_rows.push_back(row_buf);
        if ((int)tail_rows.size() > tail_save)
            tail_rows.pop_front();

        if (g % 30 == 0) {
            int mm = count_unsettled(state, prev30, center_start, center_end);
            prev30 = state;
            if (mm < 300) {
                halt_gen = g;
                std::cout << "  SETTLED gen " << g << " (mm=" << mm << ")\n";
                break;
            }
            if (g % 10000 == 0)
                std::cout << "  gen " << g << "  mm=" << mm << "\n";
        } else if (g % 10000 == 0) {
            std::cout << "  gen " << g << "\n";
        }
    }

    if (!head_done) head_pgm.close();

    long long tail_start = current_gen - (long long)tail_rows.size() + 1;
    write_pgm(outdir + "/tail.pgm", tail_rows, view_width);
    std::cout << "  Saved tail.pgm (" << tail_rows.size() << " rows, gen "
              << tail_start << "-" << current_gen << ")\n";
    tail_rows.clear();

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
        auto s1 = Rule110Runner::next_generation(state);
        size_t ck_start = 1000;
        size_t ck_end = std::min(center_start - 1000, ck_start + 100000);
        int emm = check_ether(state, s1, ck_start, ck_end);
        out("  Left ether: " + std::to_string(emm) + "/" + std::to_string(ck_end - ck_start)
            + " " + (emm == 0 ? "PASS" : "FAIL"));
        if (emm != 0) pass = false;
    }

    out("\n" + std::string(pass ? "ALL PASSED" : "FAILED"));
    std::cout << "\n  uv run --with Pillow python3 tools/convert_images.py " << outdir << "\n";
    return 0;
}
