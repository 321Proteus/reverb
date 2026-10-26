#!/usr/bin/env python3
"""
Generate IDA-parseable CLAP declarations from the free-audio/clap headers.

Run manually (not at configure time) whenever the CLAP headers are bumped;
commit the outputs. The plugin build only embeds the generated files.

    pip install libclang
    git clone https://github.com/free-audio/clap
    python3 tools/clap_decls.py --sdk clap \
        --types src/formats/clap/types.h --exts src/formats/clap/exts.inc

--sdk is the repository root (the directory containing include/clap/).
Unlike VST3, CLAP is already plain C: every interface is a struct of function
pointers, so there is nothing to flatten. The headers are parsed as C and
re-emitted as one self-contained file. Outputs:

  types.h   SDK typedefs, enums, structs and unions in declaration order.
            stdint types are lowered to builtin types (IDA's own til may
            disagree about `long` vs `long long`), every record is forward-
            declared up front, and anonymous enums are given names so they
            can be applied. CLAP_ABI is dropped: it is __cdecl on Windows,
            which is the default calling convention of every x86 PE til.
  exts.inc  X-macro tables mapping extension / factory id strings to the
            plugin-side and host-side structs:
              RV_CLAP_EXT(Name, "clap.params", clap_plugin_params_t,
                          clap_host_params_t)
              RV_CLAP_FACTORY(Name, "clap.plugin-factory",
                              clap_plugin_factory_t)
            NONE stands for "this extension has no struct on that side".

Draft headers (ext/draft, factory/draft) are skipped unless --draft is given.

Correctness check: the emitted C is re-parsed for each target ABI and compared
with the real headers: every struct/union's size and field offsets, every
field's canonical type, every typedef, and every enum constant. The script
fails if anything differs.
"""

import os
import re
import sys
from pathlib import Path

import clang.cindex as ci
from clang.cindex import CursorKind as CK
from clang.cindex import TypeKind as TK

import declgen
from declgen import GenError, TypeRenderer, fail, field_layout, kw

PRIMARY = "x86_64-linux-gnu"

# Fixed-width typedefs from <stdint.h>, lowered by width. Canonicalising
# would give `long` for int64_t on LP64 and `long long` on LLP64.
STDINT = {
    "int8_t": "signed char", "uint8_t": "unsigned char",
    "int16_t": "short", "uint16_t": "unsigned short",
    "int32_t": "int", "uint32_t": "unsigned int",
    "int64_t": "long long", "uint64_t": "unsigned long long",
}
# Typedefs whose size follows the pointer width. Emitted verbatim, so they
# must resolve in IDA for the database's own target. Only the draft headers
# use them (size_t); that has not been checked in IDA yet.
PTRSIZED = {"size_t", "ssize_t", "intptr_t", "uintptr_t", "ptrdiff_t"}

# Typedefs that cannot be spelled portably. Replacement type, with the reason.
OVERRIDES = {
    # X11 `Window`, an `unsigned long`. It only occurs in the clap_window
    # union next to a `void *`, which is at least as big on every ABI, so a
    # pointer-sized stand-in gives the same layout.
    "clap_xwnd": ("void *", "X11 Window (unsigned long), pointer-sized"),
}

# Anonymous enums that are the value set of an existing scalar typedef
# (`typedef int32_t clap_process_status;` + `enum { CLAP_PROCESS_ERROR, ...}`).
# The pair is merged into one enum carrying the typedef's name, so every field
# and return value declared with that typedef shows symbolic constants in
# Hex-Rays. Keyed by first member.
ENUM_TYPEDEFS = {
    "CLAP_NOTE_EXPRESSION_VOLUME": "clap_note_expression",
    "CLAP_PROCESS_ERROR": "clap_process_status",
    "CLAP_LOG_DEBUG": "clap_log_severity",
    "CLAP_PARAM_IS_STEPPED": "clap_param_info_flags",
    "CLAP_PARAM_RESCAN_VALUES": "clap_param_rescan_flags",
    "CLAP_PARAM_CLEAR_ALL": "clap_param_clear_flags",
    "CLAP_RENDER_REALTIME": "clap_plugin_render_mode",
}

