"""The JUCE source: one til per release tag >= 5.0.0, holding both the VST3
and the VST2 wrapper translation units (parsed separately, never #included
together). Config only; ported by hand from the earlier juce-decls tool."""

import re
import subprocess
import tarfile
import io
from pathlib import Path

from .core import ABIS, Anchor, Plan, Unit, system_includes, workdir

TAG_RE = re.compile(r"^(\d+)\.(\d+)(?:\.(\d+))?(-security)?$")
MINIMUM = (5, 0, 0)


def parse_tag(tag):
    m = TAG_RE.match(tag)
    return m and (int(m[1]), int(m[2]), int(m[3] or 0))


# The wrappers moved: 5.x/6.x keep them under VST3/ and VST/, later tags
# flatten them into juce_audio_plugin_client_VST{3,2}.cpp
WRAPPERS = {
    "vst3": ("juce_audio_plugin_client_VST3.cpp", "juce_VST3_Wrapper.cpp"),
    "vst2": ("juce_audio_plugin_client_VST2.cpp", "juce_VST_Wrapper.cpp"),
}

# Best-effort config for juce_CheckSettingMacros.h (the JucePlugin_* set a
# Projucer/CMake build generates); every tag may want a little more.
DEFINES = {
    "JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED": "1",
    "JucePlugin_Name": '"Probe"', "JucePlugin_Manufacturer": '"Probe"',
    "JucePlugin_ManufacturerWebsite": '"x"',
    "JucePlugin_ManufacturerEmail": '"x"',
    "JucePlugin_ManufacturerCode": "0x50726f62",
    "JucePlugin_PluginCode": "0x50726f62",
    "JucePlugin_VersionCode": "0x10000",
    "JucePlugin_VersionString": '"1.0.0"',
    "JucePlugin_IsSynth": "0", "JucePlugin_IsMidiEffect": "0",
    "JucePlugin_WantsMidiInput": "0", "JucePlugin_ProducesMidiOutput": "0",
    "JucePlugin_EditorRequiresKeyboardFocus": "0",
    "JucePlugin_MaxNumInputChannels": "2",
    "JucePlugin_MaxNumOutputChannels": "2",
    "JucePlugin_PreferredChannelConfigurations": "{2, 2}",
    "JucePlugin_Build_VST": "1", "JucePlugin_Build_VST3": "1",
    "JucePlugin_Build_AU": "0", "JucePlugin_Build_AUv3": "0",
    "JucePlugin_Build_RTAS": "0", "JucePlugin_Build_AAX": "0",
    "JucePlugin_Build_Standalone": "0", "JucePlugin_Build_Unity": "0",
    "JucePlugin_VSTUniqueID": "JucePlugin_PluginCode",
    "JucePlugin_VSTCategory": "kPlugCategEffect",
    "JucePlugin_Vst3Category": '"Fx"',
    "JucePlugin_VSTNumMidiInputs": "16",
    "JucePlugin_VSTNumMidiOutputs": "16",
    # 8.x+ VST3 wrapper #errors on VST2/VST3 parameter-ID mismatch; a
    # diagnostic gate, no layout
    "JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING": "1",
}


# Off outside Windows: on Linux they pull GTK/WebKit and curl headers into the
# GUI module, and the wrapper classes do not depend on them. Windows tils keep
# JUCE's defaults (what they were built and checked with).
NON_WINDOWS_DEFINES = {"JUCE_WEB_BROWSER": "0", "JUCE_USE_CURL": "0"}


def workarounds(fmt, v):
    """Per-version extra defines of a wrapper TU. The 5.4.0-6.0.x VST3
    wrapper exports GetPluginFactory with __FUNCTION__ / __FUNCDNAME__,
    which the parser does not provide at file scope."""
    out = {}
    if fmt == "vst3" and (5, 4, 0) <= v < (6, 1, 0):
        out["__FUNCTION__"] = '"GetPluginFactory"'
        out["__FUNCDNAME__"] = '"GetPluginFactory"'
    return out


def find_wrapper(tree, fmt):
    base = Path(tree) / "modules" / "juce_audio_plugin_client"
    for name in WRAPPERS[fmt]:
        hits = sorted(base.rglob(name))
        if hits:
            return hits[0]
    return None


def bundled_sdks(tree):
    """JUCE bundles the VST3 SDK and its build puts it on the include path;
    find it wherever this tag keeps it (parents of 'pluginterfaces')."""
    return sorted({str(p.parent) for p in Path(tree).rglob("pluginterfaces")
                   if p.is_dir() and "vst2.x" not in str(p)})


def vst2_sdk_dirs(tree, fallback):
    """Old tags #include <pluginterfaces/vst2.x/aeffectx.h> from Steinberg's
    VST2 SDK, which JUCE never shipped; give them a copy. Later tags carry
    their own interface (juce_VSTInterface.h), and the Steinberg one would
    clash with it, so it is added only where the tree asks for it."""
    for p in Path(tree).rglob("*"):
        if p.suffix in (".h", ".cpp", ".mm") and p.is_file() and \
                b"vst2.x/aeffect" in p.read_bytes():
            return [str(fallback)] if fallback and Path(fallback).exists() else []
    return []


