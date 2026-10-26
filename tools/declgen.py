"""
Shared plumbing for the *_decls.py generators: libclang parsing, the C type
renderer, layout extraction and the per-ABI verify loop. Each generator keeps
its own model of the SDK and decides what to emit and in which order.
"""

import argparse
import functools
import subprocess
import sys
from pathlib import Path

import clang.cindex as ci
from clang.cindex import CursorKind as CK
from clang.cindex import TypeKind as TK

# ABIs we verify against.
TARGETS = {
    "x86_64-w64-mingw32": "win64",
    "i686-w64-mingw32": "win32",
    "x86_64-linux-gnu": "linux64",
    "arm64-apple-darwin": "mac64",
}

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
# `long` is 4 bytes on LLP64 and 8 on LP64: refuse it rather than guess.
NONPORTABLE = {TK.LONG, TK.ULONG, TK.WCHAR, TK.LONGDOUBLE}


class GenError(Exception):
    pass


def fail(msg, cur=None):
    loc = ""
    if cur is not None and cur.location.file:
        loc = f"{cur.location.file.name}:{cur.location.line}: "
    raise GenError(loc + msg)


# ---------------------------------------------------------------- parsing

@functools.cache
def resource_dir():
    """Builtin headers (stdint.h, stddef.h, stdbool.h) for pip libclang."""
    for exe in ("clang", "clang++"):
        try:
            return subprocess.check_output([exe, "-print-resource-dir"],
                                           text=True).strip()
        except (OSError, subprocess.CalledProcessError):
            pass
    raise GenError("need a clang binary on PATH for its builtin headers")


