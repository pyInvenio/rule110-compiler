#include "GliderClassifier.hpp"
#include "Rule110Runner.hpp"
#include "Rule110Compiler.hpp" // For ETHER definition if needed, or define locally

namespace Rule110 {

int GliderClassifier::get_bit(const Rule110Runner::State& packed_state, size_t global_idx) {
    size_t word_idx = global_idx / 64;
    size_t bit_idx = global_idx % 64;

    if (word_idx >= packed_state.size()) return 0; 
    
    return (packed_state[word_idx] >> bit_idx) & 1;
}

std::vector<int> GliderClassifier::extract_window(const Rule110Runner::State& packed_tape, size_t start_idx, size_t length) {
    std::vector<int> window;
    window.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        window.push_back(get_bit(packed_tape, start_idx + i));
    }
    return window;
}

std::vector<std::vector<int>> GliderClassifier::generate_phases(const std::vector<int>& base_block, int generations) {
    std::vector<std::vector<int>> phases;
    
    // Ether: 00010011011111
    // We use a local definition to avoid circular dependency issues if Rule110Compiler is not fully linked yet
    std::vector<int> ether = {0,0,0,1,0,0,1,1,0,1,1,1,1,1};
    
    std::vector<int> tape;
    // Pad with ether
    for(int k=0; k<5; ++k) tape.insert(tape.end(), ether.begin(), ether.end());
    
    size_t start_pos = tape.size();
    tape.insert(tape.end(), base_block.begin(), base_block.end());
    
    for(int k=0; k<5; ++k) tape.insert(tape.end(), ether.begin(), ether.end());
    
    auto current = Rule110Runner::pack(tape);
    
    // Evolve and capture
    for(int i=0; i<generations; ++i) {
        phases.push_back(extract_window(current, start_pos, base_block.size()));
        current = Rule110Runner::next_generation(current);
    }
    return phases;
}

std::string GliderClassifier::classify_packed_multi(const Rule110Runner::State& packed_tape, 
                                                    const std::vector<GliderModel>& known_gliders) {
    std::string result = "";
    size_t total_bits = packed_tape.size() * 64;
    
    // Optimization: Pre-calculate lengths to avoid lookup in loop
    // Assuming roughly consistent lengths for now or checking dynamically
    
    for (size_t i = 0; i < total_bits; ) {
        bool found = false;

        for (const auto& glider : known_gliders) {
            if (glider.phases.empty()) continue;
            size_t len = glider.phases[0].size();
            
            if (i + len > total_bits) continue;

            // Check all phases for this glider
            for (const auto& pat : glider.phases) {
                 bool match = true;
                 // Check bits
                 for (size_t j = 0; j < len; ++j) {
                     if (get_bit(packed_tape, i + j) != pat[j]) {
                         match = false;
                         break;
                     }
                 }
                 if (match) {
                     // Found a glider!
                     // Map name to output character if possible, or just use name
                     if (glider.name == "0") result += "0";
                     else if (glider.name == "1") result += "1";
                     else result += "[" + glider.name + "]"; // Debug output for other blocks
                     
                     i += len; // Advance past this glider
                     found = true;
                     break; // Stop checking phases for this glider
                 }
            }
            if (found) break; // Stop checking other gliders, move to next position
        }
        
        if (!found) {
            i++; // No match, advance 1 bit (ether/noise)
        }
    }
    
    return result;
}

// Legacy
std::string GliderClassifier::classify(const std::vector<int>& tape, 
                                       const std::vector<int>& pattern_zero, 
                                       const std::vector<int>& pattern_one) {
    if (tape.empty()) return "";
    // Quick local pack for legacy support
    auto packed = Rule110Runner::pack(tape);
    
    // Wrap in GliderModels
    GliderModel g0{"0", pattern_zero, {pattern_zero}}; // Single phase
    GliderModel g1{"1", pattern_one, {pattern_one}};
    
    return classify_packed_multi(packed, {g0, g1});
}

} // namespace Rule110