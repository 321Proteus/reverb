"""Shared core of the til generator: ABI table, parser arguments, idalib
session (open, parse with message capture, compact, store), type counting,
anchor assertions and the manifest. Sources (formats.py, juce.py) only give
configuration; nothing here knows about a particular SDK.

idapro must be the first IDA import of the process, and pip's libclang must
never be loaded next to it, so this package imports neither anything else
from IDA first nor clang.cindex.
"""

import json
import os
import re
import time
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OUT_ROOT = REPO / "build" / "tils"


# --------------------------------------------------------------------- ABIs

@dataclass(frozen=True)
class Abi:
    triple: str
    title: str
    # Placeholders of the hand-written format headers (see
    # prepare_types_decl in src/formats/types_embed.hpp)
    subst: tuple
    bits: int = 64               # the database must be this wide (see Session)
    windows: bool = True         # Windows SDK headers (xwin) apply
    cxx: str = "c++"             # the -x language of C++ sources


# Adding an ABI is a row. Only msvc64 is exercised for now.
ABIS = {
    "msvc64": Abi("x86_64-pc-windows-msvc", "MSVC x64",
                  (("__RV_API", ""), ("__RV_THISCALL", ""),
                   ("__RV_ITANIUM_ONLY", "//"), ("__RV_MAC_ONLY", "//"))),
    "msvc32": Abi("i686-pc-windows-msvc", "MSVC x86",
                  (("__RV_API", "__stdcall"), ("__RV_THISCALL", "__thiscall"),
                   ("__RV_ITANIUM_ONLY", "//"), ("__RV_MAC_ONLY", "//")),
                  bits=32),
    # Need system headers (--sys-include) and a database of that platform;
    # the JUCE wrappers pull Cocoa headers on Apple
    "itanium64": Abi("x86_64-linux-gnu", "Itanium x64 (Linux)",
                     (("__RV_API", ""), ("__RV_THISCALL", ""),
                      ("__RV_ITANIUM_ONLY", ""), ("__RV_MAC_ONLY", "//")),
                     windows=False),
    "apple64": Abi("arm64-apple-darwin", "Apple arm64",
                   (("__RV_API", ""), ("__RV_THISCALL", ""),
                    ("__RV_ITANIUM_ONLY", ""), ("__RV_MAC_ONLY", "")),
                   windows=False, cxx="objective-c++"),
}


def substitute(text, abi):
    """The placeholder substitutions of prepare_types_decl for `abi`, except
    that a disabling placeholder ("//") removes its whole declaration up to
    the ';' instead of the rest of the line: a declaration clang-format
    wrapped onto a second line would otherwise leave that line behind (the
    runtime's prepare_types_decl has this problem; see the report).
    This is what prepare_types_decl's gate_decls does at runtime."""
    for k, v in ABIS[abi].subst:
        if v == "//":
            text = re.sub(re.escape(k) + r"[^;]*;", "", text)
        else:
            text = text.replace(k, v)
    return text


# ------------------------------------------------------------ parser config

# IDA predefines its own keywords as macros ([[clang::annotate]]), which the
# Windows SDK and MSVC STL then trip over (quirk 2). Our args come after IDA's,
# so -U wins.
UNDEF = ("__unaligned", "__ptr32", "__ptr64", "__restrict")

# The Windows headers only trip over the four above, and they need IDA's
# _MSC_VER / _WIN32 / _stdcall definitions. glibc/libstdc++ (and Apple's
# libc++) use plain identifiers IDA also defines (`__tuple`, `__off`,
# `__signed`...: `-D__tuple=struct[[clang::annotate]]`), so for the other ABIs
# every such keyword-like macro IDA predefines is removed. The list is the
# `-D__name=` set found in libida.so (IDA 9.4).
UNDEF_IDA_KEYWORDS = (
    "__array_ptr", "__at", "__bin", "__bitmask", "__char", "__cppobj",
    "__custom", "__dec", "__enum", "__export", "__far", "__fixed", "__float",
    "__gc", "__hex", "__huge", "__import", "__invbits", "__invsign",
    "__lzero", "__near", "__noreturn", "__objc_interface", "__oct", "__off",
    "__offset", "__pure", "__return_ptr", "__sbin", "__segm", "__shex",
    "__shifted", "__signed", "__soct", "__spoi", "__stroff", "__strlit",
    "__struct_ptr", "__swiftasync", "__swiftasynccall", "__swiftself",
    "__swiftthrows", "__tabform", "__tuple", "__udec", "__unused",
    "__usercall", "__userpurge")


def system_includes(abi, xwin, extra=()):
    """The platform headers for `abi`: the xwin splat for Windows ABIs,
    otherwise only what --sys-include gives."""
    x = str(xwin)
    win = ([f"{x}/crt/include", f"{x}/sdk/include/ucrt",
            f"{x}/sdk/include/um", f"{x}/sdk/include/shared"]
           if ABIS[abi].windows else [])
    return win + list(extra)


