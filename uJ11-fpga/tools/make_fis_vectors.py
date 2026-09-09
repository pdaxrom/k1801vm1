#!/usr/bin/env python3
"""Generate complete FIS register/PSW/memory-bus outcomes independently of RTL."""
import argparse
from pathlib import Path
from fis_reference import reference, vectors


def case(index, operation, a, b, reg=0, psw=0, fault_beat=-1, odd=False,
         irq=False, pointer=None):
    pre = [0x1111 + k*0x111 for k in range(6)] + [0x4000, 0x200]
    base = (0x202 if reg == 7 else 0x1000) if pointer is None else pointer
    if odd:
        assert reg != 7
        base |= 1
    pre[reg] = (base-2)&65535 if reg == 7 else base
    post = pre.copy()
    post[7] = (post[7]+2)&65535
    opcode = 0o075000 | (operation << 3) | reg
    mem = {pre[7]: opcode, 4: 0x800, 6: 0xe0, 12: 0x820, 14: 0xe0,
           0o244: 0x840, 0o246: 0xe0, 0o100: 0x860, 0o102: 0xe0}
    if not odd:
        for offset, value in zip((0, 2, 4, 6), (b >> 16, b & 65535, a >> 16, a & 65535)):
            address = (base+offset)&65535
            assert address not in mem, 'operand/code/vector alias needs a dedicated case'
            mem[address] = value
    patches = mem.copy()
    beats = [(0, pre[7], opcode)]
    result, flags, arithmetic_error = reference(operation, a, b)
    final_psw, vector, failed = psw, None, odd
    if odd:
        vector = 4
    else:
        for offset in (0, 2, 4, 6):
            address = (base+offset)&65535
            if len(beats) == fault_beat:
                beats.append((4, address, 0))
                failed, vector = True, 4
                break
            beats.append((0, address, mem[address]))
        if not failed:
            if arithmetic_error:
                final_psw, vector = (psw & ~15) | flags, 0o244
            else:
                for offset, value in ((4, result >> 16), (6, result & 65535)):
                    address = (base+offset)&65535
                    if len(beats) == fault_beat:
                        beats.append((5, address, 0))
                        failed, vector = True, 4
                        break
                    beats.append((1, address, value))
                    mem[address] = value
                if not failed:
                    post[reg] = (base+4)&65535
                    final_psw = (psw & ~15) | flags
    irq_taken = False
    if vector is None:
        if psw & 16:
            vector = 12
        elif irq and (psw >> 5) < 7:
            vector, irq_taken = 0o100, True
    frame_vectors = [] if vector is None else [vector]
    if vector == 0o244 and (psw & 16):
        # Preserve the existing uJ11/DCJ11 software-trap policy (CP17):
        # latched T traces the completed trap before a handler opcode runs.
        frame_vectors.append(12)
    expected_fault = 0
    for vector in frame_vectors:
        # Existing DCJ11 trap ABI reads vector PSW/PC before its stack writes.
        vector_pc, vector_psw = mem[vector], mem[vector+2]
        beats += [(0, vector+2, vector_psw), (0, vector, vector_pc)]
        post[6] = (post[6]-2)&65535
        if post[6] & 1:
            # Odd SP causes the existing terminal second-fault policy, before
            # the first stack write. Vector PSW has already been installed.
            expected_fault, final_psw = 1, vector_psw
            break
        beats.append((1, post[6], final_psw))
        mem[post[6]] = final_psw
        post[6] = (post[6]-2)&65535
        beats.append((1, post[6], post[7]))
        mem[post[6]] = post[7]
        post[7] = vector_pc
        final_psw = vector_psw
    # Trap frame stack values are checked through the bus and final RAM.
    values = [index, opcode, psw, int(irq), int(irq_taken), fault_beat & 0xffff, expected_fault] + pre
    values += [len(patches)] + [v for pair in patches.items() for v in pair]
    values += [final_psw] + post + [len(beats)] + [v for beat in beats for v in beat]
    written = {address: mem[address] for flags_, address, _ in beats if flags_ == 1}
    unchanged = {address: value for address, value in patches.items() if address not in written}
    checks = {**unchanged, **written}
    values += [len(checks)] + [v for pair in checks.items() for v in pair]
    return ' '.join(f'{v:x}' for v in values)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--random', type=int, default=4096)
    p.add_argument('--limit', type=int, default=0)
    p.add_argument('--output', type=Path, default=Path('build/fis-vectors.txt'))
    args = p.parse_args()
    lines = []
    for operation, a, b in vectors(args.random):
        lines.append(case(len(lines), operation, a, b, psw=(len(lines)%8)*32 | (len(lines)%16)))
        if args.limit and len(lines) >= args.limit:
            break
    if not args.limit:
        anchors = [(0x40800000, 0x40800000), (0xc1000000, 0x40c00000),
                   (0x00800001, 0x00800000), (0x00800000, 0x40800000),
                   (0x7fffffff, 0x7fffffff), (0x40800000, 0), (0, 0)]
        for reg in range(8):
            for op in range(4):
                for a, b in anchors:
                    for nzvc in range(16):
                        for mode in (0, 1, 2): # ordinary / trace / pending IRQ
                            psw = nzvc | (16 if mode == 1 else 0)
                            lines.append(case(len(lines), op, a, b, reg, psw, irq=mode == 2))
        for reg in range(8):
            for op in range(4):
                for nzvc in range(16):
                    for beat in range(1, 7):
                        lines.append(case(len(lines), op, 0x41000000, 0x40800000,
                                          reg, 0xa0 | nzvc, fault_beat=beat))
                    if reg != 7:
                        lines.append(case(len(lines), op, 0, 0, reg, 0xa0 | nzvc, odd=True))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(lines)+'\n')
    print(f'FIS: {len(lines)} independent register/PSW/memory/trace/IRQ/fault cases')


if __name__ == '__main__':
    main()
