#!/usr/bin/env python3
"""Create analytics visualizations from Rule 110 simulation output.

Usage:
    uv run --with matplotlib --with Pillow python3 tools/create_analytics.py output/3__7
"""

import argparse
import csv
import re
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
    """Plot mismatch decay and active width from mismatch.csv."""
    csv_path = outdir / "mismatch.csv"
    if not csv_path.exists():
        print("  No mismatch.csv found, skipping")
        return

    gens, mismatches, widths = [], [], []
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            g = int(row['generation'])
            m = int(row['mismatch'])
            w = int(row.get('active_width', 0))
            if g > 0:
                gens.append(g)
                mismatches.append(m)
                widths.append(w)

    if not gens:
        print("  No mismatch data, skipping")
        return

    fig, ax1 = plt.subplots(figsize=(12, 6))

    color_mm = 'tab:blue'
    ax1.plot(gens, mismatches, color=color_mm, linewidth=0.8, alpha=0.8, label='Mismatch')
    ax1.set_xscale('log')
    ax1.set_xlabel('Generation', fontsize=12)
    ax1.set_ylabel('Mismatch count (central region)', fontsize=12, color=color_mm)
    ax1.tick_params(axis='y', labelcolor=color_mm)
    ax1.set_ylim(bottom=0)
    ax1.grid(True, alpha=0.3)

    # Active width on second Y axis
    if any(w > 0 for w in widths):
        ax2 = ax1.twinx()
        color_aw = 'tab:orange'
        ax2.plot(gens, widths, color=color_aw, linewidth=0.8, alpha=0.7, label='Active width')
        ax2.set_ylabel('Active width (cells)', fontsize=12, color=color_aw)
        ax2.tick_params(axis='y', labelcolor=color_aw)

    # Mark the settling point
    for i in range(len(mismatches) - 1, -1, -1):
        if mismatches[i] < 300:
            ax1.axvline(x=gens[i], color='r', linestyle='--', alpha=0.5,
                        label=f'Settled: gen {gens[i]:,}')
            break

    # Combined legend
    lines1, labels1 = ax1.get_legend_handles_labels()
    if any(w > 0 for w in widths):
        lines2, labels2 = ax2.get_legend_handles_labels()
        ax1.legend(lines1 + lines2, labels1 + labels2, fontsize=10, loc='upper right')
    else:
        ax1.legend(fontsize=10)

    ax1.set_title('Settling: mismatch and active width over time', fontsize=14)
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

    # Create figure with the image and generation axis
    fig, ax = plt.subplots(figsize=(10, max(6, h / 200)))

    ax.imshow(img, aspect='auto', cmap='gray')
    ax.set_xlabel('Cell position (center crop)', fontsize=10)
    ax.set_ylabel('Generation', fontsize=10)
    ax.set_title('Spacetime diagram (full evolution)', fontsize=12)

    # Replace Y tick labels with generation numbers
    if gen_map:
        label_rows = []
        for target in range(0, h, max(1, h // 10)):
            if target in gen_map:
                label_rows.append(target)
        if h - 1 in gen_map and (not label_rows or label_rows[-1] != h - 1):
            label_rows.append(h - 1)

        ax.set_yticks(label_rows)
        ax.set_yticklabels([f'{gen_map[r]:,}' for r in label_rows], fontsize=8)

    fname = outdir / "spacetime.png"
    fig.savefig(fname, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f"  {fname.name}")


def plot_tm_trace(outdir: Path):
    """Visualize TM execution trace from tm_steps.csv with decode verification."""
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
                'tape': tape,
            })

    if not steps:
        print("  No TM steps data, skipping")
        return

    # Read decode.csv for verification annotation
    decode_info = None
    decode_path = outdir / "decode.csv"
    if decode_path.exists():
        with open(decode_path) as f:
            reader = csv.DictReader(f)
            for row in reader:
                decode_info = {
                    'point': row['point'],
                    'state': int(row['state']),
                    'head_pos': int(row['head_pos']),
                    'match': row['match'].strip().lower() == 'true',
                }

    sym_colors = ['#FFFFFF', '#4CAF50', '#2196F3', '#FF9800', '#9C27B0',
                  '#F44336', '#00BCD4', '#FFEB3B']

    n = len(steps)
    max_tape = max(max(len(s['tape']), s['head_pos'] + 1) for s in steps)
    max_cells = max(max_tape, 3)
    cell_size = min(1.0, 8.0 / max_cells)

    fig_width = max(6, max_cells * cell_size + 3)
    row_height = 1.2
    fig_height = max(3, n * row_height + 1.5)
    fig, ax = plt.subplots(figsize=(fig_width, fig_height))

    for i, s in enumerate(steps):
        y = n - 1 - i  # draw from top to bottom
        tape = s['tape']

        for j in range(max_cells):
            val = tape[j] if j < len(tape) else 0
            color = sym_colors[val % len(sym_colors)]
            ax.add_patch(plt.Rectangle((j * cell_size, y * row_height),
                                        cell_size, cell_size * 0.9,
                                        facecolor=color, edgecolor='black',
                                        linewidth=0.8))
            ax.text(j * cell_size + cell_size / 2, y * row_height + cell_size * 0.45,
                    str(val), ha='center', va='center',
                    fontsize=max(6, int(10 * cell_size)), fontweight='bold')

            # Head marker
            if j == s['head_pos']:
                ax.annotate('', xy=(j * cell_size + cell_size / 2, y * row_height - 0.05),
                            xytext=(j * cell_size + cell_size / 2, y * row_height - 0.25),
                            arrowprops=dict(arrowstyle='->', color='red', lw=1.5))

        # Step label
        ax.text(-0.3, y * row_height + cell_size * 0.45,
                f't={s["tm_step"]}  q{s["state"]}', ha='right', va='center',
                fontsize=9, fontfamily='monospace')

    # Decode verification annotation at step 0
    if decode_info and decode_info['point'] == 'initial':
        status = 'PASS' if decode_info['match'] else 'FAIL'
        color = '#2e7d32' if decode_info['match'] else '#c62828'
        ax.text(max_cells * cell_size + 0.2, (n - 1) * row_height + cell_size * 0.45,
                f'R110 decode: {status}',
                ha='left', va='center', fontsize=9, fontweight='bold',
                color=color,
                bbox=dict(boxstyle='round,pad=0.3', facecolor='#f5f5f5',
                          edgecolor=color, alpha=0.8))

    ax.set_xlim(-max(2, len(f't={steps[-1]["tm_step"]}  q0') * 0.09), max_cells * cell_size + 2.5)
    ax.set_ylim(-0.5, n * row_height + 0.3)
    ax.set_aspect('equal')
    ax.axis('off')
    ax.set_title('Turing Machine execution trace', fontsize=13, pad=10)

    fname = outdir / "tm_trace.png"
    fig.savefig(fname, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f"  {fname.name}")


