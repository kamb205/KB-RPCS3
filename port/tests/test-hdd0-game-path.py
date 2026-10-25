#!/usr/bin/env python3
"""Compile the HDD0 path expressions from a patched RPCS3 source checkout.

Usage: python3 port/tests/test-hdd0-game-path.py /path/to/patched/rpcs3
Requires a C++17 compiler (CXX, or c++); no PS5 SDK or RPCS3 dependencies.
SPDX-License-Identifier: GPL-2.0-only
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile


def main():
    source = (Path(sys.argv[1]) / "rpcs3/Emu/System.cpp").read_text(encoding="utf-8")
    normalization = re.search(
        r"(?:const )?std::string resolved_hdd0 = g_emu_callbacks\.resolve_path\(hdd0_game\).*?"
        r"(?=\n\s*if \(from_hdd0_game && m_cat)", source, re.S
    )
    if normalization is None:
        raise SystemExit("Could not locate the HDD0 root normalization in System.cpp")
    extraction = re.search(
        r"else if \(from_hdd0_game\)\s*\{\s*(const std::string tail = .*?)\s*argv\[0\]",
        source[normalization.end():], re.S
    )
    if extraction is None:
        raise SystemExit("Could not locate the installed-game title extraction in System.cpp")

    # Compile the source expressions themselves, so reverting the patch fails this check.
    program = r'''
#include <cstdlib>
#include <iostream>
#include <string>
namespace fs { const char* delim = "/\\"; }
struct { std::string resolve_path(const std::string& value) { return value; } } g_emu_callbacks;
std::string normalize(const std::string& hdd0_game)
{
NORMALIZATION
    return resolved_hdd0;
}
std::string title_id(const std::string& resolved_hdd0, const std::string& resolved_path)
{
EXTRACTION
    return dirname;
}
int main()
{
    unsigned checks = 0;
    for (const std::string root : {"/app0/rpcs3/dev_hdd0/game", "/data/rpcs3/dev_hdd0/game"})
        for (const std::string suffix : {"", "/"})
            for (const std::string id : {"NPEB00415", "NPEB01284", "NPUB30578"})
            {
                const auto actual = title_id(normalize(root + suffix), root + "/" + id + "/USRDIR/EBOOT.BIN");
                if (actual != id)
                {
                    std::cerr << "Title ID truncated: " << root + suffix << ": " << actual << " != " << id << '\n';
                    return EXIT_FAILURE;
                }
                ++checks;
            }
    if (normalize("") != "/" || normalize("/") != "/")
        return EXIT_FAILURE;
    std::cout << "Passed " << checks << " title-path cases and 2 root-boundary cases\n";
}
'''.replace("NORMALIZATION", normalization.group()).replace("EXTRACTION", extraction.group(1))
    with tempfile.TemporaryDirectory(prefix="kb-rpcs3-hdd0-") as directory:
        cpp = Path(directory) / "test.cpp"
        binary = Path(directory) / ("test.exe" if os.name == "nt" else "test")
        cpp.write_text(program, encoding="utf-8")
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) +
                       ["-std=c++17", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main()
