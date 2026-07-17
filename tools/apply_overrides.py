#!/usr/bin/env python3
"""Apply the widescreen gen-code patch layer for MMX (marker-injected,
restorable — same discipline as SMW's tools/apply_overrides.py).

Runs AFTER regen, BEFORE compilation. Injection is marked and idempotent;
--restore strips every marked line, returning gen to pristine output for
the standard (authentic) zip.

History: a first revision biased the committed camera snapshot (DP $22 of
the D=$1E48 scroll page) at the $00DE19/$00DE40 stores ("WS-LOOKAHEAD").
That FROZE the camera: bank_00_DC36 (and likely other consumers) diff the
committed snapshot against the live camera ($1E4D) to detect 32px column
crossings — a permanent bias wedges the scroll state machine. Lesson: the
$05->$22 copy is a prev/cur camera pair, not a one-way streaming input.
Widening now happens at the CONSUMER comparisons instead:

WS-CULL — widen the offscreen despawn window (X axis only).
  bank_02_806E is the single scroll-off cull check (JSL'd from every
  enemy epilogue): cull when (objX - $1E4D + 0x40) >= 0x180, i.e. keep
  window = camera -64..+320. The snippet recomputes the carry verdict
  through MmxWsCullVerdictX (mmx_rtl.c): window widened by the live
  widescreen margin on both sides; identical to vanilla when off.
  The Y-axis check (0x40/0x160) is untouched.

WS-SPAWN — shift the enemy spawn-scan anchor into the margins.
  bank_00_DC36 fires per 32px camera column crossing and sets the scan
  anchor $00 for the spawn list-walker (bank_00_DCDB): scrolling right
  anchor = $1E4D + 0x100 (block $00DC78), scrolling left anchor = $1E4D
  (block $00DC62). Snippets re-store the anchor through
  MmxWsSpawnAnchorRight/Left (+margin / -margin, left clamped at 0).
  Spawn bookkeeping self-heals: spawn sets the per-record flag, cull
  clears it via slot+$0C, so early spawn + wide cull keep hysteresis.
  Gated separately (SNESRECOMP_WS_SPAWN, default on) so spawning can
  fall back to authentic 4:3 if wide spawning misbehaves, per the
  feature requirements; culling stays wide regardless.

WS-STAGE — stage the BG tilemap margins before they scroll on screen.
  Tilemap screen staging during gameplay is fired by level-placed
  camera-line trigger objects (spawned from the same 5-byte records as
  enemies): bank_03_FDD3 compares live camera X ($0BA8+5, offset from
  the $86:E4DC event table) against the trigger's line (obj D+$05).
  State 0 (bank_03_FDAB) fires on cam < line -> FE05 stages the left
  region; state 2 (bank_03_FDC0) fires on cam >= line -> FE0C stages
  the right region (both post a region nibble to $1F08 and schedule
  Task_B091 via B071/B075). The snippet re-biases the compared line
  through MmxWsStageLineAdjust (mmx_rtl.c): shifted into the camera's
  travel direction by the margin, X-axis staging watchers only
  (offset 5, event code 0x15). Direction-aware single line keeps the
  two fire conditions complementary -> no fire oscillation. Only the
  M0 variants carry the real 16-bit compare (M1 variants are pruned
  BRK stubs). Gated by SNESRECOMP_WS_STAGE (default on).

Usage:
    python tools/apply_overrides.py [--gen-dir src/gen] [--check] [-v]
    python tools/apply_overrides.py --restore [--gen-dir src/gen] [-v]
"""
import argparse
import glob
import os
import re
import sys

MARKERS = ("/*WS-CULL*/", "/*WS-SPAWN*/", "/*WS-LOOKAHEAD*/", "/*WS-STAGE*/",
           "/*WS-SHADOW*/")

RE_TRACE = re.compile(r"cpu_trace_block\(cpu, (0x[0-9A-Fa-f]+)\)")
RE_STORE00 = re.compile(
    r"^(\s*)cpu_write16\(cpu, 0x7E, \(uint16\)\(cpu->D \+ 0x0000\), (_v\d+)\);\s*$")
RE_CONST180 = re.compile(r"^\s*uint16 (_v\d+) = 0x180;\s*$")
RE_READ_A = re.compile(r"^\s*uint16 (_v\d+) = cpu_read_a16\(cpu\);\s*$")
RE_BRANCH_C = re.compile(r"^(\s*)if \(cpu->_flag_C == 1\)")
# The LLE-first emitter keys nodes by the DEMANDED pc24, so a JSL'd body
# lands under its LoROM execution-mirror bank ($83:FDD3, $82:806E) while
# short-called bank-00 code keeps its canonical key. Match both: mask bit 7
# of the bank when comparing block/function PCs.
RE_FUNC = re.compile(r"^RecompReturn (bank_[08]3_[0-9A-F]+)_M\dX\d\(CpuState")


def canon_pc24(pc24):
    return pc24 & ~0x800000
RE_CMP_DX = re.compile(
    r"^(\s*)uint16 (_v\d+) = cpu_read16\(cpu, 0x7E, "
    r"\(uint16\)\(cpu->D \+ 0x0000 \+ cpu->X\)\);\s*$")
RE_B23C_ENTRY = re.compile(
    r"^(\s*)cpu_trace_func_entry\(cpu, 0x[08]0B23C, ")