def annotate_head_tail(outdir: Path):
    """Annotate head and tail images as merged left+right panels.

    Left panel shows the left-periodic → central boundary.
    Right panel shows the central → right-periodic boundary.
    Side by side = reading the tape left to right, with a gap for the
    central region that's too wide to render.
    If only one view exists, produces a single-panel image.
    """
    # Allow large images (head.ppm can be 250k x 4801)
    Image.MAX_IMAGE_PIXELS = None

    # Read settling gen once for tail gen ranges
    settle_gen = None
    summary_path = outdir / "summary.txt"
    if summary_path.exists():
        m = re.search(r'Settled: gen (\d+)', summary_path.read_text())
        if m:
            settle_gen = int(m.group(1))

    def _find_image(suffix):
        """Try PNG (from convert_images.py) then PPM."""
        for name in [f"{suffix}_center.png", f"{suffix}.ppm"]:
            p = outdir / name
            if p.exists():
                return p
        return None

    for phase in ['head', 'tail']:
        left_path = _find_image(phase)
        right_path = _find_image(f"{phase}_r")
        if not left_path and not right_path:
            print(f"  No {phase} image found, skipping")
            continue

        # Determine generation range
        ref_path = left_path or right_path
        ref_img = Image.open(ref_path)
        h = ref_img.size[1]
        ref_img.close()
        if phase == 'head':
            gen_start, gen_end = 0, h - 1
        else:
            gen_end = settle_gen if settle_gen else h - 1
            gen_start = gen_end - h + 1

        has_both = left_path and right_path
        ncols = 2 if has_both else 1
        fig, axes = plt.subplots(1, ncols, figsize=(10 * ncols, max(6, h / 200)),
                                 sharey=True, squeeze=False)

        panels = []
        if left_path:
            panels.append((left_path, axes[0][0],
                           'Left boundary', 'left periodic \u2192 central'))
        if right_path:
            col = 1 if has_both else 0
            panels.append((right_path, axes[0][col],
                           'Right boundary', 'central \u2192 right periodic'))

        for i, (src_path, ax, title, xlabel) in enumerate(panels):
            img = Image.open(src_path)
            w, h_img = img.size
            print(f"  {src_path.name}: {w}x{h_img}")

            ax.imshow(img, aspect='auto')
            ax.set_xlabel(xlabel, fontsize=9)

            # Y axis with generation labels (only on leftmost panel)
            if i == 0:
                n_ticks = min(10, h_img)
                tick_pos = [int(j * (h_img - 1) / max(1, n_ticks - 1)) for j in range(n_ticks)]
                tick_labels = [f'{gen_start + int(p * (gen_end - gen_start) / max(1, h_img - 1)):,}'
                               for p in tick_pos]
                ax.set_yticks(tick_pos)
                ax.set_yticklabels(tick_labels, fontsize=8)
                ax.set_ylabel('Generation', fontsize=10)

            ax.set_title(title, fontsize=11, pad=5)

        phase_label = 'Head' if phase == 'head' else 'Tail'
        fig.suptitle(f'{phase_label} spacetime (gen {gen_start:,} \u2013 {gen_end:,})',
                     fontsize=13, y=1.02)
        fig.tight_layout()
        fname = outdir / f"{phase}_annotated.png"
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

    print("1. Mismatch + active width")
    plot_mismatch(d)

    print("\n2. Spacetime diagram")
    annotate_spacetime(d)

    print("\n3. TM execution trace")
    plot_tm_trace(d)

    print("\n4. Head/tail annotations")
    annotate_head_tail(d)

    print(f"\nDone. All visualizations saved to {d}/")


if __name__ == "__main__":
    main()
