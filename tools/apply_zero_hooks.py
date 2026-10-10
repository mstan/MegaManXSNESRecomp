#!/usr/bin/env python3
"""Insert bounded, disabled-by-default Zero hooks in regenerated X1 code."""
import argparse
from pathlib import Path
import re

MARKER = '/*MMX-ZERO*/'
PCS = {0x81971c, 0x819793, 0x8198fc}
MUZZLE_PCS = {0x81a566, 0x81a578, 0x838b6a, 0x838d82, 0x838eb4,
              0x839518, 0x83983c, 0x839974, 0x83a3a9}
ORIGIN_PCS = {0x8283ed: 1, 0x83958c: 0, 0x839dc5: 1}
RESPONSE_BLOCKS = {0x849e15, 0x049e15}
RESPONSE_PCS = {0x849e45, 0x049e45}
REQUIRED = PCS | MUZZLE_PCS | ORIGIN_PCS.keys() | RESPONSE_PCS | {0x009da6, 0x81815c, 0x818165, 0x00d3e5, 0x849e15, 0x849e3a, 0x849e73, 0x849c16, 0x848f07, 0x848eea, 0x8491db, 0x82823e, 0x8194af, 0x818ae8}
OPTIONAL = {0x00d4f2, 0x00d50f}  # Current enemy loops run through the interpreter.
REQUIRED |= {0x049e15, 0x049e3a}  # Compiled low-bank mirror of native contact.
# Dash exits that would stand Zero up, and the dash blocks that continue it.
SLIDE_PCS = {0x81898e: '8991', 0x818999: '899C', 0x818965: '8971'}
REQUIRED |= SLIDE_PCS.keys() | {0x81a062}


