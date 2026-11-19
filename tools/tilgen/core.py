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
import shutil
import subprocess
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
    dummy_cc: tuple = ()         # compilers that can make a database for it
    compiler: str = "msvc"       # IDA compiler profile: "msvc" or "gnu"
    cxx_prelude: str = ""        # prepended to every C++ probe

    @property
    def windows(self):
        return self.compiler == "msvc"


# Adding an ABI is a row (the database must match its width: see Session).
ABIS = {
    "msvc64": Abi("x86_64-pc-windows-msvc", "MSVC x64",
                  (("__RV_API", ""), ("__RV_THISCALL", ""),
                   ("__RV_ITANIUM_ONLY", "//"), ("__RV_MAC_ONLY", "//")),
                  dummy_cc=("x86_64-w64-mingw32-gcc",)),
    "msvc32": Abi("i686-pc-windows-msvc", "MSVC x86",
                  (("__RV_API", "__stdcall"), ("__RV_THISCALL", "__thiscall"),
                   ("__RV_ITANIUM_ONLY", "//"), ("__RV_MAC_ONLY", "//")),
                  bits=32, dummy_cc=("i686-w64-mingw32-gcc",)),
    # libstdc++ headers come from the host toolchain (host_includes); IDA's
    # clang also adds the newest GCC it finds itself, so that must be the same
    # one (check_gcc; tools/tilgen/run.sh pins it)
    "itanium64": Abi("x86_64-linux-gnu", "Itanium x64 (Linux)",
                     (("__RV_API", ""), ("__RV_THISCALL", ""),
                      ("__RV_ITANIUM_ONLY", ""), ("__RV_MAC_ONLY", "//")),
                     dummy_cc=("cc", "gcc", "clang"), compiler="gnu",
                     # <string> declares the std::pmr aliases with
                     # polymorphic_allocator incomplete; IDA turns every alias
                     # into a type, which fails without this (dozens of errors)
                     cxx_prelude="#include <memory_resource>\n"),
}


def ensure_db(abi, root=OUT_ROOT.parent):
    """A database the parser can run in: a minimal executable for `abi`,
    built once into build/dummy/. Windows ABIs use mingw-w64 (its 64-bit PE is
    detected by IDA as MSVC; its 32-bit one as GNU, which breaks msvc32: see
    use_compiler)."""
    d = Path(root) / "dummy"
    exe = d / (f"dummy_{abi}" + (".exe" if ABIS[abi].windows else ""))
    if exe.exists():
        return exe
    cc = next((c for c in ABIS[abi].dummy_cc if shutil.which(c)), None)
    if cc is None:
        raise SystemExit(f"no compiler found to build a dummy for {abi} "
                         f"(tried {ABIS[abi].dummy_cc or 'none'}): pass --db")
    d.mkdir(parents=True, exist_ok=True)
    src = d / "dummy.c"
    src.write_text("int main(void) { return 0; }\n")
    subprocess.run([cc, "-o", str(exe), str(src)], check=True)
    return exe


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
# _MSC_VER / _WIN32 / _stdcall definitions. glibc/libstdc++ use plain
# identifiers IDA also defines (`__tuple`, `__off`, `__signed`...:
# `-D__tuple=struct[[clang::annotate]]`), so for the other ABIs every such
# keyword-like macro IDA predefines is removed. The list is the `-D__name=`
# set found in libida.so (IDA 9.4).
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


def host_includes(cxx):
    """The include dirs of the C++ compiler `cxx`, as it reports them."""
    r = subprocess.run([cxx, "-E", "-x", "c++", "-v", "-"], input="",
                       capture_output=True, text=True, check=True)
    dirs, on = [], False
    for line in r.stderr.splitlines():
        if line.startswith("#include <...>"):
            on = True
        elif line.startswith("End of search list"):
            break
        elif on and line.startswith(" "):
            dirs.append(os.path.normpath(line.strip()))
    return dirs


def gcc_major(cxx):
    out = subprocess.run([cxx, "-dumpversion"], capture_output=True,
                         text=True, check=True).stdout
    return int(out.split(".")[0])


