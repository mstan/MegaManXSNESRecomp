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
  Each camera-column update runs complementary record passes: the normal
  call scans the widescreen anchor but admits only type-3 enemies; a
  balanced second call scans the native anchor and admits every record.
  Per-record flags make already-created enemies no-ops in the native pass,
  while camera, staging, darkness, and encounter controllers (types 0-2)
  retain authentic activation.
  Spawn bookkeeping self-heals: spawn sets the per-record flag, cull
  clears it via slot+$0C, so early spawn + wide cull keep hysteresis.
  Gated separately (SNESRECOMP_WS_SPAWN, default on) so spawning can
  fall back to authentic 4:3 if wide spawning misbehaves, per the
  feature requirements; culling stays wide regardless.

WS-ACTIVATE — move the intro helicopter's entrance gate into the margin.
  bank_82_B964 starts the helicopter controller when it comes within $80
  pixels of X, which is the native right-edge presentation point. The hook
  expands that distance by the live right margin plus 32 pixels for the large
  sprite footprint, so its real motion and OAM begin beyond the 16:9 edge; it
  is vanilla-identical when wide spawning is disabled.

WS-OAM — emit enemy sprite tiles throughout the right widescreen margin.
  bank_00_D76A is the shared metasprite tile writer used by ordinary enemies.
  Its per-tile X test rejects when (screenX + 16) >= 0x10F, which parks every
  tile whose left edge reaches the native x=255 boundary.  The hook expands
  that comparison limit by the live widescreen margin.  D6A7's existing OAM
  high-table packer then carries screenX bit 8 into the hardware X-high bit,
  so x=256..margin coordinates remain positive instead of wrapping left.

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
  BRK stubs). Gated by SNESRECOMP_WS_STAGE (default off): renderer-side
  BG2 prefill now covers first-visit margins without prematurely swapping
  the guest's shared OBJ graphics at section boundaries.

WS-SHOT-CULL - widen X's projectile lifetime window (X axis only).
  bank_82_80B4 is the common projectile motion/camera test used by X's
  weapons: cull when (shotX - $1E4D + 0x20) >= 0x140, i.e. keep window
  = camera -32..+287. The snippet recomputes the carry verdict through
  MmxWsShotCullVerdictX, widening both sides by the live margin while
  remaining equivalent in 4:3. Its Y-axis test is untouched.

