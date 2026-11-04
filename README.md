> *"Music's the odyssey, [...] find the magic key"*
\- One-T + Cool-T, *The Magic Key*

# REverb

REverb is a Work-In-Progress virtual instrument reverse engineering and debugging toolkit, available as an IDA Pro plugin.

Audio plugins are shared objects, mostly written in C and C++ (although Rust and C# plugins are also out there). Each format is heavily standardized to ensure uniform functionality across multiple hosts. The plugins also encode lots of useful metadata.

## Features
 - **Plugin interface detection** from the exported symbols
 - **Type declarations** from the official SDKs
 - **Object labeling** Entry points, factory objects, vtables, plugin descriptors, you name it.
 - **Plugin framework identification** from strings and RTTI signatures for JUCE, DPF and iPlug2

## Requirements

- IDA 9.2 or higher **with the Hex-Rays decompiler** for the database's architecture

## Development and building from source

A few additional tools are required:
- IDA SDK for your preferred IDA version:
    - **SDK 9.4 and newer** ships its own CMake layout (`cmake/idasdk_init.cmake`), and
    REverb uses it.
    - **Older SDKs** need [`allthingsida/ida-cmake`](https://github.com/allthingsida/ida-cmake)
    cloned into the SDK root as `ida-cmake/`; REverb looks for `ida-cmake/bootstrap.cmake`.
- CMake 3.25 or higher
- A modern C++ compiler for your platform
- Recent Python and pip (for decl generation and tests)
- Clang 21 or higher for clang-format

```sh
export IDASDK=/path/to/idasdk  # or pass -DIDASDK=... to CMake
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

On Windows, add `-A x64` to the configure step (Visual Studio generators).

The resulting `reverb.dll/so/dylib` is placed by the SDK's CMake
layout (look under `build/` and `$IDASDK/bin`). Copy it to your plugins directory.

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
 - Create a minimal plugin host that uses format and framework detection results to automatically set breakpoints
 at interesting functions and integrate it with the IDA debug ecosystem
 - Recursively taint functions accessing pointers to audio signal data provided by the host (e.g. `process()` in VST3),
 labeling them for example `sub_dsp_01234`
 - Add support for more plugin formats like AAX, AU and LV2
 - Scan for typical DSP constats (like samplerates, FFT tables, biquad coefficients)


### Regenerating the type declarations

The SDK type declarations REverb loads into IDA are generated resources from each applicable `src/formats`' `types.h` and matching `.inc` tables (such as `iids.inc` in VST3). The plugin build only embeds them, so you only need this when an
SDK is updated.

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
```

Each generator re-parses its own output for every target ABI and compares struct sizes, field offsets and (for CLAP) field types and enum constants against the source headers.

## Contributing

Issues and pull requests are welcome. If you find a plugin binary that REverb mislabels or fails on, you can send the sample (or describe the problem) together with the `[REverb]` log output.

Builds CI uses an IDA SDK download key, so builds of pull requests from forks are skipped by default and set up by the maintainer.

### On AI usage

The Python parts of the project (declgens (with an exception to VST2, which decls have been written manually) and some parts of `test/test.py`) have been created strictly by "vibe-coding" -  with a locally-running Qwen3.8-27B AI model working in agent mode. I also used Claude Opus and Sonnet 5.5 in some parts of the code, for example CLAP ID scanning and vtable reconstruction, but it was mostly chat-mode assistance.

We encourage all potential contributors to use whatever code generation systems they find the most beneficial for the development of the plugin, as long as the ownership of the sources is clear and the code is maintainable, reviewable and "good".

## License

REverb is released under the [GNU General Public License v3.0](LICENSE).

The generated declaration files are derived from Steinberg's VST3 `pluginterfaces`
and the CLAP headers, both MIT-licensed. See their repositories for their notices.
The VST2 declarations are derived from various sources, mostly [Score's vst-compat](https://github.com/ossia/score/blob/master/src/plugins/score-plugin-vst/Vst/vst-compat.hpp) which I believe are clean-room reimplementation of the original SDK headers.
Building requires the Hex-Rays IDA SDK, which is not included and is subject to its
own license.