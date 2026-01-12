#!/usr/bin/env python3
"""
dependency_graph.py

Crawl a C/C++ codebase folder and generate a visual dependency graph.

Modes:
  1) file  : file include graph (#include "..." and optionally <...>)
  2) symbol: function call graph (best-effort, regex-based)

Output:
  - Interactive HTML (opens in a browser)

Usage:
  python dependency_graph.py --root . --out deps.html
  python dependency_graph.py --root . --out calls.html --mode symbol
  python dependency_graph.py --root . --out deps.html --mode file --ignore-dirs build,.git,third_party
"""

from __future__ import annotations

import argparse
import html
import json
import os
import re
import sys
import traceback
from pathlib import Path
from typing import Dict, List, Set, Tuple, Optional


# ---------------------------- helpers ----------------------------

C_EXTS = {".c", ".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx", ".hxx"}


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return ""


def strip_comments_and_strings(src: str) -> str:
    # Remove strings and comments to reduce false matches in symbol mode.
    # Not a full lexer, but good enough for call-spotting.
    pattern = r"""
        ("(\\.|[^"\\])*")            # double-quoted string
      | ('(\\.|[^'\\])*')            # single-quoted char/string
      | (//[^\n]*\n?)                 # // comment
      | (/\*.*?\*/)                   # /* */ comment
    """
    return re.sub(pattern, lambda m: " " * (m.end() - m.start()), src, flags=re.VERBOSE | re.DOTALL)


def norm_rel(root: Path, p: Path) -> str:
    try:
        return str(p.resolve().relative_to(root.resolve())).replace("\\", "/")
    except Exception:
        return str(p).replace("\\", "/")


def is_ignored(path: Path, ignore_dirs: Set[str]) -> bool:
    parts = {part.lower() for part in path.parts}
    return any(d.lower() in parts for d in ignore_dirs)


def progress_line(msg: str, width: int = 220) -> None:
    # Clears the previous line to avoid "garbage suffix" on Windows terminals.
    sys.stdout.write("\r" + (" " * width) + "\r")
    sys.stdout.write(msg)
    sys.stdout.flush()


# ---------------------------- file include graph ----------------------------

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^">]+)[">]', re.MULTILINE)


def build_file_index(root: Path, ignore_dirs: Set[str], show_progress: bool = True) -> Dict[str, Path]:
    """
    Returns a mapping: relpath_lower -> absolute Path
    """
    index: Dict[str, Path] = {}
    for p in root.rglob("*"):
        if show_progress:
            progress_line(f"[SCAN] {p}")
        if not p.is_file():
            continue
        if is_ignored(p, ignore_dirs):
            continue
        if p.suffix.lower() not in C_EXTS:
            continue
        rel = norm_rel(root, p)
        index[rel.lower()] = p
    if show_progress:
        print()
    return index


def resolve_include(include_text: str, including_file: Path, root: Path, file_index: Dict[str, Path]) -> Optional[Path]:
    """
    Resolve #include "x.h" or "dir/x.h" to a project file.
    Strategy:
      1) relative to including file directory
      2) relative to root
      3) exact rel-path lookup
      4) suffix match
      5) basename match if unique
    """
    inc = include_text.replace("\\", "/").strip()
    if not inc:
        return None

    cand1 = (including_file.parent / inc).resolve()
    if cand1.exists() and cand1.is_file():
        return cand1

    cand2 = (root / inc).resolve()
    if cand2.exists() and cand2.is_file():
        return cand2

    key = inc.lower()
    if key in file_index:
        return file_index[key]

    suffix = "/" + key
    matches = [p for rel, p in file_index.items() if rel.endswith(suffix)]
    if len(matches) == 1:
        return matches[0]

    base = Path(inc).name.lower()
    base_matches = [p for rel, p in file_index.items() if Path(rel).name == base]
    if len(base_matches) == 1:
        return base_matches[0]

    return None


