#!/usr/bin/env python3
"""
c_stats_html.py — one-file HTML report (no PNGs)

- Walks a C/C++ codebase
- Counts total/blank/comment-only/code lines
- Breakdown by extension + directory hotspots
- Top N files by code lines + by complexity proxy
- Inline "graphs" using HTML/CSS bars (no external files)

Usage:
  python c_stats_html.py --root .
  python c_stats_html.py --root . --ignore ".git,build,dist" --exclude-files "*test*.*,*/third_party/*" --top 30
  python c_stats_html.py --root . --out report.html
  Proper Usage:
  python c_stats.py --root . --ignore ".git,build,dist,venv,node_modules,assets,src" --exclude-files "miniaudio.h, miniz.h, miniz.c" --top 50 --out c.html
"""

from __future__ import annotations
import argparse
import os
import fnmatch
import re
import statistics
from dataclasses import dataclass
from typing import Dict, List, Tuple, Optional


C_LIKE_EXTS = {".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx", ".inl", ".inc"}

COMPLEX_TOKENS = [
    r"\bif\b", r"\belse\b", r"\bfor\b", r"\bwhile\b", r"\bdo\b",
    r"\bswitch\b", r"\bcase\b", r"\bdefault\b", r"\breturn\b",
    r"\bgoto\b", r"&&", r"\|\|", r"\?", r"\bcontinue\b", r"\bbreak\b"
]
COMPLEX_RE = re.compile("|".join(COMPLEX_TOKENS))


@dataclass
class FileStats:
    path: str
    ext: str
    total: int
    blank: int
    comment: int
    code: int
    complexity_hits: int


@dataclass
class AggStats:
    files: int = 0
    total: int = 0
    blank: int = 0
    comment: int = 0
    code: int = 0
    complexity_hits: int = 0


def is_ignored_dir(dirname: str, ignore_set: set[str]) -> bool:
    return dirname in ignore_set


def analyze_c_like_text(text: str) -> Tuple[int, int, int, int]:
    """
    Returns: total_lines, blank_lines, comment_only_lines, code_lines
    Heuristic line classifier for /* */ and // comments.
    """
    lines = text.splitlines()
    total = len(lines)
    blank = 0
    comment_only = 0
    code = 0
    in_block = False

    for line in lines:
        s = line.rstrip("\n")
        if not s.strip():
            blank += 1
            continue

        i = 0
        has_code = False
        in_string = False
        string_char = ""

        while i < len(s):
            ch = s[i]

            # strings (basic)
            if not in_block:
                if not in_string and ch in ('"', "'"):
                    in_string = True
                    string_char = ch
                    has_code = True
                    i += 1
                    continue
                elif in_string:
                    if ch == "\\" and i + 1 < len(s):
                        i += 2
                        continue
                    if ch == string_char:
                        in_string = False
                    has_code = True
                    i += 1
                    continue

            if in_block:
                end = s.find("*/", i)
                if end == -1:
                    i = len(s)
                    continue
                i = end + 2
                in_block = False
                continue

            if not in_string and ch.isspace():
                i += 1
                continue

            if not in_string and ch == "/":
                if i + 1 < len(s):
                    nxt = s[i + 1]
                    if nxt == "/":
                        # rest is comment
                        break
                    if nxt == "*":
                        in_block = True
                        i += 2
                        continue

            # any other non-whitespace char outside comment = code
            has_code = True
            i += 1

        if has_code:
            code += 1
        else:
            comment_only += 1

    return total, blank, comment_only, code


def read_text_file(path: str) -> Optional[str]:
    try:
        with open(path, "rb") as f:
            data = f.read()
        try:
            return data.decode("utf-8")
        except UnicodeDecodeError:
            return data.decode("latin-1", errors="replace")
    except Exception:
        return None


def walk_files(root: str, ignore_set: set[str], follow_symlinks: bool, exclude_files: List[str]) -> List[str]:
    out: List[str] = []
    for dirpath, dirnames, filenames in os.walk(root, followlinks=follow_symlinks):
        dirnames[:] = [d for d in dirnames if not is_ignored_dir(d, ignore_set)]
        for fn in filenames:
            ext = os.path.splitext(fn)[1].lower()
            if ext not in C_LIKE_EXTS:
                continue

            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, root).replace("\\", "/")

            if exclude_files and any(
                fnmatch.fnmatch(fn, pat) or fnmatch.fnmatch(rel, pat)
                for pat in exclude_files
            ):
                continue

            out.append(full)
    return out


