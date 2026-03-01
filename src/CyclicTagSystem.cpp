#include "CyclicTagSystem.hpp"

namespace Rule110 {

CyclicTagSystem::CyclicTagSystem() {}

void CyclicTagSystem::set_appendants(const Appendants& appendants) {
    appendants_ = appendants;
    cycle_head_ = 0;
}

void CyclicTagSystem::set_tape(const BinaryString& tape) {
    tape_ = tape;
    front_ = 0;
}

bool CyclicTagSystem::step() {
    if (static_cast<int>(tape_.size()) - front_ <= 0)
        return false;

    int bit = tape_[front_++];

    if (bit == 1) {
        const auto& app = appendants_[cycle_head_];
        tape_.insert(tape_.end(), app.begin(), app.end());
    }

    cycle_head_ = (cycle_head_ + 1) % static_cast<int>(appendants_.size());

    if (front_ > 100000) {
        tape_.erase(tape_.begin(), tape_.begin() + front_);
        front_ = 0;
    }

    return true;
}

CyclicTagSystem::BinaryString CyclicTagSystem::get_tape() const {
    return BinaryString(tape_.begin() + front_, tape_.end());
}

void CyclicTagSystem::print_state() const {
    std::cout << "Cycle: " << cycle_head_ << " Tape(" << (tape_.size() - front_) << "): ";
    int len = static_cast<int>(tape_.size()) - front_;
    int show = std::min(len, 80);
    for (int i = front_; i < front_ + show; i++)
        std::cout << tape_[i];
    if (show < len) std::cout << "...";
    std::cout << "\n";
}

} // namespace Rule110