def check_gcc(cxx):
    """IDA's clang adds the libstdc++ of the newest GCC under /usr/lib/gcc by
    itself, whatever the flags (-nostdinc++, --gcc-toolchain, --sysroot are
    ignored). Mixed with another GCC's headers, and with GCC 16's on its own,
    JUCE 6.1+ crashes the parser. So the newest one there must be `cxx`'s.
    -> a toolchain string for the manifest."""
    want = gcc_major(cxx)
    found = []
    for d in Path("/usr/lib/gcc").glob("*linux*/*"):    # not mingw's
        if (d / "crtbegin.o").exists():
            m = re.match(r"\d+", d.name)
            if m:
                found.append(int(m[0]))
    if found and max(found) != want:
        raise SystemExit(
            f"{cxx} is GCC {want}, but IDA's clang will also pull in GCC "
            f"{max(found)}'s headers from /usr/lib/gcc: run through "
            f"tools/tilgen/run.sh (host mode hides the others)")
    full = subprocess.run([cxx, "-dumpfullversion"], capture_output=True,
                          text=True).stdout.strip()
    return f"GCC {full or want}"


def system_includes(abi, xwin, extra=()):
    """The platform headers for `abi`: the xwin splat for Windows ABIs, then
    any --sys-include (for the others main() fills these from --cxx)."""
    if not ABIS[abi].windows:
        return list(extra)
    x = str(xwin)
    return [f"{x}/crt/include", f"{x}/sdk/include/ucrt",
            f"{x}/sdk/include/um", f"{x}/sdk/include/shared"] + list(extra)


# IDA passes only an allowlist of clang options (-x -std= -D -U -I -F -target
# -isysroot -fsyntax-only -fno-rtti ...; see cfg/idaclang.cfg) and silently
# drops the rest. These are the ones parser_args adds beyond it. Without
# -skip-function-bodies, templates instantiated in inline bodies become types
# too, and one whose name holds a lambda (libstdc++'s std::thread::_Invoker,
# std::variant's visit tables) makes IDA drop every type of the main file.
PERMITTED_OPTIONS = "-Xclang -skip-function-bodies -frtti"


# idaclang.cfg's logging switches (CLANG_LOG_<name>), for --clang-log
CLANG_LOGS = ("WARNINGS", "AST", "MACROS", "PREDEFINED", "UDTS", "FILES",
              "ARGV", "TARGET_INFO")


def parser_idausr(root=OUT_ROOT.parent, logs=()):
    """An IDA user dir holding only an idaclang.cfg that permits
    PERMITTED_OPTIONS (set_parser_option cannot change it) and turns on the
    CLANG_LOG_* switches in `logs`. It is appended to IDAUSR, so the user's
    own settings, registry and license stay in effect. One file for all
    builds: runs with different `logs` at the same time overwrite each
    other's."""
    d = Path(root) / "tilgen-idausr"
    cfg = d / "cfg" / "idaclang.cfg"
    text = f'CLANG_ARGV_PERMITTED_OPTIONS = "{PERMITTED_OPTIONS}";\n'
    text += "".join(f"CLANG_LOG_{n} = YES\n" for n in logs)
    if not cfg.exists() or cfg.read_text() != text:
        # atomically: parallel children may be reading it
        cfg.parent.mkdir(parents=True, exist_ok=True)
        tmp = cfg.with_suffix(f".{os.getpid()}.tmp")
        tmp.write_text(text)
        tmp.replace(cfg)
    return d


HOLDFREE_SRC = Path(__file__).with_name("holdfree.c")


def holdfree(root=OUT_ROOT.parent, cc="cc"):
    """holdfree.c built into build/tilgen-holdfree/ (rebuilt when the
    source is newer): the LD_PRELOAD that keeps IDA 9.4's parser from
    crashing on a container it walks while adding to it (JUCE 7.0.6+ on
    libstdc++; see the source)."""
    # per glibc: run.sh host and run.sh docker share build/
    libc = os.confstr("CS_GNU_LIBC_VERSION").replace(" ", "-")
    so = Path(root) / "tilgen-holdfree" / f"holdfree-{libc}.so"
    if so.exists() and so.stat().st_mtime >= HOLDFREE_SRC.stat().st_mtime:
        return so
    so.parent.mkdir(parents=True, exist_ok=True)
    tmp = so.with_suffix(f".{os.getpid()}.tmp")
    subprocess.run([cc, "-O2", "-shared", "-fPIC", "-o", str(tmp),
                    str(HOLDFREE_SRC), "-lpthread"], check=True)
    tmp.replace(so)
    return so


def parser_args(abi, lang, includes, defines=()):
    """The argument string for set_parser_argv. lang: 'c++17', 'c11', ...
    Only -I: IDA drops -isystem and treats its path as a second input file
    (quirk 1). Options outside IDA's allowlist need PERMITTED_OPTIONS."""
    kind = "c" if lang.startswith("c") and not lang.startswith("c++") \
        else "c++"
    args = [f"-x {kind} -std={lang}", f"-target {ABIS[abi].triple}", "-Xclang -skip-function-bodies"]
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