def parser_args(abi, lang, includes, defines=()):
    """The argument string for set_parser_argv. lang: 'c++17', 'c11', ...
    Only -I: IDA drops -isystem and treats its path as a second input file
    (quirk 1). -frtti is overridden by IDA anyway (quirk 3)."""
    kind = "c" if lang.startswith("c") and not lang.startswith("c++") \
        else ABIS[abi].cxx
    args = [f"-x {kind} -std={lang}", f"-target {ABIS[abi].triple}"]
    if kind != "c":
        args.append("-frtti")
    args.append("-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH")
    if kind == "c":
        # IDA's C dialect predefines _Bool as `bool`, which C does not have
        # (quirk 11); <stdbool.h> then maps bool back to the builtin _Bool
        args.append("-U_Bool")
    args += [f"-D{d}" for d in defines]
    undef = UNDEF if ABIS[abi].windows else UNDEF + UNDEF_IDA_KEYWORDS
    args += [f"-U{u}" for u in undef]
    args += [f"-I{i}" for i in includes]
    return " ".join(args)


# -------------------------------------------------------------- the session

class Session:
    """One idalib database, for any number of sequential builds."""

    def __init__(self, db, log_path):
        self.log_path = Path(log_path)
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log_path.write_text("")
        os.environ["IDALOG"] = str(self.log_path)  # before the import
        import idapro
        import ida_srclang
        import ida_typeinf
        self.idapro, self.sl, self.ti = idapro, ida_srclang, ida_typeinf
        if idapro.open_database(str(db), run_auto_analysis=False) != 0:
            raise RuntimeError(f"cannot open {db}")
        import ida_ida
        self.db_bits = 64 if ida_ida.inf_is_64bit() else 32

    def use_compiler(self, abi):
        """Windows ABIs parse as MSVC whatever compiler IDA detected in the
        database (a mingw PE is detected as GNU, which adds host include dirs)."""
        if ABIS[abi].windows:
            import ida_ida
            ida_ida.inf_set_cc_id(self.ti.COMP_MS)
            if ida_ida.inf_get_cc_id() != self.ti.COMP_MS:
                raise SystemExit("cannot select the MSVC compiler")

    def require_bits(self, abi):
        """-target only changes the builtin typedefs: IDA lays classes out
        with the database's own pointer size, so a 64-bit database cannot
        produce a 32-bit til (msvc32 on a 64-bit PE gave 8-byte vtable slots)."""
        if self.db_bits != ABIS[abi].bits:
            raise SystemExit(f"--db is a {self.db_bits}-bit database; "
                             f"{abi} needs a {ABIS[abi].bits}-bit one")

    def close(self):
        self.idapro.close_database(save=False)

    def new_til(self, name, title):
        return self.ti.new_til(name, title)

    def parse(self, til, args, probe_path):
        """-> (error count, the messages this parse appended to the log)."""
        self.sl.set_parser_argv("clang", args)
        start = self.log_path.stat().st_size
        errs = self.sl.parse_decls_with_parser("clang", til, str(probe_path),
                                               True)
        with open(self.log_path, "rb") as f:
            f.seek(start)
            text = f.read().decode(errors="replace")
        return errs, text

    def finish(self, til, path):
        """Compact and store. store_til(til, dir, name) writes elsewhere
        (quirk 6): pass the full path with no directory."""
        self.ti.compact_til(til)
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        if not self.ti.store_til(til, None, str(path)):
            raise RuntimeError(f"store_til failed for {path}")

    def count(self, til):
        """Named types; new_til has no ordinals (quirk 5)."""
        ti, n = self.ti, 0
        name = ti.first_named_type(til, ti.NTF_TYPE)
        while name:
            n += 1
            name = ti.next_named_type(til, name, ti.NTF_TYPE)
        return n

    # -- layout queries ------------------------------------------------

    def named(self, til, *names):
        """The first of `names` that exists in the til -> (name, tinfo)."""
        for n in names:
            t = self.ti.tinfo_t()
            if t.get_named_type(til, n):
                return n, t
        return None, None

    def member_offset(self, t, member, base=0, depth=0):
        """Byte offset of `member`, descending into base classes (quirk 7:
        find_udm does not). None if absent."""
        u = self.ti.udt_type_data_t()
        if depth > 16 or not t.get_udt_details(u):
            return None
        found = None
        for m in u:
            if m.name == member and not m.is_baseclass():
                return base + m.offset // 8
        for m in u:
            if m.is_baseclass():
                found = self.member_offset(m.type, member,
                                           base + m.offset // 8, depth + 1)
                if found is not None:
                    return found
        return None


# ----------------------------------------------------------------- anchors

@dataclass(frozen=True)
class Anchor:
    """A named type whose layout is asserted. `names`: alternatives (qualified
    and plain). `size`: total bytes if known. `members`: {name: offset or
    None}; a name may list alternatives as 'a|b'; None only requires that the
    member exists. Offsets are byte offsets, bases walked."""
    names: tuple
    size: object = None
    members: dict = field(default_factory=dict)


def check_anchors(sess, til, anchors):
    """-> [problem strings]; empty if every anchor holds."""
    problems = []
    for a in anchors:
        name, t = sess.named(til, *a.names)
        if t is None:
            problems.append(f"{a.names[0]}: missing")
            continue
        size = t.get_size()
        if a.size is not None and size != a.size:
            problems.append(f"{name}: size {size}, expected {a.size}")
        for spec, want in a.members.items():
            got = None
            for alt in spec.split("|"):
                got = sess.member_offset(t, alt)
                if got is not None:
                    break
            if got is None:
                problems.append(f"{name}.{spec}: missing")
            elif want is not None and got != want:
                problems.append(f"{name}.{spec}: +{got}, expected +{want}")
    return problems


# ----------------------------------------------------------------- messages

# dynamic_cast / typeid inside function bodies: IDA appends -fno-rtti
# (quirk 3). They never affect types.
_BENIGN = re.compile(r"dynamic_cast|typeid|-fno-rtti")
_DIAG = re.compile(r"^(?P<loc>.+?:\d+:\d+):? (?P<kind>error|fatal error):? "
                   r"(?P<msg>.*)$")


def real_errors(text):
    """The error lines of a parse log that are not the rtti ones."""
    out = []
    for line in text.splitlines():
        m = _DIAG.match(line.strip())
        if m and not _BENIGN.search(m["msg"]):
            out.append(line.strip())
    return out


# ----------------------------------------------------------------- building

@dataclass
class Unit:
    """One parse call: a probe text, and the language it is parsed as."""
    probe: str
    lang: str                    # first choice, e.g. 'c++17'
    fallbacks: tuple = ()        # tried in order if anchors fail
    defines: tuple = ()          # -D args


@dataclass
class Plan:
    """What a source needs built for one (version, abi): units parsed into
    one til, the include dirs, and what must hold afterwards."""
    units: list
    includes: list
    anchors: list
    workdir: Path = None         # a temp dir to remove afterwards
    notes: list = field(default_factory=list)


def build_one(sess, source, version, abi, plan, out_path, log):
    """Parse a plan's units into one til and store it. A unit's `fallbacks`
    languages are tried, for the whole plan, when the anchors do not hold.
    -> manifest entry."""
    langs = [u.lang for u in plan.units]
    attempts = [tuple(langs)]
    for i in range(max((len(u.fallbacks) for u in plan.units), default=0)):
        attempts.append(tuple(u.fallbacks[min(i, len(u.fallbacks) - 1)]
                              if u.fallbacks else u.lang for u in plan.units))
    t0 = time.time()
    for n, attempt in enumerate(attempts):
        til = sess.new_til(f"{source}_{version}_{abi}.til",
                           f"{source} {version} {ABIS[abi].title}")
        errs_total, msgs = 0, []
        for i, (u, lang) in enumerate(zip(plan.units, attempt)):
            probe = plan.workdir / f"probe{i}.h"
            probe.write_text(u.probe)
            args = parser_args(abi, lang, plan.includes, u.defines)
            errs, text = sess.parse(til, args, probe)
            errs_total += errs
            msgs.append(text)
        problems = check_anchors(sess, til, plan.anchors)
        if not problems or n == len(attempts) - 1:
            break
        log(f"  anchors failed with {'/'.join(attempt)} ({problems[0]}); "
            f"retrying with {'/'.join(attempts[n + 1])}")

    sess.finish(til, out_path)
    text = "\n".join(msgs)
    real = real_errors(text)
    entry = {
        "source": source, "version": version, "abi": abi,
        "file": str(Path(out_path).resolve().relative_to(REPO)
                    if Path(out_path).resolve().is_relative_to(REPO)
                    else Path(out_path).resolve()),
        "types": sess.count(til), "errors": errs_total,
        "errors_outside_rtti": len(real),
        "bytes": os.path.getsize(out_path),
        "std": "/".join(attempt),
        "status": "ok" if not problems else "anchor-fail",
        "problems": problems, "seconds": round(time.time() - t0, 1),
    }
    if n:
        entry["fallback"] = True
    for note in plan.notes:
        log(f"  note: {note}")
    for e in real[:3]:
        log(f"  error: {e}")
    return entry


# ----------------------------------------------------------------- manifest

def load_manifest(root=OUT_ROOT):
    path = Path(root) / "manifest.json"
    return json.loads(path.read_text()) if path.exists() else []


def save_manifest(entries, root=OUT_ROOT):
    path = Path(root) / "manifest.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    entries = sorted(entries, key=lambda e: (e["source"], e["abi"],
                                             str(e["version"])))
    path.write_text(json.dumps(entries, indent=1) + "\n")


def upsert(entries, entry):
    key = lambda e: (e["source"], e["abi"], e["version"])
    return [e for e in entries if key(e) != key(entry)] + [entry]