WS-CHRBIND - observe (never re-derive) the OAM tile-base bind for the
  margin-enemy garbled-CHR fix (see ISSUES.md "Widescreen margin-enemy
  garbled CHR", fix attempt #2). The OAM tile-base/palette bind sequence
  -- read WRAM $7F:8200+X (tile-base) -> store [D+0x18]; read $7F:8300+X
  (palette) -> store [D+0x11] -- is INLINED per enemy type across the
  generated code, not a single shared function: bank_82_827D_M1X1
  (src/gen/bank82_part00_v2.c:14370-14526) is only ONE of (verified by
  inspection, 2026-08-06) 8 such inlined copies:
    bank_82_827D_M1X1  bank82_part00_v2.c
    bank_82_EDA5_M1X1  bank82_part0d_v2.c
    bank_82_F554_M1X1  bank82_part0e_v2.c
    bank_83_F945_M1X1  bank83_part0f_v2.c
    bank_87_B824_M1X1  bank87_part07_v2.c
    bank_87_F42A_M1X1  bank87_part0e_v2.c
    bank_87_F477_M1X1  bank87_part0e_v2.c (stores complete before its
                        tail-call into bank_84_8F07_M1X1 -- the bind is
                        NOT split across the call)
    bank_88_EAA4_M1X1  bank88_part0d_v2.c
  If the bind fires before the allocator has written that slot for this
  level section, the object latches a stale/zero tile-base forever (the
  confirmed root cause of the garbled margin mechs, including the
  flying-mech family gfx 0x10/0x11/0x12 which binds through one of the
  non-827D inlined copies above -- never through 827D, which is why the
  original single-site fix was partial). Fix attempt #1 tried to gate
  admission on the *value* at the slot and was rejected: zero is a real,
  legitimate tile-base (page 0), so "not yet written" and "written zero"
  are indistinguishable from the value alone, and every enemy whose real
  base is page 0 (or any spawn record whose layout didn't match the
  transcription) was permanently vetoed from the wide spawn pass, undoing
  WS-SPAWN's whole point.

  This pass is pattern-based, not per-function-name: it anchors on the
  tile-base *read* itself (`cpu_read8(cpu, ..., 0x7f8200 + cpu->X, ...)`,
  emitted identically at every site) and scans forward a bounded window
  for that same bind's tile-base *store* (`cpu->D + 0x0018`), injecting
  the host call immediately after the store. Per-site inspection found
  the sequence is uniform through the tile-base store but NOT beyond it:
  most sites do a plain palette store, but bank_82_EDA5, bank_83_F945 and
  bank_87_F42A instead do an ORA-style read-modify-write for the palette
  half (`_m = read[D+0x11]; write[D+0x11] = _m | A`), merging the table's
  palette nibble into pre-existing high bits of that byte (other object
  flags share D+0x11). Because of that, this pass anchors and injects on
  the tile-base store ONLY -- see MmxWsChrRebindSweep (mmx_rtl.c) for why
  the host mechanism was correspondingly changed to heal only the
  tile-base and never replays a raw palette byte.

  Excluded on purpose: bank_87_ABAE_M1X1 (bank87_part05_v2.c) and
  bank_87_D01D_M1X1 (bank87_part0a_v2.c) each read the $7F:8300 palette
  table but NEVER $7F:8200 (verified: neither file contains a 0x7f8200
  read at all) -- they are palette-only refreshers with no tile-base
  store to latch, so the tile-base-read anchor naturally never matches
  them; they must not get a latch (nothing to heal, and latching a
  palette-only refresh would just be dead weight).

  At the injection point cpu->D (the object base) and cpu->X (the game's
  own VRAM-CHR slot index, used unmodified for the tile-base read) still
  hold the exact values the game itself used for this bind -- verified
  per-site that nothing between the anchor and the tile-base store
  reassigns cpu->X or cpu->D (the pass also checks this live and abandons
  an anchor if it ever finds otherwise, for regen-drift safety). The host
  (MmxWsChrBindNote/MmxWsChrRebindSweep, mmx_rtl.c) latches those values
  plus the slot's WRAM entry at bind time, and once per frame checks
  whether the allocator has since written a *different* value into that
  slot; if so, it replays the tile-base store with the new value. This
  tracks allocation VALIDITY via change-detection instead of the value's
  zero-ness, so it never misclassifies a legitimate page-0 bind, and it
  never touches the wide admission path at all (unlike attempt #1), so
  the WS-SPAWN margin-entry regression it caused cannot recur here.

Usage:
    python tools/apply_overrides.py [--gen-dir src/gen] [--check] [-v]
    python tools/apply_overrides.py --restore [--gen-dir src/gen] [-v]
"""
import argparse
import glob
import os
import re
import sys

MARKERS = ("/*WS-CULL*/", "/*WS-SHOT-CULL*/", "/*WS-SPAWN*/", "/*WS-SPAWN-PASS*/", "/*WS-ACTIVATE*/",
           "/*WS-OAM*/", "/*WS-OAM-L*/", "/*WS-LOOKAHEAD*/", "/*WS-STAGE*/",
           "/*WS-SHADOW*/", "/*WS-CHRBIND*/")

RE_TRACE = re.compile(r"cpu_trace_block\(cpu, (0x[0-9A-Fa-f]+)\)")
RE_STORE00 = re.compile(
    r"^(\s*)cpu_write16\(cpu, (0x(?:00|7E)), "
    r"\(uint16\)\(cpu->D \+ 0x0000\), (_v\d+)\);\s*$")
RE_CONST180 = re.compile(r"^\s*uint16 (_v\d+) = 0x180;\s*$")
RE_CONST140 = re.compile(r"^\s*uint16 (_v\d+) = 0x140;\s*$")
RE_CONST80 = re.compile(r"^(\s*)uint16 (_v\d+) = 0x80;\s*$")
RE_CONST10F = re.compile(r"^(\s*)uint16 (_v\d+) = 0x10f;\s*$")
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


def spawn_snippet(indent, bank, var, which):
    return (f"{indent}/*WS-SPAWN*/ {{ extern uint16 MmxWsSpawnAnchor{which}"
            f"(uint16); cpu_write16(cpu, {bank}, (uint16)(cpu->D + 0x0000), "
            f"MmxWsSpawnAnchor{which}((uint16)({var}))); }}\n")


def cull_snippet(indent, var):
    return (f"{indent}/*WS-CULL*/ {{ extern uint16 MmxWsCullVerdictX(uint16);"
            f" cpu->_flag_C = MmxWsCullVerdictX((uint16)({var})); }}\n")


def shot_cull_snippet(indent, var):
    return (f"{indent}/*WS-SHOT-CULL*/ {{ extern uint16 "
            f"MmxWsShotCullVerdictX(uint16); cpu->_flag_C = "
            f"MmxWsShotCullVerdictX((uint16)({var})); }}\n")


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
            out.append(spawn_snippet(
                m.group(1), m.group(2), m.group(3), which))
            n += 1
            if verbose:
                print(f"  WS-SPAWN {which} after line {len(out) - 1} "
                      f"(block {cur_block:#08x})")
    return out, n


RE_SPAWN_TYPE_READ = re.compile(
    r"^(\s*)uint8 (_v\d+) = cpu_read8\(cpu, cpu->DB, \(uint16\)\("
    r"cpu_read16\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x0018\)\)\)\);\s*$")
RE_SPAWN_POST_CALL = re.compile(
    r"^(\s*)goto L_DC92_M0X0; /\* implicit fall-through \*/\s*$")


def apply_bank00_spawn_pass(lines, verbose):
    """Filter the wide DCDB pass to type-3 enemies, then run a native pass."""
    out = []
    cur_fn = None
    cur_block = None
    n = 0
    for line in lines:
        m = re.match(r"^RecompReturn (bank_00_[0-9A-F]+)_M\dX\d\(CpuState", line)
        if m:
            cur_fn = m.group(1)
            cur_block = None
        elif line.startswith("RecompReturn "):
            cur_fn = None
            cur_block = None
        m = RE_TRACE.search(line)
        if m:
            cur_block = canon_pc24(int(m.group(1), 16))

        if cur_fn == "bank_00_DCDB" and cur_block == 0x00DD2D:
            m = RE_SPAWN_TYPE_READ.match(line)
            if m:
                out.append(line)
                out.append(
                    f"{m.group(1)}/*WS-SPAWN-PASS*/ {{ extern int "
                    f"MmxWsSpawnRecordAllowed(uint16, uint8); if "
                    f"(!MmxWsSpawnRecordAllowed(cpu->D, {m.group(2)})) "
                    f"goto L_DD94_M1X0; }}\n")
                n += 1
                continue

        if cur_fn == "bank_00_DC36" and cur_block == 0x00DC81:
            m = RE_SPAWN_POST_CALL.match(line)
            if m:
                out.append(
                    f"{m.group(1)}/*WS-SPAWN-PASS*/ {{ extern void "
                    f"MmxWsSpawnRunNativePass(CpuState *); "
                    f"MmxWsSpawnRunNativePass(cpu); }}\n")
                out.append(line)
                n += 1
                continue
        out.append(line)
    if verbose and n:
        print(f"  WS-SPAWN-PASS injected {n} site(s)")
    return out, n


def oam_limit_snippet(indent, var):
    return (f"{indent}/*WS-OAM*/ {{ extern uint16 MmxWsOamRightLimit(uint16); "
            f"{var} = MmxWsOamRightLimit({var}); }}\n")


def apply_bank00_oam(lines, verbose):
    """Widen bank_00_D76A's shared per-metasprite-tile right-edge gate."""
    out = []
    cur_fn = None
    cur_block = None
    n = 0
    for line in lines:
        out.append(line)
        m = re.match(r"^RecompReturn (bank_00_D76A)_M\dX\d\(CpuState", line)
        if m:
            cur_fn = m.group(1)
            cur_block = None
            continue
        if line.startswith("RecompReturn "):
            cur_fn = None
            cur_block = None
            continue
        m = RE_TRACE.search(line)
        if m:
            cur_block = canon_pc24(int(m.group(1), 16))
            continue
        if cur_fn == "bank_00_D76A" and cur_block == 0x00D79F:
            m = RE_CONST10F.match(line)
            if m:
                out.append(oam_limit_snippet(m.group(1), m.group(2)))
                n += 1
                if verbose:
                    print(f"  WS-OAM after line {len(out) - 1} "
                          f"(bank_00_D76A, {m.group(2)})")
    return out, n


RE_OAM_LIMIT_INJ = re.compile(
    r"MmxWsOamRightLimit\(uint16\); (_v\d+) = MmxWsOamRightLimit")


def apply_bank00_oam_left(lines, verbose):
    """Replace bank_00_D76A's unsigned X-gate verdict so left-margin
    sprites are emitted too. The vanilla verdict is one unsigned compare,
    C = (screenX + 0x10) >= limit; negative screen X wraps high and
    always rejects, i.e. sprites vanish at the native LEFT edge no
    matter how far WS-OAM widens the right limit. Runs after the WS-OAM
    pass (its own applier + marker so it also lands on gen that already
    carries WS-OAM), keying on the injected right-limit line to learn
    the limit variable."""
    out = []
    limit_var = None
    n = 0
    for line in lines:
        out.append(line)
        m = RE_OAM_LIMIT_INJ.search(line)
        if m:
            limit_var = m.group(1)
            continue
        if limit_var:
            m = re.match(
                r"^(\s*)cpu->_flag_C = \(\((_v\d+) & 0xFFFF\) >= "
                r"\((" + limit_var + r") & 0xFFFF\)\) \? 1 : 0;", line)
            if m:
                out.append(
                    f"{m.group(1)}/*WS-OAM-L*/ {{ extern uint16 "
                    f"MmxWsOamXReject(uint16, uint16); cpu->_flag_C = "
                    f"MmxWsOamXReject((uint16)({m.group(2)}), "
                    f"(uint16)({limit_var})); }}\n")
                limit_var = None
                n += 1
                if verbose:
                    print(f"  WS-OAM-L after line {len(out) - 1} "
                          f"(bank_00_D76A, {m.group(2)})")
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


def activation_snippet(indent, var):
    return (f"{indent}/*WS-ACTIVATE*/ {{ extern uint16 "
            f"MmxWsEnemyActivationDistance(uint16); {var} = "
            f"MmxWsEnemyActivationDistance({var}); }}\n")


def apply_bank82_shot_cull(lines, verbose):
    """Widen the common projectile X-axis lifetime test in bank_82_80B4."""
    out = []
    cur_block = None
    pend_val = None
    state = 0  # 0 idle, 1 saw 0x140 const, 2 have compared value var
    n = 0
    for line in lines:
        m = RE_TRACE.search(line)
        if m:
            cur_block = canon_pc24(int(m.group(1), 16))
            state = 0
        if cur_block == 0x0280B4:
            if state == 0 and RE_CONST140.match(line):
                state = 1
            elif state == 1:
                mv = RE_READ_A.match(line)
                if mv:
                    pend_val = mv.group(1)
                    state = 2
            elif state == 2:
                mb = RE_BRANCH_C.match(line)
                if mb:
                    out.append(shot_cull_snippet(mb.group(1), pend_val))
                    n += 1
                    if verbose:
                        print(f"  WS-SHOT-CULL before line {len(out)} "
                              f"(block 0x0280B4, {pend_val})")
                    state = 0
        out.append(line)
    return out, n


def apply_bank82_activation(lines, verbose):
    """Widen the Chill Penguin intro helicopter's player-distance gate.

    bank_82_B964 holds the helicopter just above the viewport until its
    controller is 0x80 pixels ahead of X.  That is exactly the native
    viewport entrance point.  The helper adds the live right margin and the
    large sprite's lead distance while preserving 0x80 when widescreen
    spawning is disabled.
    """
    out = []
    cur_block = None
    n = 0
    for line in lines:
        out.append(line)
        m = RE_TRACE.search(line)
        if m:
            cur_block = canon_pc24(int(m.group(1), 16))
            continue
        if cur_block == 0x02B964:
            m = RE_CONST80.match(line)
            if m:
                out.append(activation_snippet(m.group(1), m.group(2)))
                n += 1
                if verbose:
                    print(f"  WS-ACTIVATE after line {len(out) - 1} "
                          f"(block 0x02B964, {m.group(2)})")
    return out, n


RE_CHRBIND_ANCHOR = re.compile(
    r"^\s*uint8 (_v\d+) = cpu_read8\(cpu, \(uint8\)\(\(\(\(uint32\)0x7f8200 "
    r"\+ \(uint32\)cpu->X\)\) >> 16\), \(uint16\)\(\(\(uint32\)0x7f8200 \+ "
    r"\(uint32\)cpu->X\)\)\);\s*$")
RE_CHRBIND_STORE_D18 = re.compile(
    r"^(\s*)cpu_write8\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x0018\), _v\d+\);\s*$")
# Defensive regen-drift guards: if cpu->X or cpu->D get reassigned between
# the anchor and the tile-base store, the injection point's premise (X/D
# still hold this bind's own values) is broken and the anchor must be
# abandoned rather than silently injecting a wrong observation.
RE_CHRBIND_X_ASSIGN = re.compile(r"cpu->X\s*=[^=]")
RE_CHRBIND_D_ASSIGN = re.compile(r"cpu->D\s*=[^=]")

# Bounded forward-scan window (lines) from the anchor to the tile-base
# store. Measured across all 8 real sites the store lands 6-7 lines after
# the anchor (a fixed few flag-update lines emitted for the LDA and the
# STA); ~40 gives generous headroom for regen drift without being large
# enough to wander into an unrelated store.
CHRBIND_WINDOW = 40
# Verified 2026-08-06 by per-site inspection (see the module docstring):
# exactly 8 real tile-base bind sites match the anchor across src/gen.
EXPECTED_CHRBIND_SITES = 8
EXPECTED_CHRBIND_SITE_NAMES = (
    "bank_82_827D_M1X1", "bank_82_EDA5_M1X1", "bank_82_F554_M1X1",
    "bank_83_F945_M1X1", "bank_87_B824_M1X1", "bank_87_F42A_M1X1",
    "bank_87_F477_M1X1", "bank_88_EAA4_M1X1",
)


def chrbind_snippet(indent):
    return (f"{indent}/*WS-CHRBIND*/ {{ extern void MmxWsChrBindNote(uint16, "
            f"uint16); MmxWsChrBindNote(cpu->D, cpu->X); }}\n")


def apply_chrbind_generic(lines, verbose):
    """Pattern-based WS-CHRBIND pass: find every inlined OAM tile-base bind
    (the `0x7f8200 + cpu->X` read, common to all 8 verified sites) and
    inject the host observability hook immediately after that same bind's
    tile-base store (`cpu->D + 0x0018`). See the module docstring's
    WS-CHRBIND section for why the injection point is the tile-base store
    and not the (non-uniform) palette store."""
    out = []
    n = 0
    pending_var = None
    pending_age = 0
    for idx, line in enumerate(lines):
        out.append(line)
        if pending_var is not None:
            pending_age += 1
            if RE_CHRBIND_X_ASSIGN.search(line) or RE_CHRBIND_D_ASSIGN.search(line):
                if verbose:
                    print(f"  WS-CHRBIND: cpu->X/D reassigned before the "
                          f"tile-base store near line {idx + 1}; abandoning "
                          f"this anchor")
                pending_var = None
            elif pending_age > CHRBIND_WINDOW:
                if verbose:
                    print(f"  WS-CHRBIND: no tile-base store found within "
                          f"{CHRBIND_WINDOW} lines of the anchor near line "
                          f"{idx + 1}; abandoning this anchor")
                pending_var = None
            else:
                m = RE_CHRBIND_STORE_D18.match(line)
                if m:
                    already = (idx + 1 < len(lines) and
                               "/*WS-CHRBIND*/" in lines[idx + 1])
                    if not already:
                        out.append(chrbind_snippet(m.group(1)))
                        if verbose:
                            print(f"  WS-CHRBIND after line {len(out) - 1}")
                    n += 1
                    pending_var = None
        if pending_var is None:
            m = RE_CHRBIND_ANCHOR.match(line)
            if m:
                pending_var = m.group(1)
                pending_age = 0
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
    appliers = (
        (apply_bank00, "/*WS-SPAWN*/"),
        (apply_bank00_spawn_pass, "/*WS-SPAWN-PASS*/"),
        (apply_bank00_oam, "/*WS-OAM*/"),
        (apply_bank00_oam_left, "/*WS-OAM-L*/"),
        (apply_bank02, "/*WS-CULL*/"),
        (apply_bank82_shot_cull, "/*WS-SHOT-CULL*/"),
        (apply_bank82_activation, "/*WS-ACTIVATE*/"),
        (apply_bank03, "/*WS-STAGE*/"),
        (apply_chrbind_generic, "/*WS-CHRBIND*/"),
    )
    paths = sorted(glob.glob(os.path.join(args.gen_dir, "bank*_v2.c")))
    if not paths:
        print(f"no bank*_v2.c under {args.gen_dir}", file=sys.stderr)
        return 1
    total = 0
    patched_files = 0
    effective_counts = {}
    for path in paths:
        name = os.path.basename(path)
        if args.restore:
            n = restore_file(path, args.verbose)
            if n:
                print(f"{name}: removed {n} injected line(s)")
            total += n
            continue
        with open(path, "r", encoding="utf-8") as f:
            contents = f.read()
        n_file = 0
        already = 0
        for fn, marker in appliers:
            with open(path, "r", encoding="utf-8") as f:
                contents = f.read()
            if marker in contents:
                effective_counts[marker] = (
                    effective_counts.get(marker, 0) + contents.count(marker))
                already += 1
                continue
            n = process(path, fn, args.check, args.verbose)
            effective_counts[marker] = effective_counts.get(marker, 0) + n
            n_file += n
        if n_file:
            verb = "would inject" if args.check else "injected"
            print(f"{name}: {verb} {n_file} site(s)")
        elif already:
            patched_files += 1
        total += n_file
    if args.restore:
        print(f"restore: removed {total} injected line(s) total")
    if not args.restore and effective_counts.get("/*WS-SPAWN*/", 0) != 2:
        print(
            "ERROR: expected exactly 2 WS-SPAWN anchor hooks, found "
            f"{effective_counts.get('/*WS-SPAWN*/', 0)}",
            file=sys.stderr)
        return 1
    chrbind_found = effective_counts.get("/*WS-CHRBIND*/", 0)
    if not args.restore and chrbind_found != EXPECTED_CHRBIND_SITES:
        print(
            f"ERROR: expected exactly {EXPECTED_CHRBIND_SITES} WS-CHRBIND "
            f"anchor hooks, found {chrbind_found}. Expected sites: "
            f"{', '.join(EXPECTED_CHRBIND_SITE_NAMES)}",
            file=sys.stderr)
        return 1
    if not args.restore and total == 0 and patched_files == 0:
        print("WARNING: no anchor sites matched", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