def build_file_graph(root: Path, ignore_dirs: Set[str], include_system: bool) -> Tuple[List[dict], List[dict]]:
    file_index = build_file_index(root, ignore_dirs, show_progress=True)

    nodes: Dict[str, dict] = {}
    edges: Set[Tuple[str, str]] = set()

    def add_node(rel: str) -> None:
        if rel not in nodes:
            ext = Path(rel).suffix.lower()
            kind = "header" if ext in {".h", ".hpp", ".hh", ".hxx"} else "source"
            nodes[rel] = {"id": rel, "label": rel, "group": kind}

    items = list(file_index.items())
    total = len(items)

    for i, (_rel_lower, abs_path) in enumerate(items, 1):
        progress_line(f"[INCLUDES] {i}/{total} {abs_path}")
        rel = norm_rel(root, abs_path)
        add_node(rel)

        content = read_text(abs_path)
        for m in INCLUDE_RE.finditer(content):
            bracket = m.group(1)
            inc_path = m.group(2).strip()

            if bracket == "<" and not include_system:
                continue

            if bracket == "<":
                sys_id = f"<{inc_path}>"
                if sys_id not in nodes:
                    nodes[sys_id] = {"id": sys_id, "label": sys_id, "group": "system"}
                edges.add((rel, sys_id))
                continue

            resolved = resolve_include(inc_path, abs_path, root, file_index)
            if resolved is None:
                miss_id = f"\"{inc_path}\""
                if miss_id not in nodes:
                    nodes[miss_id] = {"id": miss_id, "label": miss_id, "group": "missing"}
                edges.add((rel, miss_id))
            else:
                tgt_rel = norm_rel(root, resolved)
                add_node(tgt_rel)
                edges.add((rel, tgt_rel))

    print()
    node_list = list(nodes.values())
    edge_list = [{"from": a, "to": b, "arrows": "to"} for (a, b) in sorted(edges)]
    return node_list, edge_list


# ---------------------------- symbol call graph (heuristic) ----------------------------

FUNC_DEF_RE = re.compile(
    r"""
    ^\s*                                  # line start
    (?:[a-zA-Z_][\w\s\*\(\),\[\]]+?)       # return type-ish + qualifiers
    \b([a-zA-Z_]\w*)\s*                   # function name
    \(\s*[^;{]*\)\s*                      # params (no ; or { inside)
    (?:\n\s*)* \{                         # opening brace
    """,
    re.VERBOSE | re.MULTILINE,
)

CALL_RE_TEMPLATE = r"\b{fname}\s*\("

CALL_TOK_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")

def build_symbol_graph(root: Path, ignore_dirs: Set[str]) -> Tuple[List[dict], List[dict]]:
    # Step 1: collect project files
    files: List[Path] = []
    for p in root.rglob("*"):
        if p.is_file() and not is_ignored(p, ignore_dirs) and p.suffix.lower() in C_EXTS:
            files.append(p)

    # Step 2: find function definitions, map func -> defining file (if unique)
    func_defs: Dict[str, List[str]] = {}
    file_src_clean: Dict[str, str] = {}

    total_files = len(files)
    for i, f in enumerate(files, 1):
        progress_line(f"[FUNCTION SCAN] {i}/{total_files} {f}")

        rel = norm_rel(root, f)
        src = read_text(f)
        clean = strip_comments_and_strings(src)
        file_src_clean[rel] = clean

        for m in FUNC_DEF_RE.finditer(clean):
            name = m.group(1)
            func_defs.setdefault(name, []).append(rel)

    print()
    print("[PHASE] Function scan done. Building call edges...")

    func_to_file = {fn: locs[0] for fn, locs in func_defs.items() if len(locs) == 1}

    nodes: Dict[str, dict] = {}
    edges: Set[Tuple[str, str]] = set()

    def add_fn_node(fn: str) -> None:
        if fn not in nodes:
            nodes[fn] = {
                "id": fn,
                "label": fn,
                "group": "function",
                "title": f"Defined in: {func_to_file.get(fn, 'unknown')}",
            }

    # Precompile regex for all known functions
    known_funcs = set(func_to_file.keys())

    # Build: file -> set(called function names) by tokenizing once per file (FAST)
    file_called: Dict[str, Set[str]] = {}
    for fpath, src in file_src_clean.items():
        called = set(m.group(1) for m in CALL_TOK_RE.finditer(src))
        # keep only calls to project-known functions
        file_called[fpath] = called & known_funcs

    # Now build edges:
    # for each caller function, only consider callees that appear in its defining file
    callers = list(func_to_file.items())
    total_callers = len(callers)

    for idx, (caller, caller_file) in enumerate(callers, 1):
        if idx == 1 or idx % 25 == 0 or idx == total_callers:
            progress_line(f"[CALL GRAPH] {idx}/{total_callers} caller={caller} file={caller_file}")

        try:
            add_fn_node(caller)
            callees_in_file = file_called.get(caller_file, set())

            # avoid self-loops
            if caller in callees_in_file:
                callees_in_file = set(callees_in_file)
                callees_in_file.discard(caller)

            for callee in callees_in_file:
                add_fn_node(callee)
                edges.add((caller, callee))

        except Exception:
            print()
            print(f"[ERROR] caller={caller} file={caller_file}")
            traceback.print_exc()
            raise
    print()
    node_list = list(nodes.values())
    edge_list = [{"from": a, "to": b, "arrows": "to"} for (a, b) in sorted(edges)]
    return node_list, edge_list




# ---------------------------- HTML visual ----------------------------

