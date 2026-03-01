#include "TuringMachine.hpp"
#include <stdexcept>

namespace Rule110 {

TuringMachine::TuringMachine(int num_states, int num_symbols)
    : num_states_(num_states), num_symbols_(num_symbols), current_state_(0), head_pos_(0) {
    Transition halt = {0, 0, Move::HALT};
    transition_table_.resize(num_states, std::vector<Transition>(num_symbols, halt));
}

void TuringMachine::add_transition(int state, int read_symbol, int next_state, int write_symbol, Move move) {
    if (state < 0 || state >= num_states_ || read_symbol < 0 || read_symbol >= num_symbols_)
        throw std::out_of_range("Invalid state or symbol");
    transition_table_[state][read_symbol] = {next_state, write_symbol, move};
}

void TuringMachine::set_initial_state(int state) {
    if (state < 0 || state >= num_states_)
        throw std::out_of_range("Invalid initial state");
    current_state_ = state;
}

void TuringMachine::set_tape(const Tape& tape, int head_position) {
    tape_ = tape;
    head_pos_ = head_position;
    if (head_pos_ >= static_cast<int>(tape_.size()))
        tape_.resize(head_pos_ + 1, 0);
}

const Transition& TuringMachine::get_transition(int state, int symbol) const {
    return transition_table_.at(state).at(symbol);
}

// Ensure head_pos_ is within tape bounds, expanding with blanks if needed.
void TuringMachine::ensure_tape_bounds() {
    if (head_pos_ < 0) {
        tape_.insert(tape_.begin(), -head_pos_, 0);
        head_pos_ = 0;
    }
    if (head_pos_ >= static_cast<int>(tape_.size()))
        tape_.resize(head_pos_ + 1, 0);
}

bool TuringMachine::step() {
    if (current_state_ < 0) return false;

    ensure_tape_bounds();
    int sym = tape_[head_pos_];
    const Transition& trans = transition_table_[current_state_][sym];

    tape_[head_pos_] = trans.write_symbol;

    if (trans.move_direction == Move::HALT)
        return false;

    head_pos_ += (trans.move_direction == Move::LEFT) ? -1 : 1;
    current_state_ = trans.next_state;
    ensure_tape_bounds();

    return true;
}

void TuringMachine::print_state() const {
    std::cout << "State: " << current_state_ << " Head: " << head_pos_ << "\nTape: ";
    for (size_t i = 0; i < tape_.size(); ++i) {
        if (static_cast<int>(i) == head_pos_) std::cout << "[" << tape_[i] << "]";
        else std::cout << " " << tape_[i] << " ";
    }
    std::cout << "\n";
}

} // namespace Rule110
