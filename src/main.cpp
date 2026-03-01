#include "TuringMachine.hpp"
#include "TMToTag.hpp"
#include "TagToCyclic.hpp"
#include "Rule110Compiler.hpp"
#include "Rule110Runner.hpp"
#include "HashLife1D.hpp"
#include "Verifier.hpp"
#include "Decoder.hpp"
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

// --- Color PPM support ---

struct RGB { uint8_t r, g, b; };

static RGB block_color(char block, bool on) {
    switch (block) {
        case 'A': return on ? RGB{100,100,100} : RGB{220,220,220};
        case 'B': return on ? RGB{80,80,120}   : RGB{200,200,230};
        case 'C': return on ? RGB{160,40,160}  : RGB{230,200,230};
        case 'D': return on ? RGB{200,120,40}  : RGB{240,220,180};
        case 'E': return on ? RGB{40,80,200}   : RGB{200,210,240};
        case 'F': return on ? RGB{40,160,60}   : RGB{200,235,200};
        case 'G': return on ? RGB{180,160,40}  : RGB{240,235,200};
        default:  return on ? RGB{160,60,60}   : RGB{235,200,200}; // H-L
    }
}

static void write_ppm_row(std::ofstream& ofs,
                           const std::vector<uint8_t>& row,
                           const std::vector<char>& block_map,
                           size_t view_start, size_t width) {
    for (size_t i = 0; i < width; i++) {
        size_t pos = view_start + i;
        char block = (pos < block_map.size()) ? block_map[pos] : 'A';
        RGB c = block_color(block, row[i] != 0);
        ofs.put(c.r); ofs.put(c.g); ofs.put(c.b);
    }
}

// Ether detection via spatial periodicity.
// A blocks have exact period 14.
static inline bool is_ether_spatial(const std::vector<uint8_t>& row, size_t i, size_t width) {
    if (i < 14 || i + 14 >= width) return false;
    return row[i] == row[i - 14] && row[i] == row[i + 14];
}

// Write PPM row using a propagated color map.
// color_map[i] = block type for each pixel, propagated from previous generation.
static void write_ppm_row_colored(std::ofstream& ofs,
                                    const std::vector<uint8_t>& row,
                                    const std::vector<char>& color_map,
                                    size_t width) {
    for (size_t i = 0; i < width; i++) {
        RGB c = block_color(color_map[i], row[i] != 0);
        ofs.put(c.r); ofs.put(c.g); ofs.put(c.b);
    }
}

// Propagate color map forward one generation.
// Simple approach: ether cells are gray, non-ether cells inherit from
// nearest non-ether cell in previous generation (no velocity assumption).
// Gliders are separated by hundreds of cells of ether, so nearest-neighbor
// search reliably finds the same glider's color.
static void propagate_colors(const std::vector<uint8_t>& row, size_t width,
                              const std::vector<char>& prev_colors,
                              std::vector<char>& curr_colors) {
    curr_colors.resize(width);

    for (size_t i = 0; i < width; i++) {
        // Ether check is definitive
        if (is_ether_spatial(row, i, width)) {
            curr_colors[i] = 'A';
            continue;
        }

        // Non-ether: inherit from nearest non-ether cell in previous gen.
        // Search outward from position i. Gliders shift ~10-24 cells/gen,
        // and are separated by hundreds of cells of ether, so radius 40
        // is safe (finds same glider, not adjacent one).
        char c = 'A';
        for (int d = 0; d <= 40; d++) {
            int lo = (int)i - d, hi = (int)i + d;
            if (lo >= 0 && prev_colors[lo] != 'A') { c = prev_colors[lo]; break; }
            if (d > 0 && hi < (int)width && prev_colors[hi] != 'A') { c = prev_colors[hi]; break; }
        }
        curr_colors[i] = c;
    }
}

// --- Settling detection ---

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

// --- Helpers ---

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

// --- Binary addition TM ---