HTML_TEMPLATE = r"""<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <title>C Dependency Graph</title>
  <meta name="viewport" content="width=device-width, initial-scale=1" />

  <!-- vis-network from CDN (no install required) -->
  <script src="https://unpkg.com/vis-network/standalone/umd/vis-network.min.js"></script>

  <style>
    :root {
      --bg: #0b0f19;
      --panel: #121a2a;
      --fg: #d6dbe6;
      --muted: #97a3b6;
      --border: #22304a;
      --chip: #1b2740;
      --chip2: #1a2f1f;
      --chip3: #2b2135;
      --chip4: #3a2a1a;
    }
    body {
      margin: 0; padding: 0;
      background: var(--bg); color: var(--fg);
      font-family: ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, Arial;
      height: 100vh; display: grid;
      grid-template-columns: 360px 1fr;
    }
    .sidebar {
      border-right: 1px solid var(--border);
      background: linear-gradient(180deg, #0c1322, #0b0f19);
      padding: 14px;
      overflow: auto;
    }
    .title { font-size: 16px; margin: 0 0 10px; }
    .sub { color: var(--muted); font-size: 12px; margin: 0 0 14px; }
    .row { display: flex; gap: 10px; align-items: center; margin: 10px 0; }
    input[type="text"] {
      width: 100%;
      background: var(--panel);
      border: 1px solid var(--border);
      color: var(--fg);
      padding: 10px 10px;
      border-radius: 10px;
      outline: none;
    }
    .btn {
      background: var(--panel);
      border: 1px solid var(--border);
      color: var(--fg);
      padding: 10px 10px;
      border-radius: 10px;
      cursor: pointer;
      user-select: none;
    }
    .btn:hover { filter: brightness(1.08); }
    .chips { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 10px; }
    .chip {
      font-size: 12px; padding: 6px 10px;
      border-radius: 999px; border: 1px solid var(--border);
      background: var(--chip);
    }
    .chip.header { background: var(--chip2); }
    .chip.system { background: var(--chip3); }
    .chip.missing { background: var(--chip4); }
    .stats {
      margin-top: 12px;
      padding: 10px;
      border-radius: 12px;
      background: rgba(18,26,42,.6);
      border: 1px solid var(--border);
      font-size: 12px;
      color: var(--muted);
      line-height: 1.5;
    }
    #network {
      width: 100%;
      height: 100vh;
      background: radial-gradient(1200px 700px at 30% 20%, rgba(61,108,255,.12), transparent 60%),
                  radial-gradient(900px 600px at 70% 70%, rgba(28,200,138,.10), transparent 60%),
                  var(--bg);
    }
    a { color: #8fb3ff; text-decoration: none; }
    .hint { margin-top: 10px; font-size: 12px; color: var(--muted); }
  </style>
</head>

<body>
  <div class="sidebar">
    <h1 class="title">C Dependency Graph</h1>
    <p class="sub">{mode_text}</p>

    <div class="row">
      <input id="search" type="text" placeholder="Search node (file or function)..." />
    </div>
    <div class="row">
      <button class="btn" id="focusBtn">Focus</button>
      <button class="btn" id="resetBtn">Reset</button>
      <button class="btn" id="physicsBtn">Toggle Physics</button>
    </div>

    <div class="chips">
      <span class="chip">source</span>
      <span class="chip header">header</span>
      <span class="chip system">system</span>
      <span class="chip missing">missing</span>
      <span class="chip">function</span>
    </div>

    <div class="stats" id="stats"></div>

    <div class="hint">
      Tips:
      <ul>
        <li>Scroll to zoom; drag to pan</li>
        <li>Click a node to highlight its neighbors</li>
        <li>Search + Focus to jump to a node</li>
      </ul>
    </div>
  </div>

  <div id="network"></div>

<script>
  const NODES = {nodes_json};
  const EDGES = {edges_json};

  const groupStyles = {
    source:  { shape: "box",   color: { background: "#20314f", border: "#385a98" }, font: { color: "#d6dbe6" } },
    header:  { shape: "box",   color: { background: "#1f3b2a", border: "#2f8d58" }, font: { color: "#d6dbe6" } },
    system:  { shape: "ellipse", color: { background: "#2a1d3d", border: "#7a4fd6" }, font: { color: "#d6dbe6" } },
    missing: { shape: "ellipse", color: { background: "#3a2a1a", border: "#d58b3a" }, font: { color: "#d6dbe6" } },
    function:{ shape: "dot", size: 10, color: { background: "#27324a", border: "#7aa2ff" }, font: { color: "#d6dbe6" } }
  };

  const nodes = new vis.DataSet(NODES.map(n => ({...n, ...(groupStyles[n.group] || {}) })));
  const edges = new vis.DataSet(EDGES.map(e => ({
    ...e,
    color: { color: "rgba(160,180,220,.55)" },
    arrows: { to: { enabled: true, scaleFactor: 0.7 } },
    smooth: { enabled: true, type: "dynamic" }
  })));

  const container = document.getElementById("network");
  const data = { nodes, edges };

  const options = {
    physics: {
      enabled: true,
      stabilization: { iterations: 120 },
      barnesHut: { gravitationalConstant: -7000, springLength: 140, springConstant: 0.02 }
    },
    interaction: { hover: true, multiselect: true },
    nodes: { borderWidth: 1.2 },
  };

  const network = new vis.Network(container, data, options);

  const statsEl = document.getElementById("stats");
  statsEl.textContent = `Nodes: ${nodes.length}  |  Edges: ${edges.length}`;

  network.on("click", function(params) {
    if (!params.nodes.length) {
      nodes.forEach(n => nodes.update({id: n.id, opacity: 1.0}));
      edges.forEach(e => edges.update({id: e.id, opacity: 1.0}));
      return;
    }
    const selected = params.nodes[0];
    const neighbors = new Set(network.getConnectedNodes(selected));
    neighbors.add(selected);

    nodes.forEach(n => nodes.update({id: n.id, opacity: neighbors.has(n.id) ? 1.0 : 0.15}));
    edges.forEach(e => {
      const keep = neighbors.has(e.from) && neighbors.has(e.to);
      edges.update({id: e.id, opacity: keep ? 1.0 : 0.08});
    });
  });

  function findNodeId(q) {
    q = (q || "").trim().toLowerCase();
    if (!q) return null;

    let hit = nodes.get().find(n => (n.id || "").toLowerCase() === q);
    if (hit) return hit.id;

    hit = nodes.get().find(n => (n.label || "").toLowerCase().includes(q));
    return hit ? hit.id : null;
  }

  document.getElementById("focusBtn").onclick = () => {
    const q = document.getElementById("search").value;
    const id = findNodeId(q);
    if (!id) return;
    network.selectNodes([id]);
    network.focus(id, { scale: 1.4, animation: { duration: 600 } });
  };

  document.getElementById("resetBtn").onclick = () => {
    network.setData({nodes, edges});
    network.fit({ animation: { duration: 500 } });
    nodes.forEach(n => nodes.update({id: n.id, opacity: 1.0}));
    edges.forEach(e => edges.update({id: e.id, opacity: 1.0}));
  };

  let physicsOn = true;
  document.getElementById("physicsBtn").onclick = () => {
    physicsOn = !physicsOn;
    network.setOptions({ physics: { enabled: physicsOn }});
  };
</script>
</body>
</html>
"""


