#!/usr/bin/env python3
"""oracle_compare.py — Compare YUV output pixel par pixel."""

import sys
import numpy as np


def compare_yuv(ref_path, test_path, width, height, bit_depth=8, chroma='420'):
    """Compare deux fichiers YUV frame par frame."""
    bytes_per_sample = 2 if bit_depth > 8 else 1
    dtype = np.uint16 if bit_depth > 8 else np.uint8

    if chroma == '420':
        frame_size = width * height * 3 // 2
    elif chroma == '422':
        frame_size = width * height * 2
    else:  # 444
        frame_size = width * height * 3
    frame_size *= bytes_per_sample

    ref_data = open(ref_path, 'rb').read()
    test_data = open(test_path, 'rb').read()

    if len(ref_data) != len(test_data):
        print(f"WARNING: file sizes differ (ref={len(ref_data)}, test={len(test_data)})")

    num_frames = len(ref_data) // frame_size
    num_frames_test = len(test_data) // frame_size

    if num_frames != num_frames_test:
        print(f"WARNING: frame counts differ (ref={num_frames}, test={num_frames_test})")
        num_frames = min(num_frames, num_frames_test)

    mismatches = []
    for f in range(num_frames):
        ref_frame = np.frombuffer(
            ref_data[f * frame_size:(f + 1) * frame_size], dtype=dtype)
        test_frame = np.frombuffer(
            test_data[f * frame_size:(f + 1) * frame_size], dtype=dtype)

        if not np.array_equal(ref_frame, test_frame):
            diff = np.abs(ref_frame.astype(int) - test_frame.astype(int))
            mismatches.append({
                'frame': f,
                'max_diff': int(diff.max()),
                'num_diff_pixels': int(np.count_nonzero(diff)),
                'first_diff_pos': int(np.argmax(diff > 0)),
                'psnr': (10 * np.log10((2**bit_depth - 1)**2 / np.mean(diff**2))
                         if diff.any() else float('inf'))
            })

    return num_frames, mismatches


if __name__ == '__main__':
    if len(sys.argv) < 5:
        print("Usage: python3 oracle_compare.py ref.yuv test.yuv width height "
              "[bit_depth] [chroma]")
        sys.exit(2)

    ref, test = sys.argv[1], sys.argv[2]
    w, h = int(sys.argv[3]), int(sys.argv[4])
    bd = int(sys.argv[5]) if len(sys.argv) > 5 else 8
    ch = sys.argv[6] if len(sys.argv) > 6 else '420'

    num_frames, mismatches = compare_yuv(ref, test, w, h, bd, ch)

    if not mismatches:
        print(f"PASS: {num_frames} frames, pixel-perfect match")
    else:
        print(f"FAIL: {len(mismatches)}/{num_frames} frames differ")
        for m in mismatches:
            print(f"  Frame {m['frame']}: {m['num_diff_pixels']} pixels differ, "
                  f"max_diff={m['max_diff']}, PSNR={m['psnr']:.2f}dB")
        sys.exit(1)
