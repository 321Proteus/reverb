"""Format sources: VST3, CLAP and VST2, merged into one til per ABI
(build/tils/formats/<abi>.til; their names do not overlap: Steinberg::...,
clap_..., VST2's own flat ones). Config only; the core does the work."""

import glob
import re
import tempfile
from pathlib import Path

from .core import REPO, Anchor, Plan, Unit, substitute, system_includes

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


def _sdk_version(sdk, clap):
    """A label for the pinned SDKs: the CLAP version header, plus the VST3
    SDK's own (VST3 has no version define in pluginterfaces; its folder
    name stays as given)."""
    v = (clap / "clap" / "version.h").read_text()
    n = [re.search(rf"CLAP_VERSION_{k} (\d+)", v)[1]
         for k in ("MAJOR", "MINOR", "REVISION")]
    return f"vst3sdk+clap-{'.'.join(n)}"


class Formats:
    name = "formats"

    def __init__(self, sdks, xwin, vst3=None, sys_inc=()):
        self.sys_inc = sys_inc
        self.vst3 = Path(vst3) if vst3 else Path(sdks) / "vst3sdk"
        self.clap = Path(sdks) / "clap" / "include"
        self.xwin = xwin

    def versions(self):
        return [_sdk_version(self.vst3, self.clap)]

    def label(self, version):
        return version

    def out_path(self, root, version, abi):
        # one pinned version: the file is named by ABI only
        return root / "formats" / f"{abi}.til"

    def plan(self, version, abi):
        wd = Path(tempfile.mkdtemp(prefix="tilgen_fmt_"))
        inc = lambda hs: "".join(f'#include "{h}"\n' for h in hs)
        # The hand header is written for IDA's own C parser (`__hidden this`,
        # builtin bool and intptr_t), so it is parsed as C with these supplied
        vst2 = (VST2_PRELUDE + substitute(VST2_HEADER.read_text(), abi))
        units = [
            Unit(inc(vst3_headers(self.vst3)), "c++17", ("c++14",)),
            Unit(inc(clap_headers(self.clap)), "c11"),
            Unit(vst2, "c11"),
        ]
        anchors = [
            # PFactoryInfo is 452 bytes with every packing: name/vendor/url/
            # email strings, flags
            Anchor(("Steinberg::PFactoryInfo",), 452, {"flags": 448}),
            Anchor(("Steinberg::IPluginFactory_vtbl",)),
            Anchor(("Steinberg::Vst::ProcessData",)),
            Anchor(("clap_plugin",)), Anchor(("clap_plugin_descriptor",)),
            Anchor(("clap_host",)),
            Anchor(("AEffect",)), Anchor(("VstEvents",)),
        ]
        return Plan(units, [str(self.vst3), str(self.clap)]
                    + system_includes(abi, self.xwin, self.sys_inc), anchors, wd)
