#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

# CLI: python verify-pe.py EXE [--report OUTPUT_JSON]. Standard library only.
# Reads normal/delay PE imports; writes only the optional report. Exit 1 rejects an import/parse error.
# Allowlist validation cannot see arbitrary runtime LoadLibrary calls; also run isolated startup.

"""Reject non-system normal AND delay-loaded DLL imports in a Windows EXE.

Uses only Python's standard library; no dumpbin/objdump/pefile installation.
This checks the PE import tables, not names loaded by arbitrary LoadLibrary calls.
The isolated application smoke test complements it for Qt's resource/plugin paths.
"""
import argparse
import json
import struct
from pathlib import Path

# Deliberately explicit: a DLL is not safe merely because someone installed it in
# System32. Qt, OpenSSL, VC redistributable and MinGW DLLs must never pass here.
SYSTEM_DLLS = set("""
advapi32 authz avrt bcrypt bcryptprimitives cabinet cfgmgr32 clbcatq combase comctl32
comdlg32 crypt32 cryptbase cryptsp d2d1 d3d11 d3d12 d3d9 d3dcompiler_47 dcomp dbghelp dnsapi
dwmapi dwrite dxcore dxgi dxguid faultrep gdi32 gdi32full glu32 hid imm32 iphlpapi
kernel32 kernelbase mpr mscms msimg32 msvcrt mswsock ncrypt netapi32 normaliz ntdll
ntmarta ole32 oleacc oleaut32 opengl32 powrprof profapi propsys psapi rasapi32
rpcrt4 secur32 setupapi shcore shell32 shlwapi sspicli ucrtbase uiautomationcore urlmon user32
userenv usp10 uxtheme version win32u windowscodecs winhttp wininet winmm
winmmbase winspool wintrust wldap32 ws2_32 wtsapi32
""".split())


def imports(path):
    data = Path(path).read_bytes()

    def unpack(fmt, offset):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError("Truncated PE structure")
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b"MZ":
        raise ValueError("Not a Windows PE executable")
    pe = unpack("<I", 0x3C)[0]
    if data[pe:pe+4] != b"PE\0\0":
        raise ValueError("Missing PE signature")
    machine, count = unpack("<HH", pe+4)
    if machine not in (0x8664, 0xAA64):
        raise ValueError("Expected a 64-bit Windows executable")
    optional_size = unpack("<H", pe+20)[0]
    optional = pe+24
    if unpack("<H", optional)[0] != 0x20B or optional_size < 112:
        raise ValueError("Expected PE32+ optional header")
    image_base = unpack("<Q", optional+24)[0]
    directory_count = unpack("<I", optional+108)[0]
    sections = []
    for i in range(count):
        virtual_size, rva, raw_size, raw = unpack("<IIII", optional+optional_size+i*40+8)
        sections.append((rva, max(virtual_size, raw_size), raw, raw_size))

    def offset(rva):
        for start, size, raw, raw_size in sections:
            if start <= rva < start+size and rva-start < raw_size:
                return raw+rva-start
        raise ValueError(f"Unmapped PE RVA: {rva:x}")

    def name(rva):
        start = offset(rva)
        end = data.find(b"\0", start, min(len(data), start+512))
        if end < 0:
            raise ValueError("Invalid DLL name")
        value = data[start:end].decode("ascii").lower()
        if not value or "/" in value or "\\" in value:
            raise ValueError("Invalid DLL import path")
        return value

    result = {"imports": [], "delay_imports": []}
    for index, key, size in [(1, "imports", 20), (13, "delay_imports", 32)]:
        if directory_count <= index:
            continue
        if 112+(index+1)*8 > optional_size:
            raise ValueError("Invalid data directory count")
        rva, length = unpack("<II", optional+112+index*8)
        if not rva:
            continue
        if length < size:
            raise ValueError("Invalid import directory size")
        terminated = False
        for position in range(0, length-size+1, size):
            fields = unpack("<"+"I"*(size//4), offset(rva+position))
            if not any(fields):
                terminated = True
                break
            dll_rva = fields[3] if index == 1 else fields[1]
            if index == 13 and not (fields[0] & 1):
                dll_rva -= image_base
            result[key].append(name(dll_rva))
        if not terminated:
            raise ValueError("Unterminated import directory")
    return {key: sorted(set(value)) for key, value in result.items()}


def verify(path):
    result = imports(path)
    names = set(result["imports"] + result["delay_imports"])
    if not names:
        raise ValueError("No imports found: cannot validate executable")
    unexpected = sorted(n for n in names if not (
        n == "winspool.drv" or n.endswith(".dll") and (n[:-4] in SYSTEM_DLLS
        or n.startswith(("api-ms-win-", "ext-ms-win-")))))
    if unexpected:
        raise ValueError("External DLL dependencies remain: " + ", ".join(unexpected))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        result = verify(args.exe)
    except (ValueError, OSError, struct.error, UnicodeError) as error:
        parser.exit(1, str(error)+"\n")
    output = json.dumps(result, indent=2)+"\n"
    if args.report:
        args.report.write_text(output, encoding="utf-8")
    print("PASS: PE imports contain only Windows system libraries.")
    print(output, end="")


if __name__ == "__main__":
    main()