# Other anonymous enums whose automatic name would be poor or missing.
ENUM_NAMES = {
    "CLAP_EVENT_NOTE_ON": "clap_event_type",
    "CLAP_NAME_SIZE": "clap_string_sizes",
    "CLAP_CONTEXT_MENU_ITEM_ENTRY": "clap_context_menu_item_kind",
    "CLAP_SURROUND_FL": "clap_surround_channel",
    "CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES": "clap_voice_info_flags",
}

# Struct tags that never carry an extension id.
FACTORY_RE = re.compile(r"^CLAP_(\w+)_FACTORY_ID(_COMPAT)?$")
EXT_RE = re.compile(r"^CLAP_EXT_(\w+)$")


# ---------------------------------------------------------------- parsing

def umbrella(inc: Path, draft: bool) -> str:
    out = []
    for h in sorted((inc / "clap").rglob("*.h")):
        rel = h.relative_to(inc / "clap")
        if rel.parts[0] == "private" or rel.name in ("all.h", "clap.h"):
            continue
        if draft or "draft" not in rel.parts:
            out.append(f'#include "clap/{rel.as_posix()}"\n')
    return "".join(out)


def parse(index, src, name, target, includes):
    return declgen.parse(index, src, name, target, includes,
                         ["-x", "c", "-std=c11"])


class Model:
    """Everything the CLAP headers declare, in translation-unit order."""

    def __init__(self, tu, inc: Path):
        self.root = (inc / "clap").resolve()
        self.private = self.root / "private"
        self.items = []       # records, enums, typedefs in source order
        self.consts = []      # (name, string) for `const char X[] = "..."`
        self.records = {}     # tag -> cursor
        self.typedefs = {}    # name -> cursor
        self.enums = {}       # tag -> cursor
        self.anon_enums = []  # cursors, in order
        self.names = {}       # anon enum hash -> assigned name
        for c in tu.cursor.get_children():
            if self.in_sdk(c):
                self._collect(c)
        self._name_anon_enums()

    def in_sdk(self, cur):
        f = cur.location.file
        if f is None:
            return False
        p = Path(f.name).resolve()
        return self.root in p.parents and self.private not in p.parents

    def _collect(self, c):
        k = c.kind
        if k in (CK.STRUCT_DECL, CK.UNION_DECL):
            if not c.is_definition():
                return
            if c.is_anonymous():
                fail("top-level anonymous record", c)
            self.records[c.spelling] = c
            self.items.append(c)
        elif k == CK.ENUM_DECL:
            if not c.is_definition():
                return
            self.items.append(c)
            if c.is_anonymous():
                self.anon_enums.append(c)
            else:
                self.enums[c.spelling] = c
        elif k == CK.TYPEDEF_DECL:
            self.typedefs[c.spelling] = c
            self.items.append(c)
        elif k == CK.VAR_DECL:
            s = self._string_value(c)
            if s is not None:
                self.consts.append((c.spelling, s))

    @staticmethod
    def _string_value(var):
        """Value of `static const char X[] = "a" "b";`, else None."""
        toks = [t.spelling for t in var.get_tokens()]
        if "=" not in toks:
            return None
        lits = toks[toks.index("=") + 1:]
        lits = [t for t in lits if t != ";"]
        if not lits or not all(t.startswith('"') for t in lits):
            return None
        return "".join(t[1:-1] for t in lits)

    @staticmethod
    def enum_members(cur):
        return [(c.spelling, c.enum_value) for c in cur.get_children()
                if c.kind == CK.ENUM_CONSTANT_DECL]

    @staticmethod
    def is_flags(members):
        """Bit flags: distinct powers of two starting at 1 (so that size
        constants such as 256/1024 are not mistaken for flags)."""
        vals = [v for _, v in members]
        return len(vals) >= 2 and min(vals) == 1 and \
            all(v > 0 and v & (v - 1) == 0 for v in vals)

    def _name_anon_enums(self):
        taken = set(self.records) | set(self.typedefs) | set(self.enums)
        self.merged = {}  # typedef name -> anonymous enum cursor
        for e in self.anon_enums:
            members = self.enum_members(e)
            if not members:
                fail("empty anonymous enum", e)
            first = members[0][0]
            td = ENUM_TYPEDEFS.get(first)
            if td is not None:
                if td not in self.typedefs:
                    fail(f"ENUM_TYPEDEFS: typedef '{td}' not found", e)
                self.merged[td] = e
                self.names[e.hash] = td
                continue
            name = ENUM_NAMES.get(first)
            if name is None:
                toks = [n.lower().split("_") for n, _ in members]
                common = list(os.path.commonprefix(toks))
                if len(members) == 1:
                    common = toks[0][:-1]
                if len(common) < 2:
                    fail(f"cannot name enum starting at {first}; add it to "
                         f"ENUM_NAMES", e)
                name = "_".join(common)
                if self.is_flags(members):
                    name += "_flags"
            while name in taken:
                name += "_values"
            taken.add(name)
            self.names[e.hash] = name


