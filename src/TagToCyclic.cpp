#include "TagToCyclic.hpp"

namespace Rule110 {

// Unary encoding: tag symbol k -> [0]^k 1 [0]^{|Φ|-1-k}
// Each symbol becomes a block of |Φ| bits with exactly one 1.
static void encode_symbol(std::vector<int>& out, int symbol, int phi_size) {
    for (int i = 0; i < symbol; i++)       out.push_back(0);
    out.push_back(1);
    for (int i = symbol + 1; i < phi_size; i++) out.push_back(0);
}

CyclicTagSystem TagToCyclicConverter::convert(const TagSystem& ts) {
    const auto& rules = ts.get_rules();
    int s = ts.get_deletion_number();

    // Alphabet size: max symbol ID + 1 (dense IDs from TMToTag)
    int phi_size = 0;
    for (const auto& [sym, rhs] : rules) {
        if (sym >= phi_size) phi_size = sym + 1;
        for (int v : rhs)
            if (v >= phi_size) phi_size = v + 1;
    }

    // Pad to multiple of 6 (required by Rule 110 stage)
    while (phi_size % 6 != 0)
        phi_size++;

    // Build appendants: one per symbol in order 0..phi_size-1
    // For symbol i: take its production RHS, unary-encode each symbol
    // Padding symbols and symbols without rules get empty appendants
    CyclicTagSystem::Appendants appendants;
    for (int i = 0; i < phi_size; i++) {
        CyclicTagSystem::BinaryString app;
        auto it = rules.find(i);
        if (it != rules.end()) {
            for (int sym : it->second)
                encode_symbol(app, sym, phi_size);
        }
        appendants.push_back(std::move(app));
    }

    // Extend to s*|Φ| appendants with (s-1)*|Φ| empties
    int total = s * phi_size;
    appendants.resize(total);

    // Initial tape: unary encoding of tag word
    CyclicTagSystem::BinaryString tape;
    for (int sym : ts.get_word())
        encode_symbol(tape, sym, phi_size);

    CyclicTagSystem cts;
    cts.set_appendants(appendants);
    cts.set_tape(tape);
    return cts;
}

} // namespace Rule110
