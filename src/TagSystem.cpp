#include "TagSystem.hpp"

namespace Rule110 {

TagSystem::TagSystem(int deletion_number) : deletion_number_(deletion_number) {}

void TagSystem::add_rule(int symbol, const Word& production) {
    rules_[symbol] = production;
}

void TagSystem::set_initial_word(const Word& word) {
    word_ = word;
    front_ = 0;
}

bool TagSystem::step() {
    if (static_cast<int>(word_.size()) - front_ < deletion_number_)
        return false;

    int head = word_[front_];
    front_ += deletion_number_;

    auto it = rules_.find(head);
    if (it != rules_.end())
        word_.insert(word_.end(), it->second.begin(), it->second.end());

    if (front_ > 100000) {
        word_.erase(word_.begin(), word_.begin() + front_);
        front_ = 0;
    }

    return true;
}

void TagSystem::set_symbol_name(int symbol, const std::string& name) {
    symbol_names_[symbol] = name;
}

std::string TagSystem::get_symbol_name(int symbol) const {
    auto it = symbol_names_.find(symbol);
    return it != symbol_names_.end() ? it->second : std::to_string(symbol);
}

void TagSystem::print_state() const {
    std::cout << "Tag Word (" << (word_.size() - front_) << "): ";
    for (int i = front_; i < static_cast<int>(word_.size()); i++)
        std::cout << get_symbol_name(word_[i]) << " ";
    std::cout << "\n";
}

} // namespace Rule110
