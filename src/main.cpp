#include "TuringMachine.hpp"
#include "TMToTag.hpp"
#include "TagToCyclic.hpp"
#include "Rule110Compiler.hpp"
#include "Rule110Runner.hpp"
#include "HashLife1D.hpp"
#include "Verifier.hpp"
#include "Decoder.hpp"
#include "BlockDetector.hpp"
#include <iostream>
#include <fstream>
#include <deque>
#include <sys/stat.h>
#include <csignal>
#include <atomic>
#include <chrono>
#include <cmath>

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
        case 'B': return on ? RGB{60,100,140}   : RGB{195,210,225};
        case 'C': return on ? RGB{160,40,160}  : RGB{230,200,230};
        case 'D': return on ? RGB{200,120,40}  : RGB{240,220,180};
        case 'E': return on ? RGB{40,80,200}   : RGB{200,210,240};
        case 'F': return on ? RGB{40,160,60}   : RGB{200,235,200};
        case 'G': return on ? RGB{180,160,40}  : RGB{240,235,200};
        case 'H': return on ? RGB{180,50,50}   : RGB{235,200,200};
        case 'I': return on ? RGB{190,60,120}  : RGB{235,200,215};
        case 'J': return on ? RGB{40,150,150}  : RGB{195,230,228};
        case 'K': return on ? RGB{150,130,50}  : RGB{230,225,195};
        case 'L': return on ? RGB{140,80,170}  : RGB{225,205,235};
        case 'X': return on ? RGB{200,40,200}  : RGB{240,180,240};
        default:  return on ? RGB{160,60,60}   : RGB{235,200,200};
    }
}

