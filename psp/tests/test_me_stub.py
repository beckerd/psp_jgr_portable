"""Check the assembled reset stub's handoff and bounded initialization writes.

The small instruction runner checks control flow after copying the code to its
reset address, including branch delay slots. MMIO and cache operations are
recorded, not emulated: this cannot validate PSP timing or hardware behavior.
Requires the PSP cross tools; no game data or firmware is used.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
RESET = 0xbfc00040
MAILBOX = 0xbfc00600


def run_stub(code, entry, argument, config):
    registers = [0] + [0xdeadbeef] * 31
    cp0 = {12: 0xffffffff, 13: 0xffffffff, 28: 0xffffffff, 29: 0xffffffff}
    memory = {MAILBOX: entry, MAILBOX + 4: argument, 0xbc100040: config}
    reads, writes, caches, events = [], [], [], []
    pc, delayed = RESET, None
    for _ in range(4096):
        if pc == entry:
            assert delayed is None, 'handoff before its delay slot'
            return registers, cp0, reads, writes, caches, events
        assert RESET <= pc < RESET + len(code) and pc % 4 == 0, hex(pc)
        word = struct.unpack_from('<I', code, pc - RESET)[0]
        opcode, rs, rt, rd = word >> 26, (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
        immediate = word & 0xffff
        signed = immediate if immediate < 0x8000 else immediate - 0x10000
        address = (registers[rs] + signed) & 0xffffffff
        target = None
        if word == 0:  # nop
            pass
        elif opcode == 0 and word & 63 == 15:  # sync
            events.append('sync')
        elif opcode == 0 and word & 63 == 8:  # jr
            target = registers[rs]
            events.append('handoff')
        elif opcode == 9:  # addiu
            registers[rt] = address
        elif opcode == 13:  # ori
            registers[rt] = registers[rs] | immediate
        elif opcode == 15:  # lui
            registers[rt] = immediate << 16
        elif opcode == 5:  # bne, with an executed delay slot either way
            target = (pc + 4 + signed * 4) if registers[rs] != registers[rt] else pc + 8
        elif opcode == 16 and rs == 4:  # mtc0
            assert rd in cp0
            cp0[rd] = registers[rt]
        elif opcode == 35:  # lw; reject any unprovided firmware/RAM reads
            registers[rt] = memory[address]
            reads.append(address)
        elif opcode == 43:  # sw
            memory[address] = registers[rt]
            writes.append((address, registers[rt]))
        elif opcode == 47:  # cache
            caches.append((rt, address, cp0[28], cp0[29], cp0[12]))
            events.append('cache')
        else:
            raise AssertionError(f'unsupported instruction {word:08x} at {pc:08x}')
        registers[0] = 0
        assert delayed is None or target is None, 'branch in delay slot'
        pc, delayed = (delayed if delayed is not None else pc + 4), target
    raise AssertionError('bootstrap did not hand off within its instruction budget')


class MeStubTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('PSP_CC') or shutil.which('psp-gcc')
        if not compiler:
            raise unittest.SkipTest('PSP compiler not configured')
        prefix = str(Path(compiler).resolve().parent / 'psp-')
        cls.temporary = tempfile.TemporaryDirectory(prefix='jgr-me-stub-')
        cls.addClassCleanup(cls.temporary.cleanup)
        work = Path(cls.temporary.name)
        obj, binary = work / 'stub.o', work / 'stub.bin'
        subprocess.run([compiler, '-c', str(ROOT / 'psp/tools/meprx/me_stub.S'), '-o', str(obj)], check=True)
        subprocess.run([prefix + 'objcopy', '-O', 'binary', '-j', '.text', str(obj), str(binary)], check=True)
        cls.relocations = subprocess.check_output([prefix + 'objdump', '-r', '-j', '.text', str(obj)], text=True)
        cls.undefined = subprocess.check_output([prefix + 'nm', '-u', str(obj)], text=True)
        symbols = subprocess.check_output([prefix + 'nm', '-n', str(obj)], text=True)
        offsets = {parts[2]: int(parts[0], 16) for line in symbols.splitlines()
                   if len(parts := line.split()) == 3}
        assert offsets['me_stub'] == 0
        cls.code = binary.read_bytes()[:offsets['me_stub_end']]

    def test_copied_code_has_no_text_relocations_or_external_symbols_and_fits(self):
        self.assertNotIn('R_MIPS_', self.relocations)
        self.assertEqual(self.undefined.strip(), '')
        self.assertGreater(len(self.code), 0)
        self.assertEqual(len(self.code) % 4, 0)
        self.assertLessEqual(len(self.code), MAILBOX - RESET)

    def test_boot_contract_with_poisoned_registers_and_varying_mailbox_values(self):
        for entry, argument, config in [(0x88012340, 0xc8904000, 0),
                                        (0x889abc00, 0, 0xa5a50001),
                                        (0x88000080, 0xffffffff, 0xffffffff)]:
            with self.subTest(entry=hex(entry), config=hex(config)):
                regs, cp0, reads, writes, caches, events = run_stub(self.code, entry, argument, config)
                self.assertEqual(regs[4], argument)
                self.assertEqual(regs[25], entry)
                self.assertEqual(regs[29], 0x80200000)
                self.assertEqual(cp0, {12: 0x30000000, 13: 0, 28: 0, 29: 0})
                self.assertCountEqual(reads, [0xbc100040, MAILBOX, MAILBOX + 4])
                expected = [(0xbc100050, 15), (0xbc100040, config | 2), (0xbc100004, 0xffffffff)]
                expected += [(address, 0xffffffff) for address in range(0xbc000000, 0xbc000030, 4)]
                self.assertCountEqual(writes, expected)
                self.assertEqual(len(caches), 256)
                for operation in (1, 17):
                    self.assertCountEqual([row[1] for row in caches if row[0] == operation],
                                          range(0x80000000, 0x80002000, 64))
                self.assertTrue(all(row[2:] == (0, 0, 0x30000000) for row in caches))
                self.assertEqual(events[-3:], ['cache', 'sync', 'handoff'])


if __name__ == '__main__':
    unittest.main()
