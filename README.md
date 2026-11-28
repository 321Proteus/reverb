> *"Music's the odyssey, [...] find the magic key"*
\- One-T + Cool-T, *The Magic Key*

# REverb

REverb is a Work-In-Progress virtual instrument reverse engineering and debugging toolkit, available as an IDA Pro plugin.

Audio plugins are shared objects, mostly written in C and C++ (although Rust and C# plugins are also out there). Each format is heavily standardized to ensure uniform functionality across multiple hosts. The plugins also encode lots of useful metadata.

## Features
 - **Plugin interface detection** from the exported symbols
 - **Object labeling** Entry points, factory objects, vtables, plugin descriptors, you name it.
 - **Plugin framework identification** from strings and RTTI signatures for JUCE, DPF and iPlug2
 - **Type declarations** from the official SDKs and the JUCE plugin framework
 - **Debugging host** `reverb-minihost` runs the plugin under IDA's debugger, locally or remotely, with breakpoints set on the interesting functions

## Requirements

- IDA 9.2 or higher **with the Hex-Rays decompiler** for the database's architecture

## Framework type libraries (optional)

For a detected plugin, REverb can load the types of the matching format and framework version to the database into the local types in two ways. Format types are served as IDA type library modules or generated resources from each applicable `src/formats`' `types.h` and matching `.inc` tables (such as `iids.inc` in VST3). The framework declarations are only distributed using TIL libraries.

The TILs are a separate download from the Releases page. To use them, extract the archive into the `til` directory of your IDA user directory (`~/.idapro/til` or `%APPDATA%\Hex-Rays\IDA Pro\til`). The libraries land in `til/pc`, where IDA looks for x86/x64 type libraries:

```sh
tar -xJf reverb-tils-<date>.tar.xz -C ~/.idapro/til
```

Without them REverb works as before and logs that they are not installed. The library is picked by the detected version: the newest one that is not newer than it (a newer JUCE gets the newest library, with a warning)

## Debugging with reverb-minihost

`reverb-minihost` is a minimal host that ships with the plugin. It currently supports all formats that are supported by REverb itself. REverb points IDA's debugger at it with the detection results, so pressing F9 loads the plugin and takes it through its lifecycle: entry point, instance creation, initialization, activation and processing. There is no audio device (Yet!): the plugin processes a test signal and gets a MIDI note every second, so instruments make sound too.

After the initial scan, REverb adds breakpoints on the functions of the detected format and framework, in groups under `/REverb/<name>`. The lifecycle ones start enabled, the per-block ones disabled.

The minihost's log shows up in IDA's Output window. The **Debugger > REverb minihost** menu and its toolbar control it while it runs:

| Control | What it does |
|---------|--------------|
| Play / Pause | Runs blocks until paused, or stops running them |
| Run blocks... | Runs a number of blocks, then pauses |
| Next step | With *Step through phases* on, goes on to the next lifecycle phase, so you can set breakpoints between them |
| Info | Prints the plugin and engine state |
| List parameters | Prints the plugin's parameters |
| Send command... | Sends any command below |
| Settings... | What the minihost is started with: the format, sample rate, block size, signal, a state to load, and the remote paths |

Commands: `info`, `play`, `pause`, `blocks N`, `next`, `run` (ends step mode), `params [part of a name]`, `set <index|#id|name> <0..1>`, `save FILE` and `load FILE` for the plugin's state blob, and `quit`. The same commands work on the minihost's standard input when it runs in a terminal (`reverb-minihost-<os> --help` lists its options).

### Remote debugging

To debug a plugin on another machine, such as a Linux or Windows plugin from a Mac, run IDA's debug server there (`linux_server`, `win64_remote.exe` or `mac_server` from IDA's `dbgsrv` directory) and copy over the plugin and the minihost for that OS: `reverb-minihost-linux`, `reverb-minihost.exe` or `reverb-minihost-macos`. In IDA, select the remote debugger and set its host in *Debugger > Process options*, then fill in the paths on the target in the minihost settings. On the target, the minihost listens on the channel port (23950 by default) and IDA connects to it, so the target's firewall has to let that port in.

## Development and building from source

