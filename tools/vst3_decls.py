#!/usr/bin/env python3
"""
Generate IDA-parseable VST3 declarations from Steinberg's pluginterfaces.

Run manually (not at configure time) whenever the SDK is bumped; commit the
outputs. The plugin build only embeds the generated files.

    pip install libclang
    git clone https://github.com/steinbergmedia/vst3_pluginterfaces pluginterfaces
    python3 tools/gen_vst3_types.py --sdk . \
        --types src/VST3/types.hpp --iids src/VST3/iids.inc

--sdk is the directory that *contains* pluginterfaces/ (the SDK's own include
root). Outputs:

  types.hpp   flat C declarations: SDK typedefs, enums, data structs, and for
              every interface X a `struct X { X_vtbl *__vftable; }` plus an
              `X_vtbl` with inherited methods flattened in, which is the
              layout Hex-Rays recognizes as a C++ object. Method pointers carry
              the placeholder __RV_API, which the plugin replaces with
              "__stdcall" on 32-bit Windows and "" elsewhere (= PLUGIN_API).
  iids.inc    X-macro table RV_IID(Name, l1, l2, l3, l4) with the four
              uint32 words exactly as written in DECLARE_CLASS_IID. Byte order
              is applied at runtime (COM order on Windows, plain elsewhere).

Correctness check: the emitted C is re-parsed for each target ABI and every
struct's size and field offsets are compared against the real SDK headers.
The script fails if anything differs.
"""

import argparse
import re
import subprocess
import sys
from collections import OrderedDict
from pathlib import Path

import clang.cindex as ci
from clang.cindex import CursorKind as CK
from clang.cindex import TypeKind as TK

HERE = Path(__file__).resolve().parent
SHIMS = HERE / "vst3_shims"

# Utility headers with C++ helpers (strings, smart pointers, templates) and
# no interfaces we want.
EXCLUDE = {
    "conststringtable.h", "doc.h", "falignpop.h", "falignpush.h",
    "funknownimpl.h", "ustring.h", "vstpshpack4.h", "typesizecheck.h",
    "futils.h",
}

# ABIs we verify against. Windows uses COM_COMPATIBLE; the others don't.
TARGETS = {
    "x86_64-w64-mingw32": "win64",
    "i686-w64-mingw32": "win32",
    "x86_64-linux-gnu": "linux64",
    "arm64-apple-darwin": "mac64",
}
PRIMARY = "x86_64-w64-mingw32"

NAMESPACES = {"Steinberg"}  # and everything nested in it

BUILTIN = {
    TK.VOID: "void", TK.BOOL: "bool",
    TK.CHAR_S: "char", TK.SCHAR: "signed char", TK.CHAR_U: "char",
    TK.UCHAR: "unsigned char",
    TK.SHORT: "short", TK.USHORT: "unsigned short",
    TK.INT: "int", TK.UINT: "unsigned int",
    TK.LONGLONG: "long long", TK.ULONGLONG: "unsigned long long",
    TK.FLOAT: "float", TK.DOUBLE: "double",
    TK.CHAR16: "unsigned short",  # IDA has no portable char16_t
    TK.CHAR32: "unsigned int",
}
# `long` differs between LLP64 and LP64; refuse it rather than guess.
NONPORTABLE = {TK.LONG, TK.ULONG, TK.WCHAR, TK.LONGDOUBLE}


class GenError(Exception):
    pass


def fail(msg, cur=None):
    loc = ""
    if cur is not None and cur.location.file:
        loc = f"{cur.location.file.name}:{cur.location.line}: "
    raise GenError(loc + msg)


# ---------------------------------------------------------------- parsing

def umbrella(sdk: Path) -> str:
    root = sdk / "pluginterfaces"
    lines = []
    for sub in ("base", "gui", "vst"):
        for h in sorted((root / sub).glob("*.h")):
            if h.name not in EXCLUDE:
                lines.append(f'#include "pluginterfaces/{sub}/{h.name}"')
    return "\n".join(lines) + "\n"


