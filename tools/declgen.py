import idapro
import ida_srclang as sl, ida_typeinf as ti
import glob, os

DB    = "./dummy"
OUT   = "/tmp/fmt_til"
X     = "/home/proteus/xwin/splat"
VST3  = "/home/proteus/Source/rv_sdks/vst3sdk"
CLAP  = "/home/proteus/Source/rv_sdks/clap/include"
VST2H = "/home/proteus/Desktop/Projekty/2026/reverb/src/formats/vst2/types.h"

COMMON = [
    "-target x86_64-pc-windows-msvc",
    "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH",
    "-U__unaligned -U__ptr32 -U__ptr64 -U__restrict",
    f"-I{X}/crt/include", f"-I{X}/sdk/include/ucrt",
    f"-I{X}/sdk/include/um", f"-I{X}/sdk/include/shared",
]

def count(til):
    n, name = 0, ti.first_named_type(til, ti.NTF_TYPE)
    while name:
        n += 1
        name = ti.next_named_type(til, name, ti.NTF_TYPE)
    return n

def size_of(til, *names):
    t = ti.tinfo_t()
    for n in names:
        if t.get_named_type(til, n):
            return n, t.get_size()
    return names[0], "missing"

def build(name, probe_text, lang_args, checks):
    probe = os.path.join(OUT, name + ".h")
    with open(probe, "w") as f:
        f.write(probe_text)

    sl.set_parser_argv("clang", " ".join(lang_args + COMMON))
    til = ti.new_til(name + ".til", name)
    errs = sl.parse_decls_with_parser("clang", til, probe, True)

    ti.compact_til(til)

    path = os.path.join(OUT, name + ".til")
    ti.store_til(til, None, path)

    print(f"{name}: errors {errs}, types {count(til)}, "
          f"{os.path.getsize(path) / 1e3:.0f} KB")
    for c in checks:
        print("  ", *size_of(til, *c))

    error_path = os.path.join(OUT, name + ".log")
    with open(error_path, "w") as ef:
        ef.write(str(errs))

idapro.enable_console_messages(True)

os.makedirs(OUT, exist_ok=True)
assert idapro.open_database(DB, run_auto_analysis=False) == 0

try:
    # VST3: every public interface header
    hdrs = sorted(glob.glob(f"{VST3}/pluginterfaces/base/*.h") +
                  glob.glob(f"{VST3}/pluginterfaces/vst/*.h") +
                  glob.glob(f"{VST3}/pluginterfaces/gui/*.h"))
    build("vst3_msvc64", "".join(f'#include "{h}"\n' for h in hdrs),
          ["-x c++ -std=c++17", f"-I{VST3}"],
          [("Steinberg::PFactoryInfo", "PFactoryInfo"),           # expect 452
           ("Steinberg::IPluginFactory_vtbl", "IPluginFactory_vtbl"),
           ("Steinberg::Vst::ProcessData", "ProcessData")])

    # CLAP: plain C, every header under include/clap
    hdrs = sorted(glob.glob(f"{CLAP}/clap/**/*.h", recursive=True))
    build("clap_msvc64", "".join(f'#include "{h}"\n' for h in hdrs),
          ["-x c -std=c11", f"-I{CLAP}"],
          [("clap_plugin",), ("clap_plugin_descriptor",), ("clap_host",)])

    # VST2: the hand-made header, with prepare_types_decl's substitutions
    # done for MSVC x64
    src = open(VST2H).read()
    src = (src.replace("__RV_API", "").replace("__RV_THISCALL", "")
              .replace("__RV_ITANIUM_ONLY", "//").replace("__RV_MAC_ONLY", "//"))
    build("vst2_msvc64", src, ["-x c++ -std=c++17"],
          [("AEffect",), ("VstEvents",)])
finally:
    idapro.close_database(save=False)