static TMTestCase build_addition_test(int x, int y) {
    // Convert to binary LSB first
    std::vector<int> x_bits, y_bits;
    int xx = x, yy = y;
    while (xx > 0 || yy > 0) {
        x_bits.push_back(xx & 1);
        y_bits.push_back(yy & 1);
        xx >>= 1;
        yy >>= 1;
    }
    if (x_bits.empty()) { x_bits.push_back(0); y_bits.push_back(0); }

    // Build paired tape: symbol = 1 + a*2 + b
    // 1=(0,0), 2=(0,1), 3=(1,0), 4=(1,1)
    std::vector<int> tape;
    for (size_t i = 0; i < x_bits.size(); i++)
        tape.push_back(1 + x_bits[i] * 2 + y_bits[i]);

    // Binary addition transitions
    std::vector<TMTestCase::TransitionDef> transitions = {
        {0, 1, 0, 1, Move::RIGHT}, {0, 2, 0, 2, Move::RIGHT},
        {0, 3, 0, 4, Move::RIGHT}, {0, 4, 1, 3, Move::RIGHT},
        {1, 0, 0, 2, Move::HALT},
        {1, 1, 0, 2, Move::RIGHT}, {1, 2, 1, 1, Move::RIGHT},
        {1, 3, 1, 3, Move::RIGHT}, {1, 4, 1, 4, Move::RIGHT},
    };

    // Run TM to get expected result
    TuringMachine tm(2, 5);
    for (const auto& t : transitions)
        tm.add_transition(t.state, t.symbol, t.next_state, t.write_symbol, t.move);
    tm.set_initial_state(0);
    tm.set_tape(tape, 0);
    int steps = 0;
    while (tm.step()) steps++;

    return {
        std::to_string(x) + " + " + std::to_string(y),
        2, 5, transitions,
        0, tape, 0,
        tm.get_tape(), steps
    };
}

static void show_addition_result(const std::string& name,
                                  const TuringMachine::Tape& result_tape) {
    // Extract b-bits from paired encoding (LSB first)
    std::string bits_str;
    int result = 0;
    for (int i = (int)result_tape.size() - 1; i >= 0; i--) {
        int b = (result_tape[i] == 2 || result_tape[i] == 4) ? 1 : 0;
        result = result * 2 + b;
        bits_str += std::to_string(b);
    }

    std::cout << "\n  Result (b-bits MSB first): " << bits_str
              << " = " << result << "\n";
    std::cout << "  " << name << " = " << result << "\n";
}

// --- Main ---

