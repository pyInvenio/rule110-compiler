#include "TMToTag.hpp"
#include <string>

namespace Rule110 {

// Symbol ID layout for alphabet Φ (4m + 3ms symbols):
//   [0, m)         : H_ψi        (state-only, 4 types)
//   [m, 2m)        : L_ψi
//   [2m, 3m)       : R_ψi
//   [3m, 4m)       : R*_ψi
//   [4m, 4m+ms)    : H_ψi_σj    (state×symbol, 3 types)
//   [4m+ms, 4m+2ms): L_ψi_σj
//   [4m+2ms, 4m+3ms): R_ψi_σj
struct SymbolMap {
    int m, s;
    int H(int i)           { return i; }
    int L(int i)           { return m + i; }
    int R(int i)           { return 2*m + i; }
    int Rstar(int i)       { return 3*m + i; }
    int Hsym(int i, int j) { return 4*m + i*s + j; }
    int Lsym(int i, int j) { return 4*m + m*s + i*s + j; }
    int Rsym(int i, int j) { return 4*m + 2*m*s + i*s + j; }
};

static std::vector<int> repeat(int symbol, int n) {
    return std::vector<int>(n, symbol);
}

static std::vector<int> concat(std::vector<int> a, const std::vector<int>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

static long long ipow(int base, int exp) {
    long long r = 1;
    for (int i = 0; i < exp; i++) r *= base;
    return r;
}

TagSystem TMToTagConverter::convert(const TuringMachine& tm) {
    int m = tm.get_num_states();
    int t = tm.get_num_symbols();
    int s = t + 2;

    TagSystem ts(s);
    SymbolMap S{m, s};

    for (int i = 0; i < m; i++) {
        ts.set_symbol_name(S.H(i),     "H_"  + std::to_string(i));
        ts.set_symbol_name(S.L(i),     "L_"  + std::to_string(i));
        ts.set_symbol_name(S.R(i),     "R_"  + std::to_string(i));
        ts.set_symbol_name(S.Rstar(i), "R*_" + std::to_string(i));
        for (int j = 0; j < s; j++) {
            std::string suf = std::to_string(i) + "," + std::to_string(j);
            ts.set_symbol_name(S.Hsym(i,j), "H_" + suf);
            ts.set_symbol_name(S.Lsym(i,j), "L_" + suf);
            ts.set_symbol_name(S.Rsym(i,j), "R_" + suf);
        }
    }

    // Rules (page 33)

    // Expansion rules

    // first 3
    for (int i = 0; i < m; i++) {
        std::vector<int> hprod, lprod, rprod;
        for (int j = 0; j < s; j++) {
            hprod.push_back(S.Hsym(i,j));
            lprod.push_back(S.Lsym(i,j));
            rprod.push_back(S.Rsym(i,j));
        }
        ts.add_rule(S.H(i), hprod);
        ts.add_rule(S.L(i), lprod);
        ts.add_rule(S.R(i), rprod);
    }

    // rule 4
    for (int i = 0; i < m; i++)
        ts.add_rule(S.Rstar(i), repeat(S.R(i), s));

    // Transition-dependent rules for each (state i, symbol j)
    // We use 0-indexed: j in {0..s-1}.
    // Γ = next_state, Υ = write_symbol -> 1
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < s; j++) {

            if (j >= t) {
                // Boundary symbols σ_{t+1}, σ_{t+2} (our j=t, j=t+1)
                // Periodic tape assumed all-blank (σ₁): a_k=1, e_k=1 (1-based), w=1, z=1
                // H_ψi_σ_{t+1} -> [H_ψi]^{t+1+s-a1} [L_ψi]^{s^w}  (a1=1, w=1)
                // H_ψi_σ_{t+2} -> [R*_ψi]^{0} [H_ψi]^{t+2+s-e1}   (e1=1, z=1)
                // L and R for both: -> [L_ψi]^s and [R_ψi]^s
                if (j == t) {
                    ts.add_rule(S.Hsym(i,j), concat(repeat(S.H(i), t+s), repeat(S.L(i), s)));
                } else {
                    ts.add_rule(S.Hsym(i,j), repeat(S.H(i), t+s+1));
                }
                ts.add_rule(S.Lsym(i,j), repeat(S.L(i), s));
                ts.add_rule(S.Rsym(i,j), repeat(S.R(i), s));

            } else {
                // Regular symbol j in {0..t-1}
                const auto& tr = tm.get_transition(i, j);

                if (tr.move_direction == Move::HALT) {
                    // Halt -> empty productions
                    ts.add_rule(S.Hsym(i,j), {});
                    ts.add_rule(S.Lsym(i,j), {});
                    ts.add_rule(S.Rsym(i,j), {});

                } else {
                    int G = tr.next_state;       // Γ
                    int Y = tr.write_symbol + 1; // Υ (1-based)
                    int jp1 = j + 1;             // j in Cook's 1-based

                    if (tr.move_direction == Move::LEFT) {
                        // H: [R*_Γ]^{s(s-Υ)} [H_Γ]^j     (j is 1-based = jp1)
                        // L: L_Γ(ψi,σj)  -> L_Γ
                        // R: [R_Γ(ψi,σj)]^{s²}  -> [R_Γ]^{s²}
                        ts.add_rule(S.Hsym(i,j), concat(repeat(S.Rstar(G), s*(s-Y)), repeat(S.H(G), jp1)));
                        ts.add_rule(S.Lsym(i,j), {S.L(G)});
                        ts.add_rule(S.Rsym(i,j), repeat(S.R(G), s*s));

                    } else { // RIGHT
                        // H: [H_Γ]^j [L_Γ]^{s(s-Υ)}
                        // L: [L_Γ]^{s²}
                        // R: R_Γ
                        ts.add_rule(S.Hsym(i,j), concat(repeat(S.H(G), jp1), repeat(S.L(G), s*(s-Y))));
                        ts.add_rule(S.Lsym(i,j), repeat(S.L(G), s*s));
                        ts.add_rule(S.Rsym(i,j), {S.R(G)});
                    }
                }
            }
        }
    }

