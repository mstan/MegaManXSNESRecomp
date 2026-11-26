"""Bind the existing mutable mod registry to generated native instructions."""
import argparse
from pathlib import Path
import re

MARKER = '/*MMX-NATIVE-HOOK*/'
INCLUDE = '#include "snes/interp_bridge.h" /*MMX-NATIVE-HOOK-INCLUDE*/\n'
# Existing pre-instruction native effects corresponding to ZeroPlugin's
# reference callback. Ownership is granted only when the full effect is
# actually present in THIS emitted PC/MX block, before its cycle charge.
ZERO_EFFECTS = {
    0x00d6a7: ('MmxRendererObserveObject',),
    0x00d76a: ('MmxRendererRecordPiece',),
    0x01815c: ('MmxZeroExtPrePlayer', 'MmxZeroSlideTick', 'MmxZeroMovementTick',
               'MmxWeaponsPlayerTick', 'MmxWeaponsCombatActive', 'MmxZeroPlayerTick'),
    0x018165: ('MmxZeroPlayerEnd', 'MmxZeroExtPlayerEnd'),
    0x048f07: ('MmxZeroAnimationStart',),
    0x048eea: ('MmxZeroAnimationAdvance',),
    0x02823e: ('MmxZeroPlayerMotion', 'MmxWeaponsPlayerMotion'),
    0x0194af: ('MmxWeaponsSelectShot',),
    0x01898e: ('MmxZeroSlideHold',),
    0x018999: ('MmxZeroSlideHold',),
    0x018965: ('MmxZeroSlideHold',),
}


def apply(source):
    source = ''.join(line for line in source.splitlines(keepends=True)
                     if MARKER not in line and '/*MMX-NATIVE-HOOK-INCLUDE*/' not in line)
    lines = source.splitlines(keepends=True)
    insertions = {}
    pc = mx = None
    count = 0
    for index, line in enumerate(lines):
        label = re.match(r'\s*L_([0-9A-Fa-f]{4})_M([01])X([01]):', line)
        if label:
            mx = int(label[2]) * 2 + int(label[3])
        trace = re.search(r'cpu_trace_block\(cpu, 0x([0-9A-Fa-f]+)\);', line)
        if trace:
            pc = int(trace[1], 16)
        # The producer emits this after its frame/deadline check and before
        # charging the instruction. A suspended boundary must not run a hook.
        if line.strip() == 'cpu->coprocessor_master_cycles = cpu->master_cycles;':
            if pc is None or mx is None:
                raise ValueError('Native hook boundary has no exact PC/M/X')
            after = None
            required = ZERO_EFFECTS.get(pc & 0x7fffff)
            if required:
                for following in range(index + 1, len(lines)):
                    candidate = lines[following]
                    if ('cpu_trace_block(' in candidate or 'cpu->cycles +=' in candidate
                            or candidate.startswith('RecompReturn ')):
                        break
                    if '/*MMX-' in candidate and all(name + '(' in candidate for name in required):
                        after = following
                        break
            owned = after is not None
            insertion = after + 1 if owned else index
            code = f'    {MARKER} if (interp_bridge_native_hooks_armed(0x{pc:06X}u)) {{ extern bool MmxNativeGuestHook(CpuState *, uint32_t, unsigned, bool, uint32_t *); extern RecompReturn MmxNativeHookTail(CpuState *, uint32_t, uint16_t, uint8_t); uint32_t target; if (MmxNativeGuestHook(cpu, 0x{pc:06X}u, {mx}u, {str(owned).lower()}, &target)) {{ RecompStackPop(); return MmxNativeHookTail(cpu, target, _entry_s, _hrv); }} }}\n'
            insertions.setdefault(insertion, []).append(code)
            count += 1
    out = []
    for index, line in enumerate(lines):
        out.extend(insertions.get(index, ()))
        out.append(line)
    out.extend(insertions.get(len(lines), ()))
    return (INCLUDE if count else '') + ''.join(out), count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--gen-dir', type=Path, required=True)
    args = parser.parse_args()
    count = 0
    for path in sorted(args.gen_dir.glob('bank*_v2.c')):
        source = path.read_text(encoding='utf8')
        patched, seen = apply(source)
        if patched != source:
            path.write_text(patched, encoding='utf8', newline='\n')
        count += seen
    if not count:
        raise SystemExit('No native instruction boundaries were emitted')
    print(f'Native mutable mod hooks: {count} instruction boundaries')


if __name__ == '__main__':
    main()
