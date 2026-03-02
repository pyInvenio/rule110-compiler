#!/usr/bin/env python3
"""Create analytics visualizations from Rule 110 simulation output.

Usage:
    uv run --with matplotlib --with Pillow python3 tools/create_analytics.py output/3__7
"""

import argparse
import csv
import sys
from pathlib import Path

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import matplotlib.ticker as ticker
    from PIL import Image
except ImportError:
    print("Run with: uv run --with matplotlib --with Pillow python3 tools/create_analytics.py <dir>")
    sys.exit(1)


def plot_mismatch(outdir: Path):
    """Plot mismatch decay curve from mismatch.csv."""
    csv_path = outdir / "mismatch.csv"
    if not csv_path.exists():
        print("  No mismatch.csv found, skipping")
        return

    gens, mismatches = [], []
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            g = int(row['generation'])
            m = int(row['mismatch'])
            if g > 0:  # skip gen 0
                gens.append(g)
                mismatches.append(m)

    if not gens:
        print("  No mismatch data, skipping")
        return

    fig, ax = plt.subplots(figsize=(12, 6))
    ax.plot(gens, mismatches, 'b-', linewidth=0.8, alpha=0.8)
    ax.set_xscale('log')
    ax.set_xlabel('Generation', fontsize=12)
    ax.set_ylabel('Mismatch count (central region)', fontsize=12)
    ax.set_title('Settling: mismatch decay over time', fontsize=14)
    ax.grid(True, alpha=0.3)

    # Mark the settling point (last entry with mm < 300)
    for i in range(len(mismatches) - 1, -1, -1):
        if mismatches[i] < 300:
            ax.axvline(x=gens[i], color='r', linestyle='--', alpha=0.5, label=f'Settled: gen {gens[i]:,}')
            ax.legend(fontsize=10)
            break

    ax.set_ylim(bottom=0)
    fname = outdir / "mismatch_plot.png"
    fig.savefig(fname, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f"  {fname.name}")


