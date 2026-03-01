#pragma once

#include <vector>
#include <string>
#include <iostream>

namespace Rule110 {

enum class Move {
    LEFT,
    RIGHT,
    HALT
};

struct Transition {
    int next_state;
    int write_symbol;
    Move move_direction;
};

class TuringMachine {
public:
    // Tape is a sequence of symbols (ints)
    using Tape = std::vector<int>;

    TuringMachine(int num_states, int num_symbols);

    void add_transition(int state, int read_symbol, int next_state, int write_symbol, Move move);
    void set_initial_state(int state);
    void set_tape(const Tape& tape, int head_position);
    
    // Run one step. Returns false if halted.
    bool step();
    
    // Getters for compiler
    int get_num_states() const { return num_states_; }
    int get_num_symbols() const { return num_symbols_; }
    const Tape& get_tape() const { return tape_; }
    int get_head_pos() const { return head_pos_; }
    int get_current_state() const { return current_state_; }
    
    // Get transition for specific state/symbol
    const Transition& get_transition(int state, int symbol) const;

    void print_state() const;

private:
    int num_states_;
    int num_symbols_;
    int current_state_;
    int head_pos_;
    Tape tape_;
    
    std::vector<std::vector<Transition>> transition_table_;
    void ensure_tape_bounds();
};

} // namespace Rule110
