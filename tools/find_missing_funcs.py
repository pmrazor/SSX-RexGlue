"""Find code addresses referenced from data sections that codegen did not register as functions.

Usage: python find_missing_funcs.py <image.bin> <ssx_register.cpp>
"""
import re
import struct
import sys

BASE = 0x82000000
img = open(sys.argv[1], 'rb').read()
known = {int(m, 16) for m in re.findall(r'SetFunction\(0x([0-9A-F]+)', open(sys.argv[2]).read())}

# PE headers live at the start of the loaded image.
pe = struct.unpack_from('<I', img, 0x3C)[0]
nsec = struct.unpack_from('<H', img, pe + 6)[0]
optsz = struct.unpack_from('<H', img, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    name = img[o:o + 8].rstrip(b'\0').decode()
    vsize, va = struct.unpack_from('<II', img, o + 8)
    flags = struct.unpack_from('<I', img, o + 36)[0]
    secs.append((name, BASE + va, vsize, bool(flags & 0x20000000)))
code = [(s, s + n) for _, s, n, x in secs if x]
print('sections:', [(n, hex(s), hex(z), x) for n, s, z, x in secs], file=sys.stderr)

def in_code(a):
    return any(s <= a < e for s, e in code)

def word(a):
    return struct.unpack_from('>I', img, a - BASE)[0]

def looks_like_start(a):
    prev = word(a - 4)
    # padding, blr, bctr, or an unconditional tail-call branch
    return prev in (0, 0x4E800020, 0x4E800420) or (prev & 0xFC000003) == 0x48000000

refs = {}
for name, s, n, x in secs:
    if x or name in ('.pdata', '.reloc'):
        continue
    for a in range(s, min(s + n, BASE + len(img)) - 3, 4):
        v = word(a)
        if v % 4 == 0 and in_code(v) and v not in known and word(v) != 0:
            refs.setdefault(v, []).append(a)

# Function pointers materialised in code: lis rX,hi ; addi/ori rY,rX,lo
for s_, e_ in code:
    for a in range(s_, e_ - 4, 4):
        w = word(a)
        if w >> 26 != 15 or (w >> 16) & 31 != 0:  # lis rD,imm (addis rD,0,imm)
            continue
        rd, hi = (w >> 21) & 31, w & 0xFFFF
        for b in range(a + 4, min(a + 64, e_), 4):
            x = word(b)
            op, ra = x >> 26, (x >> 16) & 31
            if op == 14 and ra == rd:  # addi
                lo = x & 0xFFFF
                v = ((hi << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
            elif op == 24 and (x >> 21) & 31 == rd:  # ori rA,rS,imm (rS == rd)
                v = (hi << 16) | (x & 0xFFFF)
            else:
                continue
            if v % 4 == 0 and in_code(v) and v not in known and word(v) != 0:
                refs.setdefault(v, []).append(b)
            break

starts = sorted(v for v in refs if looks_like_start(v))
print(f'{len(refs)} unregistered code refs, {len(starts)} look like function starts', file=sys.stderr)
for v in starts:
    print(f'0x{v:08X} refs={len(refs[v])} first_ref=0x{refs[v][0]:08X}')
