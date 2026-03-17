#!/bin/sh
set -eu

if [ ! -x ./rt11tool ]; then
    echo "rt11tool not found; run 'make rt11tool' first" >&2
    exit 1
fi

tmpdir=$(mktemp -d -t rt11tool.XXXXXX)
trap 'rm -rf "$tmpdir"' EXIT

img="$tmpdir/test.dsk"
out="$tmpdir/out"
mkdir -p "$out"

dd if=/dev/urandom of="$tmpdir/a.bin" bs=512 count=2 2>/dev/null
dd if=/dev/urandom of="$tmpdir/b.bin" bs=512 count=3 2>/dev/null
dd if=/dev/urandom of="$tmpdir/c.bin" bs=512 count=1 2>/dev/null

./rt11tool mkfs "$img" --rk05 --segments 8 --volid TESTVOL --owner TESTOWN \
    --sysid TESTSYS
./rt11tool info "$img" | grep -q "Volume ID: TESTVOL"
./rt11tool info "$img" | grep -q "Owner: TESTOWN"
./rt11tool info "$img" | grep -q "System ID: TESTSYS"
./rt11tool add "$img" "$tmpdir/a.bin" A.BIN
./rt11tool add "$img" "$tmpdir/b.bin" B.BIN
./rt11tool ls "$img" >/dev/null
./rt11tool ls "$img" --long >/dev/null

./rt11tool extract "$img" "$out"
cmp "$tmpdir/a.bin" "$out/A.BIN"
cmp "$tmpdir/b.bin" "$out/B.BIN"

./rt11tool rm "$img" A.BIN
./rt11tool add "$img" "$tmpdir/c.bin" C.BIN
./rt11tool ls "$img" >/dev/null

./rt11tool extract "$img" "$out" C.BIN
cmp "$tmpdir/c.bin" "$out/C.BIN"

img2="$tmpdir/part.dsk"
./rt11tool mkfs "$img2" --blocks 70000
./rt11tool info "$img2" --partition 1 >/dev/null
./rt11tool add "$img2" "$tmpdir/a.bin" P1.BIN --partition 1
./rt11tool extract "$img2" "$out" P1.BIN --partition 1
cmp "$tmpdir/a.bin" "$out/P1.BIN"

echo "rt11tool validation passed"
