#!/usr/bin/env bash
# Packs a tilgen output directory into a reproducible archive
#   --input   the output of tilgen (run.sh)
#   --output  the output archive, a SHA256 checksum is written alongside
#   --date    the timestamp of every member (default: the newest til's day)
#
# The script flattens the directories run.sh made because
# IDA does not search til/ recursively, only til/ itself and the
# processor's subdirectory (til/pc for x86/x64) in IDADIR and every IDAUSR

set -euo pipefail

usage() { sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

input= output= date=
while [ $# -gt 0 ]; do
    case "$1" in
        --input) input="${2:?}"; shift 2 ;;
        --input=*) input="${1#*=}"; shift ;;
        --output) output="${2:?}"; shift 2 ;;
        --output=*) output="${1#*=}"; shift ;;
        --date) date="${2:?}"; shift 2 ;;
        --date=*) date="${1#*=}"; shift ;;
        -h|--help) usage ;;
        *) echo "package.sh: unknown argument $1" >&2; usage ;;
    esac
done
[ -n "$input" ] && [ -n "$output" ] || usage
[ -f "$input/manifest.json" ] || { echo "no manifest.json in $input" >&2; exit 1; }
case "$output" in *.tar.xz) ;; *) echo "--output must end in .tar.xz" >&2; exit 1 ;; esac

stage="$(mktemp -d "${TMPDIR:-/tmp}/tilgen-pack.XXXXXX")"
trap 'rm -rf -- "$stage"' EXIT

# Check the manifest against the files and lay the tils out flat
python3 - "$input" "$stage" <<'EOF'
import json, shutil, sys
from pathlib import Path

PROC_DIR = {"msvc64": "pc", "msvc32": "pc", "itanium64": "pc"}

src, stage = Path(sys.argv[1]).resolve(), Path(sys.argv[2])
entries = json.loads((src / "manifest.json").read_text())
fail = []
bad = [f"{e['source']} {e['version']} {e['abi']}: {e['status']}"
       for e in entries if e["status"] != "ok"]
if bad:
    fail.append("entries not ok:\n  " + "\n  ".join(bad))

listed, out = {}, []
for e in entries:
    # `file` is relative to the repo or absolute; the til itself is found
    # under --input by its own path below <source>/
    name = Path(e["file"]).name
    hits = sorted(src.rglob(name))
    proc = PROC_DIR.get(e["abi"])
    if proc is None:
        fail.append(f"no IDA processor dir for ABI {e['abi']} (PROC_DIR)")
        continue
    if len(hits) != 1:
        fail.append(f"{name}: {len(hits)} files under {src}")
        continue
    if name in listed:
        fail.append(f"{name}: twice in the manifest (flat layout needs unique names)")
        continue
    listed[name] = hits[0]
    dest = stage / proc / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(hits[0], dest)
    out.append({**e, "file": f"{proc}/{name}"})

unlisted = sorted(str(p.relative_to(src)) for p in src.rglob("*.til")
                  if p.name not in listed)
if unlisted:
    fail.append("tils missing from the manifest:\n  " + "\n  ".join(unlisted))
if fail:
    sys.exit("package.sh: " + "\n".join(fail))

out.sort(key=lambda e: e["file"])
(stage / "rv_tils_manifest.json").write_text(json.dumps(out, indent=1) + "\n")
print(f"{len(out)} tils", file=sys.stderr)
EOF

if [ -z "$date" ]; then
    date="$(find "$stage" -name '*.til' -printf '%TY-%Tm-%Td\n' | sort | tail -1)"
fi

mkdir -p "$(dirname "$output")"
tmp="$output.$$.tmp"

(cd "$stage" && find . -type f -printf '%P\n' | LC_ALL=C sort) |
    tar -C "$stage" --sort=name --mtime="$date 00:00:00Z" --owner=0 --group=0 \
        --numeric-owner --mode='u=rw,go=r' --no-recursion -cf - -T - |
    xz -T1 --lzma2=preset=9e,dict=256MiB > "$tmp"

mv -- "$tmp" "$output"

(cd "$(dirname "$output")" && sha256sum "$(basename "$output")" > "$(basename "$output").sha256")

echo "$output: $(stat -c%s "$output") bytes, members dated $date" >&2
cat "$output.sha256"
