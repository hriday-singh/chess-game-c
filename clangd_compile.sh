#!/usr/bin/env bash
set -euo pipefail

OUT="compile_commands.json"
TMP="$(mktemp -t ccdb.XXXXXX)"

MAKE_ARGS=("$@")
if [ ${#MAKE_ARGS[@]} -eq 0 ]; then
  MAKE_ARGS=("clean" "all")
fi

make -B -n "${MAKE_ARGS[@]}" > "$TMP"

export TMP_PATH="$TMP"
export OUT_PATH="$OUT"
export GCC_PATH="C:/msys64/mingw64/bin/gcc.exe"
export GPP_PATH="C:/msys64/mingw64/bin/g++.exe"

python - <<'PY'
import json, os, shlex

tmp_path = os.environ["TMP_PATH"]
out_path = os.environ["OUT_PATH"]
cwd = os.getcwd()

GCC_PATH = os.environ["GCC_PATH"]
GPP_PATH = os.environ["GPP_PATH"]

COMPILERS = {"gcc","g++","clang","clang++","cc","c++"}
src_ext = (".c", ".cc", ".cpp", ".cxx")

def is_compile_cmd(tokens):
    if not tokens:
        return False
    exe = os.path.basename(tokens[0]).lower()
    if exe not in COMPILERS:
        return False
    if "-c" not in tokens:
        return False
    return any(t.lower().endswith(src_ext) for t in tokens)

def find_source(tokens):
    for t in tokens:
        if t.lower().endswith(src_ext):
            return t
    return None

entries = []
with open(tmp_path, "r", encoding="utf-8", errors="ignore") as f:
    for raw in f.read().splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("@"):
            line = line[1:].lstrip()

        # Parse into tokens
        try:
            tokens = shlex.split(line, posix=True)
        except ValueError:
            tokens = shlex.split(line, posix=False)

        if not is_compile_cmd(tokens):
            continue

        # Force MSYS2 compilers
        exe = os.path.basename(tokens[0]).lower()
        if exe == "gcc":
            tokens[0] = GCC_PATH
        elif exe == "g++":
            tokens[0] = GPP_PATH

        src = find_source(tokens)
        if not src:
            continue

        file_path = src
        if not os.path.isabs(file_path):
            file_path = os.path.join(cwd, file_path)
        file_path = os.path.normpath(file_path)

        entries.append({
            "directory": cwd,
            "file": file_path,
            "arguments": tokens
        })

with open(out_path, "w", encoding="utf-8") as out:
    json.dump(entries, out, indent=2)

print(f"Wrote {len(entries)} entries to {out_path}")
PY

rm -f "$TMP"