class Emitter(TypeRenderer):
    def __init__(self, model: Model, draft: bool):
        self.m = model
        self.draft = draft

    # ---- type rendering hooks (see TypeRenderer.decl)
    def typedef_ref(self, t, name, const, param):
        d = t.get_declaration()
        n = d.spelling
        if n in OVERRIDES or n in self.m.typedefs and self.m.in_sdk(d):
            return f"{const}{n} {name}".rstrip()
        if n in STDINT:
            return f"{const}{STDINT[n]} {name}".rstrip()
        if n in PTRSIZED:
            if not self.draft:
                fail(f"'{n}' used outside draft headers", d)
            return f"{const}{n} {name}".rstrip()
        fail(f"typedef '{n}' from outside CLAP is not handled", d)

    def tag_ref(self, t, name, const):
        d = t.get_declaration()
        if d.is_anonymous():
            if t.kind == TK.ENUM:
                return f"{const}int {name}".rstrip()
            fail("anonymous record used outside a field", d)
        if d.spelling not in self.m.records and \
                d.spelling not in self.m.enums:
            fail(f"type '{t.spelling}' is not a CLAP type", d)
        return f"{const}{d.spelling} {name}".rstrip()

    # ---- records
    def field(self, f):
        # function pointer: take the parameter names from the declaration
        p = f.type
        while p.kind == TK.ELABORATED:
            p = p.get_named_type()
        if p.kind == TK.POINTER and \
                p.get_pointee().kind == TK.FUNCTIONPROTO:
            names = [c.spelling for c in f.get_children()
                     if c.kind == CK.PARM_DECL]
            return self.func(p.get_pointee(), f"(*{f.spelling})", names)
        return super().field(f)

    def emit_record(self, cur):
        return f"{kw(cur)} {cur.spelling} {{\n{self.record_body(cur)}\n}};"

    # ---- enums
    def emit_enum(self, cur):
        members = self.m.enum_members(cur)
        vals = [v for _, v in members]
        lo, hi = min(vals), max(vals)
        base = ""
        if lo < -(1 << 31) or hi >= (1 << 32):
            fail("enum value does not fit 32 bits", cur)
        if hi >= (1 << 31):
            base = " : unsigned int" if lo >= 0 else ""
            if not base:
                fail("enum mixes negative and >INT_MAX values", cur)
        body = "\n".join(f"\t{n} = {v}," for n, v in members)
        name = self.m.names[cur.hash] if cur.is_anonymous() else cur.spelling
        note = "// bit flags\n" if self.m.is_flags(members) else ""
        return f"{note}typedef enum {name}{base} {{\n{body}\n}} {name};"

    def emit_typedef(self, cur):
        n = cur.spelling
        if n in OVERRIDES:
            ty, why = OVERRIDES[n]
            return f"typedef {ty}{n}; // {why}"
        under = cur.underlying_typedef_type
        return f"typedef {self.decl(under, n)};"

    def run(self):
        out = []
        fwd = [f"typedef {kw(c)} {c.spelling} {c.spelling};"
               for c in self.m.records.values()]
        out.append("\n".join(fwd))
        merged = {e.hash for e in self.m.merged.values()}
        for cur in self.m.items:
            if cur.kind in (CK.STRUCT_DECL, CK.UNION_DECL):
                out.append(self.emit_record(cur))
            elif cur.kind == CK.ENUM_DECL:
                if cur.hash not in merged:  # else: emitted at its typedef
                    out.append(self.emit_enum(cur))
            elif cur.kind == CK.TYPEDEF_DECL:
                if cur.spelling in self.m.merged:
                    out.append(self.emit_enum(self.m.merged[cur.spelling]))
                else:
                    out.append(self.emit_typedef(cur))
        return out


# ---------------------------------------------------------------- extras