# IDA 9.4's MSVC compiler profile (what a database detected as MSVC has)
MSVC_HEADER_PATH = "/Program Files/Microsoft Visual Studio/VC98/include"
MSVC_MACROS = (
    "_cdecl=__cdecl;_pascal=__pascal;_huge=__huge;_near=__near;_far=__far;"
    "__inline=;_inline=;inline=;CM_WINNT;MPR50;_INTEGRAL_MAX_BITS=64;"
    "_MSC_VER=1400;_CHAR_UNSIGNED=1;_M_IX86=300;__MT__=1;__TLS__=1;"
    "_Windows=1;__WIN32__=1;_WIN32_WINNT=0x0500;WINVER=0x0500;_WIN32=1;"
    "OLEDBVER=0x0250;SECURITY_WIN32;WIN32_SUPPORT;DBNTWIN32;W32SUT_32;")

# IDA 9.4's GNU profile (what a database of an ELF executable has)
GNU_HEADER_PATH = "/usr/local/include:/usr/include"
GNU_MACROS = ("__GNUC__=4;__CHAR_BIT__=8;unix=1;__unix=1;__unix__=1;"
              "__ELF__=1;")


class Session:
    """One idalib database, for any number of sequential builds."""

    def __init__(self, db, log_path, clang_logs=()):
        self.log_path = Path(log_path)
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log_path.write_text("")
        os.environ["IDALOG"] = str(self.log_path)  # before the import
        usr = str(parser_idausr(logs=clang_logs))
        base = os.environ.get("IDAUSR") or str(Path.home() / ".idapro")
        if usr not in base.split(os.pathsep):
            os.environ["IDAUSR"] = base + os.pathsep + usr
        import idapro
        import ida_srclang
        import ida_typeinf
        self.idapro, self.sl, self.ti = idapro, ida_srclang, ida_typeinf
        if idapro.open_database(str(db), run_auto_analysis=False) != 0:
            raise RuntimeError(f"cannot open {db}")
        import ida_ida
        self.db_bits = 64 if ida_ida.inf_is_64bit() else 32
        self.check_permitted()

    def check_permitted(self):
        """A body with an error must parse cleanly, or IDA dropped
        -skip-function-bodies (parser_idausr's cfg was not loaded)."""
        til = self.ti.new_til("permitted_check.til", "check")
        self.sl.set_parser_argv("clang", "-x c++ " + PERMITTED_OPTIONS)
        errs = self.sl.parse_decls_with_parser(
            "clang", til, "struct S { void f() { undeclared_name; } };", False)
        if errs:
            raise SystemExit(
                "IDA ignores -skip-function-bodies: the idaclang.cfg in "
                f"{parser_idausr()} was not loaded (IDAUSR="
                f"{os.environ.get('IDAUSR')})")

    def use_compiler(self, abi):
        """Windows ABIs parse with IDA's MSVC profile whatever it detected in
        the database. A mingw 32-bit PE is detected as GNU, and the database
        then carries the GNU profile: host /usr/include and `__GNUC__`, which
        pull glibc in. The compiler id alone does not change that, so the
        header path and macros are set to the MSVC profile (as IDA's own for
        a database detected as MSVC) too. GNU ABIs get IDA's GNU profile
        the same way."""
        import ida_ida
        if ABIS[abi].windows:
            cc, path, macros = self.ti.COMP_MS, MSVC_HEADER_PATH, MSVC_MACROS
        else:
            cc, path, macros = self.ti.COMP_GNU, GNU_HEADER_PATH, GNU_MACROS
        ida_ida.inf_set_cc_id(cc)
        if ida_ida.inf_get_cc_id() != cc:
            raise SystemExit(f"cannot select the {ABIS[abi].compiler} "
                             "compiler")
        if self.ti.get_c_macros() != macros:
            self.ti.set_c_header_path(path)
            self.ti.set_c_macros(macros)

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

    def names(self, til):
        """Every named type, in til order (new_til has no ordinals, quirk 5)."""
        ti, out = self.ti, []
        name = ti.first_named_type(til, ti.NTF_TYPE)
        while name:
            out.append(name)
            name = ti.next_named_type(til, name, ti.NTF_TYPE)
        return out

    def count(self, til):
        return len(self.names(til))

    def reduce(self, src, roots):
        """Delete from `src`, in place, every named type that the types
        starting with one of `roots` do not (transitively) reference. A parse
        of an SDK drags in the OS and STL headers' types too (JUCE: 34,000
        types, of which 4,000 are JUCE's and Steinberg's); the closure of the
        SDK's own is what users need, and it leaves almost no names IDA's own
        libraries (mssdk) also define.

        The closure comes from copy_named_type into a scratch til, which
        copies the dependencies but needs numbered types, and a til with
        those cannot be added to a database (add_til refuses it) and cannot
        drop them again. So that til is only measured and `src` is pruned.
        -> (types kept, [roots that could not be copied])"""
        ti = self.ti
        scratch = ti.new_til("closure_scratch.til", "scratch")
        ti.enable_numbered_types(scratch, True)
        names = self.names(src)
        failed = [n for n in names if n.startswith(roots)
                  and not ti.copy_named_type(scratch, src, n)]
        keep = set(self.names(scratch))
        for n in names:
            if n not in keep:
                ti.del_named_type(src, n, ti.NTF_TYPE)
        return len(keep), failed

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