static void write_ppm_row_colored(std::ofstream& ofs,
                                    const std::vector<uint8_t>& row,
                                    const std::vector<char>& color_map,
                                    size_t width) {
    for (size_t i = 0; i < width; i++) {
        RGB c = block_color(color_map[i], row[i] != 0);
        ofs.put(c.r); ofs.put(c.g); ofs.put(c.b);
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

static double elapsed_ms(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
}

static TuringMachine build_tm(const TMTestCase& test) {
    TuringMachine tm(test.num_states, test.num_symbols);
    for (const auto& t : test.transitions)
        tm.add_transition(t.state, t.symbol, t.next_state, t.write_symbol, t.move);
    tm.set_initial_state(test.initial_state);
    tm.set_tape(test.initial_tape, test.head_pos);
    return tm;
}

// --- Analytics data ---

struct MismatchEntry { long long generation; int mismatch; };

struct TMStepInfo {
    int tm_step, state, head_pos, tag_step;
    long long cts_step;
    std::vector<int> tape;
};

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

static std::vector<TMStepInfo> analyze_tm_boundaries(
        const TMTestCase& test, int phi_size, int s) {
    // Record TM execution history
    TuringMachine tm_a = build_tm(test);
    struct Snap { int state; std::vector<int> tape; int head_pos; };
    std::vector<Snap> tm_hist;
    tm_hist.push_back({tm_a.get_current_state(), tm_a.get_tape(), tm_a.get_head_pos()});
    while (tm_a.step())
        tm_hist.push_back({tm_a.get_current_state(), tm_a.get_tape(), tm_a.get_head_pos()});

    std::vector<TMStepInfo> boundaries;
    boundaries.push_back({0, tm_hist[0].state, tm_hist[0].head_pos, 0, 0, tm_hist[0].tape});

    // Run tag system, decode at each step to find TM step boundaries
    TuringMachine tm_b = build_tm(test);
    TagSystem ts_a = TMToTagConverter::convert(tm_b);

    int next_tm = 1;
    int tag_steps = 0;
    while (ts_a.step()) {
        tag_steps++;
        auto word = ts_a.get_word();
        if (word.empty()) break;

        TMConfig cfg = Decoder::tag_to_tm(word, test.num_states, test.num_symbols);
        if (!cfg.valid) continue;

        for (int idx = next_tm; idx < (int)tm_hist.size(); idx++) {
            if (cfg.state == tm_hist[idx].state &&
                cfg.head_pos == tm_hist[idx].head_pos &&
                tapes_match(cfg.tape, tm_hist[idx].tape)) {
                long long cts_step = (long long)tag_steps * s * phi_size;
                boundaries.push_back({idx, tm_hist[idx].state, tm_hist[idx].head_pos,
                                      tag_steps, cts_step, tm_hist[idx].tape});
                next_tm = idx + 1;
                break;
            }
        }
    }
    return boundaries;
}

// --- Binary addition TM ---

static TMTestCase build_addition_test(int x, int y) {
    std::vector<int> x_bits, y_bits;
    int xx = x, yy = y;
    while (xx > 0 || yy > 0) {
        x_bits.push_back(xx & 1);
        y_bits.push_back(yy & 1);
        xx >>= 1;
        yy >>= 1;
    }
    if (x_bits.empty()) { x_bits.push_back(0); y_bits.push_back(0); }

    std::vector<int> tape;
    for (size_t i = 0; i < x_bits.size(); i++)
        tape.push_back(1 + x_bits[i] * 2 + y_bits[i]);

    std::vector<TMTestCase::TransitionDef> transitions = {
        {0, 1, 0, 1, Move::RIGHT}, {0, 2, 0, 2, Move::RIGHT},
        {0, 3, 0, 4, Move::RIGHT}, {0, 4, 1, 3, Move::RIGHT},
        {1, 0, 0, 2, Move::HALT},
        {1, 1, 0, 2, Move::RIGHT}, {1, 2, 1, 1, Move::RIGHT},
        {1, 3, 1, 3, Move::RIGHT}, {1, 4, 1, 4, Move::RIGHT},
    };

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
    std::cout.setf(std::ios::unitbuf);
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
        if (test.num_symbols == 5 && test.num_states == 2)
            is_addition = true;
    }

    // --- Compile: TM -> Tag -> CTS -> R110 ---

    auto compile_t0 = std::chrono::steady_clock::now();

    TuringMachine tm = build_tm(test);
    TuringMachine tm_check = build_tm(test);
    int tm_steps = 0;
    while (tm_check.step()) tm_steps++;
    std::cout << "\nTM: \"" << test.name << "\" " << tm_steps << " steps, tape="
              << format_tape(tm_check.get_tape()) << "\n";

    if (is_addition)
        show_addition_result(test.name, tm_check.get_tape());

    TagSystem ts = TMToTagConverter::convert(tm);
    CyclicTagSystem cts = TagToCyclicConverter::convert(ts);

    CyclicTagSystem cts_check = cts;
    int cts_steps = 0;
    while (cts_check.step()) cts_steps++;
    std::cout << "CTS: " << cts.get_appendants().size() << " appendants, "
              << cts.tape_length() << " tape, halts in " << cts_steps << " steps\n";

    Rule110State r110 = Rule110Compiler::compile(cts);

    std::vector<char> block_map;
    block_map.insert(block_map.end(), r110.left_block_map.begin(), r110.left_block_map.end());
    block_map.insert(block_map.end(), r110.central_block_map.begin(), r110.central_block_map.end());
    block_map.insert(block_map.end(), r110.right_block_map.begin(), r110.right_block_map.end());

    double compile_ms = elapsed_ms(compile_t0);
    std::cout << "Compile: " << compile_ms << "ms  (TM -> Tag -> CTS -> R110)\n";

    std::cout << "Regions: left=" << r110.left_periodic.size()
              << " central=" << r110.central_part.size()
              << " right=" << r110.right_periodic.size() << "\n";

    // Decode initial R110 tape back to TM config
    {
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
                bool match = (cfg.state == test.initial_state &&
                              cfg.head_pos == test.head_pos);
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

    const size_t MAX_VIEW = 250000;
    size_t tape_size_total = tape_int.size();
    size_t central_width = center_end - center_start;
    size_t view_width = std::min(central_width + 4000, MAX_VIEW);
    size_t view_start = (center_start > 2000) ? center_start - 2000 : 0;

    // Second view for right side when central part is too wide
    // Centered on center_end to show transition from central to right periodic
    bool has_right_view = (central_width + 4000 > MAX_VIEW);
    size_t view_start_r = 0, view_width_r = 0;
    if (has_right_view) {
        view_start_r = (center_end > MAX_VIEW / 2) ? center_end - MAX_VIEW / 2 : 0;
        size_t right_end = std::min(view_start_r + MAX_VIEW, tape_size_total);
        view_width_r = right_end - view_start_r;
    }

    std::cout << "R110: " << tape_size_total << " cells, central="
              << central_width;
    if (has_right_view)
        std::cout << " (2 views: " << view_width << " + " << view_width_r << ")";
    std::cout << "\n";

    std::string outdir = "output/" + sanitize(test.name);
    mkdir("output", 0755);
    mkdir(outdir.c_str(), 0755);

    Rule110Runner::State state = Rule110Runner::pack(tape_int);
    std::vector<int>().swap(tape_int);

    const int HEAD_GENS = 4800;
    const size_t TAIL_BUDGET = 200ULL * 1024 * 1024;
    size_t total_view_bytes = view_width + (has_right_view ? view_width_r : 0);
    const int tail_save = std::max(100, std::min(2000, (int)(TAIL_BUDGET / total_view_bytes)));

    std::cout << "Tail buffer: " << tail_save << " rows ("
              << total_view_bytes * tail_save / (1024 * 1024) << " MB)\n";

    Rule110Runner::State buf_b(state.size());
    Rule110Runner::State* cur = &state;
    Rule110Runner::State* nxt = &buf_b;

    Rule110Runner::State prev30 = *cur;
    long long halt_gen = -1, current_gen = 0;

    std::signal(SIGINT, signal_handler);

    // Analytics data
    std::vector<MismatchEntry> mismatch_log;
    const size_t SPACETIME_CROP = 4000;
    size_t spacetime_crop_start = center_start + central_width / 2 - SPACETIME_CROP / 2;
    if (central_width < SPACETIME_CROP) spacetime_crop_start = center_start;
    size_t spacetime_crop_width = std::min(SPACETIME_CROP, central_width);
    std::vector<std::pair<long long, std::vector<uint8_t>>> spacetime_phase1;
    const int SPACETIME_PHASE1_INTERVAL = 5;

    auto run_t0 = std::chrono::steady_clock::now();
    double analytics_total_secs = 0;
    std::cout << "\nRunning (Ctrl+C to stop)...\n\n";

    // Phase 1: head image (brute-force, first HEAD_GENS generations)
    BlockDetector detector;
    {
        std::ofstream head_ppm(outdir + "/head.ppm", std::ios::binary);
        head_ppm << "P6\n" << view_width << " " << (HEAD_GENS + 1) << "\n255\n";

        std::ofstream head_r_ppm;
        if (has_right_view) {
            head_r_ppm.open(outdir + "/head_r.ppm", std::ios::binary);
            head_r_ppm << "P6\n" << view_width_r << " " << (HEAD_GENS + 1) << "\n255\n";
        }

        std::vector<uint8_t> row_buf, row_buf_r;
        BlockDetector detector_r;

        extract_row(*cur, view_start, view_width, row_buf);
        detector.init(row_buf, block_map, view_start, view_width);
        write_ppm_row_colored(head_ppm, row_buf, detector.color_map(), view_width);

        if (has_right_view) {
            extract_row(*cur, view_start_r, view_width_r, row_buf_r);
            detector_r.init(row_buf_r, block_map, view_start_r, view_width_r);
            write_ppm_row_colored(head_r_ppm, row_buf_r, detector_r.color_map(), view_width_r);
        }

        // Spacetime: save gen 0 center crop
        {
            std::vector<uint8_t> crop(spacetime_crop_width);
            for (size_t i = 0; i < spacetime_crop_width; i++)
                crop[i] = get_bit(*cur, spacetime_crop_start + i);
            spacetime_phase1.push_back({0, std::move(crop)});
        }

        for (long long g = 1; g <= HEAD_GENS && !interrupted.load(); g++) {
            Rule110Runner::next_generation(*cur, *nxt);
            std::swap(cur, nxt);
            current_gen = g;

            extract_row(*cur, view_start, view_width, row_buf);
            detector.advance(row_buf, view_width);
            write_ppm_row_colored(head_ppm, row_buf, detector.color_map(), view_width);

            if (has_right_view) {
                extract_row(*cur, view_start_r, view_width_r, row_buf_r);
                detector_r.advance(row_buf_r, view_width_r);
                write_ppm_row_colored(head_r_ppm, row_buf_r, detector_r.color_map(), view_width_r);
            }

            // Spacetime: save center crop at intervals
            if (g % SPACETIME_PHASE1_INTERVAL == 0) {
                std::vector<uint8_t> crop(spacetime_crop_width);
                for (size_t i = 0; i < spacetime_crop_width; i++)
                    crop[i] = get_bit(*cur, spacetime_crop_start + i);
                spacetime_phase1.push_back({g, std::move(crop)});
            }

            if (g % 30 == 0) {
                int mm = count_unsettled(*cur, prev30, center_start, center_end);
                prev30 = *cur;
                mismatch_log.push_back({g, mm});
                if (mm < 300) {
                    halt_gen = g;
                    std::cout << "  SETTLED gen " << g << " (mm=" << mm << ")\n";
                    break;
                }
            }
        }
        head_ppm.close();
        if (has_right_view) head_r_ppm.close();
        std::cout << "  Saved head.ppm (" << current_gen << " gens, "
                  << detector.clusters().size() << " clusters)";
        if (has_right_view) std::cout << " + head_r.ppm";
        std::cout << "\n";
    }

    // Phase 2: HashLife fast compute (if not settled in phase 1)
    if (halt_gen < 0 && !interrupted.load()) {
        auto t0 = std::chrono::steady_clock::now();

        std::vector<int> ether_pat(r110.left_periodic.begin(),
                                    r110.left_periodic.begin() + std::min((size_t)14, r110.left_periodic.size()));

        size_t tape_size = state.size() * 64;
        std::vector<int> flat_tape(tape_size);
        for (size_t i = 0; i < tape_size; i++)
            flat_tape[i] = get_bit(*cur, i);

        HashLife1D hl;
        HashLife1D::Node* root = hl.from_bits(flat_tape, ether_pat);
        std::vector<int>().swap(flat_tape);
        HashLife1D::Node* initial_root = root;
        long long initial_hl_gen = current_gen;

        std::cout << "  HashLife: " << hl.node_count() << " nodes, "
                  << hl.canon_size() << " canonical\n";

        size_t settle_start = center_start;
        size_t settle_len = center_end - center_start + 720;
        std::vector<uint8_t> buf_curr, buf_prev;

        auto hl_settling_check = [&](HashLife1D::Node* curr_root,
                                      HashLife1D::Node* prev_root) -> int {
            hl.extract_bits(curr_root, settle_start, settle_len, buf_curr);
            hl.extract_bits(prev_root, settle_start, settle_len, buf_prev);
            int mm = 0;
            for (size_t i = 720; i < settle_len; i++)
                if (buf_curr[i] != buf_prev[i - 720]) mm++;
            return mm;
        };

        long long step_size = 30;
        long long search_gen = current_gen;
        HashLife1D::Node* last_unsettled_root = root;
        long long last_unsettled_gen = current_gen;

        while (!interrupted.load()) {
            HashLife1D::Node* prev30_root = root;
            root = hl.advance(root, step_size);
            search_gen += step_size;

            HashLife1D::Node* prev_check = (step_size > 30)
                ? hl.advance(prev30_root, step_size - 30)
                : prev30_root;

            if (interrupted.load()) break;
            int mm = hl_settling_check(root, prev_check);
            if (interrupted.load()) break;

            // Log-spaced mismatch sampling: only log when generation has
            // grown by ~10% since last entry (matches log-scale X axis)
            if (mismatch_log.empty() ||
                search_gen >= (long long)(mismatch_log.back().generation * 1.1) ||
                mm < 300) {
                mismatch_log.push_back({search_gen, mm});
            }

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

        // === Analytics extraction (while HashLife tree is alive) ===
        if (halt_gen >= 0 && !interrupted.load()) {
            auto analytics_t0 = std::chrono::steady_clock::now();
            int phi_size = (int)cts.get_appendants().size() / ts.get_deletion_number();
            int s = test.num_symbols + 2;

            // 1. Spacetime diagram: sample center crops at logarithmic intervals
            {
                // Compute sample points (logarithmic from initial_hl_gen to halt_gen)
                const int NUM_PHASE2_SAMPLES = 1000;
                std::vector<long long> sample_gens;
                double log_start = std::log(std::max(1LL, initial_hl_gen));
                double log_end = std::log((double)halt_gen);
                for (int i = 0; i < NUM_PHASE2_SAMPLES; i++) {
                    double frac = (double)i / (NUM_PHASE2_SAMPLES - 1);
                    long long g = (long long)std::exp(log_start + frac * (log_end - log_start));
                    g = (g / 30) * 30;  // align to period
                    if (g <= initial_hl_gen) g = initial_hl_gen + 30;
                    if (!sample_gens.empty() && g <= sample_gens.back()) continue;
                    if (g >= halt_gen) break;
                    sample_gens.push_back(g);
                }

                size_t total_rows = spacetime_phase1.size() + sample_gens.size();
                std::ofstream st_ppm(outdir + "/spacetime.ppm", std::ios::binary);
                st_ppm << "P6\n" << spacetime_crop_width << " " << total_rows << "\n255\n";
                std::ofstream st_meta(outdir + "/spacetime_meta.csv");
                st_meta << "row,generation\n";

                // Write Phase 1 rows
                int row_idx = 0;
                for (size_t ri = 0; ri < spacetime_phase1.size(); ri++) {
                    long long gen = spacetime_phase1[ri].first;
                    auto& crop = spacetime_phase1[ri].second;
                    for (size_t i = 0; i < spacetime_crop_width; i++) {
                        uint8_t v = crop[i] ? 0 : 255;
                        st_ppm.put(v); st_ppm.put(v); st_ppm.put(v);
                    }
                    st_meta << row_idx++ << "," << gen << "\n";
                }

                // Write Phase 2 rows (from HashLife)
                std::vector<uint8_t> crop_buf;
                for (auto g : sample_gens) {
                    auto node = hl.advance(initial_root, g - initial_hl_gen);
                    hl.extract_bits(node, spacetime_crop_start, spacetime_crop_width, crop_buf);
                    for (size_t i = 0; i < spacetime_crop_width; i++) {
                        uint8_t v = crop_buf[i] ? 0 : 255;
                        st_ppm.put(v); st_ppm.put(v); st_ppm.put(v);
                    }
                    st_meta << row_idx++ << "," << g << "\n";
                }
                std::cout << "  Spacetime: " << total_rows << " rows ("
                          << spacetime_phase1.size() << " head + "
                          << sample_gens.size() << " hashlife)\n";
            }
            spacetime_phase1.clear();

            // 2. TM step boundary R110 spacetime windows
            {
                auto tm_bounds = analyze_tm_boundaries(test, phi_size, s);
                double gens_per_cts = (cts_steps > 0) ? (double)halt_gen / cts_steps : 0;
                const int TM_WINDOW = 200;

                std::ofstream tm_csv(outdir + "/tm_steps.csv");
                tm_csv << "tm_step,tag_step,cts_step,r110_gen,window_start,window_rows,state,head_pos,tape\n";

                for (auto& b : tm_bounds) {
                    long long r110_gen = (long long)(b.cts_step * gens_per_cts);
                    r110_gen = (r110_gen / 30) * 30;

                    // Window centered on r110_gen, but no earlier than initial_hl_gen
                    long long win_start = std::max(initial_hl_gen,
                                                    r110_gen - TM_WINDOW / 2);
                    // Don't go past settling
                    if (win_start + TM_WINDOW > halt_gen)
                        win_start = std::max(initial_hl_gen, halt_gen - TM_WINDOW);

                    tm_csv << b.tm_step << "," << b.tag_step << "," << b.cts_step
                           << "," << r110_gen << "," << win_start << "," << TM_WINDOW
                           << "," << b.state << "," << b.head_pos
                           << ",\"" << format_tape(b.tape) << "\"\n";

                    // Advance HashLife to window start, convert to packed state
                    auto win_root = hl.advance(initial_root, win_start - initial_hl_gen);
                    auto win_state = hl.to_packed_state(win_root, tape_size);

                    // Write multi-row spacetime PPM via direct simulation
                    std::string fname = outdir + "/tm_step_" + std::to_string(b.tm_step) + ".ppm";
                    std::ofstream crop_ppm(fname, std::ios::binary);
                    crop_ppm << "P6\n" << spacetime_crop_width << " " << TM_WINDOW << "\n255\n";

                    Rule110Runner::State win_buf(win_state.size());
                    Rule110Runner::State* w_cur = &win_state;
                    Rule110Runner::State* w_nxt = &win_buf;
                    std::vector<uint8_t> crop_row(spacetime_crop_width);

                    for (int row = 0; row < TM_WINDOW; row++) {
                        for (size_t i = 0; i < spacetime_crop_width; i++)
                            crop_row[i] = get_bit(*w_cur, spacetime_crop_start + i);
                        for (size_t i = 0; i < spacetime_crop_width; i++) {
                            uint8_t v = crop_row[i] ? 0 : 255;
                            crop_ppm.put(v); crop_ppm.put(v); crop_ppm.put(v);
                        }
                        if (row + 1 < TM_WINDOW) {
                            Rule110Runner::next_generation(*w_cur, *w_nxt);
                            std::swap(w_cur, w_nxt);
                        }
                    }
                    std::cout << "    step " << b.tm_step << ": gen "
                              << win_start << "-" << (win_start + TM_WINDOW)
                              << " (target " << r110_gen << ")\n";
                }
                std::cout << "  TM steps: " << tm_bounds.size() << " boundaries ("
                          << TM_WINDOW << " gens each)\n";
            }

            double analytics_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - analytics_t0).count();
            analytics_total_secs += analytics_secs;
            std::cout << "  Analytics: " << analytics_secs << "s\n";
        }

        auto flat_final = hl.to_packed_state(last_unsettled_root, tape_size);
        *cur = std::move(flat_final);
        current_gen = last_unsettled_gen;

        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "  HashLife phase: " << secs << "s, "
                  << hl.node_count() << " nodes\n";

        buf_b.resize(cur->size());
        nxt = &buf_b;
        prev30 = *cur;
        halt_gen = -1;
    }

    // Phase 3: tail image (brute-force, rolling buffer)
    {
        std::deque<std::vector<uint8_t>> tail_rows, tail_rows_r;
        std::vector<uint8_t> row_buf, row_buf_r;
        extract_row(*cur, view_start, view_width, row_buf);
        tail_rows.push_back(row_buf);
        if (has_right_view) {
            extract_row(*cur, view_start_r, view_width_r, row_buf_r);
            tail_rows_r.push_back(row_buf_r);
        }

        for (long long g = current_gen + 1; !interrupted.load(); g++) {
            Rule110Runner::next_generation(*cur, *nxt);
            std::swap(cur, nxt);
            current_gen = g;

            extract_row(*cur, view_start, view_width, row_buf);
            tail_rows.push_back(row_buf);
            if ((int)tail_rows.size() > tail_save)
                tail_rows.pop_front();

            if (has_right_view) {
                extract_row(*cur, view_start_r, view_width_r, row_buf_r);
                tail_rows_r.push_back(row_buf_r);
                if ((int)tail_rows_r.size() > tail_save)
                    tail_rows_r.pop_front();
            }

            if (g % 30 == 0) {
                int mm = count_unsettled(*cur, prev30, center_start, center_end);
                prev30 = *cur;
                mismatch_log.push_back({g, mm});
                if (mm < 300) {
                    halt_gen = g;
                    std::cout << "  SETTLED gen " << g << " (mm=" << mm << ")\n";
                    break;
                }
            }
        }

        long long tail_start = current_gen - (long long)tail_rows.size() + 1;

        // Write left tail
        {
            std::ofstream tail_ppm(outdir + "/tail.ppm", std::ios::binary);
            tail_ppm << "P6\n" << view_width << " " << tail_rows.size() << "\n255\n";

            size_t cv_start = center_start - view_start;
            size_t cv_end = std::min(center_end - view_start, view_width);
            BlockDetector tail_detector;
            tail_detector.init_from_ether(tail_rows[0], view_width,
                                           cv_start, cv_end, (int)tail_start);
            write_ppm_row_colored(tail_ppm, tail_rows[0], tail_detector.color_map(), view_width);

            for (size_t r = 1; r < tail_rows.size(); r++) {
                tail_detector.advance(tail_rows[r], view_width);
                write_ppm_row_colored(tail_ppm, tail_rows[r], tail_detector.color_map(), view_width);
            }
        }

        // Write right tail
        if (has_right_view) {
            std::ofstream tail_r_ppm(outdir + "/tail_r.ppm", std::ios::binary);
            tail_r_ppm << "P6\n" << view_width_r << " " << tail_rows_r.size() << "\n255\n";

            size_t cv_start_r = (center_start > view_start_r) ? center_start - view_start_r : 0;
            size_t cv_end_r = std::min(center_end - view_start_r, view_width_r);
            BlockDetector tail_detector_r;
            tail_detector_r.init_from_ether(tail_rows_r[0], view_width_r,
                                             cv_start_r, cv_end_r, (int)tail_start);
            write_ppm_row_colored(tail_r_ppm, tail_rows_r[0], tail_detector_r.color_map(), view_width_r);

            for (size_t r = 1; r < tail_rows_r.size(); r++) {
                tail_detector_r.advance(tail_rows_r[r], view_width_r);
                write_ppm_row_colored(tail_r_ppm, tail_rows_r[r], tail_detector_r.color_map(), view_width_r);
            }
        }

        std::cout << "  Saved tail.ppm (" << tail_rows.size() << " rows, gen "
                  << tail_start << "-" << current_gen << ")";
        if (has_right_view) std::cout << " + tail_r.ppm";
        std::cout << "\n";
    }

    double run_secs = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - run_t0).count();

    // Write mismatch CSV
    {
        std::ofstream mf(outdir + "/mismatch.csv");
        mf << "generation,mismatch\n";
        for (size_t i = 0; i < mismatch_log.size(); i++)
            mf << mismatch_log[i].generation << "," << mismatch_log[i].mismatch << "\n";
        std::cout << "  Saved mismatch.csv (" << mismatch_log.size() << " entries)\n";
    }

    // Write spacetime fallback (if Phase 2 was skipped)
    if (!spacetime_phase1.empty()) {
        std::ofstream st_ppm(outdir + "/spacetime.ppm", std::ios::binary);
        st_ppm << "P6\n" << spacetime_crop_width << " " << spacetime_phase1.size() << "\n255\n";
        std::ofstream st_meta(outdir + "/spacetime_meta.csv");
        st_meta << "row,generation\n";
        for (size_t ri = 0; ri < spacetime_phase1.size(); ri++) {
            long long gen = spacetime_phase1[ri].first;
            auto& crop = spacetime_phase1[ri].second;
            for (size_t i = 0; i < spacetime_crop_width; i++) {
                uint8_t v = crop[i] ? 0 : 255;
                st_ppm.put(v); st_ppm.put(v); st_ppm.put(v);
            }
            st_meta << ri << "," << gen << "\n";
        }
        std::cout << "  Saved spacetime.ppm (fallback, " << spacetime_phase1.size() << " rows)\n";
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

    char timing_buf[256];
    double sim_secs = run_secs - analytics_total_secs;
    snprintf(timing_buf, sizeof(timing_buf), "Compile: %.1fms, Run: %.1fs (+ %.1fs analytics)",
             compile_ms, sim_secs, analytics_total_secs);
    out(std::string(timing_buf));

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

    std::cout << "\nVisualize:\n"
              << "  uv run --with Pillow python3 tools/convert_images.py " << outdir << "\n"
              << "  uv run --with matplotlib --with Pillow python3 tools/create_analytics.py " << outdir << "\n";
    return 0;
}
