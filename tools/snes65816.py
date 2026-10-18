"""Small LoROM 65816 tracer for relocating X2/X3 code into X1.

Recursive descent from entry points with M/X width tracking. Jump tables
behind `JMP (abs,x)` / `JSR (abs,x)` are followed while their 16-bit entries
point at code in the same bank. Used by tools/relocate_boss.py; it reads a
user-supplied ROM and emits addresses only.
"""
from dataclasses import dataclass
import hashlib
from pathlib import Path

NAMES = ("BRK ORA COP ORA TSB ORA ASL ORA PHP ORA ASL PHD TSB ORA ASL ORA "
         "BPL ORA ORA ORA TRB ORA ASL ORA CLC ORA INC TCS TRB ORA ASL ORA "
         "JSR AND JSL AND BIT AND ROL AND PLP AND ROL PLD BIT AND ROL AND "
         "BMI AND AND AND BIT AND ROL AND SEC AND DEC TSC BIT AND ROL AND "
         "RTI EOR WDM EOR MVP EOR LSR EOR PHA EOR LSR PHK JMP EOR LSR EOR "
         "BVC EOR EOR EOR MVN EOR LSR EOR CLI EOR PHY TCD JML EOR LSR EOR "
         "RTS ADC PER ADC STZ ADC ROR ADC PLA ADC ROR RTL JMP ADC ROR ADC "
         "BVS ADC ADC ADC STZ ADC ROR ADC SEI ADC PLY TDC JMP ADC ROR ADC "
         "BRA STA BRL STA STY STA STX STA DEY BIT TXA PHB STY STA STX STA "
         "BCC STA STA STA STY STA STX STA TYA STA TXS TXY STZ STA STZ STA "
         "LDY LDA LDX LDA LDY LDA LDX LDA TAY LDA TAX PLB LDY LDA LDX LDA "
         "BCS LDA LDA LDA LDY LDA LDX LDA CLV LDA TSX TYX LDY LDA LDX LDA "
         "CPY CMP REP CMP CPY CMP DEC CMP INY CMP DEX WAI CPY CMP DEC CMP "
         "BNE CMP CMP CMP PEI CMP DEC CMP CLD CMP PHX STP JML CMP DEC CMP "
         "CPX SBC SEP SBC CPX SBC INC SBC INX SBC NOP XBA CPX SBC INC SBC "
         "BEQ SBC SBC SBC PEA SBC INC SBC SED SBC PLX XCE JSR SBC INC SBC").split()

_SPECIAL = {0x00: 'imm8', 0x02: 'imm8', 0x20: 'abs', 0x22: 'long', 0x40: 'imp', 0x42: 'imm8',
            0x44: 'mv', 0x54: 'mv', 0x60: 'imp', 0x62: 'rel16', 0x6c: '(abs)', 0x7c: '(abs,x)',
            0x80: 'rel', 0x82: 'rel16', 0x89: 'immm', 0xa0: 'immx', 0xa2: 'immx', 0xc0: 'immx',
            0xe0: 'immx', 0xc2: 'imm8', 0xe2: 'imm8', 0xd4: 'dp', 0xdc: '[abs]', 0xf4: 'abs',
            0xfc: '(abs,x)', 0x5c: 'long', 0x4c: 'abs', 0x14: 'dp', 0x1c: 'abs', 0x04: 'dp',
            0x0c: 'abs', 0x64: 'dp', 0x74: 'dp,x', 0x9c: 'abs', 0x9e: 'abs,x', 0x96: 'dp,y',
            0xb6: 'dp,y', 0xbe: 'abs,y', 0x34: 'dp,x', 0x3c: 'abs,x', 0x24: 'dp', 0x2c: 'abs',
            0x94: 'dp,x', 0xb4: 'dp,x', 0xbc: 'abs,x', 0x84: 'dp', 0x8c: 'abs', 0xa4: 'dp',
            0xac: 'abs', 0xc4: 'dp', 0xcc: 'abs', 0xe4: 'dp', 0xec: 'abs', 0x86: 'dp',
            0x8e: 'abs', 0xa6: 'dp', 0xae: 'abs'}