def write_html(out_path: Path, nodes: List[dict], edges: List[dict], mode: str) -> None:
    mode_text = "Mode: file include graph (#include \"...\")" if mode == "file" else "Mode: function call graph (best-effort)"
    html_text = (
        HTML_TEMPLATE
        .replace("{mode_text}", html.escape(mode_text))
        .replace("{nodes_json}", json.dumps(nodes, ensure_ascii=False))
        .replace("{edges_json}", json.dumps(edges, ensure_ascii=False))
    )
    out_path.write_text(html_text, encoding="utf-8")


# ---------------------------- CLI ----------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Crawl a C codebase folder and build a visual dependency graph (HTML).")
    p.add_argument("--root", required=True, help="Root folder of your C app (working directory).")
    p.add_argument("--out", default="deps.html", help="Output HTML file (default: deps.html)")
    p.add_argument("--mode", choices=["file", "symbol"], default="file",
                   help="file = include graph (recommended), symbol = function call graph (heuristic)")
    p.add_argument("--ignore-dirs", default=".git,build,dist,out,.vs,.vscode,.idea,.cache",
                   help="Comma-separated dir names to ignore.")
    p.add_argument("--include-system", action="store_true",
                   help='Include system headers (#include <...>) as external nodes (file mode only).')
    return p.parse_args()


def main() -> None:
    args = parse_args()
    root = Path(args.root).resolve()
    out = Path(args.out).resolve()
    ignore_dirs = {d.strip() for d in args.ignore_dirs.split(",") if d.strip()}

    if not root.exists() or not root.is_dir():
        raise SystemExit(f"--root must be a folder: {root}")

    if args.mode == "file":
        nodes, edges = build_file_graph(root, ignore_dirs, include_system=args.include_system)
    else:
        nodes, edges = build_symbol_graph(root, ignore_dirs)

    write_html(out, nodes, edges, args.mode)

    print(f"Wrote: {out}")
    print(f"Mode: {args.mode} | Nodes: {len(nodes)} | Edges: {len(edges)}")
    print("Open the HTML in your browser to view the interactive graph.")
    print()


if __name__ == "__main__":
    main()
