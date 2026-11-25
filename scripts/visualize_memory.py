#!/usr/bin/env python3
# Usage: python scripts/visualize_memory.py build/graph.json [output.png]
# Requires: pip install matplotlib
#
# One chart per RHIMemoryType pool: heap offset (Y) vs pass index (X), one bar per
# transient resource spanning [first_pass, last_pass] at [offset, offset+size].
# Red = aliases memory with another resource (appears in a RHI_BARRIER_ALIASING
# barrier), gray = does not.
import json
import sys

try:
    import matplotlib.pyplot as plt
    from matplotlib.patches import Rectangle
except ImportError:
    print("Missing dependency: pip install matplotlib")
    sys.exit(1)

MEMORY_TYPE_NAMES = {0: "GPU_ONLY", 1: "CPU_TO_GPU", 2: "GPU_TO_CPU"}
ALIASED_COLOR     = "#e74c3c"
NON_ALIASED_COLOR = "#888888"


def visualize(json_path: str, output_path: str) -> None:
    with open(json_path) as f:
        data = json.load(f)

    resources = [r for r in data["resources"] if r["first_pass"] is not None]

    # A resource is "aliased" if another resource in the same pool occupies the
    # same byte range at some point - i.e. shares (memory_type, offset, size).
    range_counts = {}
    for r in resources:
        key = (r["memory_type"], r["offset"], r["size"])
        range_counts[key] = range_counts.get(key, 0) + 1
    aliased_ids = {r["id"] for r in resources
                   if range_counts[(r["memory_type"], r["offset"], r["size"])] > 1}

    memory_types = sorted({r["memory_type"] for r in resources})
    if not memory_types:
        print("No transient resources with a physical range to plot.")
        return

    fig, axes = plt.subplots(len(memory_types), 1, squeeze=False, figsize=(12, 5 * len(memory_types)))

    for ax, mem_type in zip(axes[:, 0], memory_types):
        pool_resources = [r for r in resources if r["memory_type"] == mem_type]
        max_pass = max(r["last_pass"] for r in pool_resources) + 1

        mib = 1024 * 1024
        for r in pool_resources:
            color = ALIASED_COLOR if r["id"] in aliased_ids else NON_ALIASED_COLOR
            width = (r["last_pass"] - r["first_pass"]) + 1
            offset_mib, size_mib = r["offset"] / mib, r["size"] / mib
            ax.add_patch(Rectangle((r["first_pass"], offset_mib), width, size_mib,
                                    facecolor=color, edgecolor="#1a1a1a", linewidth=0.5))
            ax.text(r["first_pass"] + width / 2, offset_mib + size_mib / 2,
                    f"{r['name']}\n{size_mib:.2f} MiB", ha="center", va="center",
                    fontsize=7, color="white")

        top_mib = max(r["offset"] + r["size"] for r in pool_resources) / mib
        ax.set_xlim(0, max_pass)
        ax.set_ylim(0, top_mib * 1.02)
        ax.invert_yaxis()
        ax.set_facecolor("#111111")
        ax.set_title(MEMORY_TYPE_NAMES.get(mem_type, f"type {mem_type}"), color="white")
        ax.set_xlabel("Pass (time)")
        ax.set_ylabel("Heap offset (MiB)")

    fig.patch.set_facecolor("#111111")
    for ax in axes[:, 0]:
        ax.tick_params(colors="white")
        ax.xaxis.label.set_color("white")
        ax.yaxis.label.set_color("white")
        ax.title.set_color("white")

    fig.tight_layout()
    fig.savefig(output_path, facecolor=fig.get_facecolor())
    print(f"Saved -> {output_path}")


if __name__ == "__main__":
    json_path   = sys.argv[1] if len(sys.argv) > 1 else "graph.json"
    output_path = sys.argv[2] if len(sys.argv) > 2 else "memory.png"
    visualize(json_path, output_path)