def apply(text):
    lines = [line for line in text.splitlines(keepends=True) if MARKER not in line]
    output, found, pc, mode = [], set(), 0, ''
    for line in lines:
        label = re.match(r'\s*L_[0-9A-F]{4}_(M\dX\d):', line)
        if label:
            mode = label[1]
        block = re.search(r'cpu_trace_block\(cpu, 0x([0-9A-Fa-f]+)\);', line)
        if block:
            pc = int(block[1], 16)
        if pc == 0x818ae8 and 'cpu_write16' in line and '0x0008' in line:
            output.append(line)
            output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern void MmxZeroDeathOrbSpawn(uint8_t *, unsigned, unsigned); MmxZeroDeathOrbSpawn(g_ram, cpu->D, cpu->X); }}\n')
            found.add(pc)
            continue
        if pc in ORIGIN_PCS:
            axis = ORIGIN_PCS[pc]
            store = re.search(r'cpu_write16\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x000([58])\), (_v\d+)\);', line)
            if store and int(store[1] == '8') == axis:
                output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern unsigned MmxZeroWeaponOrigin(const uint8_t *, unsigned, unsigned, unsigned); {store[2]} = (uint16)MmxZeroWeaponOrigin(g_ram, cpu->D, {axis}, {store[2]}); cpu_write_a_m(cpu, {store[2]}); }}\n')
                found.add(pc)
        output.append(line)
        if (pc & 0x7fffff) == 0x049e15:
            load = re.search(r'uint16 (_v\d+) = cpu_read16\(cpu,', line) if '0x000a' in line and 'cpu->X' in line else None
            if load:
                output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern unsigned MmxWeaponsContactClass(const uint8_t *, unsigned, unsigned, unsigned, bool); {load[1]} = (uint16)MmxWeaponsContactClass(g_ram,cpu->D,cpu->X,{load[1]},false); }}\n')
                found.add(pc)
            if 'cpu_write8' in line and '(0x1f1d)' in line:
                output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern unsigned MmxWeaponsContactClass(const uint8_t *, unsigned, unsigned, unsigned, bool); cpu_write8(cpu,cpu->DB,0x1f1d,(uint8)MmxWeaponsContactClass(g_ram,cpu->D,cpu->X,g_ram[0x1f1d],true)); }}\n')
                found.add(pc + 0x25)
        if pc in RESPONSE_BLOCKS:
            response = re.search(r'uint8 (_v\d+) = cpu_read8\(cpu,.*0xef37', line)
            if response:
                output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern unsigned MmxZeroResponse(uint8_t *, unsigned, unsigned, unsigned); {response[1]} = (uint8)MmxZeroResponse(g_ram, cpu->D, cpu->X, {response[1]}); }}\n')
                found.add(pc + 0x30)
        if pc in (0x00d4f2,0x00d50f):
            load = re.search(r'uint8 (_v\d+) = cpu_read8\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x0000\)\);', line)
            if load:
                output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern unsigned MmxWeaponsEnemyActive(const uint8_t *, unsigned, unsigned); {load[1]} = (uint8)MmxWeaponsEnemyActive(g_ram,cpu->D,{load[1]}); }}\n')
                found.add(pc)
        if pc == 0x8194af and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern void MmxWeaponsSelectShot(const uint8_t *, unsigned); MmxWeaponsSelectShot(g_ram,cpu->X); }}\n')
            found.add(pc)
        if pc == 0x82823e and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern void MmxZeroPlayerMotion(uint8_t *, unsigned); extern void MmxWeaponsPlayerMotion(uint8_t *, unsigned); MmxZeroPlayerMotion(g_ram,cpu->D); MmxWeaponsPlayerMotion(g_ram,cpu->D); }}\n')
            found.add(pc)
        if pc == 0x8491db and 'cpu->D = cpu_read16(cpu, 0x00, cpu->S);' in line:
            output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern void MmxWeaponsTerrainEnd(uint8_t *, unsigned); MmxWeaponsTerrainEnd(g_ram,cpu->D); }}\n')
            found.add(pc)
        if pc == 0x009da6 and 'cpu_write8' in line and '0x0bcf' in line:
            output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern void MmxZeroHealthRespawn(const uint8_t *); MmxZeroHealthRespawn(g_ram); }}\n')
            found.add(pc)
        if pc in MUZZLE_PCS:
            load = re.search(r'uint8 (_v\d+) = cpu_read8.*0xbe3([9a])', line)
            if load:
                axis = int(load[2] == 'a')
                output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern unsigned MmxZeroMuzzle(const uint8_t *, unsigned, unsigned, unsigned, unsigned); {load[1]} = (uint8)MmxZeroMuzzle(g_ram, cpu->D, cpu->X, {axis}, {load[1]}); }}\n')
                found.add(pc)
        if pc in (0x848f07, 0x848eea) and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            if pc == 0x848f07:
                output.append(f'    {MARKER} {{ extern void MmxZeroAnimationStart(unsigned, unsigned); MmxZeroAnimationStart(cpu->D, cpu->A & 255); }}\n')
            else:
                output.append(f'    {MARKER} {{ extern void MmxZeroAnimationAdvance(unsigned); MmxZeroAnimationAdvance(cpu->D); }}\n')
            found.add(pc)
        if pc == 0x849c16:
            load = re.search(r'uint16 (_v\d+) = cpu_read16\(cpu,', line) if '0x0020' in line and 'cpu->X' in line else None
            if load:
                output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern unsigned MmxZeroHitbox(const uint8_t *, unsigned, unsigned, unsigned); extern unsigned MmxWeaponsHitbox(const uint8_t *, unsigned, unsigned, unsigned); {load[1]} = (uint16)MmxWeaponsHitbox(g_ram, cpu->D, cpu->X, MmxZeroHitbox(g_ram, cpu->D, cpu->X, {load[1]})); }}\n')
                found.add(pc)
        if pc == 0x81a062 and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern void MmxHadoukenInput(uint8_t *); MmxHadoukenInput(g_ram); }}\n')
            found.add(pc)
        if pc == 0x81815c and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern void MmxZeroExtPrePlayer(uint8_t *); extern void MmxZeroSlideTick(uint8_t *); extern void MmxZeroMovementTick(uint8_t *); extern void MmxZeroPlayerTick(uint8_t *); extern void MmxWeaponsPlayerTick(uint8_t *); extern bool MmxWeaponsCombatActive(void); MmxZeroExtPrePlayer(g_ram); MmxZeroSlideTick(g_ram); MmxZeroMovementTick(g_ram); MmxWeaponsPlayerTick(g_ram); if (!MmxWeaponsCombatActive()) MmxZeroPlayerTick(g_ram); }}\n')
            found.add(pc)
        if pc in SLIDE_PCS and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern bool MmxZeroSlideHold(uint8_t *, unsigned); if (MmxZeroSlideHold(g_ram, 0x{pc:06x})) goto L_{SLIDE_PCS[pc]}_{mode}; }}\n')
            found.add(pc)
        if pc == 0x818165 and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern void MmxZeroPlayerEnd(uint8_t *); extern void MmxZeroExtPlayerEnd(uint8_t *); MmxZeroPlayerEnd(g_ram); MmxZeroExtPlayerEnd(g_ram); }}\n')
            found.add(pc)
        if pc == 0x00d3e5:
            load = re.search(r'uint8 (_v\d+) = cpu_read8\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x0000\)\);', line)
            if load:
                output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern unsigned MmxZeroWeaponTick(uint8_t *, unsigned, unsigned); extern unsigned MmxWeaponsProjectileTick(uint8_t *, unsigned, unsigned); {load[1]} = (uint8)MmxWeaponsProjectileTick(g_ram, cpu->D, MmxZeroWeaponTick(g_ram, cpu->D, {load[1]})); }}\n')
                found.add(pc)
        # The ordinary damage table still decides immunity and reflection.
        # Override only the final subtraction in the positive-damage path.
        if pc == 0x849e6e and 'uint8 ' in line and 'cpu_read8' in line and '0xef37' in line:
            variable = re.search(r'uint8 (_v\d+)', line)[1]
            output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern unsigned MmxZeroDamage(uint8_t *, unsigned, unsigned, unsigned); extern unsigned MmxWeaponsDamage(uint8_t *, unsigned, unsigned, unsigned); {variable} = (uint8)MmxWeaponsDamage(g_ram, cpu->D, cpu->X, MmxZeroDamage(g_ram, cpu->D, cpu->X, {variable})); }}\n')
            found.add(0x849e73)
        if pc in PCS:
            load = re.search(r'uint8 (_v\d+) = cpu_read8\(cpu, cpu->DB, \(uint16\)\(0x1f99\)\);', line)
            if load:
                output.append(f'    {MARKER} {{ extern unsigned MmxZeroUpgradeBits(unsigned, unsigned); {load[1]} = (uint8)MmxZeroUpgradeBits(0x{pc:06x}, {load[1]}); }}\n')
                found.add(pc)
    return ''.join(output), found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gen-dir', type=Path, required=True)
    args = parser.parse_args()
    found = set()
    for path in args.gen_dir.glob('*.c'):
        text = path.read_text(encoding='utf-8')
        if not any(f'0x{pc:06X}' in text for pc in REQUIRED | OPTIONAL | {0x849e6e}):
            continue
        updated, sites = apply(text)
        found |= sites
        if updated != text:
            path.write_text(updated, encoding='utf-8', newline='\n')
    if REQUIRED - found:
        raise SystemExit(f'Missing Zero hooks: {REQUIRED - found}')
    print(f'MMX Zero: {len(found)} capability/combat sites verified')


if __name__ == '__main__':
    main()
