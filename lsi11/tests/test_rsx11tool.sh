#!/bin/sh
set -eu

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT INT TERM

./rsx11tool info disks/rsxm26.dsk >"$tmpdir/info.txt"
./rsx11tool ls disks/rsx11m46-CC.rl02 >"$tmpdir/ls.txt"
./rsx11tool ls disks/rsx11m46-CC.rl02 '[001,054]' >"$tmpdir/ls_uic.txt"
./rsx11tool extract disks/rsx11m46-CC.rl02 "$tmpdir/out" \
    '[001,002]STARTUP.CMD;11' >"$tmpdir/extract.txt"
cp disks/rsx11m46-CC.rl02 "$tmpdir/work.rl02"
cat >"$tmpdir/input.txt" <<'EOF'
RSX11TOOL add/rm validation
line 2
EOF
./rsx11tool add "$tmpdir/work.rl02" "$tmpdir/input.txt" \
    '[001,002]ZZCODX.TXT' >"$tmpdir/add.txt"
./rsx11tool ls "$tmpdir/work.rl02" '[001,002]' >"$tmpdir/ls_add.txt"
./rsx11tool extract "$tmpdir/work.rl02" "$tmpdir/out_add" \
    '[001,002]ZZCODX.TXT;1' >"$tmpdir/extract_add.txt"
./rsx11tool rm "$tmpdir/work.rl02" '[001,002]ZZCODX.TXT;1' >"$tmpdir/rm.txt"
./rsx11tool ls "$tmpdir/work.rl02" '[001,002]' >"$tmpdir/ls_rm.txt"

grep -q "Files-11 ODS-1" "$tmpdir/info.txt"
grep -q "Volume label:   RSXM26" "$tmpdir/info.txt"
grep -q "Structure:      000401" "$tmpdir/info.txt"

grep -q "MFD.*INDEXF\\.SYS;1" "$tmpdir/ls.txt"
grep -q "MFD.*BITMAP\\.SYS;1" "$tmpdir/ls.txt"
grep -q "\\[001,002\\].*STARTUP\\.CMD;11" "$tmpdir/ls.txt"
grep -q "\\[001,054\\].*MCR\\.TSK;1" "$tmpdir/ls_uic.txt"
grep -q "extracted 1 file(s)" "$tmpdir/extract.txt"
test -f "$tmpdir/out/001_002/STARTUP.CMD;11"
test "$(wc -c < "$tmpdir/out/001_002/STARTUP.CMD;11")" -eq 1328
grep -a -q "INS \\[1,54\\]DTIME" "$tmpdir/out/001_002/STARTUP.CMD;11"
grep -q "added \\[001,002\\]ZZCODX\\.TXT;1" "$tmpdir/add.txt"
grep -q "\\[001,002\\].*ZZCODX\\.TXT;1" "$tmpdir/ls_add.txt"
grep -q "extracted 1 file(s)" "$tmpdir/extract_add.txt"
cmp -s "$tmpdir/input.txt" "$tmpdir/out_add/001_002/ZZCODX.TXT;1"
grep -q "removed \\[001,002\\]ZZCODX\\.TXT;1" "$tmpdir/rm.txt"
! grep -q "ZZCODX\\.TXT;1" "$tmpdir/ls_rm.txt"

echo "rsx11tool validation passed"
