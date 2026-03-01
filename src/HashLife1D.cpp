#include "HashLife1D.hpp"
#include <cassert>

namespace Rule110 {

HashLife1D::HashLife1D() {
    leaf_zero_ = {.child = {nullptr, nullptr}, .result = nullptr, .level = 0};
    leaf_one_  = {.child = {nullptr, nullptr}, .result = nullptr, .level = 0};
    ZERO = &leaf_zero_;
    ONE  = &leaf_one_;
}

HashLife1D::Node* HashLife1D::make_node(Node* left, Node* right) {
    assert(left->level == right->level);
    auto key = std::make_pair(left, right);
    auto it = canon_.find(key);
    if (it != canon_.end()) return it->second;

    Node* n = new Node{.child = {left, right}, .result = nullptr, .level = left->level + 1};
    pool_.push_back(n);
    canon_[key] = n;
    return n;
}

// Rule 110: 01101110 in binary
static inline int rule110(int l, int c, int r) {
    return (0b01101110 >> ((l << 2) | (c << 1) | r)) & 1;
}

HashLife1D::Node* HashLife1D::base_step(Node* node) {
    assert(node->level == 2);
    int a = (node->child[0]->child[0] == ONE) ? 1 : 0;
    int b = (node->child[0]->child[1] == ONE) ? 1 : 0;
    int c = (node->child[1]->child[0] == ONE) ? 1 : 0;
    int d = (node->child[1]->child[1] == ONE) ? 1 : 0;
    return make_node(rule110(a, b, c) ? ONE : ZERO,
                     rule110(b, c, d) ? ONE : ZERO);
}

HashLife1D::Node* HashLife1D::center(Node* node) {
    assert(node->level >= 2);
    return make_node(node->child[0]->child[1], node->child[1]->child[0]);
}

// Non-wrapping step: returns center 2^(k-1) cells after 2^(k-2) gens
HashLife1D::Node* HashLife1D::step(Node* node) {
    assert(node->level >= 2);
    if (node->result) return node->result;

    if (node->level == 2) {
        node->result = base_step(node);
        return node->result;
    }

    Node* L = node->child[0];
    Node* R = node->child[1];
    Node* C = make_node(L->child[1], R->child[0]);

    Node* s_left   = step(L);
    Node* s_center = step(C);
    Node* s_right  = step(R);

    Node* combined_left  = make_node(s_left, s_center);
    Node* combined_right = make_node(s_center, s_right);

    node->result = make_node(step(combined_left), step(combined_right));
    return node->result;
}

// Non-wrapping partial step (memoized)
HashLife1D::Node* HashLife1D::step_by(Node* node, long long n) {
    assert(node->level >= 2);
    long long max_step = 1LL << (node->level - 2);
    assert(n >= 0 && n <= max_step);

    if (n == 0) return center(node);
    if (n == max_step) return step(node);

    auto key = std::make_pair(node, n);
    auto it = step_by_cache_.find(key);
    if (it != step_by_cache_.end()) return it->second;

    long long half = 1LL << (node->level - 3);

    Node* L = node->child[0];
    Node* R = node->child[1];
    Node* C = make_node(L->child[1], R->child[0]);

    Node* result;
    if (n <= half) {
        Node* s_left   = step_by(L, n);
        Node* s_center = step_by(C, n);
        Node* s_right  = step_by(R, n);
        result = make_node(
            make_node(s_left->child[1], s_center->child[0]),
            make_node(s_center->child[1], s_right->child[0])
        );
    } else {
        Node* s_left   = step(L);
        Node* s_center = step(C);
        Node* s_right  = step(R);

        Node* combined_left  = make_node(s_left, s_center);
        Node* combined_right = make_node(s_center, s_right);

        long long remaining = n - half;
        result = make_node(
            step_by(combined_left, remaining),
            step_by(combined_right, remaining)
        );
    }

    step_by_cache_[key] = result;
    return result;
}

// For a ring of 2^k cells, wrap_step returns the full ring after 2^(k-2) gens.
// Uses two non-wrapping steps: one normal, one with halves swapped (for the wrap-around).

HashLife1D::Node* HashLife1D::wrap_step(Node* node) {
    assert(node->level >= 2);

    Node* normal = step(node);
    Node* wrapped_node = make_node(node->child[1], node->child[0]);
    Node* wrapped = step(wrapped_node);

    // normal covers center [N/4, 3N/4), wrapped covers [3N/4, N) ∪ [0, N/4)
    // Recombine: left=[0,N/2) right=[N/2,N)
    return make_node(
        make_node(wrapped->child[1], normal->child[0]),
        make_node(normal->child[1], wrapped->child[0])
    );
}

HashLife1D::Node* HashLife1D::wrap_step_by(Node* node, long long n) {
    assert(node->level >= 2);
    long long max_step = 1LL << (node->level - 2);
    assert(n >= 0 && n <= max_step);

    if (n == 0) return node;
    if (n == max_step) return wrap_step(node);

    Node* normal = step_by(node, n);
    Node* wrapped_node = make_node(node->child[1], node->child[0]);
    Node* wrapped = step_by(wrapped_node, n);

    return make_node(
        make_node(wrapped->child[1], normal->child[0]),
        make_node(normal->child[1], wrapped->child[0])
    );
}

HashLife1D::Node* HashLife1D::from_bits(const std::vector<int>& tape,
                                         const std::vector<int>& ether_pattern) {
    size_t n = tape.size();
    size_t padded = 1;
    while (padded < n) padded *= 2;
    if (padded < 4) padded = 4;

    // Pad with ether at phase 0 (matching wrap to left_periodic start)
    std::vector<Node*> nodes(padded);
    for (size_t i = 0; i < padded; i++) {
        int bit = (i < n) ? tape[i] : ether_pattern[(i - n) % ether_pattern.size()];
        nodes[i] = bit ? ONE : ZERO;
    }

    // Build tree bottom-up
    while (nodes.size() > 1) {
        std::vector<Node*> next(nodes.size() / 2);
        for (size_t i = 0; i < next.size(); i++)
            next[i] = make_node(nodes[2 * i], nodes[2 * i + 1]);
        nodes = std::move(next);
    }
    return nodes[0];
}

// Advance wrapping ring by n generations
HashLife1D::Node* HashLife1D::advance(Node* root, long long n) {
    if (n <= 0) return root;
    long long max_step = 1LL << (root->level - 2);

    while (n >= max_step) {
        root = wrap_step(root);
        n -= max_step;
    }
    if (n > 0) {
        root = wrap_step_by(root, n);
    }
    return root;
}

int HashLife1D::get_bit(Node* node, size_t index) {
    if (node->level == 0)
        return (node == ONE) ? 1 : 0;
    size_t half = 1ULL << (node->level - 1);
    if (index < half)
        return get_bit(node->child[0], index);
    else
        return get_bit(node->child[1], index - half);
}

static void extract_recursive(HashLife1D::Node* node, size_t offset,
                               uint64_t* out, size_t num_bits,
                               HashLife1D::Node* ONE) {
    if (offset >= num_bits) return;
    if (node->level == 0) {
        if (node == ONE)
            out[offset / 64] |= (1ULL << (offset % 64));
        return;
    }
    size_t half = 1ULL << (node->level - 1);
    extract_recursive(node->child[0], offset, out, num_bits, ONE);
    extract_recursive(node->child[1], offset + half, out, num_bits, ONE);
}

Rule110Runner::State HashLife1D::to_packed_state(Node* node, size_t num_bits) {
    size_t num_words = (num_bits + 63) / 64;
    Rule110Runner::State state(num_words, 0);
    extract_recursive(node, 0, state.data(), num_bits, ONE);
    return state;
}

// Extract bits [start, start+count) into out[] (0/1 per byte).
// Recursive with range pruning — skips subtrees entirely outside [start, start+count).
static void extract_range_recursive(HashLife1D::Node* node, size_t node_offset,
                                     size_t start, size_t end,
                                     uint8_t* out, HashLife1D::Node* ONE) {
    size_t node_size = 1ULL << node->level;
    size_t node_end = node_offset + node_size;

    if (node_offset >= end || node_end <= start) return;

    if (node->level == 0) {
        out[node_offset - start] = (node == ONE) ? 1 : 0;
        return;
    }

    size_t half = node_size / 2;
    extract_range_recursive(node->child[0], node_offset, start, end, out, ONE);
    extract_range_recursive(node->child[1], node_offset + half, start, end, out, ONE);
}

void HashLife1D::extract_bits(Node* node, size_t start, size_t count, std::vector<uint8_t>& out) {
    out.resize(count);
    std::fill(out.begin(), out.end(), 0);
    extract_range_recursive(node, 0, start, start + count, out.data(), ONE);
}

} // namespace Rule110
