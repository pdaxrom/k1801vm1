# CP66 physical UART evidence

See [hardware test journal](../../../docs/board-uart-cp66.md).

Raw `uart.bin`, `session.json`, HG logs and native test/readback files were
copied byte for byte from `/tmp/uj11-cp66-uart-test-20260913` on the board host.
The HG backing disk is not included. Historical CP66 simulation and installation
archives are unchanged. `verification.json` hashes every archived file except
itself; run `python3 verify_capture.py` to check the recorded scenarios.

Known setup failures are intentionally retained: `install-test` (HG directory
error), `test-readback-2` (45-second capture timeout, subsequently verified file
and RT-11 prompt), `debug-enable` (ODT unavailable before its normal reload).
The empty passive captures do not contain the two manual RESET entry banners;
the following `entry-registers` / `cancel-check` captures contain the stopped
contexts. User RESET confirmations are recorded in the manifest.
