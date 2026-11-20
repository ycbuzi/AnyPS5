import re, sys

class Lower:
    def __init__(self, text):
        self.lines = text.splitlines()
        self.bound = int(re.search(r'; Bound: (\d+)', text).group(1))
        self.types = {}
        self.vals = {}
        self.globals = []
        self.consts = {}
        self.ptrs = {}
        self.pending = {}

    def new(self):
        self.bound += 1
        return '%%x%d' % self.bound

    def find_type(self, pattern):
        for l in self.lines:
            m = re.match(r'\s*(%\w+) = ' + pattern + r'\s*$', l)
            if m:
                return m.group(1)
        return None

    def global_type(self, text):
        found = self.find_type(re.escape(text))
        if found:
            return found
        for g in self.globals:
            if g.endswith('= ' + text):
                return g.split(' = ')[0].strip()
        r = self.new()
        self.globals.append('%s = %s' % (r, text))
        return r

    def u32const(self, v):
        v &= 0xffffffff
        if v not in self.consts:
            r = self.new()
            self.pending[r] = '%s = OpConstant %s %d' % (r, self.u32, v)
            self.consts[v] = r
        return self.consts[v]

    def emit(self, *ids):
        return [self.pending.pop(i) for i in ids if i in self.pending]

    def run(self):
        text = '\n'.join(self.lines)
        if 'PhysicalStorageBufferAddresses' not in text and 'OpTypeInt 64' not in text and 'OpAtomicStore' not in text:
            return None
        self.u32 = self.find_type(r'OpTypeInt 32 0')
        self.wide = self.find_type(r'OpTypeInt 64 0')
        if not self.u32:
            raise SystemExit('lower64: no 32-bit unsigned type')
        for l in self.lines:
            if re.search(r'OpTypeInt 64 1|OpTypeFloat 64|OpTypeVector %s [2-4]' % re.escape(self.wide or '%none'), l):
                raise SystemExit('lower64: unsupported 64-bit type: ' + l.strip())
        self.uvec2 = self.find_type(r'OpTypeVector %s 2' % re.escape(self.u32))
        self.bool = self.global_type('OpTypeBool')
        self.bvec2 = self.global_type('OpTypeVector %s 2' % self.bool)
        self.zero = self.u32const(0)
        self.one = self.u32const(1)
        self.c32 = self.u32const(32)
        self.c2 = self.u32const(2)
        self.c63 = self.u32const(63)
        self.psb = {}
        for l in self.lines:
            m = re.match(r'\s*(%\w+) = OpTypePointer PhysicalStorageBuffer (%\w+)', l)
            if m:
                self.psb[m.group(1)] = m.group(2)
        self.rta = self.new()
        self.blk = self.new()
        self.arr = self.new()
        self.arrptr = self.new()
        self.eptr = self.new()
        self.mem = self.new()
        if self.psb:
          self.globals += [
            '%s = OpTypeRuntimeArray %s' % (self.rta, self.u32),
            '%s = OpTypeStruct %s' % (self.blk, self.rta),
            '%s = OpTypeRuntimeArray %s' % (self.arr, self.blk),
            '%s = OpTypePointer StorageBuffer %s' % (self.arrptr, self.arr),
            '%s = OpTypePointer StorageBuffer %s' % (self.eptr, self.u32),
            '%s = OpVariable %s StorageBuffer' % (self.mem, self.arrptr),
        ]
        out = []
        for l in self.lines:
            out.extend(self.line(l))
        return self.finish(out)

    def line(self, l):
        s = l.strip()
        if s in ('OpCapability Int64', 'OpCapability PhysicalStorageBufferAddresses', 'OpExtension "SPV_KHR_physical_storage_buffer"'):
            return []
        if s.startswith('OpMemoryModel PhysicalStorageBuffer64'):
            return [s.replace('PhysicalStorageBuffer64', 'Logical')]
        m = re.match(r'(%\w+) = OpTypePointer PhysicalStorageBuffer', s)
        if m:
            return []
        if self.wide and re.match(re.escape(self.wide) + r' = OpTypeInt 64 0$', s):
            if self.uvec2:
                return self.vec2decl()
            return ['%s = OpTypeVector %s 2' % (self.wide, self.u32)]
        if self.wide and self.uvec2 and re.match(r'OpName %s ' % re.escape(self.wide), s):
            return []
        if self.wide and self.uvec2 and s == '%s = OpTypeVector %s 2' % (self.uvec2, self.u32):
            return self.vec2decl()
        m = re.match(r'(%\w+) = OpConstant ' + re.escape(self.wide or '%none') + r' (\d+)$', s)
        if m:
            v = int(m.group(2))
            lo, hi = self.u32const(v), self.u32const(v >> 32)
            self.vals[m.group(1)] = self.wide
            return self.emit(lo, hi) + ['%s = OpConstantComposite %s %s %s' % (m.group(1), self.wide, lo, hi)]
        if re.match(r'(%\w+) = OpSpecConstant\w* ' + re.escape(self.wide or '%none') + r'\b', s):
            raise SystemExit('lower64: 64-bit spec constant')
        m = re.match(r'(%\w+) = (Op\w+) (%\w+)(.*)$', s)
        if not m:
            m2 = re.match(r'(OpStore|OpAtomicStore) (%\w+) (.*)$', s)
            if m2 and m2.group(2) in self.ptrs:
                return self.store(m2.group(1), m2.group(2), m2.group(3))
            if m2 and m2.group(1) == 'OpAtomicStore':
                a = m2.group(3).split()
                return ['%s = OpAtomicExchange %s %s %s' % (self.new(), self.vals.get(a[2], self.u32), m2.group(2), m2.group(3))]
            return [l]
        rid, op, rt, rest = m.groups()
        args = rest.split()
        if not op.startswith('OpType'):
            self.vals[rid] = rt
        w = self.wide
        if op == 'OpUConvert':
            src = args[0]
            if rt == w:
                if self.vals.get(src) != self.u32:
                    x = self.new()
                    return ['%s = OpUConvert %s %s' % (x, self.u32, src), '%s = OpCompositeConstruct %s %s %s' % (rid, w, x, self.zero)]
                return ['%s = OpCompositeConstruct %s %s %s' % (rid, w, src, self.zero)]
            if self.vals.get(src) == w:
                if rt == self.u32:
                    return ['%s = OpCompositeExtract %s %s 0' % (rid, rt, src)]
                x = self.new()
                return ['%s = OpCompositeExtract %s %s 0' % (x, self.u32, src), '%s = OpUConvert %s %s' % (rid, rt, x)]
            return [l]
        if op in ('OpIAdd', 'OpISub') and rt == w:
            return self.addsub(rid, op, args[0], args[1])
        if op in ('OpShiftLeftLogical', 'OpShiftRightLogical') and rt == w:
            return self.shift(rid, op, args[0], args[1])
        if op in ('OpShiftLeftLogical', 'OpShiftRightLogical') and self.vals.get(args[1]) == w:
            x = self.new()
            return ['%s = OpCompositeExtract %s %s 0' % (x, self.u32, args[1]), '%s = %s %s %s %s' % (rid, op, rt, args[0], x)]
        if op in ('OpULessThan', 'OpULessThanEqual', 'OpUGreaterThan', 'OpUGreaterThanEqual', 'OpIEqual', 'OpINotEqual') and self.vals.get(args[0]) == w:
            return self.compare(rid, op, args[0], args[1])
        if op == 'OpSelect' and rt == w:
            c = self.new()
            return ['%s = OpCompositeConstruct %s %s %s' % (c, self.bvec2, args[0], args[0]), '%s = OpSelect %s %s %s %s' % (rid, w, c, args[1], args[2])]
        if op in ('OpIMul', 'OpUDiv', 'OpUMod', 'OpSDiv', 'OpSMod', 'OpSConvert', 'OpBitcast', 'OpSNegate', 'OpConvertUToF', 'OpConvertFToU', 'OpConvertPtrToU', 'OpBitCount', 'OpBitReverse', 'OpBitFieldUExtract', 'OpBitFieldInsert', 'OpSLessThan', 'OpSGreaterThan', 'OpShiftRightArithmetic') and (rt == w or any(self.vals.get(a) == w for a in args)):
            raise SystemExit('lower64: unsupported 64-bit %s' % op)
        if op == 'OpConvertUToPtr':
            return self.toptr(rid, rt, args[0])
        if op == 'OpLoad' and args[0] in self.ptrs:
            return self.load(rid, rt, args[0])
        if op in ('OpAtomicLoad', 'OpAtomicCompareExchange', 'OpAtomicExchange', 'OpAtomicIAdd', 'OpAtomicISub', 'OpAtomicUMin', 'OpAtomicUMax', 'OpAtomicSMin', 'OpAtomicSMax', 'OpAtomicAnd', 'OpAtomicOr', 'OpAtomicXor', 'OpAtomicIIncrement', 'OpAtomicIDecrement') and args[0] in self.ptrs:
            p = self.ptrs[args[0]]
            if p[0] != 'u32':
                raise SystemExit('lower64: vector atomic')
            return ['%s = %s %s %s %s' % (rid, op, rt, p[1], ' '.join(args[1:]))]
        if any(a in self.ptrs for a in args):
            raise SystemExit('lower64: unsupported use of a device address in %s' % op)
        return [l]

    def vec2decl(self):
        if getattr(self, 'vec2done', False):
            return []
        self.vec2done = True
        return ['%s = OpTypeVector %s 2' % (self.uvec2, self.u32)]

    def parts(self, v):
        lo, hi = self.new(), self.new()
        return lo, hi, ['%s = OpCompositeExtract %s %s 0' % (lo, self.u32, v), '%s = OpCompositeExtract %s %s 1' % (hi, self.u32, v)]

    def addsub(self, rid, op, a, b):
        al, ah, c1 = self.parts(a)
        bl, bh, c2 = self.parts(b)
        lo, f, cy, hi, h2 = self.new(), self.new(), self.new(), self.new(), self.new()
        code = c1 + c2 + ['%s = %s %s %s %s' % (lo, op, self.u32, al, bl)]
        if op == 'OpIAdd':
            code.append('%s = OpULessThan %s %s %s' % (f, self.bool, lo, al))
        else:
            code.append('%s = OpULessThan %s %s %s' % (f, self.bool, al, bl))
        code += ['%s = OpSelect %s %s %s %s' % (cy, self.u32, f, self.one, self.zero),
                 '%s = %s %s %s %s' % (hi, op, self.u32, ah, bh),
                 '%s = %s %s %s %s' % (h2, op, self.u32, hi, cy),
                 '%s = OpCompositeConstruct %s %s %s' % (rid, self.wide, lo, h2)]
        return code

    def shift(self, rid, op, a, n):
        al, ah, code = self.parts(a)
        if self.vals.get(n) == self.wide:
            x = self.new()
            code.append('%s = OpCompositeExtract %s %s 0' % (x, self.u32, n))
            n = x
        s, small, nz, inv, sub = self.new(), self.new(), self.new(), self.new(), self.new()
        code += ['%s = OpBitwiseAnd %s %s %s' % (s, self.u32, n, self.c63),
                 '%s = OpULessThan %s %s %s' % (small, self.bool, s, self.c32),
                 '%s = OpINotEqual %s %s %s' % (nz, self.bool, s, self.zero),
                 '%s = OpISub %s %s %s' % (inv, self.u32, self.c32, s),
                 '%s = OpISub %s %s %s' % (sub, self.u32, s, self.c32)]
        if op == 'OpShiftLeftLogical':
            near, far, keep = al, ah, 'OpShiftLeftLogical'
            back = 'OpShiftRightLogical'
        else:
            near, far, keep = ah, al, 'OpShiftRightLogical'
            back = 'OpShiftLeftLogical'
        n1, f1, f2, f3, f4, big, rn, rf = (self.new() for _ in range(8))
        code += ['%s = %s %s %s %s' % (n1, keep, self.u32, near, s),
                 '%s = %s %s %s %s' % (f1, keep, self.u32, far, s),
                 '%s = %s %s %s %s' % (f2, back, self.u32, near, inv),
                 '%s = OpSelect %s %s %s %s' % (f3, self.u32, nz, f2, self.zero),
                 '%s = OpBitwiseOr %s %s %s' % (f4, self.u32, f1, f3),
                 '%s = %s %s %s %s' % (big, keep, self.u32, near, sub),
                 '%s = OpSelect %s %s %s %s' % (rn, self.u32, small, n1, self.zero),
                 '%s = OpSelect %s %s %s %s' % (rf, self.u32, small, f4, big)]
        if op == 'OpShiftLeftLogical':
            code.append('%s = OpCompositeConstruct %s %s %s' % (rid, self.wide, rn, rf))
        else:
            code.append('%s = OpCompositeConstruct %s %s %s' % (rid, self.wide, rf, rn))
        return code

    def compare(self, rid, op, a, b):
        al, ah, c1 = self.parts(a)
        bl, bh, c2 = self.parts(b)
        code = c1 + c2
        if op in ('OpIEqual', 'OpINotEqual'):
            e1, e2 = self.new(), self.new()
            code += ['%s = OpIEqual %s %s %s' % (e1, self.bool, al, bl), '%s = OpIEqual %s %s %s' % (e2, self.bool, ah, bh)]
            if op == 'OpIEqual':
                code.append('%s = OpLogicalAnd %s %s %s' % (rid, self.bool, e1, e2))
            else:
                e3 = self.new()
                code += ['%s = OpLogicalAnd %s %s %s' % (e3, self.bool, e1, e2), '%s = OpLogicalNot %s %s' % (rid, self.bool, e3)]
            return code
        swap = op in ('OpUGreaterThan', 'OpULessThanEqual')
        xl, xh, yl, yh = (bl, bh, al, ah) if swap else (al, ah, bl, bh)
        hl, he, ll, both, lt = (self.new() for _ in range(5))
        code += ['%s = OpULessThan %s %s %s' % (hl, self.bool, xh, yh),
                 '%s = OpIEqual %s %s %s' % (he, self.bool, xh, yh),
                 '%s = OpULessThan %s %s %s' % (ll, self.bool, xl, yl),
                 '%s = OpLogicalAnd %s %s %s' % (both, self.bool, he, ll)]
        if op in ('OpULessThan', 'OpUGreaterThan'):
            code.append('%s = OpLogicalOr %s %s %s' % (rid, self.bool, hl, both))
        else:
            code += ['%s = OpLogicalOr %s %s %s' % (lt, self.bool, hl, both), '%s = OpLogicalNot %s %s' % (rid, self.bool, lt)]
        return code

    def toptr(self, rid, rt, addr):
        pointee = self.psb.get(rt)
        if pointee is None:
            raise SystemExit('lower64: pointer type %s' % rt)
        lo, hi, code = self.parts(addr)
        idx, word = self.new(), self.new()
        code += ['%s = OpISub %s %s %s' % (idx, self.u32, hi, self.one),
                 'OpDecorate %s NonUniform' % idx,
                 '%s = OpShiftRightLogical %s %s %s' % (word, self.u32, lo, self.c2)]
        if pointee == self.u32:
            ac = self.new()
            code.append('%s = OpAccessChain %s %s %s %s %s' % (ac, self.eptr, self.mem, idx, self.zero, word))
            self.ptrs[rid] = ('u32', ac)
            return code
        vec = re.search(r'OpTypeVector %s ([234])' % re.escape(self.u32), next((l for l in self.lines if re.match(r'\s*' + re.escape(pointee) + ' = ', l)), ''))
        if not vec:
            raise SystemExit('lower64: pointee %s' % pointee)
        self.ptrs[rid] = ('vec', idx, word, int(vec.group(1)))
        return code

    def chain(self, p, k):
        w, ac, code = self.new(), self.new(), []
        if k:
            code.append('%s = OpIAdd %s %s %s' % (w, self.u32, p[2], self.u32const(k)))
        else:
            w = p[2]
        code.append('%s = OpAccessChain %s %s %s %s %s' % (ac, self.eptr, self.mem, p[1], self.zero, w))
        return ac, code

    def load(self, rid, rt, ptr):
        p = self.ptrs[ptr]
        if p[0] == 'u32':
            return ['%s = OpLoad %s %s' % (rid, rt, p[1])]
        code, comps = [], []
        for k in range(p[3]):
            ac, c = self.chain(p, k)
            v = self.new()
            code += c + ['%s = OpLoad %s %s' % (v, self.u32, ac)]
            comps.append(v)
        code.append('%s = OpCompositeConstruct %s %s' % (rid, rt, ' '.join(comps)))
        return code

    def store(self, op, ptr, rest):
        p = self.ptrs[ptr]
        args = rest.split()
        val = args[0]
        if op == 'OpAtomicStore':
            if p[0] != 'u32':
                raise SystemExit('lower64: vector atomic store')
            return ['%s = OpAtomicExchange %s %s %s' % (self.new(), self.u32, p[1], rest)]
        if p[0] == 'u32':
            return ['OpStore %s %s' % (p[1], val)]
        code = []
        for k in range(p[3]):
            ac, c = self.chain(p, k)
            v = self.new()
            code += c + ['%s = OpCompositeExtract %s %s %d' % (v, self.u32, val, k), 'OpStore %s %s' % (ac, v)]
        return code

    def finish(self, out):
        self.globals = list(self.pending.values()) + self.globals
        if self.wide and self.uvec2:
            pat = re.compile(re.escape(self.wide) + r'\b')
            out = [pat.sub(self.uvec2, l) for l in out]
            self.globals = [pat.sub(self.uvec2, l) for l in self.globals]
        decos = ['OpDecorate %s ArrayStride 4' % self.rta, 'OpMemberDecorate %s 0 Offset 0' % self.blk, 'OpDecorate %s Block' % self.blk,
                 'OpDecorate %s DescriptorSet 10' % self.mem, 'OpDecorate %s Binding 0' % self.mem]
        body_decos = [l for l in out if l.strip().startswith('OpDecorate') and 'NonUniform' in l and not l.startswith(' ')]
        out = [l for l in out if l not in body_decos]
        decos += body_decos
        caps = ['OpCapability ShaderNonUniform', 'OpCapability RuntimeDescriptorArray', 'OpCapability StorageBufferArrayNonUniformIndexing']
        ext = ['OpExtension "SPV_EXT_descriptor_indexing"']
        if not self.psb:
            decos, caps, ext = [], [], []
        res = []
        placed_caps = placed_decos = placed_globals = False
        last_deco = max(i for i, l in enumerate(out) if l.strip().startswith(('OpDecorate', 'OpMemberDecorate', 'OpEntryPoint', 'OpExecutionMode', 'OpName', 'OpSource')))
        for i, l in enumerate(out):
            s = l.strip()
            if not placed_caps and s.startswith('OpCapability'):
                res += caps
                placed_caps = True
            if ext and placed_caps and not s.startswith('OpCapability'):
                res += ext
                ext = []
            if not placed_globals and s.startswith('OpFunction ') or (not placed_globals and re.match(r'%\w+ = OpFunction ', s)):
                res += self.globals
                placed_globals = True
            res.append(l)
            if i == last_deco:
                res += decos
        return '\n'.join(res) + '\n'

def main():
    src, dst = sys.argv[1], sys.argv[2]
    text = open(src).read()
    out = Lower(text).run()
    if out is None:
        return 3
    open(dst, 'w').write(out)
    return 0

if __name__ == '__main__':
    sys.exit(main())