def ext_table(model: Model):
    """Extension and factory ids paired with their structs."""
    def struct(base):
        for cand in (f"{base}_t", base):
            if cand in model.typedefs or cand in model.records:
                return cand
        return "NONE"

    exts, facts, unmatched = [], [], []
    for name, value in model.consts:
        m = EXT_RE.match(name)
        if m:
            base = m.group(1)
            core = re.sub(r"_COMPAT$", "", base).lower()
            p, h = struct(f"clap_plugin_{core}"), struct(f"clap_host_{core}")
            exts.append((base, value, p, h))
            if p == h == "NONE":
                unmatched.append(name)
            continue
        m = FACTORY_RE.match(name)
        if m:
            core = m.group(1).lower()
            s = struct(f"clap_{core}_factory")
            facts.append((m.group(1) + (m.group(2) or ""), value, s))
            if s == "NONE":
                unmatched.append(name)
    return exts, facts, unmatched


# ---------------------------------------------------------------- verify

PRELUDE = "typedef _Bool bool;\ntypedef __SIZE_TYPE__ size_t;\n"
LP64 = [False]  # set per target by verify(): is `long` 64-bit?
ATTR_RE = re.compile(r"\s*__attribute__\(\(\w+\)\)")


def canon(t, merged_under=None):
    """Canonical type spelling. On the generated side, enums that replace a
    scalar typedef are mapped back to the scalar the real headers use."""
    s = ATTR_RE.sub("", t.get_canonical().spelling)
    # Compare integers by width: the generator spells every 64-bit integer
    # `long long`, which is `long` on LP64 targets.
    s = re.sub(r"\bunsigned long long\b", "u64", s)
    s = re.sub(r"\blong long\b", "i64", s)
    if LP64[0]:
        s = re.sub(r"\bunsigned long\b", "u64", s)
        s = re.sub(r"\blong\b", "i64", s)
    if merged_under:
        s = re.sub(r"\benum (\w+)\b",
                   lambda m: merged_under.get(m.group(1), m.group(0)), s)
    return s


def field_type(mu=None):
    """field_layout() callback: canonical field type, None to skip."""
    def typeof(f):
        if "clap_xwnd" in f.type.spelling:
            return None  # override, see OVERRIDES
        return canon(f.type, mu)
    return typeof


def enum_consts(tu_cursor, keep):
    out = {}

    def walk(c):
        for x in c.get_children():
            if x.kind == CK.ENUM_DECL and x.is_definition() and keep(x):
                for m in x.get_children():
                    if m.kind == CK.ENUM_CONSTANT_DECL:
                        out[m.spelling] = m.enum_value
            elif x.kind in (CK.STRUCT_DECL, CK.UNION_DECL):
                walk(x)
    walk(tu_cursor)
    return out


def verify(index, inc, umb, text, target, model):
    LP64[0] = "mingw" not in target
    real = parse(index, umb, "real.c", target, [inc])
    gen = parse(index, PRELUDE + text, "gen.c", target, [])
    problems = []
    # merged enums: the scalar the real headers declare for each typedef
    mu = {td: canon(c.underlying_typedef_type)
          for c in real.cursor.get_children()
          if c.kind == CK.TYPEDEF_DECL and (td := c.spelling) in model.merged}

    rrec = {c.spelling: c for c in real.cursor.get_children()
            if c.kind in (CK.STRUCT_DECL, CK.UNION_DECL)
            and c.is_definition() and model.in_sdk(c)}
    grec = {c.spelling: c for c in gen.cursor.get_children()
            if c.kind in (CK.STRUCT_DECL, CK.UNION_DECL)
            and c.is_definition()}
    if set(rrec) != set(grec):
        problems.append(f"record sets differ: missing "
                        f"{sorted(set(rrec) - set(grec))[:5]}, extra "
                        f"{sorted(set(grec) - set(rrec))[:5]}")
    for name in sorted(set(rrec) & set(grec)):
        rs, gs = rrec[name].type.get_size(), grec[name].type.get_size()
        if rs != gs:
            problems.append(f"{name}: size {gs} != {rs}")
            continue
        rl = field_layout(rrec[name], field_type())
        gl = field_layout(grec[name], field_type(mu))
        for f in sorted(set(rl) | set(gl)):
            if f not in rl or f not in gl:
                problems.append(f"{name}.{f}: only in "
                                f"{'real' if f in rl else 'generated'}")
                continue
            (ro, rt), (go, gt) = rl[f], gl[f]
            if ro != go:
                problems.append(f"{name}.{f}: offset {go} != {ro}")
            elif rt is not None and gt is not None and rt != gt:
                problems.append(f"{name}.{f}: type '{gt}' != '{rt}'")

    skip = set(OVERRIDES)
    rt = {c.spelling: canon(c.underlying_typedef_type)
          for c in real.cursor.get_children()
          if c.kind == CK.TYPEDEF_DECL and model.in_sdk(c)
          and c.spelling not in skip}
    gt = {c.spelling: canon(c.underlying_typedef_type, mu)
          for c in gen.cursor.get_children()
          if c.kind == CK.TYPEDEF_DECL and c.spelling not in skip}
    # The generated side adds forward typedefs and enum typedefs of its own;
    # every typedef the real headers declare must exist and agree.
    for n in sorted(rt):
        if n in model.merged:
            continue  # now an enum, checked through the fields that use it
        if rt[n] != gt.get(n):
            problems.append(f"typedef {n}: '{gt.get(n)}' != '{rt[n]}'")

    re_, ge = enum_consts(real.cursor, model.in_sdk), \
        enum_consts(gen.cursor, lambda _: True)
    for n in sorted(set(re_) | set(ge)):
        if re_.get(n) != ge.get(n):
            problems.append(f"enum constant {n}: {ge.get(n)} != {re_.get(n)}")
    return len(rrec), len(rt), len(re_), problems


