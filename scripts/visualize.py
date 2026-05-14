#!/usr/bin/env python3
# Usage: python scripts/visualize.py build/graph.json [--format svg|png|pdf|dot]
# Requires: pip install graphviz  +  apt / brew install graphviz
import json
import sys
import argparse
import os

try:
    import graphviz
except ImportError:
    print("Missing dependency: pip install graphviz")
    sys.exit(1)

PASS_COLORS = {
    0: "#4a90d9",
    1: "#7ed321",
    2: "#f5a623",
    3: "#9b59b6",
}

def build(data: dict) -> graphviz.Digraph:
    passes    = {p["id"]: p for p in data["passes"]}
    resources = {r["id"]: r for r in data["resources"]}

    g = graphviz.Digraph("rendergraph")
    g.attr(rankdir="TB", fontname="Helvetica")
    g.attr("node", fontname="Helvetica", fontsize="12")
    g.attr("edge", fontname="Helvetica", fontsize="10")

    for p in data["passes"]:
        color = "#888888" if p["culled"] else PASS_COLORS.get(p["queue"], "#4a90d9")
        label = p["name"] + (" [culled]" if p["culled"] else "")
        g.node(f"p{p['id']}", label=label, shape="box",
               style="filled", fillcolor=color, fontcolor="white")

    for r in data["resources"]:
        color = "#e8c96d" if r["kind"] == 1 else "#e8956d"
        g.node(f"r{r['id']}", label=r["name"], shape="ellipse",
               style="filled", fillcolor=color)

    seen = set()
    for e in data["edges"]:
        wkey = ("w", e["from_pass"], e["resource_id"])
        if wkey not in seen:
            seen.add(wkey)
            g.edge(f"p{e['from_pass']}", f"r{e['resource_id']}", color="#555555")
        if e["to_pass"] is None:
            continue
        rkey = ("r", e["to_pass"], e["resource_id"])
        if rkey not in seen:
            seen.add(rkey)
            g.edge(f"r{e['resource_id']}", f"p{e['to_pass']}", color="#555555")

    return g

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input",   nargs="?", default="build/graph.json")
    parser.add_argument("--format", default="svg", choices=["svg", "png", "pdf", "dot"])
    args = parser.parse_args()

    with open(args.input) as f:
        data = json.load(f)

    g      = build(data)
    output = os.path.splitext(args.input)[0]

    if args.format == "dot":
        path = output + ".dot"
        with open(path, "w") as f:
            f.write(g.source)
        print(f"Saved -> {path}")
    else:
        g.render(output, format=args.format, cleanup=True)
        print(f"Saved -> {output}.{args.format}")

if __name__ == "__main__":
    main()
