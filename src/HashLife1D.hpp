#pragma once

#include "Rule110Runner.hpp"
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace Rule110 {

class HashLife1D {
public:
    struct Node {
        Node* child[2];  // [0]=left, [1]=right; both nullptr for leaves
        Node* result;    // memoized full-step result (null until computed)
        int level;       // 0=leaf, k means node represents 2^k cells
    };

    HashLife1D();

    Node* make_node(Node* left, Node* right);

    // Full step (non-wrapping): center 2^(k-1) cells after 2^(k-2) gens
    Node* step(Node* node);

    // Partial step (non-wrapping): center after n gens, n <= 2^(k-2)
    Node* step_by(Node* node, long long n);

    // Wrapping step: full ring after 2^(k-2) gens. Returns same-level node.
    Node* wrap_step(Node* node);

    // Wrapping partial step: full ring after n gens, n <= 2^(k-2)
    Node* wrap_step_by(Node* node, long long n);

    // Build tree from flat tape (wrapping ring), padded to power-of-2 with ether
    Node* from_bits(const std::vector<int>& tape, const std::vector<int>& ether_pattern);

    // Advance ring by exactly n generations
    Node* advance(Node* root, long long n);

    // Extract single bit from tree
    int get_bit(Node* node, size_t index);

    // Extract range of bits into packed State
    Rule110Runner::State to_packed_state(Node* node, size_t num_bits);

    // Extract a subrange of bits into a vector<uint8_t> (0/1 per byte)
    void extract_bits(Node* node, size_t start, size_t count, std::vector<uint8_t>& out);

    // Stats
    size_t node_count() const { return pool_.size() + 2; }
    size_t canon_size() const { return canon_.size(); }

    // Leaf singletons
    Node* ZERO;
    Node* ONE;

private:
    Node* base_step(Node* node);
    Node* center(Node* node);

    struct PairHash {
        size_t operator()(const std::pair<Node*, Node*>& p) const {
            auto h1 = std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(p.first));
            auto h2 = std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(p.second));
            return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    struct StepByHash {
        size_t operator()(const std::pair<Node*, long long>& p) const {
            auto h1 = std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(p.first));
            auto h2 = std::hash<long long>{}(p.second);
            return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    std::unordered_map<std::pair<Node*, Node*>, Node*, PairHash> canon_;
    std::unordered_map<std::pair<Node*, long long>, Node*, StepByHash> step_by_cache_;
    std::vector<Node*> pool_;

    Node leaf_zero_;
    Node leaf_one_;
};

} // namespace Rule110
