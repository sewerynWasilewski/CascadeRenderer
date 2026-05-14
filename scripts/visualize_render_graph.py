#!/usr/bin/env python3
# Usage: python visualize.py graph.json
# Requires: pip install pyvis
import json
import sys

try:
    from pyvis.network import Network
except ImportError:
    print("Missing dependency: pip install pyvis")
    sys.exit(1)

QUEUE_COLORS = {
    0: "#4a90d9",  # raster   - blue
    1: "#7ed321",  # compute  - green
    2: "#f5a623",  # async compute - orange
    3: "#9b59b6",  # copy     - purple
}

def visualize(json_path: str, output_path: str = "graph.html") -> None:
    with open(json_path) as f:
        data = json.load(f)

    passes    = {p["id"]: p for p in data["passes"]}
    resources = {r["id"]: r for r in data["resources"]}

    net = Network(directed=True, height="900px", width="100%", bgcolor="#1a1a2e", font_color="white")
    net.set_options("""{
        "edges": { "arrows": { "to": { "enabled": true } }, "smooth": { "type": "cubicBezier" } },
        "physics": { "barnesHut": { "springLength": 200 } }
    }""")

    for p in data["passes"]:
        color  = "#555555" if p["culled"] else QUEUE_COLORS.get(p["queue"], "#4a90d9")
        border = "#888888" if p["culled"] else "#ffffff"
        label  = f"{p['name']}" + (" [culled]" if p["culled"] else "")
        net.add_node(f"p{p['id']}", label=label, color={"background": color, "border": border},
                     shape="box", font={"size": 14})

    # Pass-to-pass edges labelled with resource name
    shown = set()
    for e in data["edges"]:
        if e["to_pass"] is None:
            continue
        key = (e["from_pass"], e["to_pass"], e["resource_id"])
        if key in shown:
            continue
        shown.add(key)
        res_name = resources[e["resource_id"]]["name"] if e["resource_id"] in resources else str(e["resource_id"])
        net.add_edge(f"p{e['from_pass']}", f"p{e['to_pass']}",
                     label=res_name, color="#aaaaaa", font={"size": 11, "color": "#cccccc"})

    # Barrier edges (dashed red)
    for b in data["barriers"]:
        res_name = resources[b["resource_id"]]["name"] if b["resource_id"] in resources else str(b["resource_id"])
        kind     = "alias" if b["kind"] == 1 else "barrier"
        net.add_edge(f"p{b['src_pass']}", f"p{b['dst_pass']}",
                     label=f"{kind}: {res_name}",
                     color="#e74c3c", dashes=True, font={"size": 10, "color": "#e74c3c"})

    net.write_html(output_path)
    print(f"Saved → {output_path}")

if __name__ == "__main__":
    json_path   = sys.argv[1] if len(sys.argv) > 1 else "graph.json"
    output_path = sys.argv[2] if len(sys.argv) > 2 else "graph.html"
    visualize(json_path, output_path)