def anchors_for(v, abi, only=()):
    """JucePluginFactory (VST3 wrapper) and JuceVSTWrapper (VST2 wrapper).
    Offsets differ per tag, so only presence is asserted generically; the
    pins are the layouts proven on real binaries (JUCE 8.0.x: 480 bytes,
    PFactoryInfo at +12, flags at +460)."""
    fac = {"refCount": None, "factoryInfo": None, "host": None}
    wrap = {"hostCallback": None, "processor|filter": None,
            "vstEffect": None}
    size = None
    if abi == "msvc64" and (8, 0, 2) <= v:
        size = 480
        fac["factoryInfo"] = 12
    anchors = [Anchor(("juce::JucePluginFactory", "JucePluginFactory"), size,
                      fac),
               Anchor(("juce::JuceVSTWrapper", "JuceVSTWrapper"), None, wrap)]
    fmts = only or ("vst3", "vst2")
    if not ABIS[abi].windows and v < (6, 0, 0):
        # 5.x builds its VST3 wrapper on Apple and Windows only (the file is
        # #if'ed out elsewhere): no factory to assert
        fmts = tuple(f for f in fmts if f != "vst3")
    keep = {"vst3": 0, "vst2": 1}
    return [anchors[keep[f]] for f in fmts]


# Names of what JUCE's own sources and its bundled SDKs define (the rest of a
# til is the OS and STL headers' types). JUCE's wrapper classes are in the
# global namespace, the VST2/ARA/VST3 helper SDKs have their own. The anchors
# are checked on the reduced til, so a missing root shows up as a failure.
ROOTS = ("juce::", "Juce", "Steinberg::", "VST3::", "Vst2::", "ARA::",
         "AEffect", "Vst", "fx")


class Juce:
    name = "juce"

    def __init__(self, sdks, xwin, sys_inc=(), only=()):
        self.sys_inc = sys_inc
        # parse only these wrapper TUs (e.g. ("vst2",)): for a tag whose other
        # wrapper crashes IDA's parser; the til is then labelled `<tag>-vst2-only`
        self.only = tuple(only)
        self.repo = Path(sdks) / "JUCE"
        self.vst3_sdk = Path(sdks) / "vst3sdk"
        self.vst2_sdk = Path(sdks) / "VST_SDK_2.4"
        self.xwin = xwin

    def versions(self):
        tags = subprocess.check_output(
            ["git", "-C", str(self.repo), "tag", "--list"], text=True).split()
        out = [(parse_tag(t), t) for t in tags]
        return [t for v, t in sorted((v, t) for v, t in out
                                     if v and v >= MINIMUM)]

    def label(self, tag):
        return tag + (f"-{'-'.join(self.only)}-only" if self.only else "")

    def title(self, label, abi):
        """The description IDA shows for the til."""
        what = "VST2 wrapper only" if self.only == ("vst2",) else \
            "VST3 + VST2 wrappers"
        return f"REverb JUCE {label} types, {ABIS[abi].title}: {what}"

    def out_path(self, root, version, abi):
        return root / "juce" / abi / f"rv_juce_v{version}_{abi}.til"

    def extract(self, tag, dest):
        """`git archive` of the modules only: never touch the user's clone."""
        data = subprocess.check_output(
            ["git", "-C", str(self.repo), "archive", tag, "modules"])
        with tarfile.open(fileobj=io.BytesIO(data)) as tf:
            tf.extractall(dest, filter="data")

    def plan(self, tag, abi):
        v = parse_tag(tag)
        wd = workdir(f"juce-{tag}-{abi}")
        self.extract(tag, wd)
        notes, units = [], []
        includes = [str(wd), str(wd / "modules")]
        sdks = bundled_sdks(wd)
        if not sdks:
            sdks = [str(self.vst3_sdk)]
            notes.append("no bundled VST3 SDK: using the standalone one")
        includes += sdks
        includes += vst2_sdk_dirs(wd, self.vst2_sdk)
        for fmt in self.only or ("vst3", "vst2"):
            w = find_wrapper(wd, fmt)
            if w is None:
                notes.append(f"no {fmt} wrapper found")
                continue
            d = {**DEFINES, **workarounds(fmt, v)}
            if not ABIS[abi].windows:
                d.update(NON_WINDOWS_DEFINES)
            text = "".join(f"#define {k} {val}\n" for k, val in d.items())
            if fmt == "vst2":
                # The wrapper includes Steinberg's aeffect.h inside
                # namespace Vst2, where `typedef struct AEffect AEffect`
                # would otherwise find the global forward declaration of
                # juce_ExtensionsVisitor.h and then clash with the definition
                # (the workaround JUCE documents there)
                text += "namespace Vst2 { struct AEffect; }\n"
            text += f'#include "{w}"\n'
            units.append(Unit(text, "c++17", ("c++14",)))
        return Plan(units, includes + system_includes(abi, self.xwin, self.sys_inc),
                    anchors_for(v, abi, self.only), wd, notes, ROOTS)