A few additional tools are required:
- IDA SDK for your preferred IDA version:
    - **SDK 9.4 and newer** ships its own CMake layout (`cmake/idasdk_init.cmake`), and  REverb uses it.
    - **Older SDKs** need [`allthingsida/ida-cmake`](https://github.com/allthingsida/ida-cmake)
    cloned into the SDK root as `ida-cmake/`; REverb looks for `ida-cmake/bootstrap.cmake`.
- CMake 3.25 or higher
- A modern C++ compiler for your platform
- Recent Python and pip (for decl generation and tests)
- Docker (for TIL generation)
- Clang 21 or higher for clang-format

```sh
export IDASDK=/path/to/idasdk  # or pass -DIDASDK=... to CMake
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

On Windows, add `-A x64` to the configure step (Visual Studio generators).

The resulting `reverb.dll/so/dylib` is placed by the SDK's CMake
layout in `$IDABIN/plugins` (pass `-DIDABIN=$HOME/.idapro` or set the `IDABIN` environment variable to build straight into your user directory, without either it is `$IDASDK/bin`). Copy it to your plugins directory otherwise.

`reverb-minihost` is built next to it, named for its OS (`reverb-minihost-linux`, `reverb-minihost-macos` or `reverb-minihost.exe`), so that the release can ship all of them in one directory. Pass `-DRV_BUILD_MINIHOST=OFF` to build the plugin alone.

The CI workflow in [`.github/workflows/build.yml`](.github/workflows/build.yml) is a
complete, working example of the steps above for every platform.

## Testing
The repository features a simple batch headless IDA analysis utility:
| Option            | Meaning                                                     |
|-------------------|-------------------------------------------------------------|
| `--idat PATH`     | `idat` executable to use (default `idat64`)                 |
| `-j N`            | parallel runs                                               |
| `--timeout SEC`   | per-sample limit (default 300)                              |
| `--work DIR`      | scratch directory for databases and logs                    |
| `--keep-logs DIR` | also save each sample's full IDA log                        |
| `--full-analysis` | wait for full auto-analysis before exiting (useful if one only needs to check the plugin format and the framework used) |

The name "test" is misleading here, because it doesn't actually check anything, I originally used it for bulk-checking
my plugin library. It also requires a licensed IDA and therefore is not ran by CI.

## To Do (planned):
 - Figure out how to do type declarations for other frameworks, like DPF and iPlug2 - they don't have typical release cycles, just a steady stream of commits
  - Implement per-platform GUI and audio support for the minihost
 - Recursively taint functions accessing pointers to audio signal data provided by the host (e.g. `process()` in VST3),
 labeling them for example `sub_dsp_01234`
 - Add support for more plugin formats like AAX, AU and LV2
 - Scan for typical DSP constats (like samplerates, FFT tables, biquad coefficients)


### Regenerating the type declarations

```sh
pip install libclang

# VST3
git clone https://github.com/steinbergmedia/vst3_pluginterfaces
python3 tools/vst3_decls.py --sdk vst3_pluginterfaces \
    --types src/formats/vst3/types.h \
    --iids src/formats/vst3/iids.inc

# CLAP
git clone https://github.com/free-audio/clap
python3 tools/clap_decls.py --sdk clap \
    --types src/formats/clap/types.h \
    --exts src/formats/clap/exts.inc

# reconfigure to embed the new files
cmake -B build -S .

# Format & framework TILs, built in a Docker container
cd tools/tilgen

# Paths used by run.sh (these are the defaults)
export TILGEN_IDA=/opt/ida-pro          # a licensed IDA, run once by this user
export TILGEN_SDKS=~/Source/rv_sdks     # holds JUCE/ (git clone), vst3sdk/, clap/, VST_SDK_2.X/
export TILGEN_XWIN=~/xwin/splat         # Windows SDK + CRT headers (`xwin splat`), for msvc ABIs
export TILGEN_IMAGE=reverb-tilgen       # the Docker image name

# One run per ABI (itanium64|msvc64|msvc32) (--source all|formats|juce)
# --raw stores the tils without IDA's zlib, so the final archive compresses better
./run.sh docker --abi itanium64 --source all --raw --force -j 4 --out ../../build/tils-final

# Pack the libraries into a release archive (optional)
./package.sh --input ../../build/tils-final --output ../../build/reverb-tils.tar.xz
```

`run.sh docker` builds the image from `tools/tilgen/Dockerfile` (GCC 12 and Debian 12 headers) on every run, only the first one takes a while.
It mounts IDA, the SDKs and the xwin headers read-only and the repository read-write, so `--out` must be inside the repository.
IDA's license is taken from `~/.idapro/ida.reg`, which is why IDA must have been started once by the same user. Pick `-j` for your machine: each job can use 1-2 GB of RAM.

Each til is parsed in its own process with `tools/tilgen/quarantine.c` preloaded. It delays `free()`, which works around a use-after-free in IDA 9.4's clang parser that otherwise crashes it on JUCE 7.0.6 and newer (and 7.0.3 on MSVC).

Each generator re-parses its own output for every target ABI and compares struct sizes, field offsets and (for CLAP) field types and enum constants against the source headers.
VST2 type declarations are not generated - please read the note in `src/formats/vst2/types.h`.

## Contributing

Issues and pull requests are welcome. If you find a plugin binary that REverb mislabels or fails on, you can send the sample (or describe the problem) together with the `[REverb]` log output.

### On AI usage

The Python parts of the project (declgens (with an exception to VST2, which decls have been written manually) and some parts of `test/test.py`) have been created strictly by "vibe-coding" -  with a locally-running Qwen3.8-27B AI model working in agent mode. I also used Claude Opus and Sonnet 5.5 in some parts of the code, for example CLAP ID scanning and vtable reconstruction, but it was mostly chat-mode assistance.

We encourage all potential contributors to use whatever code generation systems they find the most beneficial for the development of the plugin, as long as the ownership of the sources is clear and the code is maintainable, reviewable and "good".

## License

REverb is released under the [GNU General Public License v3.0](LICENSE).

The generated declaration files are derived from Steinberg's VST3 `pluginterfaces` and the CLAP headers, both MIT-licensed. See their repositories for their notices.
Building requires the Hex-Rays IDA SDK, which is not included and is subject to its own license.
