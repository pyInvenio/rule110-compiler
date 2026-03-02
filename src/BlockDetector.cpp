#include "BlockDetector.hpp"
#include "BlockData.hpp"
#include <algorithm>
#include <cmath>

namespace Rule110 {

// --- Ether-break cluster detection (stateless, per-row) ---

std::vector<TipCluster> BlockDetector::find_clusters(
    const std::vector<uint8_t>& row, size_t width)
{
    std::vector<bool> is_break(width, false);
    for (size_t i = 14; i + 14 < width; i++) {
        if (row[i] != row[i - 14] || row[i] != row[i + 14])
            is_break[i] = true;
    }
    for (size_t i = 0; i < std::min((size_t)14, width); i++)
        is_break[i] = true;
    for (size_t i = (width > 14) ? width - 14 : 0; i < width; i++)
        is_break[i] = true;

    std::vector<TipCluster> clusters;
    size_t i = 0;
    while (i < width) {
        while (i < width && !is_break[i]) i++;
        if (i >= width) break;

        size_t cluster_start = i;
        size_t cluster_end = i;

        while (i < width) {
            while (i < width && is_break[i]) {
                cluster_end = i;
                i++;
            }
            size_t gap_start = i;
            while (i < width && !is_break[i]) i++;
            if (i < width && (i - gap_start) <= 28) {
                continue;
            } else {
                i = gap_start;
                break;
            }
        }

        clusters.push_back({cluster_start, cluster_end,
                            (cluster_start + cluster_end) / 2,
                            'A', 'A', -1, false, 0});
    }

    return clusters;
}

// --- Assign colors: phase-aware gap tracking ---

std::vector<char> BlockDetector::assign_colors(
    const std::vector<TipCluster>& clusters, size_t width) const
{
    std::vector<char> colors(width, 'A');

    // Color cluster cells with tracked type
    for (const auto& cl : clusters) {
        char tracked = cl.collision ? 'X' :
            (cl.left_block != 'A' ? cl.left_block : cl.right_block);
        if (tracked == 'A') continue;
        for (size_t j = cl.start; j <= cl.end && j < width; j++)
            colors[j] = tracked;
    }

    // Color gap cells with phase-aware width cap
    for (size_t ci = 0; ci + 1 < clusters.size(); ci++) {
        if (ci >= gaps_.size() || gaps_[ci].type == 'A') continue;

        size_t gap_start = clusters[ci].end + 1;
        size_t gap_end = clusters[ci + 1].start;
        if (gap_start >= gap_end) continue;

        char color = gaps_[ci].type;
        int phase = (gaps_[ci].initial_phase + generation_) % 30;
        size_t expected = BlockData::get_block_row(color, phase).size();
        size_t half = expected / 2;

        for (size_t j = gap_start; j < gap_end; j++) {
            size_t dl = j - gap_start;
            size_t dr = gap_end - 1 - j;
            size_t dist = std::min(dl, dr);
            if (dist < half)
                colors[j] = color;
        }
    }

    return colors;
}

// --- Initialization from gen-0 block_map ---

void BlockDetector::init(const std::vector<uint8_t>& row,
                          const std::vector<char>& block_map,
                          size_t view_start, size_t view_width)
{
    next_id_ = 0;
    generation_ = 0;

    // Store view-local slice of block_map (not the full 121M map)
    block_map_.resize(view_width);
    for (size_t i = 0; i < view_width; i++) {
        size_t pos = view_start + i;
        block_map_[i] = (pos < block_map.size()) ? block_map[pos] : 'A';
    }
    view_start_ = view_start;

    auto clusters = find_clusters(row, view_width);

    for (auto& cl : clusters) {
        char center_block = 'A';
        size_t center_pos = view_start + cl.center;
        if (center_pos < block_map.size())
            center_block = block_map[center_pos];

        // Scan left of cluster for block type
        cl.left_block = 'A';
        for (size_t j = cl.start; j > 0; j--) {
            size_t pos = view_start + j - 1;
            if (pos < block_map.size()) {
                char b = block_map[pos];
                if (b != 'A' && b != 'B') { cl.left_block = b; break; }
                if (b == 'A') break;
            } else break;
        }

        // Scan right of cluster for block type
        cl.right_block = 'A';
        for (size_t j = cl.end + 1; j < view_width; j++) {
            size_t pos = view_start + j;
            if (pos < block_map.size()) {
                char b = block_map[pos];
                if (b != 'A' && b != 'B') { cl.right_block = b; break; }
                if (b == 'A') break;
            } else break;
        }

        // D separators absorbed into E clusters: override if center is D
        if (center_block == 'D' && cl.left_block == cl.right_block) {
            cl.left_block = 'D';
            cl.right_block = 'D';
        }

        cl.gen0_disp = 0;
        cl.id = next_id_++;
    }

    // Phase reconstruction: build phase_at parallel to block_map
    std::vector<int> phase_at(block_map.size(), 0);

    size_t c_start = block_map.size(), c_end = 0;
    for (size_t i = 0; i < block_map.size(); i++) {
        if (block_map[i] == 'C') {
            if (i < c_start) c_start = i;
            c_end = i + 1;
        }
    }

    if (c_start < block_map.size()) {
        for (size_t i = c_start; i < c_end; i++)
            phase_at[i] = BlockData::ZERO_LOC;

        // Scan right from C: track phase through each block
        int phase = BlockData::INITIAL_PHASE;
        size_t pos = c_end;
        while (pos < block_map.size()) {
            char b = block_map[pos];
            size_t bstart = pos;
            while (pos < block_map.size() && block_map[pos] == b) pos++;
            if (b != 'A' && b != 'B')
                phase = BlockData::next_phase(phase, b);
            for (size_t i = bstart; i < pos; i++)
                phase_at[i] = phase;
        }
    }

    // Build gaps: one entry per pair of consecutive clusters
    gaps_.clear();
    for (size_t ci = 0; ci + 1 < clusters.size(); ci++) {
        size_t mid = (clusters[ci].end + clusters[ci + 1].start) / 2;
        size_t map_pos = view_start + mid;
        char gap_type = 'A';
        int gap_phase = 0;
        if (map_pos < block_map.size()) {
            char b = block_map[map_pos];
            if (b != 'A' && b != 'B') {
                // Tiled blocks (H-L) have no ether gaps — skip gap fill
                // to keep thin diagonal traces like central region
                bool tiled = (b == 'H' || b == 'I' || b == 'J' ||
                              b == 'K' || b == 'L');
                if (!tiled) {
                    gap_type = b;
                    gap_phase = phase_at[map_pos];
                }
            }
        }
        gaps_.push_back({gap_type, gap_phase});
    }

    prev_clusters_ = clusters;
    color_map_ = assign_colors(clusters, view_width);
}

// --- Initialization for tail (no block_map) ---

void BlockDetector::init_from_ether(const std::vector<uint8_t>& row, size_t width,
                                     size_t center_view_start, size_t center_view_end,
                                     int start_generation)
{
    next_id_ = 0;
    generation_ = start_generation;
    block_map_.clear();
    auto clusters = find_clusters(row, width);

    for (auto& cl : clusters) {
        if (cl.center >= center_view_start && cl.center < center_view_end) {
            cl.left_block = 'E';
            cl.right_block = 'E';
        } else if (cl.center >= center_view_end) {
            cl.left_block = 'H';
            cl.right_block = 'H';
        } else {
            cl.left_block = 'A';
            cl.right_block = 'A';
        }
        cl.gen0_disp = 0;
        cl.id = next_id_++;
    }

    // Build gaps from cluster types (no block_map available)
    gaps_.clear();
    for (size_t ci = 0; ci + 1 < clusters.size(); ci++) {
        char left_type = clusters[ci].right_block;
        char right_type = clusters[ci + 1].left_block;
        char gap_type = 'A';
        if (left_type != 'A' && right_type != 'A')
            gap_type = left_type;
        else if (left_type != 'A')
            gap_type = left_type;
        else if (right_type != 'A')
            gap_type = right_type;
        gaps_.push_back({gap_type, 0});
    }

    prev_clusters_ = clusters;
    color_map_ = assign_colors(clusters, width);
}

// --- Forward tracking ---

std::vector<char> BlockDetector::advance(const std::vector<uint8_t>& row, size_t width)
{
    auto new_clusters = find_clusters(row, width);

    const int MAX_SHIFT = 50;

    std::vector<int> new_to_prev(new_clusters.size(), -1);
    std::vector<bool> prev_matched(prev_clusters_.size(), false);

    for (size_t ni = 0; ni < new_clusters.size(); ni++) {
        int best_pi = -1;
        int best_dist = MAX_SHIFT + 1;

        for (size_t pi = 0; pi < prev_clusters_.size(); pi++) {
            if (prev_matched[pi]) continue;
            int dist = std::abs((int)new_clusters[ni].center - (int)prev_clusters_[pi].center);
            if (dist < best_dist) {
                best_dist = dist;
                best_pi = (int)pi;
            }
        }

        if (best_pi >= 0 && best_dist <= MAX_SHIFT) {
            new_to_prev[ni] = best_pi;
            prev_matched[best_pi] = true;
        }
    }

    for (size_t ni = 0; ni < new_clusters.size(); ni++) {
        int pi = new_to_prev[ni];

        if (pi >= 0) {
            new_clusters[ni].id = prev_clusters_[pi].id;
            new_clusters[ni].left_block = prev_clusters_[pi].left_block;
            new_clusters[ni].right_block = prev_clusters_[pi].right_block;
            new_clusters[ni].collision = false;
            new_clusters[ni].gen0_disp = prev_clusters_[pi].gen0_disp
                + ((long long)new_clusters[ni].center - (long long)prev_clusters_[pi].center);
        } else {
            new_clusters[ni].id = next_id_++;
            new_clusters[ni].collision = false;

            // Find nearest unmatched prev cluster
            int best_prev = -1;
            int best_dist = 200;
            for (size_t pj = 0; pj < prev_clusters_.size(); pj++) {
                if (prev_matched[pj]) continue;
                int d = std::abs((int)new_clusters[ni].center - (int)prev_clusters_[pj].center);
                if (d < best_dist) {
                    best_dist = d;
                    best_prev = (int)pj;
                }
            }

            if (best_prev >= 0) {
                new_clusters[ni].left_block = prev_clusters_[best_prev].left_block;
                new_clusters[ni].right_block = prev_clusters_[best_prev].right_block;
                new_clusters[ni].gen0_disp = prev_clusters_[best_prev].gen0_disp
                    + ((long long)new_clusters[ni].center - (long long)prev_clusters_[best_prev].center);
            } else {
                char block_type = 'A';
                if (ni > 0) {
                    block_type = new_clusters[ni - 1].right_block;
                    new_clusters[ni].gen0_disp = new_clusters[ni - 1].gen0_disp;
                } else {
                    new_clusters[ni].gen0_disp = 0;
                }
                new_clusters[ni].left_block = block_type;
                new_clusters[ni].right_block = block_type;
            }
        }
    }

    // Detect collisions: two adjacent unmatched prev clusters within 100 cells
    for (size_t pi = 0; pi + 1 < prev_clusters_.size(); pi++) {
        if (prev_matched[pi] || prev_matched[pi + 1]) continue;
        int gap = (int)prev_clusters_[pi + 1].center - (int)prev_clusters_[pi].center;
        if (gap > 100) continue;

        size_t mid = (prev_clusters_[pi].center + prev_clusters_[pi + 1].center) / 2;
        for (size_t ni = 0; ni < new_clusters.size(); ni++) {
            if (std::abs((int)new_clusters[ni].center - (int)mid) <= MAX_SHIFT * 2) {
                new_clusters[ni].collision = true;
                break;
            }
        }
    }

    // Override clusters in tiled regions to prevent central type contamination
    if (!block_map_.empty()) {
        for (auto& cl : new_clusters) {
            if (cl.center < block_map_.size()) {
                char bm = block_map_[cl.center];
                bool bm_tiled = (bm == 'H' || bm == 'I' || bm == 'J' ||
                                 bm == 'K' || bm == 'L');
                bool cl_tiled = (cl.left_block == 'H' || cl.left_block == 'I' ||
                                 cl.left_block == 'J' || cl.left_block == 'K' ||
                                 cl.left_block == 'L');
                if (bm_tiled && !cl_tiled) {
                    cl.left_block = bm;
                    cl.right_block = bm;
                }
            }
        }
    }

    // Rebuild gaps for new cluster list
    std::vector<GapEntry> new_gaps;
    for (size_t ni = 0; ni + 1 < new_clusters.size(); ni++) {
        int pi = new_to_prev[ni];
        int pj = new_to_prev[ni + 1];

        if (pi >= 0 && pj >= 0 && pj == pi + 1 && (size_t)pi < gaps_.size()) {
            // Consecutive prev match: inherit gap
            new_gaps.push_back(gaps_[pi]);
        } else if (pi >= 0 && pj >= 0 && pj > pi + 1) {
            // Skipped prev clusters between them: consumed blocks -> ether
            new_gaps.push_back({'A', 0});
        } else if (pi >= 0 && (size_t)pi < gaps_.size()) {
            // Right unmatched, inherit from left prev gap
            new_gaps.push_back(gaps_[pi]);
        } else if (pj >= 0 && pj > 0 && (size_t)(pj - 1) < gaps_.size()) {
            // Left unmatched, inherit from right prev gap
            new_gaps.push_back(gaps_[pj - 1]);
        } else {
            new_gaps.push_back({'A', 0});
        }
    }
    gaps_ = new_gaps;
    generation_++;

    prev_clusters_ = new_clusters;
    color_map_ = assign_colors(new_clusters, width);
    return color_map_;
}

} // namespace Rule110
