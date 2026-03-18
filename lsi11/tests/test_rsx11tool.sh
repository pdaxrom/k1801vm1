#!/bin/sh
set -eu

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT INT TERM

./rsx11tool info disks/rsxm26.dsk >"$tmpdir/info.txt"
./rsx11tool ls disks/rsx11m46-CC.rl02 >"$tmpdir/ls.txt"
./rsx11tool ls disks/rsx11m46-CC.rl02 '[001,054]' >"$tmpdir/ls_uic.txt"
./rsx11tool ls disks/rsx11m46-CC.rl02 '[001,002]START*.CMD;11' \
    >"$tmpdir/ls_wild.txt"
./rsx11tool extract disks/rsx11m46-CC.rl02 "$tmpdir/out" \
    '[001,002]START*.CMD;11' >"$tmpdir/extract.txt"
cp disks/rsx11m46-CC.rl02 "$tmpdir/work.rl02"
cat >"$tmpdir/input.txt" <<'EOF'
RSX11TOOL add/rm validation
line 2
EOF
./rsx11tool mkdir "$tmpdir/work.rl02" '[001,123]' >"$tmpdir/mkdir.txt"
./rsx11tool ls "$tmpdir/work.rl02" MFD >"$tmpdir/mfd_after_mkdir.txt"
./rsx11tool ls "$tmpdir/work.rl02" '[001,123]' >"$tmpdir/ls_empty_dir.txt"
./rsx11tool add "$tmpdir/work.rl02" "$tmpdir/input.txt" \
    '[001,123]ZZCODX.TXT' >"$tmpdir/add.txt"
./rsx11tool ls "$tmpdir/work.rl02" '[001,123]' >"$tmpdir/ls_add.txt"
./rsx11tool extract "$tmpdir/work.rl02" "$tmpdir/out_add" \
    '[001,123]ZZCODX.TXT;1' >"$tmpdir/extract_add.txt"
./rsx11tool rm "$tmpdir/work.rl02" '[001,123]ZZC*.TXT' >"$tmpdir/rm.txt"
./rsx11tool rmdir "$tmpdir/work.rl02" '[001,123]' >"$tmpdir/rmdir.txt"
./rsx11tool ls "$tmpdir/work.rl02" MFD >"$tmpdir/mfd_after_rmdir.txt"

cat >"$tmpdir/mkfs_input.txt" <<'EOF'
Fresh mkfs validation
EOF
./rsx11tool mkfs "$tmpdir/mkfs.dsk" --blocks 2048 --label MKTEST \
    >"$tmpdir/mkfs.txt"
./rsx11tool info "$tmpdir/mkfs.dsk" >"$tmpdir/mkfs_info.txt"
./rsx11tool ls "$tmpdir/mkfs.dsk" MFD >"$tmpdir/mkfs_ls.txt"
./rsx11tool fsck "$tmpdir/mkfs.dsk" >"$tmpdir/mkfs_fsck.txt" 2>&1
./rsx11tool mkdir "$tmpdir/mkfs.dsk" '[001,123]' >"$tmpdir/mkfs_mkdir.txt"
./rsx11tool add "$tmpdir/mkfs.dsk" "$tmpdir/mkfs_input.txt" \
    '[001,123]MKTEST.TXT' >"$tmpdir/mkfs_add.txt"
./rsx11tool ls "$tmpdir/mkfs.dsk" '[001,123]' >"$tmpdir/mkfs_ls_ufd.txt"
./rsx11tool extract "$tmpdir/mkfs.dsk" "$tmpdir/mkfs_out" \
    '[001,123]MKTEST.TXT;1' >"$tmpdir/mkfs_extract.txt"
./rsx11tool rm "$tmpdir/mkfs.dsk" '[001,123]MKTEST.TXT;1' \
    >"$tmpdir/mkfs_rm.txt"
./rsx11tool rmdir "$tmpdir/mkfs.dsk" '[001,123]' >"$tmpdir/mkfs_rmdir.txt"
./rsx11tool fsck "$tmpdir/mkfs.dsk" >"$tmpdir/mkfs_fsck_after.txt" 2>&1

cp disks/rsx11m46-CC.rl02 "$tmpdir/fsck.rl02"
cat >"$tmpdir/fsck_input.txt" <<'EOF'
RSX11 fsck validation
EOF
./rsx11tool mkdir "$tmpdir/fsck.rl02" '[001,123]' >"$tmpdir/fsck_mkdir.txt"
./rsx11tool add "$tmpdir/fsck.rl02" "$tmpdir/fsck_input.txt" \
    '[001,123]ZZFSCK.TXT' >"$tmpdir/fsck_add.txt"
./rsx11tool ls "$tmpdir/fsck.rl02" '[001,123]' >"$tmpdir/fsck_ls.txt"
fnum_oct="$(awk '/ZZFSCK\.TXT;1/ {split($4,a,","); print a[1]}' \
    "$tmpdir/fsck_ls.txt")"
iblb="$(./rsx11tool info "$tmpdir/fsck.rl02" | awk '/Index bitmap:/ {print $8}')"
fnum_dec="$(awk -v s="$fnum_oct" \
    'BEGIN{n=0; for(i=1;i<=length(s);i++) n=n*8+substr(s,i,1); print n}')"