def fmt_int(n: int) -> str:
    return f"{n:,}"


def pct(n: int, d: int) -> float:
    return 0.0 if d <= 0 else (n / d) * 100.0


def html_escape(s: str) -> str:
    return (s.replace("&", "&amp;")
             .replace("<", "&lt;")
             .replace(">", "&gt;")
             .replace('"', "&quot;")
             .replace("'", "&#39;"))


def bar_row(label: str, value: int, max_value: int, right_text: str) -> str:
    w = 0 if max_value <= 0 else int(round((value / max_value) * 100))
    return f"""
    <div class="row">
      <div class="lbl">{html_escape(label)}</div>
      <div class="barwrap"><div class="bar" style="width:{w}%"></div></div>
      <div class="val">{html_escape(right_text)}</div>
    </div>
    """


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".", help="Root directory of codebase")
    ap.add_argument(
        "--ignore",
        default=".git,build,dist,out,.cache,__pycache__,node_modules,venv,.venv",
        help="Comma-separated dir names to ignore (exact folder names)",
    )
    ap.add_argument(
        "--exclude-files",
        default="",
        help="Comma-separated file patterns to exclude (supports * wildcards), e.g. '*test*.*,generated_*.c,foo.h'",
    )
    ap.add_argument("--top", type=int, default=25, help="Top N rows for tables/graphs")
    ap.add_argument("--follow-symlinks", action="store_true", help="Follow symlinks")
    ap.add_argument("--out", default="c_stats_report.html", help="Output HTML file path")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    ignore_set = set([x.strip() for x in args.ignore.split(",") if x.strip()])
    exclude_files = [x.strip() for x in args.exclude_files.split(",") if x.strip()]

    files = walk_files(root, ignore_set, args.follow_symlinks, exclude_files)
    if not files:
        print("No C/C++ files found.")
        return

    per_ext: Dict[str, AggStats] = {}
    per_dir: Dict[str, AggStats] = {}
    all_stats = AggStats()
    file_stats: List[FileStats] = []

    for path in files:
        ext = os.path.splitext(path)[1].lower()
        text = read_text_file(path)
        if text is None:
            continue

        total, blank, comment, code = analyze_c_like_text(text)
        complexity = len(COMPLEX_RE.findall(text))

        fs = FileStats(path=path, ext=ext, total=total, blank=blank, comment=comment, code=code, complexity_hits=complexity)
        file_stats.append(fs)

        if ext not in per_ext:
            per_ext[ext] = AggStats()

        rel = os.path.relpath(path, root).replace("\\", "/")
        top_dir = rel.split("/", 1)[0] if "/" in rel else "."

        if top_dir not in per_dir:
            per_dir[top_dir] = AggStats()

        for agg in (all_stats, per_ext[ext], per_dir[top_dir]):
            agg.files += 1
            agg.total += total
            agg.blank += blank
            agg.comment += comment
            agg.code += code
            agg.complexity_hits += complexity

    file_stats.sort(key=lambda x: x.code, reverse=True)

    denom = max(1, all_stats.total)
    comment_to_code = (all_stats.comment / max(1, all_stats.code))

    code_lines = [fs.code for fs in file_stats] or [0]
    avg_code = int(statistics.mean(code_lines))
    med_code = int(statistics.median(code_lines))
    p90_code = int(statistics.quantiles(code_lines, n=10)[8]) if len(code_lines) >= 10 else max(code_lines)
    p95_code = int(statistics.quantiles(code_lines, n=20)[18]) if len(code_lines) >= 20 else max(code_lines)
    p99_code = int(statistics.quantiles(code_lines, n=100)[98]) if len(code_lines) >= 100 else max(code_lines)

    exts_sorted = sorted(per_ext.items(), key=lambda kv: kv[1].code, reverse=True)
    dirs_sorted = sorted(per_dir.items(), key=lambda kv: kv[1].code, reverse=True)
    max_ext_code = max([a.code for _, a in exts_sorted] or [0])
    max_dir_code = max([a.code for _, a in dirs_sorted] or [0])
    max_top_file = max([fs.code for fs in file_stats[:args.top]] or [0])

    # Top complexity
    file_by_cx = sorted(file_stats, key=lambda f: f.complexity_hits, reverse=True)[: args.top]
    max_cx = max([f.complexity_hits for f in file_by_cx] or [0])

    def short_rel(p: str) -> str:
        rel = os.path.relpath(p, root).replace("\\", "/")
        parts = rel.split("/")
        return "/".join(parts[-2:]) if len(parts) >= 2 else rel

    # Build HTML
    html = f"""<!doctype html>
<html>
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>C/C++ Codebase Report</title>
<style>
  :root {{
    --bg: #0f1115;
    --panel: #151823;
    --card: #1a1f2e;
    --fg: #e7e9ee;
    --muted: #a8adbb;
    --border: rgba(255,255,255,0.08);
    --bar: #6aa2ff;
    --bar2: #7ee2b8;
    --bar3: #ffcc66;
    --bad: #ff6b6b;
    --mono: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
    --sans: ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
  }}

  body {{
    margin: 0;
    background: var(--bg);
    color: var(--fg);
    font-family: var(--sans);
    line-height: 1.35;
  }}

  .wrap {{
    max-width: 1100px;
    margin: 0 auto;
    padding: 22px;
  }}

  h1 {{
    font-size: 22px;
    margin: 0 0 6px;
  }}

  .sub {{
    color: var(--muted);
    font-family: var(--mono);
    font-size: 12px;
    margin-bottom: 16px;
  }}

  .grid {{
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 14px;
  }}

  .card {{
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 14px;
  }}

  .card h2 {{
    margin: 0 0 10px;
    font-size: 14px;
    color: var(--fg);
    letter-spacing: 0.2px;
  }}

  table {{
    width: 100%;
    border-collapse: collapse;
    font-size: 13px;
  }}
  th, td {{
    border-bottom: 1px solid var(--border);
    padding: 8px 6px;
    vertical-align: top;
  }}
  th {{
    text-align: left;
    color: var(--muted);
    font-weight: 600;
  }}
  td code {{
    font-family: var(--mono);
    font-size: 12px;
    color: var(--fg);
  }}

  .kpi {{
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 10px;
  }}
  .k {{
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 10px 12px;
  }}
  .k .name {{
    color: var(--muted);
    font-size: 12px;
  }}
  .k .val {{
    font-family: var(--mono);
    font-size: 18px;
    margin-top: 4px;
  }}

  .rows {{
    display: flex;
    flex-direction: column;
    gap: 8px;
  }}
  .row {{
    display: grid;
    grid-template-columns: 180px 1fr 120px;
    gap: 10px;
    align-items: center;
    font-size: 13px;
  }}
  .lbl {{
    color: var(--muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }}
  .barwrap {{
    height: 10px;
    background: rgba(255,255,255,0.06);
    border-radius: 999px;
    overflow: hidden;
    border: 1px solid rgba(255,255,255,0.06);
  }}
  .bar {{
    height: 100%;
    border-radius: 999px;
    background: var(--bar);
  }}
  .val {{
    font-family: var(--mono);
    font-size: 12px;
    text-align: right;
    color: var(--fg);
  }}

  .bar2 {{ background: var(--bar2); }}
  .bar3 {{ background: var(--bar3); }}
  .bad  {{ background: var(--bad); }}

  .mono {{ font-family: var(--mono); }}

  .footer {{
    margin-top: 18px;
    color: var(--muted);
    font-size: 12px;
  }}

  .tip {{
    border-bottom: 1px dotted rgba(255,255,255,0.35);
    cursor: help;
  }}

  @media (max-width: 980px) {{
    .grid {{ grid-template-columns: 1fr; }}
    .row {{ grid-template-columns: 140px 1fr 110px; }}
  }}
</style>
</head>
<body>
<div class="wrap">
  <h1>C/C++ Codebase Report</h1>
  <div class="sub">root: {html_escape(root)} | files: {fmt_int(all_stats.files)} | generated by c_stats.py</div>

  <div class="grid">
    <div class="card">
      <h2>Summary KPIs</h2>
      <div class="kpi">
        <div class="k"><div class="name">Total lines</div><div class="val">{fmt_int(all_stats.total)}</div></div>
        <div class="k"><div class="name">Code lines</div><div class="val">{fmt_int(all_stats.code)}</div></div>
        <div class="k"><div class="name">Comment-only lines</div><div class="val">{fmt_int(all_stats.comment)}</div></div>
        <div class="k"><div class="name">Blank lines</div><div class="val">{fmt_int(all_stats.blank)}</div></div>
        <div class="k"><div class="name">Comment/Code ratio</div><div class="val">{comment_to_code:.2f}</div></div>
        <div class="k"><div class="name">Complexity hits</div><div class="val">{fmt_int(all_stats.complexity_hits)}</div></div>
      </div>
    </div>

    <div class="card" style="margin-top:14px;">
        <h2>What is “complexity hits”?</h2>
        <div style="color:var(--muted); font-size:13px;">
            Complexity hits is a quick proxy metric: it counts how often a file contains common control-flow / branching tokens
            like <span class="mono">if/else</span>, <span class="mono">for/while</span>, <span class="mono">switch/case</span>,
            boolean operators <span class="mono">&&</span>, <span class="mono">||</span>, and the ternary <span class="mono">?</span>.
            <br/><br/>
            Higher hits usually means “denser logic” and potentially harder-to-read code. It is not a true cyclomatic complexity score
            and may overcount because this script does not fully parse C (strings/comments can affect it).
        </div>
    </div>

    <div class="card">
      <h2>Line composition</h2>
      <div class="rows">
        {bar_row("Code", all_stats.code, denom, f"{fmt_int(all_stats.code)} ({pct(all_stats.code, denom):.2f}%)")}
        {bar_row("Comment-only", all_stats.comment, denom, f"{fmt_int(all_stats.comment)} ({pct(all_stats.comment, denom):.2f}%)")}
        {bar_row("Blank", all_stats.blank, denom, f"{fmt_int(all_stats.blank)} ({pct(all_stats.blank, denom):.2f}%)")}
      </div>
      <div class="footer">Heuristic counts: comment-only means “line contains no code tokens.”</div>
    </div>
  </div>

  <div class="grid" style="margin-top:14px;">
    <div class="card">
      <h2>File size distribution (code lines)</h2>
      <table>
        <tr><th>Avg</th><td class="mono">{fmt_int(avg_code)}</td></tr>
        <tr><th>Median</th><td class="mono">{fmt_int(med_code)}</td></tr>
        <tr><th>P90</th><td class="mono">{fmt_int(p90_code)}</td></tr>
        <tr><th>P95</th><td class="mono">{fmt_int(p95_code)}</td></tr>
        <tr><th>P99</th><td class="mono">{fmt_int(p99_code)}</td></tr>
      </table>
    </div>

    <div class="card">
      <h2>Breakdown by extension (graph)</h2>
      <div class="rows">
"""

    # Extension bars
    for ext, agg in exts_sorted[: min(args.top, len(exts_sorted))]:
        w = 0 if max_ext_code <= 0 else int(round((agg.code / max_ext_code) * 100))
        html += f"""
        <div class="row">
          <div class="lbl">{html_escape(ext)}</div>
          <div class="barwrap"><div class="bar bar2" style="width:{w}%"></div></div>
          <div class="val">{fmt_int(agg.code)} code</div>
        </div>
        """

    html += f"""
      </div>
      <div class="footer">Bars normalized to the largest extension bucket.</div>
    </div>
  </div>

  <div class="grid" style="margin-top:14px;">
    <div class="card">
      <h2>Directory hotspots (top-level)</h2>
      <div class="rows">
"""

    for d, agg in dirs_sorted[: min(args.top, len(dirs_sorted))]:
        w = 0 if max_dir_code <= 0 else int(round((agg.code / max_dir_code) * 100))
        html += f"""
        <div class="row">
          <div class="lbl">{html_escape(d)}</div>
          <div class="barwrap"><div class="bar bar3" style="width:{w}%"></div></div>
          <div class="val">{fmt_int(agg.code)} code</div>
        </div>
        """

    html += f"""
      </div>
      <div class="footer">This groups by the first folder under root. Tell me if you want depth=2 or 3.</div>
    </div>

    <div class="card">
      <h2>Top {args.top} files by code lines</h2>
      <div class="rows">
"""

    for fs in file_stats[: args.top]:
        label = short_rel(fs.path)
        w = 0 if max_top_file <= 0 else int(round((fs.code / max_top_file) * 100))
        html += f"""
        <div class="row">
          <div class="lbl">{html_escape(label)}</div>
          <div class="barwrap"><div class="bar" style="width:{w}%"></div></div>
          <div class="val">{fmt_int(fs.code)} code</div>
        </div>
        """

    html += f"""
      </div>
      <div class="footer">Bars normalized to the largest file in the list.</div>
    </div>
  </div>

  <div class="card" style="margin-top:14px;">
    <h2>Top {args.top} files by “complexity proxy”</h2>
    <div class="rows">
"""

    for fs in file_by_cx:
        label = short_rel(fs.path)
        w = 0 if max_cx <= 0 else int(round((fs.complexity_hits / max_cx) * 100))
        html += f"""
        <div class="row">
          <div class="lbl">{html_escape(label)}</div>
          <div class="barwrap"><div class="bar bad" style="width:{w}%"></div></div>
          <div class="val">{fmt_int(fs.complexity_hits)} hits</div>
        </div>
        """

    html += f"""
    </div>
    <div class="footer">
      Complexity proxy counts token hits: {html_escape(", ".join(["if","else","for","while","switch","case","&&","||","?","return","goto"]))}.
      Use it to find “dense logic” files quickly.
    </div>
  </div>

  <div class="card" style="margin-top:14px;">
    <h2>Raw tables</h2>
    <h3 style="margin:10px 0 6px; font-size:13px; color:var(--muted);">Extension table</h3>
    <table>
      <tr>
        <th>Ext</th><th>Files</th><th>Total</th><th>Code</th><th>Comment</th><th>Blank</th><th>Code %</th><th><span class="tip" title="Proxy count of branching/control-flow tokens: if/else, loops, switch/case, &&, ||, ?, etc. Not true cyclomatic complexity.">Complexity</span></th>
      </tr>
"""

    for ext, agg in exts_sorted:
        html += f"""
      <tr>
        <td class="mono">{html_escape(ext)}</td>
        <td class="mono">{fmt_int(agg.files)}</td>
        <td class="mono">{fmt_int(agg.total)}</td>
        <td class="mono">{fmt_int(agg.code)}</td>
        <td class="mono">{fmt_int(agg.comment)}</td>
        <td class="mono">{fmt_int(agg.blank)}</td>
        <td class="mono">{pct(agg.code, max(1, agg.total)):.1f}%</td>
        <td class="mono">{fmt_int(agg.complexity_hits)}</td>
      </tr>
"""

    html += f"""
    </table>

    <h3 style="margin:14px 0 6px; font-size:13px; color:var(--muted);">Directory table (top-level)</h3>
    <table>
      <tr>
        <th>Dir</th><th>Files</th><th>Total</th><th>Code</th><th>Comment</th><th>Blank</th><th><span class="tip" title="Proxy count of branching/control-flow tokens: if/else, loops, switch/case, &&, ||, ?, etc. Not true cyclomatic complexity.">Complexity</span></th>
      </tr>
"""

    for d, agg in dirs_sorted[: max(1, min(80, len(dirs_sorted)))]:
        html += f"""
      <tr>
        <td class="mono">{html_escape(d)}</td>
        <td class="mono">{fmt_int(agg.files)}</td>
        <td class="mono">{fmt_int(agg.total)}</td>
        <td class="mono">{fmt_int(agg.code)}</td>
        <td class="mono">{fmt_int(agg.comment)}</td>
        <td class="mono">{fmt_int(agg.blank)}</td>
        <td class="mono">{fmt_int(agg.complexity_hits)}</td>
      </tr>
"""

    html += f"""
    </table>

    <h3 style="margin:14px 0 6px; font-size:13px; color:var(--muted);">Top files table</h3>
    <table>
      <tr>
        <th>File</th><th>Ext</th><th>Total</th><th>Code</th><th>Comment</th><th>Blank</th><th><span class="tip" title="Proxy count of branching/control-flow tokens: if/else, loops, switch/case, &&, ||, ?, etc. Not true cyclomatic complexity.">Complexity</span></th>
      </tr>
"""

    for fs in file_stats[: args.top]:
        rel = os.path.relpath(fs.path, root).replace("\\", "/")
        html += f"""
      <tr>
        <td class="mono">{html_escape(rel)}</td>
        <td class="mono">{html_escape(fs.ext)}</td>
        <td class="mono">{fmt_int(fs.total)}</td>
        <td class="mono">{fmt_int(fs.code)}</td>
        <td class="mono">{fmt_int(fs.comment)}</td>
        <td class="mono">{fmt_int(fs.blank)}</td>
        <td class="mono">{fmt_int(fs.complexity_hits)}</td>
      </tr>
"""

    html += f"""
    </table>
  </div>

  <div class="footer">
    Notes: comment-only / code classification is heuristic (not a full C parser).
    Exclude patterns used: <span class="mono">{html_escape(", ".join(exclude_files) if exclude_files else "(none)")}</span>
  </div>
</div>
</body>
</html>
"""

    out_path = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(html)

    print(f"Wrote HTML report: {out_path}")


if __name__ == "__main__":
    main()