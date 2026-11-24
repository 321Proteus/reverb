"""Format sources: VST3, CLAP, VST2 and LV2, merged into one til per ABI
(build/tils/formats/<abi>.til; their names do not overlap: Steinberg::...,
clap_..., LV2_..., VST2's own flat ones). Config only; the core does the
work."""

import glob
import re
from pathlib import Path

from .core import (ABIS, REPO, Anchor, Plan, Unit, substitute,
                   system_includes, workdir)

VST2_HEADER = REPO / "src" / "formats" / "vst2" / "types.h"

# Fragment headers that only push/pop packing or are otherwise not
# self-contained; included on their own they would leak state into the next
# header (falignpush.h / vstpshpack4.h have no matching pop)
_FRAGMENTS = {"falignpush.h", "falignpop.h", "vstpshpack4.h", "vstpoppack.h"}
VST2_PRELUDE = ("#include <stdint.h>\n#include <stdbool.h>\n"
                "#define __hidden\n")
_DIRS = ("pluginterfaces/base", "pluginterfaces/vst", "pluginterfaces/gui")


def vst3_headers(sdk):
    out = []
    for d in _DIRS:
        headers = sorted(glob.glob(f"{sdk}/{d}/*.h"))
        out += [h for h in headers if Path(h).name not in _FRAGMENTS]
    return out


def clap_headers(clap_include):
    return sorted(glob.glob(f"{clap_include}/clap/**/*.h", recursive=True))


def lv2_headers(lv2_include):
    return sorted(glob.glob(f"{lv2_include}/lv2/**/*.h", recursive=True))


def vst3_version(sdk):
    """The VST3 SDK's own version string (kVstVersionString), e.g. 3.8.1."""
    f = Path(sdk) / "pluginterfaces" / "vst" / "vsttypes.h"
    m = re.search(r'kVstVersionString\s*"VST ([\d.]+)"',
                  f.read_text(errors="replace")) if f.exists() else None
    return m[1] if m else "unknown"


def clap_version(clap):
    v = (Path(clap) / "clap" / "version.h").read_text()
    return ".".join(re.search(rf"CLAP_VERSION_{k} (\d+)", v)[1]
                    for k in ("MAJOR", "MINOR", "REVISION"))


def lv2_version(lv2_include):
    """The LV2 release of the tree holding include/ (meson.build's project
    version), e.g. 1.18.11."""
    f = Path(lv2_include).parent / "meson.build"
    m = re.search(r"\bversion:\s*'([\d.]+)'",
                  f.read_text(errors="replace")) if f.exists() else None
    return m[1] if m else "unknown"


class Formats:
    name = "formats"

    def __init__(self, sdks, xwin, vst3=None, sys_inc=(), lv2=None):
        self.sys_inc = sys_inc
        self.vst3 = Path(vst3) if vst3 else Path(sdks) / "vst3sdk"
        self.clap = Path(sdks) / "clap" / "include"
        self.lv2 = Path(lv2) if lv2 else Path(sdks) / "lv2" / "include"
        self.xwin = xwin

    def versions(self):
        return [f"vst3-{vst3_version(self.vst3)}+clap-{clap_version(self.clap)}"
                f"+lv2-{lv2_version(self.lv2)}"]

    def title(self, version, abi):
        """The description IDA shows for the til."""
        return (f"REverb format types, {ABIS[abi].title}: VST3 SDK "
                f"{vst3_version(self.vst3)}, CLAP {clap_version(self.clap)}, "
                f"LV2 {lv2_version(self.lv2)}, VST2 (hand-written)")

    def label(self, version):
        return version

    def out_path(self, root, version, abi):
        # one pinned version: the file is named by ABI only
        return root / "formats" / f"rv_formats_{abi}.til"

    def plan(self, version, abi):
        wd = workdir(f"formats-{abi}")
        inc = lambda hs: "".join(f'#include "{h}"\n' for h in hs)
        # The hand header is written for IDA's own C parser (`__hidden this`,
        # builtin bool and intptr_t), so it is parsed as C with these supplied
        vst2 = (VST2_PRELUDE + substitute(VST2_HEADER.read_text(), abi))
        units = [
            Unit(inc(vst3_headers(self.vst3)), "c++17", ("c++14",)),
            Unit(inc(clap_headers(self.clap)), "c11"),
            Unit(vst2, "c11"),
            # Includes its deprecated extensions (event, uri-map): old
            # plugins still ask for them through extension_data/features
            Unit(inc(lv2_headers(self.lv2)), "c11"),
        ]
        ptr = ABIS[abi].bits // 8
        anchors = [
            # PFactoryInfo is 452 bytes with every packing: name/vendor/url/
            # email strings, flags
            Anchor(("Steinberg::PFactoryInfo",), 452, {"flags": 448}),
            Anchor(("Steinberg::IPluginFactory_vtbl",)),
            Anchor(("Steinberg::Vst::ProcessData",)),
            Anchor(("clap_plugin",)), Anchor(("clap_plugin_descriptor",)),
            Anchor(("clap_host",)),
            Anchor(("AEffect",)), Anchor(("VstEvents",)),


            Anchor(("LV2_Descriptor",), 8 * ptr,
                   {"URI": 0, "instantiate": ptr, "run": 4 * ptr,
                    "extension_data": 7 * ptr}),
            Anchor(("LV2_Lib_Descriptor",), None, {"get_plugin": None}),
            Anchor(("LV2_Feature",), 2 * ptr, {"URI": 0, "data": ptr}),
            Anchor(("LV2_URID_Map",), 2 * ptr, {"map": ptr}),
            Anchor(("LV2_Atom",), 8, {"type": 4}),
            Anchor(("LV2_Atom_Event",), 16, {"body": 8}),
            Anchor(("LV2_Worker_Interface",), 3 * ptr),
            Anchor(("LV2_State_Interface",), 2 * ptr),
            Anchor(("LV2UI_Descriptor",), 5 * ptr),
        ]
        return Plan(units, [str(self.vst3), str(self.clap), str(self.lv2)]
                    + system_includes(abi, self.xwin, self.sys_inc), anchors, wd)
