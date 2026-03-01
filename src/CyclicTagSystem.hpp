#pragma once

#include <vector>
#include <iostream>

namespace Rule110 {

class CyclicTagSystem {
public:
    using BinaryString = std::vector<int>;
    using Appendants = std::vector<BinaryString>;

    CyclicTagSystem();

    void set_appendants(const Appendants& appendants);
    void set_tape(const BinaryString& tape);

    bool step();

    BinaryString get_tape() const;
    int tape_length() const { return static_cast<int>(tape_.size()) - front_; }
    const Appendants& get_appendants() const { return appendants_; }
    int get_cycle_head() const { return cycle_head_; }

    void print_state() const;

private:
    BinaryString tape_;
    int front_ = 0;
    Appendants appendants_;
    int cycle_head_ = 0;
};

} // namespace Rule110
