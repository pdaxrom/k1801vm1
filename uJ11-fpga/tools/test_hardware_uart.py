#!/usr/bin/env python3
"""Host recovery transport checks on a real PTY; no board or JTAG access."""
import contextlib
import io
import os
import pty
import select
import subprocess
import sys
import threading
import time
import tty
import unittest
from pathlib import Path
from hardware_uart import escape_until_boot


class RecoveryUART(unittest.TestCase):
    def session(self, replies, existing=b'', timeout=1):
        master, slave = pty.openpty()
        tty.setraw(slave)
        capture = bytearray(existing)
        received = bytearray()
        record = {}
        done = threading.Event()

        def board():
            for reply in replies:
                while not done.is_set():
                    if select.select([master], [], [], .02)[0]:
                        data = os.read(master, 4096)
                        received.extend(data)
                        os.write(master, reply)
                        break
            while not done.is_set():
                if select.select([master], [], [], .02)[0]:
                    received.extend(os.read(master, 4096))

        def read_for(seconds):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                if select.select([slave], [], [], max(0, end-time.monotonic()))[0]:
                    capture.extend(os.read(slave, 4096))

        thread = threading.Thread(target=board)
        thread.start()
        error = None
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                try:
                    escape_until_boot(slave, capture, read_for, timeout, record)
                except RuntimeError as exc:
                    error = str(exc)
            # Successful return must leave no background ESC sender running.
            before = len(received)
            read_for(.15)
            self.assertEqual(len(received), before)
            self.assertEqual(bytes(received), b'\x1b' * record['escape_bytes'])
            return record, error
        finally:
            done.set()
            thread.join(1)
            os.close(slave)
            os.close(master)
            self.assertFalse(thread.is_alive())

    def test_fragmented_new_banner_stops_escape(self):
        result, error = self.session([b'\x1b\r\n.', b'RT-', b'11FB (S) V05.', b'03\r\n.'])
        self.assertIsNone(error)
        self.assertTrue(result['fresh_rt11_banner'])
        self.assertEqual(result['escape_bytes'], 4)

    def test_old_banner_and_prompt_are_not_a_new_boot(self):
        result, error = self.session([b'\x1b\r\n.'] * 5,
                                     existing=b'RT-11FB (S) V05.03\r\n.', timeout=.25)
        self.assertIn('timed out', error)
        self.assertFalse(result['fresh_rt11_banner'])
        self.assertGreater(result['escape_bytes'], 0)

    def test_partial_old_banner_cannot_complete_new_attempt(self):
        result, error = self.session([b'03\r\n.'],
                                     existing=b'RT-11FB (S) V05.', timeout=.2)
        self.assertIn('timed out', error)
        self.assertFalse(result['fresh_rt11_banner'])

    def test_cli_rejects_mutating_combinations_before_uart_open(self):
        script = Path(__file__).with_name('hardware_uart.py')
        for extra in (['--command','DIR'], ['--xcf','missing.xcf'],
                      ['--interrupt'], ['--expect-prompt']):
            with self.subTest(extra=extra):
                run = subprocess.run([sys.executable, str(script), '--out', '/nonexistent/uj11-test',
                                      '--escape-until-boot','1', *extra], capture_output=True, text=True)
                self.assertEqual(run.returncode, 2)
                self.assertIn('capture-only', run.stderr)
        for value in ('0', '-1', '601', 'nan', 'inf'):
            with self.subTest(value=value):
                run = subprocess.run([sys.executable, str(script), '--out', '/nonexistent/uj11-test',
                                      '--escape-until-boot',value], capture_output=True, text=True)
                self.assertEqual(run.returncode, 2)
                self.assertIn('SECONDS', run.stderr)


if __name__ == '__main__':
    unittest.main()
