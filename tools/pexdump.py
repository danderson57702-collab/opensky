"""Lists a compiled Papyrus script's functions as opcode lines, read from the user's own Skyrim -
   Misc.bsa (a reading aid: what a quest's stage does - MQ201's escape keys, its SetEnemy).
   python3 pexdump.py <script name in Skyrim - Misc.bsa> [function filter]"""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "converter"))
import bsa
from extract_textures import SKYRIM_DATA

OPS = {0: ("nop", 0), 1: ("iadd", 3), 2: ("fadd", 3), 3: ("isub", 3), 4: ("fsub", 3), 5: ("imul", 3), 6: ("fmul", 3),
       7: ("idiv", 3), 8: ("fdiv", 3), 9: ("imod", 3), 10: ("not", 2), 11: ("ineg", 2), 12: ("fneg", 2), 13: ("assign", 2),
       14: ("cast", 2), 15: ("cmp_eq", 3), 16: ("cmp_lt", 3), 17: ("cmp_le", 3), 18: ("cmp_gt", 3), 19: ("cmp_ge", 3),
       20: ("jmp", 1), 21: ("jmpt", 2), 22: ("jmpf", 2), 23: ("callmethod", 3), 24: ("callparent", 2), 25: ("callstatic", 3),
       26: ("return", 1), 27: ("strcat", 3), 28: ("propget", 3), 29: ("propset", 3), 30: ("array_create", 2),
       31: ("array_length", 2), 32: ("array_getelement", 3), 33: ("array_setelement", 3), 34: ("array_findelement", 4),
       35: ("array_rfindelement", 4)}
VARARG = {23, 24, 25}


class R:
    def __init__(self, d):
        self.d, self.o = d, 0

    def u8(self):
        self.o += 1
        return self.d[self.o - 1]

    def u16(self):
        self.o += 2
        return struct.unpack_from(">H", self.d, self.o - 2)[0]

    def u32(self):
        self.o += 4
        return struct.unpack_from(">I", self.d, self.o - 4)[0]

    def s(self):
        n = self.u16()
        self.o = self.o + n
        return self.d[self.o - n:self.o].decode("latin-1")


def main():
    name, filt = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else "")
    a = bsa.BSA(os.path.join(SKYRIM_DATA, "Skyrim - Misc.bsa"))
    r = R(a.read("scripts\\" + name))
    assert r.u32() == 0xFA57C0DE
    r.u8(), r.u8(), r.u16(), r.o.__add__(0)
    r.o += 8
    r.s(), r.s(), r.s()
    st = [r.s() for _ in range(r.u16())]
    if r.u8():
        r.o += 8
        for _ in range(r.u16()):
            r.u16(), r.u16(), r.u16(), r.u8()
            c = r.u16()
            r.o += 2 * c
    for _ in range(r.u16()):
        r.u16(), r.u8()

    def val():
        t = r.u8()
        if t == 0:
            return "None"
        if t in (1, 2):
            v = st[r.u16()]
            return v if t == 1 else repr(v)
        if t == 3:
            return str(struct.unpack(">i", struct.pack(">I", r.u32()))[0])
        if t == 4:
            return f"{struct.unpack('>f', struct.pack('>I', r.u32()))[0]:g}"
        return str(bool(r.u8()))

    def func(fname):
        r.u16(), r.u16(), r.u32(), r.u8()
        for _ in range(r.u16()):
            r.u16(), r.u16()
        for _ in range(r.u16()):
            r.u16(), r.u16()
        lines = []
        for _ in range(r.u16()):
            op = r.u8()
            nm, n = OPS[op]
            args = [val() for _ in range(n)]
            if op in VARARG:
                args += [val() for _ in range(int(val()))]
            lines.append(f"    {nm} " + " ".join(args))
        if filt.lower() in fname.lower() or any(filt.lower() in l.lower() for l in lines):
            print(fname)
            print("\n".join(lines))

    for _ in range(r.u16()):
        r.u16()
        r.u32()
        r.u16(), r.u16(), r.u32(), r.u16()
        for _ in range(r.u16()):  # variables
            vn, vt = st[r.u16()], st[r.u16()]
            r.u32()
            v = val()
            if filt == "vars":
                print(f"var {vt} {vn} = {v}")
        for _ in range(r.u16()):  # properties
            r.u16(), r.u16(), r.u16(), r.u32()
            flags = r.u8()
            if flags & 4:
                r.u16()
            else:
                if flags & 1:
                    func("(get)")
                if flags & 2:
                    func("(set)")
        for _ in range(r.u16()):  # states
            r.u16()
            for _ in range(r.u16()):
                func(st[r.u16()])


main()
