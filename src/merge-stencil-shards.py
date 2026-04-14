#!/usr/bin/env python3
"""
Merge per-rank shard files from stencil-2d-mpi into one binary .dat matching
stencil-2d / verify format: int rows, int cols, rows*cols doubles.

Usage:
  python3 merge-stencil-shards.py <path_stem> <output.dat>

Shards are expected at <path_stem>.0, <path_stem>.1, ... (same stem passed to
stencil-2d-mpi -o).

Header per shard (little-endian): uint32 magic, then 7x int32:
  version, global_rows, global_cols, rank, world_size,
  file_global_first_row, file_n_rows
followed by file_n_rows * global_cols float64 rows.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

MAGIC = 0x31485453
# C writes: uint32 magic, int32 version, global_rows, global_cols, rank, world_size,
#          file_global_first_row, file_n_rows
HDR = struct.Struct("<Iiiiiiii")


def read_shard(path: str) -> tuple:
    with open(path, "rb") as fp:
        blob = fp.read(HDR.size)
        if len(blob) != HDR.size:
            raise ValueError(f"{path}: short header ({len(blob)} bytes)")
        magic, ver, gr, gc, rank, ws, fg, nr = HDR.unpack(blob)
        if magic != MAGIC:
            raise ValueError(f"{path}: bad magic 0x{magic:08x}")
        if ver != 1:
            raise ValueError(f"{path}: unsupported version {ver}")
        data = fp.read(nr * gc * 8)
        if len(data) != nr * gc * 8:
            raise ValueError(f"{path}: expected {nr * gc * 8} data bytes, got {len(data)}")
    rows = struct.unpack(f"<{nr * gc}d", data)
    matrix_rows = [rows[i * gc : (i + 1) * gc] for i in range(nr)]
    return gr, gc, rank, ws, fg, nr, matrix_rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("stem", help="Output stem used with stencil-2d-mpi -o (e.g. out/run.dat)")
    ap.add_argument("out_dat", help="Combined .dat path")
    args = ap.parse_args()

    stem = args.stem
    paths = []
    r = 0
    while True:
        p = f"{stem}.{r}"
        if not os.path.isfile(p):
            break
        paths.append(p)
        r += 1

    if not paths:
        print(f"No shards found: {stem}.0, ...", file=sys.stderr)
        return 1

    shards = []
    for p in paths:
        try:
            shards.append(read_shard(p))
        except (OSError, ValueError) as e:
            print(e, file=sys.stderr)
            return 1

    gr0, gc0 = shards[0][0], shards[0][1]
    for s in shards:
        if s[0] != gr0 or s[1] != gc0:
            print("Inconsistent global_rows/cols across shards", file=sys.stderr)
            return 1
    gr, gc = gr0, gc0

    grid = [[0.0] * gc for _ in range(gr)]
    filled = [False] * gr

    for _gr, _gc, rank, ws, fg, nr, mrows in shards:
        for i in range(nr):
            gi = fg + i
            if gi < 0 or gi >= gr:
                print(f"rank {rank}: global row {gi} out of range", file=sys.stderr)
                return 1
            if filled[gi]:
                print(f"overlap at global row {gi}", file=sys.stderr)
                return 1
            filled[gi] = True
            grid[gi][:] = mrows[i]

    if not all(filled):
        missing = [i for i, ok in enumerate(filled) if not ok]
        print(f"Missing global rows after merge: {missing[:10]}...", file=sys.stderr)
        return 1

    ws_set = {s[3] for s in shards}
    if len(ws_set) != 1:
        print("Inconsistent world_size in shards", file=sys.stderr)
        return 1
    if list(ws_set)[0] != len(paths):
        print(
            f"Warning: found {len(paths)} shard files but header world_size={list(ws_set)[0]}",
            file=sys.stderr,
        )

    try:
        with open(args.out_dat, "wb") as fp:
            fp.write(struct.pack("<ii", gr, gc))
            for i in range(gr):
                fp.write(struct.pack(f"<{gc}d", *grid[i]))
    except OSError as e:
        print(e, file=sys.stderr)
        return 1

    print(f"Wrote {args.out_dat} ({gr}x{gc}) from {len(paths)} shards.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
