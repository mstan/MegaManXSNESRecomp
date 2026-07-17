import os, sys
# Dump LoROM bytes for bank:addr. Usage: dump_rom.py <bank_hex> <addr_hex> <n>
p = 'mmx.sfc'
sz = os.path.getsize(p)
hdr = sz % 0x8000
bank = int(sys.argv[1], 16)
addr = int(sys.argv[2], 16)
n = int(sys.argv[3]) if len(sys.argv) > 3 else 0x40
fo = hdr + (bank << 15) + (addr & 0x7fff)
with open(p, 'rb') as f:
    f.seek(fo)
    data = f.read(n)
print('bank %02X addr %04X file_off 0x%X hdr %d' % (bank, addr, fo, hdr))
# print address: byte rows
a = addr
for i, b in enumerate(data):
    sys.stdout.write('%02X:%04X %02X\n' % (bank, addr + i, b))