byte_index=$(( (fnum_dec - 1) / 8 ))
bit_index=$(( (fnum_dec - 1) % 8 ))
offset=$(( iblb * 512 + byte_index ))
cur_byte="$(od -An -tu1 -N1 -j "$offset" "$tmpdir/fsck.rl02" | tr -d ' ')"
new_byte=$(( cur_byte & ~(1 << bit_index) ))
printf '%b' "\\$(printf '%03o' "$new_byte")" | \
    dd of="$tmpdir/fsck.rl02" bs=1 seek="$offset" conv=notrunc >/dev/null 2>&1
set +e
./rsx11tool fsck "$tmpdir/fsck.rl02" >"$tmpdir/fsck_bad.txt" 2>&1
status_bad=$?
./rsx11tool fsck "$tmpdir/fsck.rl02" --repair >"$tmpdir/fsck_repair.txt" 2>&1
status_repair=$?
./rsx11tool fsck "$tmpdir/fsck.rl02" >"$tmpdir/fsck_after.txt" 2>&1
status_after=$?
set -e

grep -q "Files-11 ODS-1" "$tmpdir/info.txt"
grep -q "Volume label:   RSXM26" "$tmpdir/info.txt"
grep -q "Structure:      000401" "$tmpdir/info.txt"

grep -q "MFD.*INDEXF\\.SYS;1" "$tmpdir/ls.txt"
grep -q "MFD.*BITMAP\\.SYS;1" "$tmpdir/ls.txt"
grep -q "\\[001,002\\].*STARTUP\\.CMD;11" "$tmpdir/ls.txt"
grep -q "\\[001,054\\].*MCR\\.TSK;1" "$tmpdir/ls_uic.txt"
grep -q "\\[001,002\\].*STARTUP\\.CMD;11" "$tmpdir/ls_wild.txt"
grep -q "extracted 1 file(s)" "$tmpdir/extract.txt"
test -f "$tmpdir/out/001_002/STARTUP.CMD;11"
test "$(wc -c < "$tmpdir/out/001_002/STARTUP.CMD;11")" -eq 1328
grep -a -q "INS \\[1,54\\]DTIME" "$tmpdir/out/001_002/STARTUP.CMD;11"
grep -q "created \\[001,123\\]" "$tmpdir/mkdir.txt"
grep -q "MFD.*001123\\.DIR;1" "$tmpdir/mfd_after_mkdir.txt"
grep -q "^dir" "$tmpdir/ls_empty_dir.txt"
grep -q "added \\[001,123\\]ZZCODX\\.TXT;1" "$tmpdir/add.txt"
grep -q "\\[001,123\\].*ZZCODX\\.TXT;1" "$tmpdir/ls_add.txt"
grep -q "extracted 1 file(s)" "$tmpdir/extract_add.txt"
cmp -s "$tmpdir/input.txt" "$tmpdir/out_add/001_123/ZZCODX.TXT;1"
grep -q "removed \\[001,123\\]ZZCODX\\.TXT;1" "$tmpdir/rm.txt"
grep -q "removed \\[001,123\\]" "$tmpdir/rmdir.txt"
! grep -q "001123\\.DIR;1" "$tmpdir/mfd_after_rmdir.txt"
grep -q "created $tmpdir/mkfs.dsk (2048 blocks)" "$tmpdir/mkfs.txt"
grep -q "Volume label:   MKTEST" "$tmpdir/mkfs_info.txt"
grep -q "MFD.*INDEXF\\.SYS;1" "$tmpdir/mkfs_ls.txt"
grep -q "MFD.*BITMAP\\.SYS;1" "$tmpdir/mkfs_ls.txt"
grep -q "MFD.*BADBLK\\.SYS;1" "$tmpdir/mkfs_ls.txt"
grep -q "MFD.*000000\\.DIR;1" "$tmpdir/mkfs_ls.txt"
grep -q "fsck: clean" "$tmpdir/mkfs_fsck.txt"
grep -q "created \\[001,123\\]" "$tmpdir/mkfs_mkdir.txt"
grep -q "added \\[001,123\\]MKTEST\\.TXT;1" "$tmpdir/mkfs_add.txt"
grep -q "\\[001,123\\].*MKTEST\\.TXT;1" "$tmpdir/mkfs_ls_ufd.txt"
grep -q "extracted 1 file(s)" "$tmpdir/mkfs_extract.txt"
cmp -s "$tmpdir/mkfs_input.txt" "$tmpdir/mkfs_out/001_123/MKTEST.TXT;1"
grep -q "removed \\[001,123\\]MKTEST\\.TXT;1" "$tmpdir/mkfs_rm.txt"
grep -q "removed \\[001,123\\]" "$tmpdir/mkfs_rmdir.txt"
grep -q "fsck: clean" "$tmpdir/mkfs_fsck_after.txt"
test "$status_bad" -eq 2
test "$status_repair" -eq 2
test "$status_after" -eq 2
grep -q "\\[001,123\\]ZZFSCK\\.TXT;1: index bitmap bit is clear" \
    "$tmpdir/fsck_bad.txt"
grep -q "repaired" "$tmpdir/fsck_repair.txt"
! grep -q "\\[001,123\\]ZZFSCK\\.TXT;1: index bitmap bit is clear" \
    "$tmpdir/fsck_after.txt"

echo "rsx11tool validation passed"
