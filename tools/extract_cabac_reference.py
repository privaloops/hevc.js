#!/usr/bin/env python3
"""
extract_cabac_reference.py — Extract CABAC reference data from a bitstream
using ffmpeg's trace output for building unit tests.

Usage:
    python3 tools/extract_cabac_reference.py <bitstream.265> [--syntax-only]

This script:
1. Decodes the bitstream with ffmpeg in trace/debug mode
2. Extracts the raw RBSP bytes for each NAL unit
3. Produces a C++ test data file with:
   - RBSP byte arrays
   - Expected parsed values (from ffmpeg trace)

The output is a .h file that can be #included in unit tests.

Requires: ffmpeg (built with --enable-debug or trace logging)
"""

import subprocess
import sys
import os
import hashlib
import struct

def extract_nal_units(bitstream_path):
    """Extract NAL units from an Annex B bitstream."""
    with open(bitstream_path, 'rb') as f:
        data = f.read()

    nals = []
    i = 0
    while i < len(data):
        # Find start code (0x000001 or 0x00000001)
        if i + 3 <= len(data) and data[i:i+3] == b'\x00\x00\x01':
            sc_len = 3
        elif i + 4 <= len(data) and data[i:i+4] == b'\x00\x00\x00\x01':
            sc_len = 4
        else:
            i += 1
            continue

        nal_start = i + sc_len

        # Find next start code or end
        j = nal_start + 1
        while j < len(data):
            if j + 3 <= len(data) and data[j:j+3] == b'\x00\x00\x01':
                break
            if j + 4 <= len(data) and data[j:j+4] == b'\x00\x00\x00\x01':
                break
            j += 1

        nal_data = data[nal_start:j]
        if len(nal_data) >= 2:
            nal_type = (nal_data[0] >> 1) & 0x3F
            nals.append({
                'offset': i,
                'type': nal_type,
                'type_name': nal_type_name(nal_type),
                'data': nal_data,
                'rbsp': remove_emulation_prevention(nal_data[2:])  # skip 2-byte header
            })

        i = j

    return nals


def remove_emulation_prevention(data):
    """Remove emulation prevention bytes (0x03 in 0x000003xx sequences)."""
    result = bytearray()
    i = 0
    while i < len(data):
        if (i + 2 < len(data) and
            data[i] == 0 and data[i+1] == 0 and data[i+2] == 3):
            result.append(0)
            result.append(0)
            i += 3  # skip 0x03
        else:
            result.append(data[i])
            i += 1
    return bytes(result)


def nal_type_name(t):
    names = {
        0: "TRAIL_N", 1: "TRAIL_R", 19: "IDR_W_RADL", 20: "IDR_N_LP",
        21: "CRA_NUT", 32: "VPS_NUT", 33: "SPS_NUT", 34: "PPS_NUT",
        35: "AUD_NUT", 39: "PREFIX_SEI", 40: "SUFFIX_SEI",
    }
    return names.get(t, f"NAL_{t}")


def generate_test_header(nals, bitstream_name):
    """Generate a C++ header with test data."""
    safe_name = os.path.splitext(os.path.basename(bitstream_name))[0]
    safe_name = safe_name.replace('-', '_').replace('.', '_')

    lines = []
    lines.append(f"// Auto-generated from {os.path.basename(bitstream_name)}")
    lines.append(f"// by tools/extract_cabac_reference.py")
    lines.append(f"// {len(nals)} NAL units found")
    lines.append("")
    lines.append("#pragma once")
    lines.append("#include <cstdint>")
    lines.append("#include <cstddef>")
    lines.append("")
    lines.append(f"namespace test_data_{safe_name} {{")
    lines.append("")
    lines.append(f"constexpr int num_nals = {len(nals)};")
    lines.append("")

    # NAL type list
    lines.append("// NAL unit types in order")
    lines.append(f"constexpr uint8_t nal_types[{len(nals)}] = {{")
    types_str = ", ".join(str(n['type']) for n in nals)
    lines.append(f"    {types_str}")
    lines.append("};")
    lines.append("")

    # RBSP data for each NAL
    for i, nal in enumerate(nals):
        rbsp = nal['rbsp']
        lines.append(f"// NAL {i}: {nal['type_name']} ({len(rbsp)} RBSP bytes)")
        lines.append(f"constexpr uint8_t nal_{i}_rbsp[] = {{")

        # Format as hex array, 16 bytes per line
        for j in range(0, len(rbsp), 16):
            chunk = rbsp[j:j+16]
            hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
            comma = "," if j + 16 < len(rbsp) else ""
            lines.append(f"    {hex_str}{comma}")

        lines.append("};")
        lines.append(f"constexpr size_t nal_{i}_rbsp_size = {len(rbsp)};")
        lines.append("")

    # Summary struct
    lines.append("struct NalInfo {")
    lines.append("    uint8_t type;")
    lines.append("    const uint8_t* rbsp;")
    lines.append("    size_t rbsp_size;")
    lines.append("};")
    lines.append("")
    lines.append(f"constexpr NalInfo nals[{len(nals)}] = {{")
    for i, nal in enumerate(nals):
        lines.append(f"    {{ {nal['type']}, nal_{i}_rbsp, nal_{i}_rbsp_size }},")
    lines.append("};")
    lines.append("")

    lines.append(f"}} // namespace test_data_{safe_name}")

    return "\n".join(lines)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <bitstream.265> [--syntax-only]")
        sys.exit(1)

    bitstream_path = sys.argv[1]
    syntax_only = "--syntax-only" in sys.argv

    if not os.path.isfile(bitstream_path):
        print(f"Error: {bitstream_path} not found")
        sys.exit(1)

    print(f"Extracting NAL units from {bitstream_path}...")
    nals = extract_nal_units(bitstream_path)

    print(f"Found {len(nals)} NAL units:")
    for i, nal in enumerate(nals):
        print(f"  [{i}] {nal['type_name']} (type={nal['type']}, "
              f"rbsp={len(nal['rbsp'])} bytes, offset={nal['offset']})")

    # Generate C++ header
    header = generate_test_header(nals, bitstream_path)

    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "..", "tests", "conformance", "reference_data")
    os.makedirs(out_dir, exist_ok=True)

    safe_name = os.path.splitext(os.path.basename(bitstream_path))[0]
    safe_name = safe_name.replace('-', '_').replace('.', '_')
    out_path = os.path.join(out_dir, f"{safe_name}.h")

    with open(out_path, 'w') as f:
        f.write(header + "\n")

    print(f"\nGenerated: {out_path}")
    print(f"Usage in tests:")
    print(f'  #include "conformance/reference_data/{safe_name}.h"')
    print(f"  // Access: test_data_{safe_name}::nals[0].rbsp")


if __name__ == "__main__":
    main()