    // Initial Word (page 33)
    // Tape: ...a  b_x...b_1  [c]  d_1...d_y  e...
    // a, e are periodic (we assume blank = symbol 0, period 1)
    // b = tape cells left of head, d = tape cells right of head
    // c = symbol under head (1-based in formula)
    // Initial word: [H_γ]^{1+s-c}  [L_γ]^{s^{x+1} + Σ_{k=1}^{x}(s-b_k)s^k}  [R_γ]^{Σ_{k=1}^{y}(s-d_k)s^k}

    int gamma = tm.get_current_state();
    int c_1based = tm.get_tape()[tm.get_head_pos()] + 1;
    int pos = tm.get_head_pos();

    // b_k: k=1 is tape[pos-1], k=x is tape[0]
    int x = pos;
    long long expL = ipow(s, x + 1);
    for (int k = 1; k <= x; k++) {
        int b_k = tm.get_tape()[pos - k] + 1; // 1-based
        expL += (long long)(s - b_k) * ipow(s, k);
    }

    // d_k: k=1 is tape[pos+1], k=y is tape[end]
    int y = static_cast<int>(tm.get_tape().size()) - pos - 1;
    long long expR = 0;
    for (int k = 1; k <= y; k++) {
        int d_k = tm.get_tape()[pos + k] + 1; // 1-based
        expR += (long long)(s - d_k) * ipow(s, k);
    }

    std::vector<int> word;
    word.reserve(1 + s - c_1based + expL + expR);
    for (int k = 0; k < 1 + s - c_1based; k++) word.push_back(S.H(gamma));
    for (long long k = 0; k < expL; k++)       word.push_back(S.L(gamma));
    for (long long k = 0; k < expR; k++)        word.push_back(S.R(gamma));

    ts.set_initial_word(word);
    return ts;
}

} // namespace Rule110
