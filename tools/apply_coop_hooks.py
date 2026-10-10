#!/usr/bin/env python3
"""Keep the co-op controller boundary authoritative under generated dispatch."""
import argparse
from pathlib import Path
import re

MARKER = '/*MMX-COOP*/'
TARGETS = {0x01812e, 0x048fca, 0x0280b4, 0xd2bd, 0xd3dd, 0xd3fa, 0xd43a, 0xd457,
           0x049b03, 0x049b43, 0x019d67, 0xde9d, 0xdebc, 0xe543, 0x049c0e,
           0x018a5c, 0x018a92, 0x018add, 0x9d9e, 0x01e70d, 0x01ec98, 0xd48d,
           0xde40, 0x03a8bd, 0x04ab81, 0x04ab56, 0x07c07a, 0xf478, 0x019c70, 0x0897ba,
           0x079794, 0x07add8, 0x07af10, 0x07b91c, 0x07ba72, 0x07ba44, 0x07bb09, 0x02c715,
           0x07dabd, 0x08c333,
           # $82:E62A: Highway's falling slab, one-frame drop state; slab_drop_hook.
           0x02e62a,
           # $84:9A02: the canister's ($4D) side-contact test; canister_side_hook.
           0x049a02,
           # $83:8129: an empty Ride Armor's idle/boarding state; armor_board_hook.
           0x038129}
VIEW_TARGETS={0x00dc36,0x00dcdb,0x02806e,0x02808f,0x00d4aa}
TARGETS |= VIEW_TARGETS
# Routed through the interpreter when the generated code has them as entries,
# so co-op diagnostics can observe them; a build without one only warns.
# $82:D7D7: enemy rider-contact query (ship lift; probably the E-tank elevator).
OPTIONAL = {0x02d7d7}
# Targets whose interpreter entry depends on the caller's state: the policy
# receives the CPU and is asked at each generated entry. The airship door's
# $81:EC98 state crashes when interpreted during the Storm Eagle lift ride
# (dispatch to $50:D2ED), so the doors enter the interpreter only when
# door_hook can open its pass.
POLICIES = {0x01e70d: 'MmxCoopDoorRouteE70D', 0x01ec98: 'MmxCoopDoorRouteEC98'}


def apply(text):
    output, found = [], set()
    native_pc = 0
    for line in text.splitlines(keepends=True):
        if MARKER in line:
            continue
        output.append(line)
        entry = re.match(r'RecompReturn bank_([0-9A-Fa-f]{2})_([0-9A-Fa-f]{4})_M[01]X[01]\(CpuState \*cpu\) \{', line)
        pc = int(entry[1] + entry[2],16) if entry else 0
        if entry:
            native_pc = pc if (pc & 0x7fffff) in TARGETS | OPTIONAL else 0
        if native_pc and 'g_cpu_entry_s[g_recomp_stack_top - 1] = _entry_s;' in line:
            # Run the generated prologue first: a JMP/JML caller may have
            # supplied an inherited return context. Leaving it pending lets
            # an unrelated compiled child adopt the wrong frame and execute
            # the platform's continuation twice (Highway $82:E9ED).
            key = native_pc & 0x7fffff
            if key in POLICIES:
                decl, test = f'{POLICIES[key]}(const CpuState *)', f'{POLICIES[key]}(cpu)'
            else:
                policy = 'MmxCoopViewsOnline' if key in VIEW_TARGETS else 'MmxCoopEnabled'
                decl, test = f'{policy}(void)', f'{policy}()'
            output.append(f'  {MARKER} {{ extern bool {decl}; if ({test}) {{ RecompReturn r = interp_tier_dispatch_bank_miss(cpu, 0x{native_pc:06x}u, _entry_s, _hrv); RecompStackPop(); return r; }} }}\n')
            found.add(native_pc & 0x7fffff)
            native_pc = 0
    return ''.join(output), found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gen-dir', type=Path, required=True)
    args = parser.parse_args()
    found = set()
    for path in args.gen_dir.glob('*.c'):
        text = path.read_text(encoding='utf-8')
        if not any(f'_{pc & 65535:04X}_' in text for pc in TARGETS | OPTIONAL):
            continue
        updated, count = apply(text)
        found |= count
        if updated != text:
            path.write_text(updated, encoding='utf-8', newline='\n')
    if TARGETS - found:
        raise SystemExit(f'Missing co-op native entries: {TARGETS - found}')
    for pc in sorted(OPTIONAL - found):
        print(f'MMX co-op: optional diagnostic entry ${pc:06X} is not a generated entry; not observed')
    print(f'MMX co-op: {len(found & TARGETS)} native routine boundaries verified'
          f' (+{len(found & OPTIONAL)} optional)')


if __name__ == '__main__':
    main()