def parse(index, src, name, target, includes, lang_args):
    args = [f"--target={target}", "-nostdlibinc", "-Wno-pragma-pack",
            f"-resource-dir={resource_dir()}"] + \
           [f"-I{i}" for i in includes] + list(lang_args)
    tu = index.parse(name, args=args, unsaved_files=[(name, src)],
                     options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    errs = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    if errs:
        msg = "\n".join(f"  {d}" for d in errs[:20])
        raise GenError(f"{target}: {len(errs)} parse error(s):\n{msg}")
    return tu


def kw(cur):
    return "union" if cur.kind == CK.UNION_DECL else "struct"


def anon_record(cur):
    return cur.kind in (CK.STRUCT_DECL, CK.UNION_DECL) and cur.is_anonymous()


def anon_member(cur, parent):
    """`union { ... };` with no member name (C11 anonymous member), as
    opposed to `union { ... } u;`. Older libclang lacks
    is_anonymous_record_decl(), so check whether any field uses it."""
    return anon_record(cur) and not any(
        f.kind == CK.FIELD_DECL and f.type.get_declaration() == cur
        for f in parent.get_children())


# ---------------------------------------------------------------- rendering

def with_const(const, decl):
    return const + decl if const and not decl.startswith("const ") else decl


class TypeRenderer:
    """C declarators for clang types. Subclasses decide how typedefs and
    named records/enums are spelled (and what they pull in)."""

    def typedef_ref(self, t, name, const, param):
        raise NotImplementedError

    def tag_ref(self, t, name, const):
        raise NotImplementedError

    def decl(self, t, name="", param=False):
        k = t.kind
        const = "const " if t.is_const_qualified() else ""

        if k in (TK.LVALUEREFERENCE, TK.RVALUEREFERENCE):
            # same ABI as a pointer
            return self.decl(t.get_pointee(), f"*{name}")
        if k == TK.POINTER:
            inner = f"*{const.strip() + ' ' if const else ''}{name}".rstrip()
            p = t.get_pointee()
            if p.kind in (TK.FUNCTIONPROTO, TK.FUNCTIONNOPROTO):
                return self.func(p, f"({inner})")
            return self.decl(p, inner)
        if k in (TK.CONSTANTARRAY, TK.INCOMPLETEARRAY):
            elem = t.element_type
            if param:  # arrays decay in parameter lists
                return with_const(const, self.decl(elem, f"*{name}"))
            n = t.element_count if k == TK.CONSTANTARRAY else ""
            inner = f"({name})" if name.startswith("*") else name
            return self.decl(elem, f"{inner}[{n}]")
        if k == TK.ELABORATED:
            return with_const(const, self.decl(t.get_named_type(), name,
                                               param))
        if k == TK.TYPEDEF:
            return self.typedef_ref(t, name, const, param)
        if k in (TK.RECORD, TK.ENUM):
            return self.tag_ref(t, name, const)
        if k in NONPORTABLE:
            fail(f"non-portable builtin '{t.spelling}'")
        if k in BUILTIN:
            return f"{const}{BUILTIN[k]} {name}".rstrip()
        if k == TK.NULLPTR:
            return f"void *{name}"
        fail(f"unhandled type kind {k} ('{t.spelling}')")

    def func(self, fn, inner, names=()):
        params = []
        for i, a in enumerate(fn.argument_types()):
            nm = names[i] if i < len(names) and names[i] else f"a{i + 1}"
            params.append(self.decl(a, nm, param=True))
        ret = self.decl(fn.get_result())
        return f"{ret} ({inner[1:-1] if inner.startswith('(') else inner})" \
               f"({', '.join(params) or 'void'})"

    # ---- records
    def field(self, f):
        """Declarator for a field whose type is not an anonymous record."""
        return self.decl(f.type, f.spelling)

    def record_lines(self, cur, indent):
        lines = []
        for c in cur.get_children():
            # Anonymous union/struct member: libclang shows the record decl
            # and hides the implicit field, so inline it where it is declared.
            if anon_member(c, cur):
                lines.append(self.inline_record(c, indent, ""))
            elif c.kind == CK.FIELD_DECL:
                td = c.type.get_declaration()
                if anon_record(td):
                    lines.append(self.inline_record(td, indent,
                                                    " " + c.spelling))
                else:
                    bits = f" : {c.get_bitfield_width()}" \
                        if c.is_bitfield() else ""
                    lines.append(f"{indent}{self.field(c)}{bits};")
        return lines

    def inline_record(self, cur, indent, suffix):
        return f"{indent}{kw(cur)} {{\n{self.record_body(cur, indent + chr(9))}" \
               f"\n{indent}}}{suffix};"

    def record_body(self, cur, indent="\t"):
        return "\n".join(self.record_lines(cur, indent))


# ---------------------------------------------------------------- verify

def field_layout(rec, typeof=None, base=0, out=None, top=None):
    """name -> byte offset for every named member, anonymous members
    included; with `typeof`, name -> (offset, typeof(field)). clang resolves
    names inside anonymous records from the enclosing type, so offsets are
    always asked of `top`."""
    out = {} if out is None else out
    top = rec if top is None else top
    for c in rec.get_children():
        if anon_member(c, rec):
            field_layout(c, typeof, base, out, top)
            continue
        if c.kind != CK.FIELD_DECL:
            continue
        off = top.type.get_offset(c.spelling)
        if off < 0:
            continue
        off = base + off // 8
        td = c.type.get_declaration()
        if typeof is None:
            out[c.spelling] = off
        else:
            out[c.spelling] = (off, None if anon_record(td) else typeof(c))
        if anon_record(td):  # named member of anonymous type
            for k, v in field_layout(td, typeof, off, None, td).items():
                out[f"{c.spelling}.{k}"] = v
    return out


def run_verify(verify_one):
    """verify_one(target) -> (summary, problems). Exits on any mismatch."""
    for target, label in TARGETS.items():
        summary, problems = verify_one(target)
        status = "OK" if not problems else f"{len(problems)} MISMATCH"
        print(f"verify {label:8} {summary}: {status}")
        if problems:
            print("\n".join("  " + p for p in problems[:30]))
            sys.exit(1)


# ---------------------------------------------------------------- main

def arg_parser(doc, sdk_help, *outputs):
    """--sdk, one required path per output, and --no-verify."""
    ap = argparse.ArgumentParser(description=doc,
                                 formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--sdk", type=Path, required=True, help=sdk_help)
    for o in outputs:
        ap.add_argument(f"--{o}", type=Path, required=True)
    ap.add_argument("--no-verify", action="store_true")
    return ap


def write(*outputs):
    for path, text in outputs:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)


def sdk_revision(*dirs):
    for d in dirs:
        try:
            return subprocess.check_output(
                ["git", "-C", str(d), "describe", "--always", "--tags",
                 "--dirty"], stderr=subprocess.DEVNULL, text=True).strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            continue
    return "unknown"


def generated_header(script, source, *notes):
    """Comment block opening every generated file (no trailing newline)."""
    return "\n".join([f"// GENERATED by tools/{script} - do not edit.",
                      f"// Source: {source}"] + [f"// {n}" for n in notes])
