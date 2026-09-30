import argparse
import os
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import fnmatch

TAG = "[REverb]"

BINARY_EXTS = {".vst3", ".dll", ".so", ".aaxplugin", ".clap", ".vst", ".dylib", ".x64", ".arm64"}
BUNDLE_EXTS = {".vst", ".vst3", ".component", ".aaxplugin", ".clap"}

EXCLUDE_PATTERNS = [
    # Windows API set / UCRT forwarders
    "api-ms-*.dll",
    "ext-ms-*.dll",
    "ucrtbase*.dll",
    # MSVC runtimes
    "vcruntime*.dll",
    "msvcp*.dll",
    "msvcr*.dll",
    "concrt*.dll",
    "vcomp*.dll",
    "mfc*.dll",
    # MinGW/GCC/libc++ runtimes
    "libgcc_s*",
    "libstdc++*",
    "libwinpthread*",
    "libc++*",
    # Graphics/misc runtimes commonly shipped next to plugins
    "d3dcompiler_*.dll",
    "opengl32sw.dll",
    "libegl*.dll",
    "libglesv2*.dll",
    # Qt
    "qt5*.dll",
    "qt6*.dll",
    # Common bundled libs
    "libcrypto*",
    "libssl*",
    "zlib1.dll",
    "sqlite3*.dll",
    # Uninstaller/helper leftovers
    "unins*.dll",
    # Python
    "python*.dll"
]

EXCLUDE_DIRS = {"__MACOSX", ".git", "$RECYCLE.BIN", "System Volume Information"}

# REverb does its work in ui_database_inited, before -S scripts run, so the
# default script exits right away without running the full auto-analysis
EXIT_SCRIPT = """\
import ida_pro
ida_pro.qexit(0)
"""

FULL_ANALYSIS_EXIT_SCRIPT = """\
import ida_auto, ida_pro
ida_auto.auto_wait()
ida_pro.qexit(0)
"""

def find_plugins(root: Path):

    pats = [p.lower() for p in EXCLUDE_PATTERNS]
    found = set()
    excluded = 0

    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [n for n in dirnames if n not in EXCLUDE_DIRS]
        d = Path(dirpath)

        in_bundle_macos = (
            d.name == "MacOS"
            and d.parent.name == "Contents"
            and d.parent.parent.suffix.lower() in BUNDLE_EXTS
        )

        for fn in filenames:
            p = d / fn
            is_candidate = p.suffix.lower() in BINARY_EXTS or (
                in_bundle_macos and p.suffix == ""
            )

            if not is_candidate:
                continue

            lower = fn.lower()
            if any(fnmatch.fnmatchcase(lower, pat) for pat in pats):
                excluded += 1
                continue
            found.add(p)

    return sorted(found), excluded

def run_plugin(idat, sample: Path, work: Path, exit_script: Path, timeout: int):

    with tempfile.TemporaryDirectory(dir=work) as tmp:
        
        tmp = Path(tmp)
        log_path = tmp / "ida.log"
        idb_path = tmp / (sample.name + ".i64")
        cmd = [
            str(idat),
            "-A",
            f"-L{log_path}",
            f"-S{exit_script}",
            f"-o{idb_path}",
            str(sample)
        ]

        start = time.monotonic()
        timed_out = False
        rcode = None

        try:
            proc = subprocess.run(cmd,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=timeout
            )
            rcode = proc.returncode

        except subprocess.TimeoutExpired:
            timed_out = True

        elapsed = time.monotonic() - start
 
        full_log = ""
        if log_path.exists():
            full_log = log_path.read_text(errors="replace")
 
    lines = []
    for line in full_log.splitlines():
        idx = line.find(TAG)
        if idx != -1:
            lines.append(line[idx:])

    return {
        "sample": sample,
        "lines": lines,
        "full_log": full_log,
        "elapsed": elapsed,
        "timed_out": timed_out,
        "returncode": rcode,
    }
 
 
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", type=Path, help="directory to scan")
    ap.add_argument("--idat", default="idat64", help="path to idat executable")
    ap.add_argument("-j", "--jobs", type=int, default=1, help="parallel runs")
    ap.add_argument("--timeout", type=int, default=300, help="seconds per sample")
    ap.add_argument(
        "--work",
        type=Path,
        default=Path(tempfile.gettempdir()) / "rv_work",
        help="scratch directory for databases and logs",
    )
    ap.add_argument(
        "--keep-logs",
        type=Path,
        default=None,
        help="also save each sample's full IDA log into this directory",
    )
    ap.add_argument(
        "--full-analysis",
        action="store_true",
        help="let auto-analysis finish before exiting (slow; for inspecting IDBs)",
    )
    
    args = ap.parse_args()
 
    if not args.root.is_dir():
        sys.exit(f"Not a directory: {args.root}")
 
    args.work.mkdir(parents=True, exist_ok=True)
    exit_script = args.work / "exit_after_analysis.py"
    exit_script.write_text(FULL_ANALYSIS_EXIT_SCRIPT if args.full_analysis else EXIT_SCRIPT)

    if args.keep_logs:
        args.keep_logs.mkdir(parents=True, exist_ok=True)
 
    samples, excluded = find_plugins(args.root)
    if not samples:
        sys.exit("No plugin binaries found.")

    print(f"Found {len(samples)} candidate binaries under {args.root}, skipped {excluded} excluded binaries\n")
 
    def task(s):
        print(f"Running test for {s.name}")
        return run_plugin(args.idat, s, args.work, exit_script, args.timeout)
 
    no_output = 0
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:

        for r in pool.map(task, samples):
            
            status = []
            if r["timed_out"]:
                status.append("TIMEOUT")

            elif r["returncode"] not in (0, None):
                status.append(f"exit {r['returncode']}")

            status.append(f"{r['elapsed']:.1f}s")

            try:
                shown = r["sample"].relative_to(args.root)

            except ValueError:
                shown = r["sample"]

            print(f"=== {shown}  ({', '.join(status)})")

            if r["lines"]:
                for line in r["lines"]:
                    print("  " + line)
            else:
                no_output += 1
                print("  (no REverb output)")

            print()
 
            if args.keep_logs:
                safe = str(shown).replace(os.sep, "__").replace(":", "_")
                (args.keep_logs / (safe + ".log")).write_text(
                    r["full_log"], errors="replace"
                )
 
    print(f"Done: {len(samples)} scanned, {no_output} with no REverb output")

if __name__ == "__main__":
    main()