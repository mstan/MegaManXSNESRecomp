#!/usr/bin/env python3
"""Bind X1 convenience controls to their normal compiled instruction owners."""
import argparse
from pathlib import Path
import re

MARKER='/*MMX-QOL*/'
REQUIRED={0x00c723,0x00c93c,0x018775,0x07ca92,0x07ca9a,0x03f5ce,0x03f5d9,
          0x03f602,0x03f60b,0x03f5e9,0x049e99,0x01984a,0x019859,0x01988a}
COUNTERS={0x03f5ce:(0x1a,0x83f5d1),0x03f5d9:(0x1b,0x83f5dc),
          0x03f602:(0x1a,0x83f605),0x03f60b:(0x1b,0x83f60e)}

DECL='extern uint32 MmxQolApplyOpcode(CpuState *, uint32);'

def apply(text):
    lines=[line for line in text.splitlines(keepends=True) if MARKER not in line]
    out=[];found=set();shots=set();pc=0;mode=''
    for index,line in enumerate(lines):
        label=re.match(r'\s*L_[0-9A-F]{4}_(M\dX\d):',line)
        if label:mode=label[1]
        block=re.search(r'cpu_trace_block\(cpu, 0x([0-9a-fA-F]+)\)',line)
        if block:pc=int(block[1],16)&0x7fffff
        if pc==0x018775 and re.search(r'uint16 _v\d+ = 0x178;',line):
            out.append(f'    {MARKER} {{ {DECL} if (MmxQolApplyOpcode(cpu,0x818777)) goto L_8789_M0X1; }}\n')
            found.add(pc)
        if pc==0x07ca92 and 'if (cpu->_flag_C == 0)' in line and 'L_CAC4_' in line:
            out.append(f'    {MARKER} {{ {DECL} if (MmxQolApplyOpcode(cpu,0x87ca98)) goto MMX_HADOUKEN_REQUIREMENTS_{mode}; }}\n')
            found.add(pc)
        if pc==0x07ca9a and re.search(r'uint8 _v\d+ = cpu_read8\(cpu, cpu->DB, \(uint16\)\(0x1f99\)\);',line):
            out.append(f'  {MARKER} MMX_HADOUKEN_REQUIREMENTS_{mode}:;\n')
            found.add(pc)
        if pc in COUNTERS:
            offset,opcode=COUNTERS[pc]
            if re.search(r'uint8 _im = cpu_read8\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x%04x\)\);'%offset,line):
                out.append(f'    {MARKER} {{ {DECL} (void)MmxQolApplyOpcode(cpu,0x{opcode:06x}); }}\n')
                found.add(pc)
        out.append(line)
        read=re.search(r'uint8 (_v\d+) = cpu_read8\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x00(3[7b])\)\);',line)
        if read:
            following=''.join(lines[index+1:index+15])
            mask=re.search(r'uint8 _v\d+ = 0x([0-9a-f]+);',following)
            held=pc in (0x01984a,0x019859,0x01988a) and read[2]=='37'
            shot=0x0182ee<=pc<0x019000 and read[2]=='3b'
            if (held or shot) and mask and int(mask[1],16)==64 and '_bt =' in following:
                out.append(f'    {MARKER} {{ extern unsigned MmxQolNativeShotRead(CpuState *, unsigned, bool); {read[1]}=(uint8)MmxQolNativeShotRead(cpu,{read[1]},{str(held).lower()}); }}\n')
                if held:found.add(pc)
                else:shots.add(pc)
        if 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            target={0x00c723:(0x80c723,'C767'),0x00c93c:(0x80c93f,'C94E')}.get(pc)
            if target:
                opcode,destination=target
                out.append(f'    {MARKER} {{ extern uint32 MmxQolApplyOpcode(CpuState *, uint32); if (MmxQolApplyOpcode(cpu,0x{opcode:06x})) goto L_{destination}_{mode}; }}\n')
                found.add(pc)
            if pc==0x03f5e9:
                out.append(f'    {MARKER} {{ extern uint32 MmxQolApplyOpcode(CpuState *, uint32); (void)MmxQolApplyOpcode(cpu,0x83f5e9); }}\n')
                found.add(pc)
        if pc==0x049e99 and 'cpu_write8' in line and 'cpu->D + 0x0027' in line:
            out.append(f'    {MARKER} {{ extern uint32 MmxQolApplyOpcode(CpuState *, uint32); (void)MmxQolApplyOpcode(cpu,0x849e9d); }}\n')
            found.add(pc)
    return ''.join(out),found,shots

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gen-dir',type=Path,required=True)
    args=parser.parse_args();found=set();shots=set()
    for path in args.gen_dir.glob('bank*_v2.c'):
        text=path.read_text(encoding='utf-8')
        updated,sites,fire=apply(text);found|=sites;shots|=fire
        if updated!=text:path.write_text(updated,encoding='utf-8',newline='\n')
    if REQUIRED-found:raise SystemExit(f'Missing compiled QoL owners: {REQUIRED-found}')
    if len(shots)!=12:raise SystemExit(f'Expected twelve normal Shot readers, found {len(shots)}: {sorted(shots)}')
    print(f'MMX QoL: {len(found)} native control blocks and {len(shots)} Shot readers verified')

if __name__=='__main__':main()