def resource_dir():
    """Builtin headers (stdint.h, stddef.h) for the pip-installed libclang."""
    for exe in ("clang", "clang++"):
        try:
            return subprocess.check_output([exe, "-print-resource-dir"],
                                           text=True).strip()
        except (OSError, subprocess.CalledProcessError):
            pass
    raise GenError("need a clang binary on PATH for its builtin headers")


def parse(index, src: str, name: str, target: str, includes, lang="c++"):
    args = [
        f"--target={target}", "-nostdinc++", "-nostdlibinc",
        "-Wno-pragma-pack", "-Wno-ignored-attributes",
        f"-resource-dir={resource_dir()}",
    ] + [f"-I{i}" for i in includes]
    args += ["-x", "c++", "-std=c++17"] if lang == "c++" else \
            ["-x", "c", "-std=c11", "-fms-extensions"]
    tu = index.parse(name, args=args, unsaved_files=[(name, src)],
                     options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    errs = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    if errs:
        msg = "\n".join(f"  {d}" for d in errs[:20])
        raise GenError(f"{target}: {len(errs)} parse error(s):\n{msg}")
    return tu


def qual_path(cur):
    """Namespaces and enclosing records of a declaration, outermost first."""
    parts = []
    c = cur.semantic_parent
    while c is not None and c.kind != CK.TRANSLATION_UNIT:
        parts.append((c.kind, c.spelling))
        c = c.semantic_parent
    return list(reversed(parts))


def in_sdk(cur):
    p = qual_path(cur)
    return bool(p) and p[0][0] == CK.NAMESPACE and p[0][1] in NAMESPACES

class Model:
    """Everything reachable in the SDK, keyed by clang USR."""

    def __init__(self, tu):
        self.decls = {}  # usr -> definition cursor
        self.interfaces = OrderedDict()  # usr -> cursor
        self.names = {}  # usr -> flat C name
        self._collect(tu.cursor)
        self._name_everything()

    # A record is an interface if it has virtual methods and no data
    @staticmethod
    def is_interface(cur):
        if cur.kind not in (CK.CLASS_DECL, CK.STRUCT_DECL):
            return False
        has_virtual = has_field = False
        for c in cur.get_children():
            if c.kind == CK.CXX_METHOD and c.is_virtual_method():
                has_virtual = True
            elif c.kind == CK.CXX_BASE_SPECIFIER and \
                    Model.is_interface(c.type.get_declaration()):
                has_virtual = True  # marker interface, e.g. IVst3ToVst2Wrapper
            elif c.kind == CK.FIELD_DECL:
                has_field = True
        return has_virtual and not has_field

    def _collect(self, cur):
        for c in cur.get_children():
            if c.kind == CK.NAMESPACE:
                self._collect(c)
                continue
            if not in_sdk(c):
                continue
            if c.kind in (CK.STRUCT_DECL, CK.CLASS_DECL, CK.UNION_DECL,
                          CK.ENUM_DECL) and c.is_definition():
                self.decls[c.get_usr()] = c
                if self.is_interface(c):
                    self.interfaces[c.get_usr()] = c
                self._collect(c)  # nested types
            elif c.kind == CK.TYPEDEF_DECL:
                self.decls[c.get_usr()] = c

    def _name_everything(self):
        # Short name: enclosing records joined with '_', namespaces dropped.
        # Fall back to namespace-qualified names only on collisions.
        def short(cur):
            recs = [n for k, n in qual_path(cur) if k != CK.NAMESPACE]
            return "_".join(recs + [cur.spelling])

        def longn(cur):
            return "_".join([n for _, n in qual_path(cur)
                             if n != "Steinberg"] + [cur.spelling])

        by_short = {}
        for usr, cur in self.decls.items():
            if not cur.spelling or cur.is_anonymous():
                continue
            by_short.setdefault(short(cur), []).append(usr)
        for name, usrs in by_short.items():
            if len(usrs) == 1:
                self.names[usrs[0]] = name
            else:
                for u in usrs:
                    self.names[u] = longn(self.decls[u])
        taken = {}
        for u, n in self.names.items():
            if n in taken:
                fail(f"name collision on '{n}' even after qualifying",
                     self.decls[u])
            taken[n] = u

class Emitter:
    def __init__(self, model: Model, packs=None):
        self.m = model
        self.packs = packs or {}  # flat name -> pack(N) needed on some ABI
        self.out = []
        self.done = set()      # usrs fully emitted
        self.forwarded = {}  # record usrs with a forward decl (ordered)
        self.active = set()    # cycle guard

    # ---- type rendering: returns a C declarator for `name` of type `t`
    def decl(self, t, name="", param=False):
        t_kind = t.kind
        const = "const " if t.is_const_qualified() else ""

        if t_kind in (TK.LVALUEREFERENCE, TK.RVALUEREFERENCE):
            # same ABI as a pointer
            return self.decl(t.get_pointee(), f"*{name}")
        if t_kind == TK.POINTER:
            inner = f"*{const.strip() + ' ' if const else ''}{name}".rstrip()
            p = t.get_pointee()
            if p.kind in (TK.FUNCTIONPROTO, TK.FUNCTIONNOPROTO):
                return self.func(p, f"({inner})")
            return self.decl(p, inner)
        if t_kind in (TK.CONSTANTARRAY, TK.INCOMPLETEARRAY):
            elem = t.element_type
            if param:  # arrays decay in parameter lists
                r = self.decl(elem, f"*{name}")
                return const + r if const and not r.startswith("const ") else r
            n = t.element_count if t_kind == TK.CONSTANTARRAY else ""
            inner = f"({name})" if name.startswith("*") else name
            return self.decl(elem, f"{inner}[{n}]")
        if t_kind == TK.ELABORATED:
            r = self.decl(t.get_named_type(), name, param)
            return const + r if const and not r.startswith("const ") else r
        if t_kind == TK.TYPEDEF:
            d = t.get_declaration()
            if d.get_usr() in self.m.decls and in_sdk(d):
                self.need(d, by_value=True)
                if param and t.get_canonical().kind in (
                        TK.CONSTANTARRAY, TK.INCOMPLETEARRAY):
                    # `const TUID iid` decays to `const int8 *iid`; spell it
                    # that way so IDA never treats it as a 16-byte by-value arg
                    r = self.decl(d.underlying_typedef_type, name, param=True)
                    return const + r if const and \
                        not r.startswith("const ") else r
                return f"{const}{self.m.names[d.get_usr()]} {name}".rstrip()
            # typedef from outside the SDK (stdint): use its canonical type
            return self.decl(t.get_canonical(), name, param)
        if t_kind in (TK.RECORD, TK.ENUM):
            d = t.get_declaration()
            if d.is_anonymous():
                fail("anonymous type used outside a field", d)
            usr = d.get_usr()
            if usr not in self.m.decls:
                if t_kind == TK.RECORD and usr in self.forwarded:
                    # opaque foreign handle (wl_display etc.)
                    return f"{const}{d.spelling} {name}".rstrip()
                fail(f"type '{t.spelling}' is outside the SDK namespace", d)
            return f"{const}{self.m.names[usr]} {name}".rstrip()
        if t_kind in NONPORTABLE:
            fail(f"non-portable builtin '{t.spelling}'")
        if t_kind in BUILTIN:
            return f"{const}{BUILTIN[t_kind]} {name}".rstrip()
        if t_kind == TK.NULLPTR:
            return f"void *{name}"
        fail(f"unhandled type kind {t_kind} ('{t.spelling}')")

    def func(self, fn, inner, this=None, cc=""):
        params = []
        if this:
            params.append(f"{this} *__hidden this")
        for i, a in enumerate(fn.argument_types()):
            params.append(self.decl(a, f"a{i + 1}", param=True))
        ret = self.decl(fn.get_result())
        return f"{ret} ({cc}{inner[1:-1] if inner.startswith('(') else inner})" \
               f"({', '.join(params) or 'void'})"

    # ---- dependency handling
    def need(self, cur, by_value):
        """Make sure `cur`'s type is declared before the current item."""
        usr = cur.get_usr()
        if usr in self.done:
            return
        if cur.kind in (CK.STRUCT_DECL, CK.CLASS_DECL, CK.UNION_DECL) \
                and not by_value:
            self.forward(cur)
            return
        if usr not in self.m.decls:
            fail(f"foreign type '{cur.spelling}' used by value", cur)
        self.emit(cur)

    def forward(self, cur):
        usr = cur.get_usr()
        if usr in self.forwarded or usr in self.done:
            return
        kw = "union" if cur.kind == CK.UNION_DECL else "struct"
        n = self.m.names.get(usr, cur.spelling)
        self.out.append(f"typedef {kw} {n} {n};")
        self.forwarded[usr] = None

    def deps(self, t):
        """Walk a type and request declarations: complete for by-value
        records, forward-only behind pointers."""
        by_value = True
        while True:
            k = t.kind
            if k in (TK.POINTER, TK.LVALUEREFERENCE, TK.RVALUEREFERENCE):
                t, by_value = t.get_pointee(), False
            elif k in (TK.CONSTANTARRAY, TK.INCOMPLETEARRAY):
                t = t.element_type
            elif k == TK.ELABORATED:
                t = t.get_named_type()
            elif k in (TK.FUNCTIONPROTO, TK.FUNCTIONNOPROTO):
                self.deps(t.get_result())
                for a in t.argument_types():
                    self.deps(a)
                return
            elif k == TK.TYPEDEF:
                d = t.get_declaration()
                if d.get_usr() in self.m.decls and in_sdk(d):
                    # The typedef itself must be complete; behind a pointer
                    # its target only needs a forward declaration.
                    if by_value:
                        self.need(d, by_value=True)
                    else:
                        self.emit_typedef_behind_pointer(d)
                    return
                t = t.get_canonical()
            elif k in (TK.RECORD, TK.ENUM):
                d = t.get_declaration()
                if not d.is_anonymous():
                    self.need(d, by_value)
                return
            else:
                return

    def emit_typedef_behind_pointer(self, cur):
        under = cur.underlying_typedef_type
        base = under
        while base.kind == TK.ELABORATED:
            base = base.get_named_type()
        if base.kind == TK.RECORD:
            self.forward(base.get_declaration())
            self.emit(cur, forward_ok=True)
        else:
            self.need(cur, by_value=True)

    # ---- emission
    def emit(self, cur, forward_ok=False):
        usr = cur.get_usr()
        if usr in self.done:
            return
        if usr in self.active:
            if forward_ok or cur.kind in (CK.STRUCT_DECL, CK.CLASS_DECL,
                                          CK.UNION_DECL):
                self.forward(cur)
                return
            fail("unresolvable by-value cycle", cur)
        self.active.add(usr)
        if cur.kind == CK.TYPEDEF_DECL:
            text = self.emit_typedef(cur, forward_ok)
        elif cur.kind == CK.ENUM_DECL:
            text = self.emit_enum(cur)
        elif self.m.is_interface(cur):
            text = self.emit_interface(cur)
        else:
            text = self.emit_record(cur)
        self.active.discard(usr)
        if text is not None:
            self.out.append(text)
            self.done.add(usr)

    def emit_typedef(self, cur, forward_ok):
        under = cur.underlying_typedef_type
        if not forward_ok:
            self.deps(under)
        return f"typedef {self.decl(under, self.m.names[cur.get_usr()])};"

    def emit_enum(self, cur):
        name = self.m.names[cur.get_usr()]
        u = cur.enum_type.get_canonical()
        base = "" if u.kind in (TK.INT, TK.UINT) else f" : {BUILTIN[u.kind]}"
        vals = []
        for c in cur.get_children():
            if c.kind == CK.ENUM_CONSTANT_DECL:
                # Prefix with the enum name: SDK enums are unscoped C++ enums
                # nested in classes, so constant names collide when flattened.
                vals.append(f"\t{name}_{c.spelling} = {c.enum_value},")
        return (f"typedef enum {name}{base} {{\n" + "\n".join(vals) +
                f"\n}} {name};")

    @staticmethod
    def anon_record(cur):
        return cur.kind in (CK.STRUCT_DECL, CK.UNION_DECL) and \
            cur.is_anonymous()

    @staticmethod
    def anon_member(cur):
        """`union { ... };` with no member name (C11 anonymous member), as
        opposed to `union { ... } u;`. Older libclang lacks
        is_anonymous_record_decl(), so check whether any field uses it."""
        if not Emitter.anon_record(cur):
            return False
        parent = cur.semantic_parent
        return not any(f.kind == CK.FIELD_DECL and
                       f.type.get_declaration() == cur
                       for f in parent.get_children())

    def record_body(self, cur, indent="\t"):
        lines = []
        if self.has_vptr(cur):
            lines.append(f"{indent}void *__vftable;")
        for c in cur.get_children():
            # C++ anonymous union/struct: libclang shows the record decl and
            # hides the implicit field, so inline it where it is declared.
            if self.anon_member(c):
                kw = "union" if c.kind == CK.UNION_DECL else "struct"
                inner = self.record_body(c, indent + "\t")
                lines.append(f"{indent}{kw} {{\n{inner}\n{indent}}};")
                continue
            if c.kind != CK.FIELD_DECL:
                continue
            t = c.type
            td = t.get_declaration()
            if self.anon_record(td):
                if self.anon_member(td):
                    continue  # already inlined above
                kw = "union" if td.kind == CK.UNION_DECL else "struct"
                inner = self.record_body(td, indent + "\t")
                lines.append(f"{indent}{kw} {{\n{inner}\n{indent}}} "
                             f"{c.spelling};")
                continue
            self.deps(t)
            bits = f" : {c.get_bitfield_width()}" if c.is_bitfield() else ""
            lines.append(f"{indent}{self.decl(t, c.spelling)}{bits};")
        return "\n".join(lines)

    @staticmethod
    def has_vptr(cur):
        """Data records with virtuals (FUID's virtual dtor) carry a hidden
        vtable pointer at offset 0 on both MSVC and Itanium ABIs."""
        return any((c.kind == CK.CXX_METHOD and c.is_virtual_method()) or
                   (c.kind == CK.DESTRUCTOR and c.is_virtual_method())
                   for c in cur.get_children())

    @staticmethod
    def pack_of(cur):
        """pack(N) the header applied to this record, or None if natural."""
        rec_align = cur.type.get_align()
        natural = 1
        stack = [cur]
        while stack:
            r = stack.pop()
            for c in r.get_children():
                if Emitter.anon_record(c):
                    stack.append(c)
                    continue
                if c.kind != CK.FIELD_DECL:
                    continue
                t = c.type.get_canonical()
                while t.kind in (TK.CONSTANTARRAY, TK.INCOMPLETEARRAY):
                    t = t.element_type.get_canonical()
                td = t.get_declaration()
                if td.kind in (CK.STRUCT_DECL, CK.UNION_DECL) and \
                        td.is_anonymous():
                    stack.append(td)
                elif t.kind == TK.RECORD:
                    natural = max(natural, Emitter.natural_align(td))
                else:
                    natural = max(natural, t.get_align())
        return rec_align if rec_align < natural else None

    @staticmethod
    def natural_align(cur):
        p = Emitter.pack_of(cur)
        return cur.type.get_align() if p is None else p

    def emit_record(self, cur):
        name = self.m.names[cur.get_usr()]
        kw = "union" if cur.kind == CK.UNION_DECL else "struct"
        for b in cur.get_children():
            if b.kind == CK.CXX_BASE_SPECIFIER:
                fail("data struct with a base class is not supported", cur)
            if b.kind == CK.CXX_METHOD and b.is_virtual_method():
                fail("struct mixes data and virtual methods", cur)
        self.forward(cur)
        body = self.record_body(cur)
        text = f"{kw} {name} {{\n{body}\n}};" if body else \
            f"{kw} {name} {{\n\tchar __empty;\n}};"
        pack = self.packs.get(name, self.pack_of(cur))
        if pack:
            text = f"#pragma pack(push, {pack})\n{text}\n#pragma pack(pop)"
        return text

    def vtable(self, cur):
        """Virtual methods in vtable order, base interface first."""
        methods = []
        bases = [c for c in cur.get_children()
                 if c.kind == CK.CXX_BASE_SPECIFIER]
        if len(bases) > 1:
            fail("interface with multiple bases", cur)
        if bases:
            base = bases[0].type.get_declaration()
            base = self.m.decls.get(base.get_usr(), base)
            methods += self.vtable(base)
        own = [c for c in cur.get_children()
               if c.kind == CK.CXX_METHOD and c.is_virtual_method()]
        names = [m.spelling for m in own]
        if len(set(names)) != len(names):
            # MSVC groups overloads in reverse order; we'd need per-ABI
            # tables. None exist in pluginterfaces today.
            fail("overloaded virtual method; vtable order is ABI-specific",
                 cur)
        for m in own:
            if any(m.spelling == x.spelling for x in methods):
                continue  # override of a base slot (does not happen in SDK)
            methods.append(m)
        return methods

    def emit_interface(self, cur):
        usr = cur.get_usr()
        name = self.m.names[usr]
        self.forward(cur)
        methods = self.vtable(cur)
        lines = []
        for m in methods:
            ft = m.type
            self.deps(ft.get_result())
            for a in ft.argument_types():
                self.deps(a)
            params = [f"{name} *__hidden this"]
            for i, (a, pc) in enumerate(zip(ft.argument_types(),
                                            m.get_arguments())):
                params.append(self.decl(a, pc.spelling or f"a{i + 1}",
                                        param=True))
            ret = self.decl(ft.get_result())
            lines.append(f"\t{ret} (__RV_API *{m.spelling})"
                         f"({', '.join(params)});")
        vt = f"{name}_vtbl"
        self.out.append(f"struct {vt} {{\n" + "\n".join(lines) + "\n};")
        return f"struct {name} {{\n\tstruct {vt} *__vftable;\n}};"

    def run(self):
        # Interfaces drive the closure; named enums ride along since they're
        # useful to apply by hand even when fields are plain int32.
        for cur in self.m.interfaces.values():
            self.emit(cur)
        # Records seen only behind pointers so far were just forward-declared;
        # define them too (this can pull in more, so iterate to a fixpoint).
        while True:
            pending = [u for u in self.forwarded
                       if u in self.m.decls and u not in self.done]
            if not pending:
                break
            for u in pending:
                self.emit(self.m.decls[u])
        for usr, cur in self.m.decls.items():
            if cur.kind == CK.ENUM_DECL and usr in self.m.names:
                self.emit(cur)
        return self.out


# ---------------------------------------------------------------- extras

IID_RE = re.compile(
    r"DECLARE_CLASS_IID\s*\(\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*"
    r"(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\)")


def collect_iids(sdk: Path, model: Model):
    by_spelling = {}
    for usr, cur in model.interfaces.items():
        by_spelling.setdefault(cur.spelling, []).append(model.names[usr])
    iids = []
    for h in sorted(p for sub in ("base", "gui", "vst")
                    for p in (sdk / "pluginterfaces" / sub).glob("*.h")
                    if p.name not in EXCLUDE):
        src = re.sub(r"/\*.*?\*/|//[^\n]*", "", h.read_text(errors="replace"),
                     flags=re.S)
        for m in IID_RE.finditer(src):
            cls, *words = m.groups()
            names = by_spelling.get(cls)
            if not names or len(names) != 1:
                print(f"warning: IID for '{cls}' ({h.name}) has no unique "
                      f"interface, emitting under the C++ name",
                      file=sys.stderr)
                names = [cls]
            iids.append((names[0], [int(w, 16) for w in words]))
    if not iids:
        raise GenError("no DECLARE_CLASS_IID found - macro renamed?")
    return iids


TRESULT_RE = re.compile(r"#if COM_COMPATIBLE.*?enum\s*\{(.*?)\};", re.S)


def tresult_enums(sdk: Path):
    """kResultOk & co differ on Windows; emit both under distinct names."""
    src = (sdk / "pluginterfaces/base/funknown.h").read_text()
    m = re.search(
        r"#if COM_COMPATIBLE\s*#if SMTG_OS_WINDOWS\s*enum\s*\{(.*?)\};"
        r".*?#endif\s*#else\s*enum\s*\{(.*?)\};", src, re.S)
    if not m:
        raise GenError("tresult enum layout in funknown.h changed")

    def body(block, prefix):
        out, prev = [], -1
        for line in block.split("\n"):
            line = line.split("//")[0].strip().rstrip(",")
            if not line:
                continue
            if "=" in line:
                k, v = (s.strip() for s in line.split("=", 1))
                v = re.sub(r"static_cast<tresult>\((.*)\)", r"\1", v)
                v = v.rstrip("L") if v.startswith("0x") else v
                if re.fullmatch(r"k\w+", v):
                    out.append(f"\t{prefix}_{k} = {prefix}_{v},")
                    continue
                prev = int(v, 0)
                if prev >= 0x80000000:
                    prev -= 1 << 32
            else:
                k = line
                prev += 1
            out.append(f"\t{prefix}_{k} = {prev},")
        return "\n".join(out)

    return (f"enum tresult_com {{\n{body(m.group(1), 'COM')}\n}};\n"
            f"enum tresult_posix {{\n{body(m.group(2), 'POSIX')}\n}};")


# ---------------------------------------------------------------- verify

def field_layout(rec, base=0, prefix="", out=None, top=None):
    """name -> byte offset for every named member, anonymous members
    included. clang resolves names inside anonymous records from the
    enclosing type, so offsets are always asked of `top`."""
    out = {} if out is None else out
    top = rec if top is None else top
    for c in rec.get_children():
        if Emitter.anon_member(c):
            field_layout(c, base, prefix, out, top)
            continue
        if c.kind != CK.FIELD_DECL:
            continue
        td = c.type.get_declaration()
        if Emitter.anon_member(td):
            continue
        off = top.type.get_offset(c.spelling)
        if off < 0:
            continue
        out[prefix + c.spelling] = base + off // 8
        if Emitter.anon_record(td):  # named member of anonymous type
            field_layout(td, base + off // 8, prefix + c.spelling + ".",
                         out, td)
    return out


def records_by_name(tu_cursor, want):
    found = {}

    def walk(c):
        for x in c.get_children():
            if x.kind in (CK.STRUCT_DECL, CK.UNION_DECL, CK.CLASS_DECL) \
                    and x.is_definition() and x.spelling in want:
                found[x.spelling] = x
            if x.kind in (CK.NAMESPACE, CK.STRUCT_DECL, CK.CLASS_DECL,
                          CK.UNION_DECL):
                walk(x)
    walk(tu_cursor)
    return found


def verify(index, sdk, umb, text, target, model):
    sdk_tu = parse(index, umb, "umbrella.cpp", target, [SHIMS, sdk])
    sdk_model = Model(sdk_tu)
    cc = "__stdcall" if target.startswith("i686") and "mingw" in target \
        else ""
    prelude = (f"#define __RV_API {cc}\n#define __hidden\n"
               "typedef _Bool bool;\n")
    c_tu = parse(index, prelude + text, "gen.c", target, [], lang="c")
    data = {sdk_model.names[u]: c for u, c in sdk_model.decls.items()
            if u in sdk_model.names and c.kind in
            (CK.STRUCT_DECL, CK.CLASS_DECL, CK.UNION_DECL)
            and not sdk_model.is_interface(c)}
    gen = records_by_name(c_tu.cursor, set(data))
    problems, checked = [], 0
    for name, gcur in gen.items():
        scur = data[name]
        checked += 1
        ssz, gsz = scur.type.get_size(), gcur.type.get_size()
        if ssz != gsz:
            problems.append(f"{name}: size {gsz} != SDK {ssz}")
            continue
        sl, gl = field_layout(scur), field_layout(gcur)
        for f, o in sl.items():
            if gl.get(f) != o:
                problems.append(f"{name}.{f}: offset {gl.get(f)} != SDK {o}")
    # vtables: slot count == number of virtual methods
    vts = records_by_name(c_tu.cursor,
                          {model.names[u] + "_vtbl" for u in model.interfaces})
    ptr = 4 if target.startswith("i686") else 8
    em = Emitter(sdk_model)
    for usr, icur in sdk_model.interfaces.items():
        vname = sdk_model.names[usr] + "_vtbl"
        if vname in vts:
            n = len(em.vtable(icur))
            if vts[vname].type.get_size() != n * ptr:
                problems.append(f"{vname}: {vts[vname].type.get_size()} "
                                f"bytes for {n} slots")
    return checked, len(vts), problems


# ---------------------------------------------------------------- main

def sdk_revision(sdk: Path):
    for d in (sdk / "pluginterfaces", sdk):
        try:
            return subprocess.check_output(
                ["git", "-C", str(d), "describe", "--always", "--tags",
                 "--dirty"], stderr=subprocess.DEVNULL, text=True).strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            continue
    return "unknown"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--sdk", type=Path, required=True,
                    help="directory containing pluginterfaces/")
    ap.add_argument("--types", type=Path, required=True)
    ap.add_argument("--iids", type=Path, required=True)
    ap.add_argument("--no-verify", action="store_true")
    args = ap.parse_args()

    sdk = args.sdk.resolve()
    if not (sdk / "pluginterfaces/base/funknown.h").exists():
        sys.exit(f"{sdk}/pluginterfaces/base/funknown.h not found")

    index = ci.Index.create()
    umb = umbrella(sdk)
    try:
        tu = parse(index, umb, "umbrella.cpp", PRIMARY, [SHIMS, sdk])
        model = Model(tu)
        # Some packing is ABI-specific (ivstnoteexpression.h uses pack(4)
        # only on 32-bit Windows). Take the tightest pack any ABI uses; the
        # verify step proves the result is layout-identical on every ABI.
        packs = {}
        for target in TARGETS:
            tm = model if target == PRIMARY else Model(
                parse(index, umb, "umbrella.cpp", target, [SHIMS, sdk]))
            for usr, cur in tm.decls.items():
                if usr in tm.names and cur.kind in (
                        CK.STRUCT_DECL, CK.CLASS_DECL, CK.UNION_DECL) \
                        and not tm.is_interface(cur):
                    p = Emitter.pack_of(cur)
                    if p:
                        n = tm.names[usr]
                        packs[n] = min(p, packs.get(n, p))
        body = Emitter(model, packs).run()
        rev = sdk_revision(sdk)
        text = "\n\n".join([
            "// GENERATED by tools/gen_vst3_types.py - do not edit.\n"
            f"// Source: steinbergmedia/vst3_pluginterfaces @ {rev} (MIT)\n"
            "// __RV_API is replaced at load time: __stdcall on 32-bit "
            "Windows, empty elsewhere.",
            "\n".join(body),
            tresult_enums(sdk),
        ]) + "\n"

        if not args.no_verify:
            for target, label in TARGETS.items():
                n, v, problems = verify(index, sdk, umb, text, target, model)
                status = "OK" if not problems else f"{len(problems)} MISMATCH"
                print(f"verify {label:8} {n:3} structs, {v:3} vtables: "
                      f"{status}")
                if problems:
                    print("\n".join("  " + p for p in problems[:30]))
                    sys.exit(1)

        iids = collect_iids(sdk, model)
        iid_text = (
            "// GENERATED by tools/gen_vst3_types.py - do not edit.\n"
            f"// Source: steinbergmedia/vst3_pluginterfaces @ {rev} (MIT)\n"
            "// RV_IID(Name, l1, l2, l3, l4): words as in DECLARE_CLASS_IID.\n"
            + "".join(f"RV_IID({n}, 0x{a:08X}, 0x{b:08X}, 0x{c:08X}, "
                      f"0x{d:08X})\n" for n, (a, b, c, d) in iids))
    except GenError as e:
        sys.exit(f"error: {e}")

    args.types.parent.mkdir(parents=True, exist_ok=True)
    args.types.write_text(text)
    args.iids.write_text(iid_text)
    print(f"{len(model.interfaces)} interfaces, {len(iids)} IIDs, "
          f"{len(text) // 1024} KiB -> {args.types}, {args.iids}")


if __name__ == "__main__":
    main()