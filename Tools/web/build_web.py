#!/usr/bin/env python3
"""
Builds a one-page browser version of Petrichor Piano for quick tests: the engine compiled to
WebAssembly (running in an AudioWorklet), driven by the plug-in's own UI through a stand-in for
the JUCE backend (Tools/web/shell.js). Everything is inlined into a single HTML file.

    cd frontend && npm run build && cd ..          # the UI: frontend/dist/index.html
    python3 Tools/web/build_web.py out/petrichor-web.html

Needs clang with the wasm32-wasi target and its sysroot (Debian/Ubuntu: wasi-libc,
libc++-18-dev-wasm32, libc++abi-18-dev-wasm32, libclang-rt-18-dev-wasm32). Set CXX_WASM to use
another clang++. The output is an HTML fragment (no <html>/<head>/<body>) for hosts that wrap pages
in their own document skeleton; it also opens fine on its own.
"""

import base64
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ENGINE_SOURCES = ["KolmogorovNoise.cpp", "PetrichorEngine.cpp", "PianoHybrid.cpp", "PianoVoice.cpp", "RainTexture.cpp"]


def blob_cpp(path_bin, path_cpp):
    """Same layout as Tools/BinToCpp.cpp: little-endian 32-bit words plus the byte count."""
    data = open(path_bin, "rb").read()
    words = [int.from_bytes(data[i:i + 4].ljust(4, b"\0"), "little") for i in range(0, len(data), 4)]
    with open(path_cpp, "w") as f:
        f.write("#include <cstddef>\n#include <cstdint>\nnamespace petrichor { namespace data {\n")
        f.write("extern const std::uint32_t pianoHybridWords[];\nextern const std::size_t pianoHybridBytes;\n")
        f.write(f"const std::size_t pianoHybridBytes = {len(data)};\nconst std::uint32_t pianoHybridWords[] = {{\n")
        for i in range(0, len(words), 12):
            f.write(",".join(f"0x{w:08x}" for w in words[i:i + 12]) + ",\n")
        f.write("};\n} }\n")


def build_wasm(tmp):
    blob = os.path.join(tmp, "PianoHybridBlob.cpp")
    blob_cpp(os.path.join(ROOT, "Source", "DSP", "Data", "piano_hybrid.bin"), blob)
    out = os.path.join(tmp, "petrichor.wasm")
    cmd = [os.environ.get("CXX_WASM", "clang++"), "--target=wasm32-wasi", "-O3", "-std=c++17", "-msimd128", "-mbulk-memory",
           "-fno-exceptions", "-mexec-model=reactor", "-DNDEBUG", "-I" + os.path.join(ROOT, "Source", "DSP"),
           os.path.join(ROOT, "Tools", "web", "PetrichorWeb.cpp"), blob]
    cmd += [os.path.join(ROOT, "Source", "DSP", s) for s in ENGINE_SOURCES]
    cmd += ["-Wl,--strip-all", "-o", out]
    subprocess.run(cmd, check=True)
    return open(out, "rb").read()


def split_dist(html):
    """The Vite single-file build: its <style> and <script> blocks and the body's markup."""
    head = html[:html.index("</head>")]
    styles = re.findall(r"<style[^>]*>.*?</style>", head, re.S)
    scripts = re.findall(r"<script[^>]*>.*?</script>", head, re.S)
    body = re.search(r"<body[^>]*>(.*)</body>", html, re.S).group(1)
    body_scripts = re.findall(r"<script[^>]*>.*?</script>", body, re.S)
    body_markup = re.sub(r"<script[^>]*>.*?</script>", "", body, flags=re.S).strip()
    return styles, scripts + body_scripts, body_markup


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out_path = sys.argv[1]
    dist = os.path.join(ROOT, "frontend", "dist", "index.html")
    if not os.path.exists(dist):
        sys.exit("frontend/dist/index.html is missing: run `npm run build` in frontend/ first")

    with tempfile.TemporaryDirectory() as tmp:
        wasm = build_wasm(tmp)

    styles, app_scripts, markup = split_dist(open(dist, encoding="utf-8").read())
    worklet = open(os.path.join(ROOT, "Tools", "web", "worklet.js"), encoding="utf-8").read()
    shell = open(os.path.join(ROOT, "Tools", "web", "shell.js"), encoding="utf-8").read()
    no_close = lambda s: s.replace("</", "<\\/")  # keep inline strings from closing their <script>

    page = ["<title>Petrichor Piano</title>"]
    page += styles
    page.append("<script>window.__PETRICHOR_WASM__=\"" + base64.b64encode(wasm).decode("ascii") + "\";"
                "window.__PETRICHOR_WORKLET__=" + no_close(json.dumps(worklet)) + ";</script>")
    page.append("<script>\n" + worklet + "\n</script>")  # the main-thread fallback uses the same code
    page.append("<script>\n" + shell + "\n</script>")
    page.append(markup)
    page += app_scripts

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(page) + "\n")
    print(f"wrote {out_path}: {os.path.getsize(out_path) / 1e6:.1f} MB (engine {len(wasm) / 1e6:.2f} MB)")


if __name__ == "__main__":
    main()
