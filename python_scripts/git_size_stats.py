#!/usr/bin/env python3
"""
git_repo_stats_html.py

Generates a single self-contained HTML report (with inline SVG + JS) showing:
- Top N largest files (bytes)
- Top N files by line count
- Breakdown by file extension: total bytes + total lines
- Pie charts (extension share by bytes / by lines)
- Bar charts (top files, top extensions)
- Summary numbers

Crucially: it respects .gitignore by asking Git for the file list using:
  git ls-files -co --exclude-standard

So ignored files are excluded automatically.

Usage:
  python git_repo_stats_html.py --root . --out repo_stats.html --top 30

Notes:
- Requires "git" available on PATH and root being inside a git repo for proper ignore behavior.
- If not in a git repo (or git missing), it falls back to a best-effort scan with a minimal .gitignore parser
  (still decent, but Git mode is the accurate one).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# -----------------------------
# Data structures
# -----------------------------

@dataclass
class FileStat:
    relpath: str
    ext: str
    size_bytes: int
    lines: int
    blank_lines: int


# -----------------------------
# Git file listing (respects .gitignore)
# -----------------------------

def _run(cmd: List[str], cwd: Path) -> Tuple[int, str, str]:
    p = subprocess.Popen(cmd, cwd=str(cwd), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = p.communicate()
    return p.returncode, out.decode("utf-8", "replace"), err.decode("utf-8", "replace")

def list_files_via_git(root: Path) -> Optional[List[str]]:
    # Check if we're inside a git work tree
    rc, out, _ = _run(["git", "rev-parse", "--is-inside-work-tree"], root)
    if rc != 0 or out.strip().lower() != "true":
        return None

    # -c = cached(tracked), -o = others(untracked), --exclude-standard = respects .gitignore, .git/info/exclude, global excludes
    # Use -z for safe delimiting
    rc, out, err = _run(["git", "ls-files", "-co", "--exclude-standard", "-z"], root)
    if rc != 0:
        return None

    items = out.split("\x00")
    items = [x for x in items if x]
    # Filter out directories or weird entries
    items = [x for x in items if not x.endswith("/")]
    return items


# -----------------------------
# Fallback ignore (best-effort)
# -----------------------------

def load_gitignore_patterns(root: Path) -> List[str]:
    # Best-effort: just read root/.gitignore
    p = root / ".gitignore"
    if not p.exists():
        return []
    lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
    pats = []
    for ln in lines:
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        pats.append(ln)
    return pats

def _glob_to_regex(glob_pat: str) -> re.Pattern:
    # Minimal conversion:
    # - supports *, ?, **, leading / (root relative)
    # - supports trailing / for directory matches
    # - does NOT fully implement gitignore spec, but works for common patterns.
    dir_only = glob_pat.endswith("/")
    anchored = glob_pat.startswith("/")
    pat = glob_pat.strip("/")
    # Escape regex special chars except our glob tokens
    pat_esc = ""
    i = 0
    while i < len(pat):
        c = pat[i]
        if c == "*":
            # handle **
            if i + 1 < len(pat) and pat[i + 1] == "*":
                pat_esc += ".*"
                i += 2
            else:
                pat_esc += "[^/]*"
                i += 1
        elif c == "?":
            pat_esc += "[^/]"
            i += 1
        else:
            pat_esc += re.escape(c)
            i += 1

    if anchored:
        rx = "^" + pat_esc
    else:
        rx = r"(^|.*/)" + pat_esc  # match anywhere in path segments

    if dir_only:
        rx += r"(/.*)?$"
    else:
        rx += r"$"

    return re.compile(rx)

def list_files_fallback(root: Path) -> List[str]:
    pats = load_gitignore_patterns(root)
    neg = []
    pos = []
    for p in pats:
        if p.startswith("!"):
            neg.append(_glob_to_regex(p[1:]))
        else:
            pos.append(_glob_to_regex(p))

    files = []
    for dp, dn, fn in os.walk(root):
        # Always skip .git
        if ".git" in dn:
            dn.remove(".git")
        for f in fn:
            full = Path(dp) / f
            rel = full.relative_to(root).as_posix()
            # ignore match?
            ignored = False
            for rx in pos:
                if rx.search(rel):
                    ignored = True
                    break
            for rx in neg:
                if rx.search(rel):
                    ignored = False
            if not ignored:
                files.append(rel)
    return files


# -----------------------------
# Stats collection
# -----------------------------

def guess_ext(relpath: str) -> str:
    p = Path(relpath)
    ext = p.suffix.lower()
    if ext == "":
        return "(noext)"
    return ext

def count_lines(file_path: Path) -> Tuple[int, int]:
    # robust-ish line counting (treat as text with replacement)
    try:
        data = file_path.read_text(encoding="utf-8", errors="replace").splitlines()
        lines = len(data)
        blanks = sum(1 for x in data if not x.strip())
        return lines, blanks
    except Exception:
        return 0, 0

def collect_stats(root: Path, relpaths: List[str], sample_lines: bool = True) -> List[FileStat]:
    out: List[FileStat] = []
    for rel in relpaths:
        full = root / rel
        if not full.exists() or not full.is_file():
            continue
        try:
            size = full.stat().st_size
        except Exception:
            size = 0
        ext = guess_ext(rel)
        if sample_lines:
            lines, blanks = count_lines(full)
        else:
            lines, blanks = 0, 0
        out.append(FileStat(relpath=rel, ext=ext, size_bytes=size, lines=lines, blank_lines=blanks))
    return out


# -----------------------------
# HTML report (self-contained: inline SVG + JS, no external libs)
# -----------------------------

def fmt_bytes(n: int) -> str:
    units = ["B", "KB", "MB", "GB", "TB"]
    x = float(n)
    for u in units:
        if x < 1024 or u == units[-1]:
            if u == "B":
                return f"{int(x)} {u}"
            return f"{x:.2f} {u}"
        x /= 1024.0
    return f"{n} B"

HTML_TEMPLATE = r"""<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <title>Repo Stats Report</title>
  <style>
    :root {
      --bg: #0b0f14;
      --panel: #111827;
      --panel2: #0f172a;
      --text: #d6deeb;
      --muted: #9ca3af;
      --grid: rgba(255,255,255,0.08);
      --accent: #60a5fa;
      --good: #34d399;
      --warn: #fbbf24;
      --bad: #fb7185;
    }
    * { box-sizing: border-box; }
    body { margin: 0; font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif; background: var(--bg); color: var(--text); }
    header { padding: 18px 18px 10px; border-bottom: 1px solid var(--grid); }
    header h1 { margin: 0; font-size: 18px; }
    header .sub { margin-top: 6px; color: var(--muted); font-size: 12px; }

    .wrap { display: grid; grid-template-columns: 1fr; gap: 12px; padding: 14px; max-width: 1400px; margin: 0 auto; }
    .grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
    .grid3 { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 12px; }

    .card { background: linear-gradient(180deg, var(--panel), var(--panel2)); border: 1px solid var(--grid); border-radius: 14px; padding: 12px; }
    .card h2 { margin: 0 0 8px; font-size: 14px; }
    .muted { color: var(--muted); font-size: 12px; }
    .big { font-size: 22px; font-weight: 700; }
    .kpi { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    .pill { display: inline-block; padding: 2px 8px; border: 1px solid var(--grid); border-radius: 999px; font-size: 12px; color: var(--muted); }

    table { width: 100%; border-collapse: collapse; font-size: 12px; }
    th, td { padding: 8px; border-bottom: 1px solid var(--grid); vertical-align: top; }
    th { text-align: left; color: var(--muted); position: sticky; top: 0; background: rgba(11,15,20,0.9); backdrop-filter: blur(6px); }
    .mono { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
    .right { text-align: right; }
    .chartwrap { width: 100%; overflow: hidden; }
    .row { display: flex; justify-content: space-between; gap: 10px; align-items: baseline; }
    .note { color: var(--muted); font-size: 12px; margin-top: 6px; }
    .tiny { color: var(--muted); font-size: 11px; }
    .legend { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; margin-top: 10px; }
    .legitem { display: flex; gap: 8px; align-items: center; font-size: 12px; color: var(--muted); }
    .sw { width: 10px; height: 10px; border-radius: 3px; display: inline-block; }
    .search { display: flex; gap: 8px; }
    .search input { width: 100%; padding: 8px 10px; border-radius: 10px; border: 1px solid var(--grid); background: rgba(255,255,255,0.03); color: var(--text); }
    .btn { padding: 8px 10px; border-radius: 10px; border: 1px solid var(--grid); background: rgba(255,255,255,0.06); color: var(--text); cursor: pointer; }
  </style>
</head>
<body>
<header>
  <h1>Repo Stats Report</h1>
  <div class="sub">Root: <span class="mono">{{ROOT}}</span> • Generated: {{GENERATED}}</div>
</header>

<div class="wrap">

  <div class="grid3">
    <div class="card">
      <div class="row"><h2>Files</h2><span class="pill">.gitignore respected</span></div>
      <div class="big">{{FILE_COUNT}}</div>
      <div class="note">Counted files = tracked + untracked (excluding ignored), via <span class="mono">git ls-files -co --exclude-standard</span>.</div>
    </div>
    <div class="card">
      <div class="row"><h2>Total size</h2><span class="pill">bytes on disk</span></div>
      <div class="big">{{TOTAL_BYTES_H}}</div>
      <div class="note">Sum of file sizes for included files.</div>
    </div>
    <div class="card">
      <div class="row"><h2>Total lines</h2><span class="pill">text-ish</span></div>
      <div class="big">{{TOTAL_LINES}}</div>
      <div class="note">Lines counted as UTF-8 with replacement for non-text bytes.</div>
    </div>
  </div>

  <div class="grid2">
    <div class="card">
      <div class="row">
        <h2>Top files by size</h2>
        <span class="pill">Top {{TOPN}}</span>
      </div>
      <div class="chartwrap" id="chart_top_size"></div>
      <div class="note">Hover on bars for exact numbers.</div>
    </div>
    <div class="card">
      <div class="row">
        <h2>Top files by lines</h2>
        <span class="pill">Top {{TOPN}}</span>
      </div>
      <div class="chartwrap" id="chart_top_lines"></div>
      <div class="note">Line counts are approximate for binary-heavy files (decoded with replacement).</div>
    </div>
  </div>

  <div class="grid2">
    <div class="card">
      <div class="row"><h2>Extension breakdown by size</h2><span class="pill">pie</span></div>
      <div class="chartwrap" id="pie_ext_bytes"></div>
      <div class="legend" id="leg_ext_bytes"></div>
    </div>
    <div class="card">
      <div class="row"><h2>Extension breakdown by lines</h2><span class="pill">pie</span></div>
      <div class="chartwrap" id="pie_ext_lines"></div>
      <div class="legend" id="leg_ext_lines"></div>
    </div>
  </div>

  <div class="grid2">
    <div class="card">
      <div class="row"><h2>Top extensions by size</h2><span class="pill">bar</span></div>
      <div class="chartwrap" id="bar_ext_bytes"></div>
    </div>
    <div class="card">
      <div class="row"><h2>Top extensions by lines</h2><span class="pill">bar</span></div>
      <div class="chartwrap" id="bar_ext_lines"></div>
    </div>
  </div>

  <div class="card">
    <div class="row">
      <h2>Tables</h2>
      <div class="search">
        <input id="q" placeholder="Filter by filename substring (e.g. ai_controller.c)"/>
        <button class="btn" onclick="applyFilter()">Filter</button>
        <button class="btn" onclick="clearFilter()">Clear</button>
      </div>
    </div>
    <div class="tiny">Tip: Click table headers to sort. (Sorting is basic but useful.)</div>

    <div style="height:12px"></div>

    <h2 style="margin-top:0">Top files (size)</h2>
    <div id="tbl_top_size"></div>

    <div style="height:16px"></div>

    <h2 style="margin-top:0">Top files (lines)</h2>
    <div id="tbl_top_lines"></div>

    <div style="height:16px"></div>

    <h2 style="margin-top:0">Extensions</h2>
    <div id="tbl_ext"></div>
  </div>

</div>

<script>
const DATA = {{DATA_JSON}};

function fmtBytes(n){
  const u = ["B","KB","MB","GB","TB"];
  let x = n;
  let i = 0;
  while (x >= 1024 && i < u.length-1) { x /= 1024; i++; }
  return (i===0 ? String(Math.round(x)) : x.toFixed(2)) + " " + u[i];
}

function clamp(v, a, b){ return Math.max(a, Math.min(b, v)); }

function colorFor(i){
  // deterministic palette, no external libs
  const pal = ["#60a5fa","#34d399","#fbbf24","#fb7185","#a78bfa","#22d3ee","#f472b6","#c084fc","#fde047","#4ade80"];
  return pal[i % pal.length];
}

function svgBarChart(opts){
  const {title, items, valueKey, labelKey, height=320, valueFmt=(x)=>String(x)} = opts;
  const w = 860;
  const padL = 220, padR = 20, padT = 24, padB = 28;
  const h = height;
  const innerW = w - padL - padR;
  const innerH = h - padT - padB;

  const maxV = Math.max(1, ...items.map(it => it[valueKey]));
  const barH = innerH / Math.max(1, items.length);
  const gap = clamp(barH * 0.18, 2, 8);
  const actualBarH = barH - gap;

  let svg = `<svg viewBox="0 0 ${w} ${h}" width="100%" height="${h}" role="img" aria-label="${title}">
    <rect x="0" y="0" width="${w}" height="${h}" fill="transparent"/>
    <g transform="translate(${padL},${padT})">`;

  // grid + axis labels
  const ticks = 5;
  for (let i=0;i<=ticks;i++){
    const x = (innerW * i / ticks);
    svg += `<line x1="${x}" y1="0" x2="${x}" y2="${innerH}" stroke="rgba(255,255,255,0.08)" stroke-width="1"/>`;
    const v = maxV * i / ticks;
    svg += `<text x="${x}" y="${innerH+18}" fill="rgba(255,255,255,0.55)" font-size="11" text-anchor="middle">${valueFmt(v)}</text>`;
  }

  items.forEach((it, idx) => {
    const v = it[valueKey];
    const bw = innerW * (v / maxV);
    const y = idx * barH + (gap/2);
    const c = colorFor(idx);

    // bar
    svg += `<rect x="0" y="${y}" width="${bw}" height="${actualBarH}" rx="6" fill="${c}" fill-opacity="0.85">
      <title>${it[labelKey]}\n${valueKey}: ${valueFmt(v)}</title>
    </rect>`;

    // label (left)
    const label = it[labelKey];
    svg += `<text x="${-10}" y="${y + actualBarH/2 + 4}" fill="rgba(255,255,255,0.82)" font-size="12" text-anchor="end">${escapeHtml(label)}</text>`;

    // value at end
    svg += `<text x="${bw + 8}" y="${y + actualBarH/2 + 4}" fill="rgba(255,255,255,0.72)" font-size="12">${valueFmt(v)}</text>`;
  });

  svg += `</g></svg>`;
  return svg;
}

function svgPieChart(opts){
  const {title, items, valueKey, labelKey, size=320, valueFmt=(x)=>String(x), maxSlices=10} = opts;
  const w = size, h = size;
  const cx = w/2, cy = h/2;
  const r = Math.min(w,h)*0.42;
  const total = items.reduce((a,b)=>a+b[valueKey],0) || 1;

  // keep top slices, remainder as "Other"
  let top = [...items].sort((a,b)=>b[valueKey]-a[valueKey]).slice(0, maxSlices);
  const rest = items.filter(x => !top.includes(x)).reduce((a,b)=>a+b[valueKey],0);
  if (rest > 0) top.push({[labelKey]:"(other)", [valueKey]: rest});

  let ang = -Math.PI/2;
  let svg = `<svg viewBox="0 0 ${w} ${h}" width="100%" height="${h}" role="img" aria-label="${title}">
    <rect x="0" y="0" width="${w}" height="${h}" fill="transparent"/>`;

  top.forEach((it, idx) => {
    const v = it[valueKey];
    const frac = v / total;
    const sweep = frac * Math.PI * 2;
    const ang2 = ang + sweep;

    const x1 = cx + r * Math.cos(ang);
    const y1 = cy + r * Math.sin(ang);
    const x2 = cx + r * Math.cos(ang2);
    const y2 = cy + r * Math.sin(ang2);

    const large = sweep > Math.PI ? 1 : 0;
    const c = colorFor(idx);

    const d = [
      `M ${cx} ${cy}`,
      `L ${x1} ${y1}`,
      `A ${r} ${r} 0 ${large} 1 ${x2} ${y2}`,
      `Z`
    ].join(" ");

    const pct = (frac*100).toFixed(2) + "%";
    svg += `<path d="${d}" fill="${c}" fill-opacity="0.9">
      <title>${it[labelKey]}\n${valueKey}: ${valueFmt(v)}\nshare: ${pct}</title>
    </path>`;

    ang = ang2;
  });

  // donut hole
  svg += `<circle cx="${cx}" cy="${cy}" r="${r*0.55}" fill="rgba(11,15,20,0.95)" />`;
  svg += `<text x="${cx}" y="${cy-4}" fill="rgba(255,255,255,0.85)" font-size="13" text-anchor="middle">${escapeHtml(title)}</text>`;
  svg += `<text x="${cx}" y="${cy+16}" fill="rgba(255,255,255,0.65)" font-size="12" text-anchor="middle">Total: ${valueFmt(total)}</text>`;
  svg += `</svg>`;
  return {svg, top};
}

function escapeHtml(s){
  return String(s).replaceAll("&","&amp;").replaceAll("<","&lt;").replaceAll(">","&gt;");
}

function buildLegend(elId, slices, valueKey, labelKey, valueFmt){
  const el = document.getElementById(elId);
  el.innerHTML = "";
  slices.forEach((it, idx) => {
    const row = document.createElement("div");
    row.className = "legitem";
    row.innerHTML = `<span class="sw" style="background:${colorFor(idx)}"></span>
      <span class="mono">${escapeHtml(it[labelKey])}</span>
      <span style="margin-left:auto">${escapeHtml(valueFmt(it[valueKey]))}</span>`;
    el.appendChild(row);
  });
}

function renderTables(data){
  const topSize = data.top_files_by_size;
  const topLines = data.top_files_by_lines;
  const exts = data.ext_rows;

  document.getElementById("tbl_top_size").innerHTML = tableHtml(topSize, [
    ["relpath","File"],
    ["size_bytes","Size", (v)=>fmtBytes(v)],
    ["lines","Lines"],
    ["blank_lines","Blank"]
  ], "size_bytes");

  document.getElementById("tbl_top_lines").innerHTML = tableHtml(topLines, [
    ["relpath","File"],
    ["lines","Lines"],
    ["blank_lines","Blank"],
    ["size_bytes","Size", (v)=>fmtBytes(v)]
  ], "lines");

  document.getElementById("tbl_ext").innerHTML = tableHtml(exts, [
    ["ext","Ext"],
    ["files","Files"],
    ["bytes","Bytes", (v)=>fmtBytes(v)],
    ["lines","Lines"]
  ], "bytes");
}

function tableHtml(rows, cols, defaultSortKey){
  // basic sortable table
  const tid = "t_" + Math.random().toString(16).slice(2);
  const thead = cols.map(([k, name]) => `<th onclick="sortTable('${tid}','${k}')">${escapeHtml(name)}</th>`).join("");
  const body = rows.map(r => {
    const tds = cols.map(([k,_,fmt]) => {
      const v = r[k];
      const txt = fmt ? fmt(v) : String(v);
      const cls = (typeof v === "number") ? "right mono" : (k==="relpath" ? "mono" : "");
      return `<td class="${cls}" data-k="${k}" data-v="${escapeHtml(v)}">${escapeHtml(txt)}</td>`;
    }).join("");
    return `<tr>${tds}</tr>`;
  }).join("");

  // store data-sort key on table
  return `<table id="${tid}" data-sort="${defaultSortKey}" data-dir="desc">
    <thead><tr>${thead}</tr></thead>
    <tbody>${body}</tbody>
  </table>`;
}

function sortTable(tid, key){
  const tbl = document.getElementById(tid);
  const tbody = tbl.querySelector("tbody");
  const rows = Array.from(tbody.querySelectorAll("tr"));

  const curKey = tbl.getAttribute("data-sort");
  let dir = tbl.getAttribute("data-dir");
  if (curKey === key) dir = (dir === "asc") ? "desc" : "asc";
  else dir = "desc";

  rows.sort((a,b)=>{
    const av = a.querySelector(`td[data-k="${key}"]`).getAttribute("data-v");
    const bv = b.querySelector(`td[data-k="${key}"]`).getAttribute("data-v");
    const an = Number(av), bn = Number(bv);
    let cmp;
    if (!Number.isNaN(an) && !Number.isNaN(bn)) cmp = an - bn;
    else cmp = String(av).localeCompare(String(bv));
    return dir === "asc" ? cmp : -cmp;
  });

  tbody.innerHTML = "";
  rows.forEach(r => tbody.appendChild(r));

  tbl.setAttribute("data-sort", key);
  tbl.setAttribute("data-dir", dir);
}

function applyFilter(){
  const q = document.getElementById("q").value.trim().toLowerCase();
  if (!q) return;
  // Filter tables by hiding rows whose relpath does not include q
  document.querySelectorAll("table tbody tr").forEach(tr=>{
    const td = tr.querySelector('td[data-k="relpath"]');
    if (!td) return;
    const v = td.textContent.toLowerCase();
    tr.style.display = v.includes(q) ? "" : "none";
  });
}
function clearFilter(){
  document.getElementById("q").value = "";
  document.querySelectorAll("table tbody tr").forEach(tr=> tr.style.display = "");
}

function main(){
  // Charts: top files
  document.getElementById("chart_top_size").innerHTML = svgBarChart({
    title: "Top files by size",
    items: DATA.top_files_by_size,
    valueKey: "size_bytes",
    labelKey: "relpath",
    valueFmt: (v)=>fmtBytes(v),
    height: 360
  });

  document.getElementById("chart_top_lines").innerHTML = svgBarChart({
    title: "Top files by lines",
    items: DATA.top_files_by_lines,
    valueKey: "lines",
    labelKey: "relpath",
    valueFmt: (v)=>String(Math.round(v)),
    height: 360
  });

  // Pie: ext breakdown
  const pieB = svgPieChart({
    title: "Extensions • bytes",
    items: DATA.ext_rows.map(x=>({label:x.ext, value:x.bytes})),
    valueKey: "value",
    labelKey: "label",
    valueFmt: (v)=>fmtBytes(v),
    size: 340,
    maxSlices: 10
  });
  document.getElementById("pie_ext_bytes").innerHTML = pieB.svg;
  buildLegend("leg_ext_bytes", pieB.top, "value", "label", (v)=>fmtBytes(v));

  const pieL = svgPieChart({
    title: "Extensions • lines",
    items: DATA.ext_rows.map(x=>({label:x.ext, value:x.lines})),
    valueKey: "value",
    labelKey: "label",
    valueFmt: (v)=>String(Math.round(v)),
    size: 340,
    maxSlices: 10
  });
  document.getElementById("pie_ext_lines").innerHTML = pieL.svg;
  buildLegend("leg_ext_lines", pieL.top, "value", "label", (v)=>String(Math.round(v)));

  // Bars: top extensions
  const topExtBytes = [...DATA.ext_rows].sort((a,b)=>b.bytes-a.bytes).slice(0, 18).map(x=>({ext:x.ext, bytes:x.bytes}));
  document.getElementById("bar_ext_bytes").innerHTML = svgBarChart({
    title: "Top extensions by bytes",
    items: topExtBytes,
    valueKey: "bytes",
    labelKey: "ext",
    valueFmt: (v)=>fmtBytes(v),
    height: 380
  });

  const topExtLines = [...DATA.ext_rows].sort((a,b)=>b.lines-a.lines).slice(0, 18).map(x=>({ext:x.ext, lines:x.lines}));
  document.getElementById("bar_ext_lines").innerHTML = svgBarChart({
    title: "Top extensions by lines",
    items: topExtLines,
    valueKey: "lines",
    labelKey: "ext",
    valueFmt: (v)=>String(Math.round(v)),
    height: 380
  });

  renderTables(DATA);
}
main();
</script>
</body>
</html>
"""

def build_report(root: Path, stats: List[FileStat], topn: int) -> str:
    total_bytes = sum(s.size_bytes for s in stats)
    total_lines = sum(s.lines for s in stats)
    file_count = len(stats)

    top_by_size = sorted(stats, key=lambda x: x.size_bytes, reverse=True)[:topn]
    top_by_lines = sorted(stats, key=lambda x: x.lines, reverse=True)[:topn]

    # extension aggregation
    ext_bytes: Dict[str, int] = defaultdict(int)
    ext_lines: Dict[str, int] = defaultdict(int)
    ext_files: Dict[str, int] = defaultdict(int)

    for s in stats:
        ext_bytes[s.ext] += s.size_bytes
        ext_lines[s.ext] += s.lines
        ext_files[s.ext] += 1

    ext_rows = []
    for ext in sorted(ext_bytes.keys(), key=lambda e: ext_bytes[e], reverse=True):
        ext_rows.append({
            "ext": ext,
            "files": ext_files[ext],
            "bytes": ext_bytes[ext],
            "lines": ext_lines[ext],
        })

    data = {
        "top_files_by_size": [
            {"relpath": s.relpath, "size_bytes": s.size_bytes, "lines": s.lines, "blank_lines": s.blank_lines}
            for s in top_by_size
        ],
        "top_files_by_lines": [
            {"relpath": s.relpath, "size_bytes": s.size_bytes, "lines": s.lines, "blank_lines": s.blank_lines}
            for s in top_by_lines
        ],
        "ext_rows": ext_rows,
    }

    html = HTML_TEMPLATE
    html = html.replace("{{ROOT}}", str(root))
    html = html.replace("{{GENERATED}}", datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
    html = html.replace("{{FILE_COUNT}}", str(file_count))
    html = html.replace("{{TOTAL_BYTES_H}}", fmt_bytes(total_bytes))
    html = html.replace("{{TOTAL_LINES}}", str(total_lines))
    html = html.replace("{{TOPN}}", str(topn))
    html = html.replace("{{DATA_JSON}}", json.dumps(data, ensure_ascii=False))
    return html


# -----------------------------
# Main
# -----------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".", help="Repo root")
    ap.add_argument("--out", default="repo_stats.html", help="Output HTML filename")
    ap.add_argument("--top", type=int, default=30, help="Top N files for charts/tables")
    ap.add_argument("--no-lines", action="store_true", help="Skip line counting (faster)")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    if not root.exists():
        raise SystemExit(f"Root does not exist: {root}")

    relpaths = list_files_via_git(root)
    mode = "git"
    if relpaths is None:
        mode = "fallback"
        relpaths = list_files_fallback(root)

    print(f"[i] Root: {root}")
    print(f"[i] Mode: {mode} (gitignore respected best in git mode)")
    print(f"[i] Files: {len(relpaths)}")

    stats = collect_stats(root, relpaths, sample_lines=(not args.no_lines))

    html = build_report(root, stats, topn=max(5, args.top))
    outp = Path(args.out).resolve()
    outp.write_text(html, encoding="utf-8")

    print(f"[ok] Wrote: {outp}")

if __name__ == "__main__":
    main()