def annotate_spacetime(outdir: Path):
    """Convert spacetime.ppm to annotated PNG with generation labels."""
    ppm_path = outdir / "spacetime.ppm"
    meta_path = outdir / "spacetime_meta.csv"
    if not ppm_path.exists():
        print("  No spacetime.ppm found, skipping")
        return

    img = Image.open(ppm_path)
    w, h = img.size
    print(f"  spacetime.ppm: {w}x{h}")

    # Read metadata for generation labels
    gen_map = {}
    if meta_path.exists():
        with open(meta_path) as f:
            reader = csv.DictReader(f)
            for row in reader:
                gen_map[int(row['row'])] = int(row['generation'])

    # Read TM step boundaries for markers
    tm_steps_path = outdir / "tm_steps.csv"
    tm_gens = []
    if tm_steps_path.exists():
        with open(tm_steps_path) as f:
            reader = csv.DictReader(f)
            for row in reader:
                tm_gens.append((int(row['tm_step']), int(row['r110_gen'])))

    # Create figure with the image and generation axis
    fig, ax = plt.subplots(figsize=(10, max(6, h / 200)))

    ax.imshow(img, aspect='auto', cmap='gray')
    ax.set_xlabel('Cell position (center crop)', fontsize=10)
    ax.set_ylabel('Generation', fontsize=10)
    ax.set_title('Spacetime diagram (full evolution)', fontsize=12)

    # Replace Y tick labels with generation numbers
    if gen_map:
        # Pick ~10 evenly spaced rows for labels
        label_rows = []
        for target in range(0, h, max(1, h // 10)):
            if target in gen_map:
                label_rows.append(target)
        if h - 1 in gen_map and (not label_rows or label_rows[-1] != h - 1):
            label_rows.append(h - 1)

        ax.set_yticks(label_rows)
        ax.set_yticklabels([f'{gen_map[r]:,}' for r in label_rows], fontsize=8)

    # Mark TM step boundaries
    if tm_gens and gen_map:
        all_gens = list(gen_map.values())
        for tm_step, r110_gen in tm_gens:
            # Find closest row
            best_row = min(gen_map.keys(), key=lambda r: abs(gen_map[r] - r110_gen))
            ax.axhline(y=best_row, color='r', linestyle='--', alpha=0.4, linewidth=0.8)
            ax.text(w + 10, best_row, f'TM step {tm_step}', fontsize=7,
                    color='red', va='center', clip_on=False)

    fname = outdir / "spacetime.png"
    fig.savefig(fname, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f"  {fname.name}")


def create_tm_sidebyside(outdir: Path):
    """Create TM / R110 side-by-side composite from tm_steps.csv and tm_step_N.ppm."""
    csv_path = outdir / "tm_steps.csv"
    if not csv_path.exists():
        print("  No tm_steps.csv found, skipping")
        return

    steps = []
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            tape_str = row['tape'].strip('"')
            tape = [int(x) for x in tape_str.strip('[]').split(',') if x.strip()]
            steps.append({
                'tm_step': int(row['tm_step']),
                'state': int(row['state']),
                'head_pos': int(row['head_pos']),
                'r110_gen': int(row['r110_gen']),
                'window_start': int(row.get('window_start', row['r110_gen'])),
                'window_rows': int(row.get('window_rows', 1)),
                'tape': tape,
            })

    if not steps:
        print("  No TM steps data, skipping")
        return

    # Load R110 spacetime window images
    crops = {}
    for s in steps:
        ppm_path = outdir / f"tm_step_{s['tm_step']}.ppm"
        if ppm_path.exists():
            crops[s['tm_step']] = Image.open(ppm_path)

    # TM symbol colors
    sym_colors = ['#FFFFFF', '#4CAF50', '#2196F3', '#FF9800', '#9C27B0',
                  '#F44336', '#00BCD4', '#FFEB3B']

    n = len(steps)
    fig_height = 4 * n + 1
    fig, axes = plt.subplots(n, 2, figsize=(16, fig_height),
                              gridspec_kw={'width_ratios': [1, 6]},
                              squeeze=False)

    for i, s in enumerate(steps):
        ax_tm = axes[i][0]
        ax_r110 = axes[i][1]

        # Draw TM tape
        tape = s['tape']
        max_cells = max(len(tape), 5)
        for j in range(max_cells):
            val = tape[j] if j < len(tape) else 0
            color = sym_colors[val % len(sym_colors)]
            ax_tm.add_patch(plt.Rectangle((j, 0), 1, 1, facecolor=color,
                                           edgecolor='black', linewidth=1))
            ax_tm.text(j + 0.5, 0.5, str(val), ha='center', va='center',
                       fontsize=12, fontweight='bold')
            # Head marker
            if j == s['head_pos']:
                ax_tm.annotate('', xy=(j + 0.5, -0.1), xytext=(j + 0.5, -0.5),
                               arrowprops=dict(arrowstyle='->', color='red', lw=2))

        ax_tm.set_xlim(-0.5, max_cells + 0.5)
        ax_tm.set_ylim(-0.8, 1.5)
        ax_tm.set_aspect('equal')
        ax_tm.axis('off')
        ax_tm.set_title(f'TM step {s["tm_step"]}\nstate={s["state"]}',
                         fontsize=10, pad=2)

        # Draw R110 spacetime window
        if s['tm_step'] in crops:
            crop_img = crops[s['tm_step']]
            ax_r110.imshow(crop_img, aspect='auto', cmap='gray',
                           interpolation='nearest')
            win_start = s['window_start']
            win_rows = s['window_rows']
            target = s['r110_gen']
            ax_r110.set_title(
                f'R110 spacetime @ gen {win_start:,} \u2013 {win_start + win_rows:,}'
                f'  (TM step {s["tm_step"]} \u2248 gen {target:,})',
                fontsize=9)
            # Mark the target generation within the window
            target_row = target - win_start
            if 0 <= target_row < win_rows:
                ax_r110.axhline(y=target_row, color='r', linestyle='--',
                                alpha=0.6, linewidth=0.8)
        else:
            ax_r110.text(0.5, 0.5, 'No R110 data', ha='center', va='center',
                         transform=ax_r110.transAxes, fontsize=12)
            ax_r110.set_title(f'R110 @ gen {s["r110_gen"]:,}', fontsize=9)

        ax_r110.set_xlabel('Cell position (center crop)', fontsize=8)
        ax_r110.set_ylabel('Generation', fontsize=8)

    fig.suptitle('Turing Machine steps \u2194 Rule 110 spacetime', fontsize=14, y=1.01)
    fig.tight_layout()
    fname = outdir / "tm_r110_sidebyside.png"
    fig.savefig(fname, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f"  {fname.name}")


def main():
    parser = argparse.ArgumentParser(description="Create R110 analytics visualizations")
    parser.add_argument("dir", help="Output directory containing simulation results")
    args = parser.parse_args()

    d = Path(args.dir)
    if not d.is_dir():
        print(f"Not a directory: {d}")
        sys.exit(1)

    print(f"Creating analytics for {d}\n")

    print("1. Mismatch decay curve")
    plot_mismatch(d)

    print("\n2. Spacetime diagram")
    annotate_spacetime(d)

    print("\n3. TM / R110 side-by-side")
    create_tm_sidebyside(d)

    print(f"\nDone. All visualizations saved to {d}/")


if __name__ == "__main__":
    main()
