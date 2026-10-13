Minimal stand-ins for the C/C++ standard headers that `pluginterfaces/` includes.

`gen_vst3_types.py` parses the SDK for four ABIs (win64, win32, linux64, mac64).
Most machines only have standard headers for their own target, so these shims
provide just enough declarations for the headers to parse. Nothing is ever
compiled or linked against them. If a new SDK release includes another system
header, add an empty file with that name (or the declarations it needs).