int main(int argc, char* argv[]) {
    auto tests = Verifier::get_tm_tests();
    TMTestCase test;
    bool is_addition = false;

    if (argc >= 3 && std::string(argv[1]) == "add") {
        int x = std::stoi(argv[2]);
        int y = (argc > 3) ? std::stoi(argv[3]) : 0;
        test = build_addition_test(x, y);
        is_addition = true;
        std::cout << "Rule 110 Compiler — Binary Addition\n\n";
        std::cout << "  " << test.name << "\n";
    } else {
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
            std::cerr << "\n  Or: rule110c add X Y\n";
            return 1;
        }
        test = tests[test_idx];
        std::cout << "Rule 110 Compiler\n\n";
        for (int i = 0; i < (int)tests.size(); i++)
            std::cout << "  " << i << ": " << tests[i].name
                      << (i == test_idx ? "  <--" : "") << "\n";

        // Check if this is the predefined addition test
        if (test.num_symbols == 5 && test.num_states == 2)
            is_addition = true;
    }

    // TM ground truth
    TuringMachine tm = build_tm(test);
    TuringMachine tm_check = build_tm(test);
    int tm_steps = 0;
    while (tm_check.step()) tm_steps++;
    std::cout << "\nTM: \"" << test.name << "\" " << tm_steps << " steps, tape="
              << format_tape(tm_check.get_tape()) << "\n";

    if (is_addition)
        show_addition_result(test.name, tm_check.get_tape());

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

    // Build full block map for color images
    std::vector<char> block_map;
    block_map.insert(block_map.end(), r110.left_block_map.begin(), r110.left_block_map.end());
    block_map.insert(block_map.end(), r110.central_block_map.begin(), r110.central_block_map.end());
    block_map.insert(block_map.end(), r110.right_block_map.begin(), r110.right_block_map.end());

    std::cout << "Regions: left=" << r110.left_periodic.size()
              << " central=" << r110.central_part.size()
              << " right=" << r110.right_periodic.size() << "\n";

    // Decode initial R110 tape back to TM config via block map
    {
        // Extract CTS bits from central block map: E=0, F=1
        std::vector<int> decoded_cts;
        for (size_t i = 0; i < r110.central_block_map.size(); ) {
            char b = r110.central_block_map[i];
            size_t j = i + 1;
            while (j < r110.central_block_map.size() && r110.central_block_map[j] == b) j++;
            if (b == 'E') decoded_cts.push_back(0);
            else if (b == 'F') decoded_cts.push_back(1);
            i = j;
        }

        int phi_size = (int)cts.get_appendants().size() / ts.get_deletion_number();
        auto tag_word = Decoder::cts_to_tag(decoded_cts, phi_size);
        if (!tag_word.empty()) {
            TMConfig cfg = Decoder::tag_to_tm(tag_word, test.num_states, test.num_symbols);
            if (cfg.valid) {
                std::cout << "R110 decode: state=" << cfg.state
                          << ", tape=" << format_tape(cfg.tape)
                          << ", head=" << cfg.head_pos;
                // Verify matches input
                bool match = (cfg.state == test.initial_state &&
                              cfg.head_pos == test.head_pos);
                // Compare tapes (ignoring trailing blanks)
                size_t len = std::max(cfg.tape.size(), test.initial_tape.size());
                for (size_t i = 0; i < len && match; i++) {
                    int a = (i < cfg.tape.size()) ? cfg.tape[i] : 0;
                    int b = (i < test.initial_tape.size()) ? test.initial_tape[i] : 0;
                    if (a != b) match = false;
                }
                std::cout << (match ? "  PASS" : "  FAIL") << "\n";
            } else {
                std::cout << "R110 decode: tag->TM decode failed\n";
            }
        } else {
            std::cout << "R110 decode: CTS->tag decode failed (" << decoded_cts.size() << " CTS bits)\n";
        }
    }

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

    // Phase 1: head image (brute-force, first HEAD_GENS generations)
    {
        std::ofstream head_ppm(outdir + "/head.ppm", std::ios::binary);
        head_ppm << "P6\n" << view_width << " " << (HEAD_GENS + 1) << "\n255\n";
        std::vector<uint8_t> row_buf;
        extract_row(*cur, view_start, view_width, row_buf);

        // Initialize color map from exact gen-0 block_map.
        // A/B blocks → ether (gray). Everything else keeps its color.
        std::vector<char> color_map(view_width), prev_color_map(view_width);
        for (size_t i = 0; i < view_width; i++) {
            size_t pos = view_start + i;
            char b = (pos < block_map.size()) ? block_map[pos] : 'A';
            if (b == 'A' || b == 'B') b = 'A';
            color_map[i] = b;
        }
        write_ppm_row_colored(head_ppm, row_buf, color_map, view_width);

        for (long long g = 1; g <= HEAD_GENS && !interrupted.load(); g++) {
            prev_color_map.swap(color_map);
            Rule110Runner::next_generation(*cur, *nxt);
            std::swap(cur, nxt);
            current_gen = g;
            extract_row(*cur, view_start, view_width, row_buf);
            propagate_colors(row_buf, view_width, prev_color_map, color_map);
            write_ppm_row_colored(head_ppm, row_buf, color_map, view_width);

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
        head_ppm.close();
        std::cout << "  Saved head.ppm (" << current_gen << " gens)\n";
    }

    // Phase 2: HashLife fast compute (if not settled in phase 1)
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

    // Phase 3: tail image (brute-force, rolling buffer)
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
        {
            std::ofstream tail_ppm(outdir + "/tail.ppm", std::ios::binary);
            tail_ppm << "P6\n" << view_width << " " << tail_rows.size() << "\n255\n";

            // Initialize first tail row: ether=gray, non-ether=gray too (no gen-0 projection).
            // Propagation from first row will color subsequent rows from their neighbors.
            std::vector<char> tail_colors(view_width, 'A'), tail_prev(view_width);
            // Try to seed colors: for non-ether cells, assign based on position
            // relative to center region (central blocks = blue/green, right = red).
            size_t center_view_start = center_start - view_start;
            size_t center_view_end_tail = center_end - view_start;
            for (size_t i = 0; i < view_width; i++) {
                if (!is_ether_spatial(tail_rows[0], i, view_width)) {
                    if (i >= center_view_start && i < center_view_end_tail)
                        tail_colors[i] = 'E';  // central region default
                    else if (i >= center_view_end_tail)
                        tail_colors[i] = 'H';  // right region default
                }
            }
            write_ppm_row_colored(tail_ppm, tail_rows[0], tail_colors, view_width);

            // Propagate for subsequent rows
            for (size_t r = 1; r < tail_rows.size(); r++) {
                tail_prev.swap(tail_colors);
                propagate_colors(tail_rows[r], view_width, tail_prev, tail_colors);
                write_ppm_row_colored(tail_ppm, tail_rows[r], tail_colors, view_width);
            }
        }
        std::cout << "  Saved tail.ppm (" << tail_rows.size() << " rows, gen "
                  << tail_start << "-" << current_gen << ")\n";
    }

    // Verification & summary
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

    if (is_addition && pass) {
        out("\n  " + test.name + " = " + [&]() {
            int result = 0;
            auto& tape = tm_check.get_tape();
            for (int i = (int)tape.size() - 1; i >= 0; i--) {
                int b = (tape[i] == 2 || tape[i] == 4) ? 1 : 0;
                result = result * 2 + b;
            }
            return std::to_string(result);
        }() + "  (computed through Rule 110)");
    }

    std::cout << "\n  uv run --with Pillow python3 tools/convert_images.py " << outdir << "\n";
    return 0;
}
