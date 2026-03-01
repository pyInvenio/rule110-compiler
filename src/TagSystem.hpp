#pragma once

#include <vector>
#include <string>
#include <map>
#include <iostream>

namespace Rule110 {

class TagSystem {
public:
    using Word = std::vector<int>;
    using ProductionRules = std::map<int, Word>;

    TagSystem(int deletion_number);

    void add_rule(int symbol, const Word& production);
    void set_initial_word(const Word& word);

    bool step();

    Word get_word() const { return Word(word_.begin() + front_, word_.end()); }
    int word_length() const { return static_cast<int>(word_.size()) - front_; }
    int get_deletion_number() const { return deletion_number_; }
    const ProductionRules& get_rules() const { return rules_; }
    
    void print_state() const;
    
    void set_symbol_name(int symbol, const std::string& name);
    std::string get_symbol_name(int symbol) const;

private:
    int deletion_number_;
    Word word_;
    int front_ = 0;
    ProductionRules rules_;
    std::map<int, std::string> symbol_names_;
};

} // namespace Rule110
