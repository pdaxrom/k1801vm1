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
import json
import signal
import tempfile
import tty
import unittest
from unittest import mock
from pathlib import Path
from hardware_uart import escape_until_boot, main


class RecoveryUART(unittest.TestCase):
    def test_odt_prompt_and_wrong_prompt_stops_sequence(self):
        for good in (True, False):
            with self.subTest(good=good):
                master, slave = pty.openpty()
                tty.setraw(slave)
                commands = []
                stop = threading.Event()

                def board():
                    pending = b''
                    while not stop.is_set():
                        if not select.select([master], [], [], .02)[0]:
                            continue
                        pending += os.read(master, 4096)
                        if b'\r' not in pending:
                            continue
                        command, pending = pending.split(b'\r', 1)
                        commands.append(command)
                        os.write(master, command+b'\r\nREPLY\r\n'+(b'OD' if good else b'.'))
                        if good:
                            time.sleep(.02)
                            os.write(master, b'T> ')

                thread = threading.Thread(target=board)
                thread.start()
                try:
                    with tempfile.TemporaryDirectory() as directory:
                        args = ['hardware_uart.py', '--port', os.ttyname(slave),
                                '--out', directory, '--listen', '.01', '--command', 'R',
                                '--command', 'F', '--expect-odt-prompt', '--command-wait', '.15']
                        with mock.patch.object(sys, 'argv', args), contextlib.redirect_stdout(io.StringIO()):
                            if good:
                                main()
                            else:
                                with self.assertRaisesRegex(RuntimeError, 'selected prompt'):
                                    main()
                        self.assertEqual(commands, [b'R', b'F'] if good else [b'R'])
                        record = json.loads((Path(directory)/'session.json').read_text())
                        self.assertEqual(record['uart_bytes'], (Path(directory)/'uart.bin').stat().st_size)
                finally:
                    stop.set()
                    thread.join(1)
                    os.close(slave)
                    os.close(master)
                    self.assertFalse(thread.is_alive())

    def test_ready_fd_may_be_temporarily_empty(self):
        master, slave = pty.openpty()
        tty.setraw(slave)
        real_read = os.read
        first = True

        def raced_read(fd, size):
            nonlocal first
            if first:
                first = False
                raise BlockingIOError(11, 'temporarily unavailable')
            return real_read(fd, size)

        try:
            with tempfile.TemporaryDirectory() as directory:
                os.write(master, b'RT-11 test\r\n.')
                args = ['hardware_uart.py', '--port', os.ttyname(slave),
                        '--out', directory, '--listen', '.01']
                with mock.patch.object(sys, 'argv', args), mock.patch('hardware_uart.os.read', raced_read), contextlib.redirect_stdout(io.StringIO()):
                    main()
                record = json.loads((Path(directory)/'session.json').read_text())
                self.assertEqual((Path(directory)/'uart.bin').read_bytes(), b'RT-11 test\r\n.')
                self.assertEqual(record['uart_bytes'], 13)
                self.assertTrue(record['rt11_banner_seen'])
        finally:
            os.close(slave)
            os.close(master)

    def test_reader_exit_does_not_lose_capture_metadata(self):
        master, slave = pty.openpty()
        tty.setraw(slave)
        real_text = Path.read_text

        def read_text(path, *args, **kwargs):
            if str(path) == '/proc/123456/stat':
                return '123456 (test-reader) S'
            return real_text(path, *args, **kwargs)

        def kill(pid, sig):
            self.assertEqual(pid, 123456)
            if sig == signal.SIGCONT:
                raise ProcessLookupError(3, 'reader exited')

        try:
            with tempfile.TemporaryDirectory() as directory:
                os.write(master, b'CAPTURE')
                args = ['hardware_uart.py', '--port', os.ttyname(slave),
                        '--pause-pid', '123456', '--out', directory, '--listen', '.01']
                with mock.patch.object(sys, 'argv', args), mock.patch.object(Path, 'iterdir', return_value=[Path('/proc/123456/fd/3')]), mock.patch.object(Path, 'read_text', read_text), mock.patch('hardware_uart.os.readlink', return_value=os.ttyname(slave)), mock.patch('hardware_uart.os.kill', kill), contextlib.redirect_stdout(io.StringIO()):
                    main()
                record = json.loads((Path(directory)/'session.json').read_text())
                self.assertTrue(record['reader_exited'])
                self.assertEqual(record['uart_bytes'], 7)
                self.assertEqual((Path(directory)/'uart.bin').read_bytes(), b'CAPTURE')
        finally:
            os.close(slave)
            os.close(master)

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
                      ['--interrupt'], ['--expect-prompt'], ['--expect-odt-prompt']):
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