# Errors that only occur inside function bodies and never affect types:
# dynamic_cast / typeid (IDA appends -fno-rtti, quirk 3) and MS-style inline
# assembly in the Windows SDK's x86 inline functions (IDA's clang has no
# target registered for them).
_BENIGN = re.compile(r"dynamic_cast|typeid|-fno-rtti|"
                     r"MS-style inline assembly is not available")
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
    # Type-name prefixes of the SDK's own types. If given, the stored til is
    # their closure (Session.reduce) instead of everything the headers pulled
    # in; the anchors are checked on that til. () keeps the full til.
    roots: tuple = ()


def build_one(sess, source, version, abi, plan, out_path, log, title=None,
              full=False):
    """Parse a plan's units into one til and store it. A unit's `fallbacks`
    languages are tried, for the whole plan, when the anchors do not hold.
    The til's name is its file name (IDA lists a til by it); `title` is the
    description IDA shows next to it. `full` keeps the unreduced til even if
    the plan has roots. -> manifest entry."""
    reduce = bool(plan.roots) and not full
    title = title or f"{source} {version} {ABIS[abi].title}"
    langs = [u.lang for u in plan.units]
    attempts = [tuple(langs)]
    for i in range(max((len(u.fallbacks) for u in plan.units), default=0)):
        attempts.append(tuple(u.fallbacks[min(i, len(u.fallbacks) - 1)]
                              if u.fallbacks else u.lang for u in plan.units))
    t0 = time.time()
    for n, attempt in enumerate(attempts):
        til = sess.new_til(Path(out_path).name, title)
        errs_total, msgs = 0, []
        for i, (u, lang) in enumerate(zip(plan.units, attempt)):
            probe = plan.workdir / f"probe{i}.h"
            cxx = lang.startswith("c++")
            probe.write_text((ABIS[abi].cxx_prelude if cxx else "") + u.probe)
            args = parser_args(abi, lang, plan.includes, u.defines)
            errs, text = sess.parse(til, args, probe)
            errs_total += errs
            msgs.append(text)
        problems = check_anchors(sess, til, plan.anchors)
        if not problems or n == len(attempts) - 1:
            break
        log(f"  anchors failed with {'/'.join(attempt)} ({problems[0]}); "
            f"retrying with {'/'.join(attempts[n + 1])}")

    meta = {}
    if reduce:
        full_types = sess.count(til)
        _, failed = sess.reduce(til, plan.roots)
        # what ships must still satisfy the anchors (a missing root shows here)
        problems = check_anchors(sess, til, plan.anchors)
        meta = {"types_full": full_types, "roots_failed": len(failed)}
        for n in failed[:3]:
            log(f"  could not copy {n}")
    if til.flags & sess.ti.TIL_ORD:     # add_til refuses such a til
        problems = problems + ["the til has numbered types: IDA cannot add it"]
    sess.finish(til, out_path)
    text = "\n".join(msgs)
    real = real_errors(text)
    entry = {
        "source": source, "version": version, "abi": abi,
        "description": title,
        "file": str(Path(out_path).resolve().relative_to(REPO)
                    if Path(out_path).resolve().is_relative_to(REPO)
                    else Path(out_path).resolve()),
        "types": sess.count(til), "errors": errs_total,
        "errors_outside_rtti": len(real),
        "bytes": os.path.getsize(out_path),
        "std": "/".join(attempt),
        "status": "ok" if not problems else "anchor-fail",
        "problems": problems, "seconds": round(time.time() - t0, 1),
        "reduced": reduce, **meta,
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
