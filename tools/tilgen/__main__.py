"""python -m tilgen [--db PE] [--source formats|juce|all] [--refs A,B]
[--abi msvc64] [--force] [-j N]. Each til is built by its own child process,
on its own copy of the database (idalib locks the file); -j runs N at once.
Without --db a dummy executable is built into build/dummy/."""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from . import core
from .formats import Formats
from .juce import Juce


def main():
    ap = argparse.ArgumentParser(prog="tilgen")

    ap.add_argument(
        "--db",
        help="Any small binary of the target ABI (default: a dummy built "
        "into build/dummy/ with a compiler found for the ABI)"
    )

    ap.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=1,
        help="tils built at the same time"
    )

    ap.add_argument("--entry-out", help=argparse.SUPPRESS)
    ap.add_argument("--log", help=argparse.SUPPRESS)

    ap.add_argument(
        "--source",
        default="all",
        choices=("formats", "juce", "all")
    )

    ap.add_argument(
        "--refs",
        help="Comma list of versions/tags (juce)"
    )

    ap.add_argument(
        "--abi",
        default="msvc64",
        choices=sorted(core.ABIS)
    )

    ap.add_argument(
        "--force",
        action="store_true",
        help="Rebuild existing"
    )

    ap.add_argument(
        "--inprocess",
        action="store_true",
        help="build in this process (default: one child process "
        "per til, so a parser crash costs one til, not the run)"
    )

    ap.add_argument(
        "--sdks",
        help="path to the directory containing the selected SDKs",
        default=str(Path.home() / "Source/rv_sdks")
    )


    ap.add_argument(
        "--only",
        help="juce: parse only these wrapper TUs (vst3, vst2; comma list); "
        "the til is named <tag>-vst2-only.til"
    )

    ap.add_argument(
        "--vst3",
        help="a VST3 SDK root holding pluginterfaces/ "
        "(default: <sdks>/vst3sdk)"
    )

    ap.add_argument(
        "--sys-include",
        action="append",
        default=[],
        help="extra system include dir (-I), repeatable"
    )

    ap.add_argument(
        "--xwin",
        default=str(Path.home() / "xwin/splat")
    )

    ap.add_argument(
        "--out",
        default=str(core.OUT_ROOT)
    )

    a = ap.parse_args()
    out = Path(a.out)
    sources = []

    if a.source in ("formats", "all"):
        sources.append(Formats(a.sdks, a.xwin, a.vst3, a.sys_include))
    if a.source in ("juce", "all"):
        sources.append(Juce(a.sdks, a.xwin, a.sys_include,
                          a.only.split(",") if a.only else ()))

    log = lambda s: print(s, flush=True)
    if not a.db:
        a.db = str(core.ensure_db(a.abi))
    if not a.inprocess:
        sys.exit(run_children(a, sources, out, log))

    sess = core.Session(a.db, a.log or out / "tilgen.log")
    sess.require_bits(a.abi)
    sess.use_compiler(a.abi)

    entries = core.load_manifest(out)
    bad = 0

    try:
        for src in sources:
            for ver in pick_versions(a, src):

                lab = src.label(ver)
                path = src.out_path(out, lab, a.abi)
                log(f"[{src.name} {lab} {a.abi}]")
                plan = src.plan(ver, a.abi)

                try:
                    e = core.build_one(sess, src.name, lab, a.abi, plan, path, log)
                finally:
                    shutil.rmtree(plan.workdir, ignore_errors=True)

                log(f"  {e['status']}: {e['types']} types, {e['errors']} errors "
                    f"({e['errors_outside_rtti']} not rtti), "
                    f"{e['bytes'] // 1024} KB, {e['std']}, {e['seconds']}s")

                for p in e["problems"]:
                    log(f"  PROBLEM {p}")

                bad += e["status"] != "ok"
                if a.entry_out:     # the parent merges them into the manifest
                    Path(a.entry_out).write_text(json.dumps(e))
                else:
                    entries = core.upsert(entries, e)
                    core.save_manifest(entries, out)

    finally:
        sess.close()

    sys.exit(1 if bad else 0)


def pick_versions(a, src):
    versions = src.versions()

    if a.refs and src.name == "juce":
        want = a.refs.split(",")
        missing = set(want) - set(versions)

        if missing:
            sys.exit(f"unknown refs: {sorted(missing)}")

        versions = [v for v in versions if v in want]

    return versions


def run_children(a, sources, out, log):
    """One `--inprocess` child per til, on its own copy of the database, up to
    -j at once. IDA's clang crashes on some JUCE tags; the parent notes the
    crash in the manifest and goes on. Only the parent writes the manifest."""
    lock = threading.Lock()
    state = {"entries": core.load_manifest(out), "bad": 0}
    jobs = []

    for src in sources:
        for ver in pick_versions(a, src):
            lab = src.label(ver)
            path = src.out_path(out, lab, a.abi)
            if path.exists() and not a.force:
                log(f"[{src.name} {lab}] exists, skipped")
                continue
            jobs.append((src, ver, lab))

    def run(job):
        src, ver, lab = job
        tmp = Path(tempfile.mkdtemp(prefix="tilgen_db_"))
        try:
            db = tmp / Path(a.db).name
            shutil.copy2(a.db, db)
            entry = tmp / "entry.json"
            logf = out / "logs" / f"{src.name}-{lab}-{a.abi}.log"
            logf.parent.mkdir(parents=True, exist_ok=True)

            cmd = [sys.executable, "-m", "tilgen", "--db", str(db),
                   "--source", src.name, "--refs", ver, "--abi", a.abi,
                   "--sdks", a.sdks, "--xwin", a.xwin, "--out", str(out),
                   "--inprocess", "--entry-out", str(entry),
                   "--log", str(logf)]
            if a.vst3:
                cmd += ["--vst3", a.vst3]
            if a.only:
                cmd += ["--only", a.only]
            for d in a.sys_include:
                cmd += ["--sys-include", d]

            p = subprocess.run(cmd, cwd=Path(__file__).resolve().parents[1],
                               capture_output=True, text=True)
            rc = p.returncode
            lines = p.stdout.splitlines()
            if rc not in (0, 1):    # 1: ran, anchors failed (in the output)
                what = "crash" if rc < 0 else "error"
                lines.append(f"  {what.upper()}: child exited {rc}"
                             f"{' (signal %d)' % -rc if rc < 0 else ''}")
                lines += p.stderr.splitlines()[-6:]
                e = {"source": src.name, "version": lab, "abi": a.abi,
                     "status": what, "returncode": rc, "types": 0,
                     "errors": 0, "bytes": 0}
            else:
                e = json.loads(entry.read_text()) if entry.exists() else None
            with lock:
                log("\n".join(lines))
                if e:
                    state["entries"] = core.upsert(state["entries"], e)
                    core.save_manifest(state["entries"], out)
                state["bad"] += rc != 0
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        list(ex.map(run, jobs))

    return 1 if state["bad"] else 0


try:
    main()
except SystemExit:
    raise
except BaseException:     # an exception must not look like "anchors failed"
    import traceback
    traceback.print_exc()
    sys.exit(3)
