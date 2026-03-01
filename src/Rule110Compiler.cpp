#include "Rule110Compiler.hpp"
#include "BlockData.hpp"

namespace Rule110 {

static void emit(std::vector<int>& out, const std::vector<int>& bits) {
    out.insert(out.end(), bits.begin(), bits.end());
}

static void emit_repeat(std::vector<int>& out, const std::vector<int>& bits, int count) {
    for (int i = 0; i < count; i++)
        out.insert(out.end(), bits.begin(), bits.end());
}

static void emit_block(std::vector<int>& out, int& phase, char block) {
    phase = BlockData::next_phase(phase, block);
    emit(out, BlockData::get_block_row(block, phase));
}

// Y->II, N->IJ, first I->KH, empty->L. Move first K to end.
static std::vector<char> build_right_sequence(const CyclicTagSystem::Appendants& appendants) {
    std::vector<char> seq;
    for (const auto& app : appendants) {
        if (app.empty()) {
            seq.push_back('L');
        } else {
            std::vector<char> blocks;
            for (int bit : app) {
                blocks.push_back('I');
                blocks.push_back(bit == 1 ? 'I' : 'J');
            }
            blocks[0] = 'K';
            blocks.insert(blocks.begin() + 1, 'H');
            for (char c : blocks) seq.push_back(c);
        }
    }
    for (size_t i = 0; i < seq.size(); i++) {
        if (seq[i] == 'K') {
            seq.erase(seq.begin() + i);
            seq.push_back('K');
            break;
        }
    }
    return seq;
}

static void count_appendant_stats(const CyclicTagSystem::Appendants& appendants,
                                  int& ys, int& ns, int& nonempty, int& empty) {
    ys = ns = nonempty = empty = 0;
    for (const auto& app : appendants) {
        if (app.empty()) { empty++; continue; }
        nonempty++;
        for (int bit : app) { if (bit == 1) ys++; else ns++; }
    }
}

// Left periodic: A^v B A^13 B A^11 B A^12 B (spatial left to right)
// Phase tracking from C outward (right to left), then emit in reverse.
// B uses incoming phase; phase change applies to subsequent A blocks.
static std::vector<int> build_left_periodic(const CyclicTagSystem::Appendants& appendants) {
    int ys, ns, nonempty, empty;
    count_appendant_stats(appendants, ys, ns, nonempty, empty);
    long long v = 76LL * ys + 80LL * ns + 60LL * nonempty + 43LL * empty;

    struct Segment { char block; int phase; int count; };
    std::vector<Segment> segs;

    int phase = 0;
    int a_counts[] = {12, 11, 13, static_cast<int>(v)};
    for (int a_count : a_counts) {
        segs.push_back({'B', phase, 1});
        phase = BlockData::left_phase_change('B', phase);
        segs.push_back({'A', phase, a_count});
    }

    size_t total = 0;
    for (const auto& s : segs)
        total += BlockData::get_block_row(s.block, s.phase).size() * s.count;

    std::vector<int> bits;
    bits.reserve(total);
    for (int i = (int)segs.size() - 1; i >= 0; i--)
        emit_repeat(bits, BlockData::get_block_row(segs[i].block, segs[i].phase), segs[i].count);

    return bits;
}

Rule110State Rule110Compiler::compile(const CyclicTagSystem& cts) {
    Rule110State state;
    auto tape = cts.get_tape();
    const auto& appendants = cts.get_appendants();

    int phase = BlockData::INITIAL_PHASE;
    emit(state.central_part, BlockData::block_C(BlockData::ZERO_LOC));

    if (!tape.empty()) {
        for (int i = 0; i < (int)tape.size() - 1; i++) {
            emit_block(state.central_part, phase, tape[i] == 0 ? 'E' : 'F');
            emit_block(state.central_part, phase, 'D');
        }
        emit_block(state.central_part, phase, tape.back() == 0 ? 'E' : 'F');
        emit_block(state.central_part, phase, 'G');
    }

    auto right_seq = build_right_sequence(appendants);
    int right_phase = phase;
    for (char block : right_seq)
        emit_block(state.right_periodic, right_phase, block);

    state.left_periodic = build_left_periodic(appendants);

    return state;
}

} // namespace Rule110