_COLUMN = {0x01: '(dp,x)', 0x03: 'sr', 0x05: 'dp', 0x07: '[dp]', 0x09: 'immm', 0x0d: 'abs',
           0x0f: 'long', 0x11: '(dp),y', 0x12: '(dp)', 0x13: '(sr),y', 0x15: 'dp,x',
           0x17: '[dp],y', 0x19: 'abs,y', 0x1d: 'abs,x', 0x1f: 'long,x', 0x06: 'dp',
           0x0e: 'abs', 0x16: 'dp,x', 0x1e: 'abs,x'}
_SIZES = {'imp': 1, 'imm8': 2, 'dp': 2, 'dp,x': 2, 'dp,y': 2, '(dp,x)': 2, '(dp)': 2,
          '(dp),y': 2, '[dp]': 2, '[dp],y': 2, 'sr': 2, '(sr),y': 2, 'rel': 2, 'rel16': 3,
          'abs': 3, 'abs,x': 3, 'abs,y': 3, '(abs)': 3, '(abs,x)': 3, '[abs]': 3,
          'long': 4, 'long,x': 4, 'mv': 3}


def mode(op):
    if op in _SPECIAL:
        return _SPECIAL[op]
    if op & 0x1f == 0x10:
        return 'rel'
    return _COLUMN.get(op & 0x1f, 'imp')


def size(md, m, x):
    if md == 'immm':
        return 3 - m
    if md == 'immx':
        return 3 - x
    return _SIZES[md]


class Rom:
    def __init__(self, path, sha256):
        data = Path(path).read_bytes()
        if len(data) % 32768 == 512:
            data = data[512:]
        if hashlib.sha256(data).hexdigest() != sha256:
            raise SystemExit(f'Incorrect original ROM: {path}')
        self.data = data

    def offset(self, address):
        if address & 0xffff < 0x8000:
            raise ValueError(f'not a ROM address: ${address:06X}')
        return ((address >> 16) & 0x7f) * 0x8000 + (address & 0x7fff)

    def byte(self, address):
        return self.data[self.offset(address)]

    def bytes(self, address, n):
        o = self.offset(address)
        return self.data[o:o + n]

    def word(self, address):
        return self.byte(address) | self.byte(address + 1) << 8


@dataclass
class Insn:
    address: int
    op: int
    name: str
    mode: str
    size: int
    value: int  # operand, little-endian


def _next(address, n):
    return (address & 0xff0000) | ((address + n) & 0xffff)


def trace(rom, entries, stop=lambda target: False, width=(1, 1)):
    """Return {address: Insn} reachable from entries. `stop(target)` returns
    True for JSL/JML targets that must not be followed (mapped or shimmed)."""
    seen, tables, calls = {}, {}, set()
    work = [(e, *width) for e in entries]
    while work:
        a, m, x = work.pop()
        while a not in seen:
            op = rom.byte(a)
            md = mode(op)
            n = size(md, m, x)
            raw = rom.bytes(a, n)
            v = int.from_bytes(raw[1:], 'little') if n > 1 else 0
            name = NAMES[op]
            seen[a] = Insn(a, op, name, md, n, v)
            bank = a & 0xff0000
            if op == 0xc2:
                m, x = (0 if v & 0x20 else m), (0 if v & 0x10 else x)
            if op == 0xe2:
                m, x = (1 if v & 0x20 else m), (1 if v & 0x10 else x)
            if op == 0x28:
                m, x = 1, 1
            if md in ('rel', 'rel16'):
                off = v - 256 if md == 'rel' and v > 127 else v - 65536 if md == 'rel16' and v > 32767 else v
                work.append((bank | ((a + n + off) & 0xffff), m, x))
                if name in ('BRA', 'BRL'):
                    break
            if name == 'JSR' and md == 'abs':
                work.append((bank | v, m, x))
            if name in ('JSL', 'JML') and md == 'long':
                calls.add(v)
                if not stop(v):
                    work.append((v, m, x))
            if md == '(abs,x)':
                table, entries_ = bank | v, []
                for k in range(64):
                    p = rom.word(table + 2 * k)
                    if p < 0x8000 or (entries_ and rom.offset(table + 2 * k) >= rom.offset(bank | min(entries_))):
                        break
                    entries_.append(p)
                tables[table] = entries_
                work.extend((bank | p, m, x) for p in entries_)
                if name == 'JMP':
                    break
            if name in ('RTS', 'RTL', 'RTI', 'STP', 'BRK', 'JML') or (name == 'JMP' and md != '(abs,x)'):
                if name == 'JMP' and md == 'abs':
                    work.append((bank | v, m, x))
                break
            a = _next(a, n)
    return seen, tables, calls