# ---------------------------------------------------------------- main

def clap_version(inc: Path):
    src = (inc / "clap/version.h").read_text()
    v = [re.search(rf"#define CLAP_VERSION_{k}\s+(\d+)", src)
         for k in ("MAJOR", "MINOR", "REVISION")]
    if not all(v):
        raise GenError("CLAP_VERSION_* macros not found in version.h")
    return ".".join(m.group(1) for m in v)


def main():
    ap = declgen.arg_parser(__doc__, "repository root containing include/clap/",
                            "types", "exts")
    ap.add_argument("--draft", action="store_true",
                    help="also include ext/draft and factory/draft")
    args = ap.parse_args()

    sdk = args.sdk.resolve()
    inc = sdk / "include"
    if not (inc / "clap/plugin.h").exists():
        sys.exit(f"{inc}/clap/plugin.h not found")

    index = ci.Index.create()
    umb = umbrella(inc, args.draft)
    try:
        model = Model(parse(index, umb, "umbrella.c", PRIMARY, [inc]), inc)
        body = Emitter(model, args.draft).run()
        rev, ver = declgen.sdk_revision(sdk), clap_version(inc)
        source = f"free-audio/clap {ver} @ {rev} (MIT)"
        text = "\n\n".join([
            declgen.generated_header(
                "clap_decls.py",
                source + (" + draft" if args.draft else ""),
                "CLAP_ABI (__cdecl on Windows) is the default convention, "
                "so it is omitted."),
            *body,
        ]) + "\n"

        if not args.no_verify:
            def verify_one(target):
                n, td, ec, problems = verify(index, inc, umb, text, target,
                                             model)
                return (f"{n:3} records, {td:3} typedefs, "
                        f"{ec:3} enum constants", problems)
            declgen.run_verify(verify_one)

        exts, facts, unmatched = ext_table(model)
        inc_text = (
            declgen.generated_header(
                "clap_decls.py", source,
                "RV_CLAP_EXT(Name, id, plugin_struct, host_struct)",
                "RV_CLAP_FACTORY(Name, id, struct)",
                "Names ending in _COMPAT are the legacy .draft ids of the "
                "same struct.")
            + "\n" + "".join(f'RV_CLAP_FACTORY({n}, "{v}", {s})\n'
                      for n, v, s in facts)
            + "".join(f'RV_CLAP_EXT({n}, "{v}", {p}, {h})\n'
                      for n, v, p, h in exts))
    except GenError as e:
        sys.exit(f"error: {e}")

    for name in unmatched:
        print(f"warning: no struct found for {name}", file=sys.stderr)
    declgen.write((args.types, text), (args.exts, inc_text))
    print(f"{len(model.records)} records, {len(model.enums)} named + "
          f"{len(model.anon_enums)} anonymous enums, {len(exts)} extension "
          f"ids, {len(facts)} factory ids, {len(text) // 1024} KiB -> "
          f"{args.types}, {args.exts}")


if __name__ == "__main__":
    main()
