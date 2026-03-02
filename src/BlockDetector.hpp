#pragma once

#include <vector>
#include <cstddef>
#include <cstdint>

namespace Rule110 {

struct TipCluster {
    size_t start, end, center;
    char left_block;
    char right_block;
    int id;
    bool collision;
    long long gen0_disp;
};

struct GapEntry {
    char type;          // block type ('E','F','D','G', etc.) or 'A' for ether
    int initial_phase;  // phase at gen 0 (for width lookup)
};

class BlockDetector {
public:
    static std::vector<TipCluster> find_clusters(
        const std::vector<uint8_t>& row, size_t width);

    void init(const std::vector<uint8_t>& row,
              const std::vector<char>& block_map,
              size_t view_start, size_t view_width);

    void init_from_ether(const std::vector<uint8_t>& row, size_t width,
                         size_t center_view_start, size_t center_view_end,
                         int start_generation = 0);

    std::vector<char> advance(const std::vector<uint8_t>& row, size_t width);

    const std::vector<TipCluster>& clusters() const { return prev_clusters_; }
    const std::vector<char>& color_map() const { return color_map_; }

private:
    std::vector<TipCluster> prev_clusters_;
    std::vector<char> color_map_;
    std::vector<char> block_map_;
    std::vector<GapEntry> gaps_;
    size_t view_start_ = 0;
    int next_id_ = 0;
    int generation_ = 0;

    std::vector<char> assign_colors(
        const std::vector<TipCluster>& clusters, size_t width) const;
};

} // namespace Rule110
