#!/bin/sh
set -eu

BIN="${1:-./pdp1184}"
ROOT_IMG="${2:-disks/bsd2.9/2.9BSD-root.rl02}"
USR_IMG="${3:-disks/bsd2.9/2.9BSD-usr.rm05}"

TMP_LOG="$(mktemp /tmp/bsd291-xp-mount.XXXXXX.log)"
TMP_EXP="$(mktemp /tmp/bsd291-xp-mount.XXXXXX.expect)"
trap 'rm -f "$TMP_LOG" "$TMP_EXP"' EXIT INT TERM

if [ ! -x "$BIN" ]; then
  echo "FAIL: binary not executable: $BIN" >&2
  exit 1
fi

if [ ! -f "$ROOT_IMG" ]; then
  echo "SKIP: test_bsd291_xp_mount (root image not found: $ROOT_IMG)"
  exit 0
fi

if [ ! -f "$USR_IMG" ]; then
  echo "SKIP: test_bsd291_xp_mount (usr image not found: $USR_IMG)"
  exit 0
fi

if ! command -v expect >/dev/null 2>&1; then
  echo "SKIP: test_bsd291_xp_mount (expect not found)"
  exit 0
fi

cat >"$TMP_EXP" <<EOF
#!/usr/bin/expect -f
set timeout 200
set send_slow {1 0.02}
log_user 0
log_file -noappend "$TMP_LOG"

spawn "$BIN" -rl "$ROOT_IMG" -xp "$USR_IMG" -boot rl0

expect {
    -re "70Boot" {}
    timeout { puts "FAIL: no 70Boot banner"; exit 11 }
}
expect {
    -re ":" { send -s "rl(0,0)rlunix\r" }
    timeout { puts "FAIL: no 70Boot prompt"; exit 12 }
}
expect {
    -re "xp 0 csr 176700 vector 254 attached" {}
    timeout { puts "FAIL: no xp autoconfig line"; exit 13 }
}
expect {
    -re "Erase=.*intr=.*" {}
    timeout { puts "FAIL: no single-user prompt header"; exit 14 }
}
expect {
    -re "#" { send "\004" }
    timeout { puts "FAIL: no single-user shell prompt"; exit 15 }
}
expect {
    -re "Mounted /usr on /dev/xp0h" { exit 0 }
    -re "FAILED: I/O error" { puts "FAIL: /usr mount returned I/O error"; exit 21 }
    timeout { puts "FAIL: timeout waiting for /usr mount result"; exit 22 }
}
EOF

 /usr/bin/expect "$TMP_EXP"
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "FAIL: test_bsd291_xp_mount (rc=$rc)" >&2
  tail -n 120 "$TMP_LOG" >&2 || true
  exit "$rc"
fi

echo "PASS: test_bsd291_xp_mount"