def spawn_snippet(indent, var, which):
    return (f"{indent}/*WS-SPAWN*/ {{ extern uint16 MmxWsSpawnAnchor{which}"
            f"(uint16); cpu_write16(cpu, 0x7E, (uint16)(cpu->D + 0x0000), "
            f"MmxWsSpawnAnchor{which}((uint16)({var}))); }}\n")


def cull_snippet(indent, var):
    return (f"{indent}/*WS-CULL*/ {{ extern uint16 MmxWsCullVerdictX(uint16);"
            f" cpu->_flag_C = MmxWsCullVerdictX((uint16)({var})); }}\n")


def apply_bank00(lines, verbose):
    out = []
    cur_block = None
    n = 0
    # NOTE: the WS-SHADOW pass (B23C staging note -> MmxWsShadowStageNote)
    # belongs to the tier-2 stage-prefill subsystem, which has NOT been
    # ported to this branch (no host-side consumer exists). It must come
    # back together with that port; injecting it alone is a link error.
    for line in lines:
        out.append(line)
        m = RE_TRACE.search(line)
        if m:
            cur_block = canon_pc24(int(m.group(1), 16))
            continue
        m = RE_STORE00.match(line)
        if m and cur_block in (0x00DC78, 0x00DC62):
            which = "Right" if cur_block == 0x00DC78 else "Left"
            out.append(spawn_snippet(m.group(1), m.group(2), which))
            n += 1
            if verbose:
                print(f"  WS-SPAWN {which} after line {len(out) - 1} "
                      f"(block {cur_block:#08x})")
    return out, n


def stage_snippet(indent, var):
    return (f"{indent}/*WS-STAGE*/ {{ extern uint16 MmxWsStageLineAdjust"
            f"(uint16, uint16, uint16); {var} = MmxWsStageLineAdjust("
            f"{var}, cpu->D, cpu->X); }}\n")


def apply_bank03(lines, verbose):
    out = []
    cur_fn = None
    n = 0
    for line in lines:
        out.append(line)
        m = RE_FUNC.match(line)
        if m:
            cur_fn = m.group(1)
            continue
        if cur_fn in ("bank_03_FDD3", "bank_83_FDD3"):
            m = RE_CMP_DX.match(line)
            if m:
                out.append(stage_snippet(m.group(1), m.group(2)))
                n += 1
                if verbose:
                    print(f"  WS-STAGE after line {len(out) - 1} "
                          f"(bank_03_FDD3, {m.group(2)})")
    return out, n


def apply_bank02(lines, verbose):
    out = []
    cur_block = None
    pend_val = None
    state = 0  # 0 idle, 1 saw 0x180 const, 2 have value var
    n = 0
    for line in lines:
        m = RE_TRACE.search(line)
        if m:
            cur_block = canon_pc24(int(m.group(1), 16))
            state = 0
        if cur_block == 0x02806E:
            if state == 0 and RE_CONST180.match(line):
                state = 1
            elif state == 1:
                mv = RE_READ_A.match(line)
                if mv:
                    pend_val = mv.group(1)
                    state = 2
            elif state == 2:
                mb = RE_BRANCH_C.match(line)
                if mb:
                    out.append(cull_snippet(mb.group(1), pend_val))
                    n += 1
                    if verbose:
                        print(f"  WS-CULL before line {len(out)} "
                              f"(block 0x02806E, {pend_val})")
                    state = 0
        out.append(line)
    return out, n


def process(path, fn, check, verbose):
    with open(path, "r", encoding="utf-8", newline="") as f:
        lines = f.readlines()
    out, n = fn(lines, verbose)
    if n and not check:
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.writelines(out)
    return n


def restore_file(path, verbose):
    with open(path, "r", encoding="utf-8", newline="") as f:
        lines = f.readlines()
    out = [ln for ln in lines if not any(mk in ln for mk in MARKERS)]
    n = len(lines) - len(out)
    if n:
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.writelines(out)
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen-dir", default="src/gen")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    # Sharded, mirror-bank-keyed generation: anchor patterns are gated on
    # block PCs / function symbols, so every applier can safely run over
    # every generated translation unit; non-matching files are no-ops.
    appliers = (apply_bank00, apply_bank02, apply_bank03)
    paths = sorted(glob.glob(os.path.join(args.gen_dir, "bank*_v2.c")))
    if not paths:
        print(f"no bank*_v2.c under {args.gen_dir}", file=sys.stderr)
        return 1
    total = 0
    for path in paths:
        name = os.path.basename(path)
        if args.restore:
            n = restore_file(path, args.verbose)
            if n:
                print(f"{name}: removed {n} injected line(s)")
            total += n
            continue
        with open(path, "r", encoding="utf-8") as f:
            if any(mk in f.read() for mk in MARKERS):
                print(f"{name}: already patched — run --restore first")
                continue
        n_file = 0
        for fn in appliers:
            n_file += process(path, fn, args.check, args.verbose)
        if n_file:
            verb = "would inject" if args.check else "injected"
            print(f"{name}: {verb} {n_file} site(s)")
        total += n_file
    if args.restore:
        print(f"restore: removed {total} injected line(s) total")
    if not args.restore and total == 0:
        print("WARNING: no anchor sites matched", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
