#!/bin/sh
set -eu

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT INT TERM

./rsx11tool info disks/rsxm26.dsk >"$tmpdir/info.txt"
./rsx11tool mkfs --list >"$tmpdir/mkfs_list.txt"
./rsx11tool bootblock disks/rsxm26.dsk --dump "$tmpdir/rsxm26.boot"
dd if=disks/rsxm26.dsk of="$tmpdir/rsxm26.block0" bs=512 count=1 \
    >/dev/null 2>&1
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
: >"$tmpdir/mkfs_empty.bin"
./rsx11tool mkfs "$tmpdir/mkfs.dsk" --blocks 2048 --label MKTEST \
    >"$tmpdir/mkfs.txt"
./rsx11tool mkfs "$tmpdir/mkfs_type.dsk" --type rk05 --label MKTYPE \
    >"$tmpdir/mkfs_type.txt"
./rsx11tool mkfs "$tmpdir/mkfs_bootcmd.dsk" --blocks 2048 --label BOOTCMD \
    >"$tmpdir/mkfs_bootcmd.txt"
./rsx11tool mkfs "$tmpdir/mkfs_bootsrc.dsk" --blocks 2048 --label BOOTSRC \
    --boot-from disks/rsxm26.dsk >"$tmpdir/mkfs_bootsrc.txt"
./rsx11tool mkfs "$tmpdir/mkfs_bootfile.dsk" --blocks 2048 --label BOOTFILE \
    --bootblock "$tmpdir/rsxm26.boot" >"$tmpdir/mkfs_bootfile.txt"
./rsx11tool bootblock "$tmpdir/mkfs_bootcmd.dsk" --write "$tmpdir/rsxm26.boot"
./rsx11tool info "$tmpdir/mkfs.dsk" >"$tmpdir/mkfs_info.txt"
./rsx11tool info "$tmpdir/mkfs_type.dsk" >"$tmpdir/mkfs_type_info.txt"
./rsx11tool info "$tmpdir/mkfs_bootcmd.dsk" >"$tmpdir/mkfs_bootcmd_info.txt"
./rsx11tool info "$tmpdir/mkfs_bootsrc.dsk" >"$tmpdir/mkfs_bootsrc_info.txt"
./rsx11tool info "$tmpdir/mkfs_bootfile.dsk" >"$tmpdir/mkfs_bootfile_info.txt"
./rsx11tool ls "$tmpdir/mkfs.dsk" MFD >"$tmpdir/mkfs_ls.txt"
./rsx11tool fsck "$tmpdir/mkfs.dsk" >"$tmpdir/mkfs_fsck.txt" 2>&1
./rsx11tool fsck "$tmpdir/mkfs_type.dsk" >"$tmpdir/mkfs_type_fsck.txt" 2>&1
./rsx11tool fsck "$tmpdir/mkfs_bootcmd.dsk" >"$tmpdir/mkfs_bootcmd_fsck.txt" 2>&1
./rsx11tool fsck "$tmpdir/mkfs_bootsrc.dsk" >"$tmpdir/mkfs_bootsrc_fsck.txt" 2>&1
./rsx11tool fsck "$tmpdir/mkfs_bootfile.dsk" >"$tmpdir/mkfs_bootfile_fsck.txt" 2>&1
dd if="$tmpdir/mkfs_bootcmd.dsk" of="$tmpdir/mkfs_bootcmd.block0" bs=512 count=1 \
    >/dev/null 2>&1
dd if="$tmpdir/mkfs_bootsrc.dsk" of="$tmpdir/mkfs_bootsrc.block0" bs=512 count=1 \
    >/dev/null 2>&1
dd if="$tmpdir/mkfs_bootfile.dsk" of="$tmpdir/mkfs_bootfile.block0" bs=512 count=1 \
    >/dev/null 2>&1

./rsx11tool mkfs "$tmpdir/frag.dsk" --blocks 128 --label FRAG \
    >"$tmpdir/frag_mkfs.txt"
./rsx11tool mkdir "$tmpdir/frag.dsk" '[001,123]' >"$tmpdir/frag_mkdir.txt"
(dd if=/dev/zero bs=512 count=1 2>/dev/null | tr '\0' 'F') >"$tmpdir/frag_one.bin"
(dd if=/dev/zero bs=512 count=2 2>/dev/null | tr '\0' 'G') >"$tmpdir/frag_pair.bin"
printf 'FRAGPAIR\n' | dd of="$tmpdir/frag_pair.bin" conv=notrunc \
    >/dev/null 2>&1
frag_count=0
while :; do
    frag_spec="$(printf '[001,123]F%05o.BIN' "$frag_count")"
    if ./rsx11tool add "$tmpdir/frag.dsk" "$tmpdir/frag_one.bin" \
        "$frag_spec" >/dev/null 2>&1; then
        frag_count=$((frag_count + 1))
    else
        break
    fi
done
frag_i=0
while [ "$frag_i" -lt "$frag_count" ]; do
    if [ $((frag_i % 2)) -eq 0 ]; then
        frag_spec="$(printf '[001,123]F%05o.BIN;1' "$frag_i")"
        ./rsx11tool rm "$tmpdir/frag.dsk" "$frag_spec" >/dev/null
    fi
    frag_i=$((frag_i + 1))
done
./rsx11tool add "$tmpdir/frag.dsk" "$tmpdir/frag_pair.bin" \
    '[001,123]PAIR.BIN' >"$tmpdir/frag_add_pair.txt"
./rsx11tool ls "$tmpdir/frag.dsk" '[001,123]' >"$tmpdir/frag_ls.txt"
./rsx11tool extract "$tmpdir/frag.dsk" "$tmpdir/frag_out" \
    '[001,123]PAIR.BIN;1' >"$tmpdir/frag_extract.txt"
./rsx11tool fsck "$tmpdir/frag.dsk" >"$tmpdir/frag_fsck.txt" 2>&1
cp "$tmpdir/mkfs.dsk" "$tmpdir/mkfs_alt_home.dsk"
dd if="$tmpdir/mkfs_alt_home.dsk" of="$tmpdir/mkfs_alt_home.dsk" \
    bs=512 skip=1 seek=256 count=1 conv=notrunc >/dev/null 2>&1
printf '\0\0' | dd of="$tmpdir/mkfs_alt_home.dsk" bs=1 seek=$((512 + 58)) \
    conv=notrunc >/dev/null 2>&1
printf '\0\0' | dd of="$tmpdir/mkfs_alt_home.dsk" bs=1 seek=$((512 + 510)) \
    conv=notrunc >/dev/null 2>&1
./rsx11tool info "$tmpdir/mkfs_alt_home.dsk" >"$tmpdir/mkfs_alt_info.txt"
./rsx11tool fsck "$tmpdir/mkfs_alt_home.dsk" >"$tmpdir/mkfs_alt_fsck.txt" 2>&1
strings -a "$tmpdir/mkfs.dsk" >"$tmpdir/mkfs_strings.txt"
mkfs_iblb="$(./rsx11tool info "$tmpdir/mkfs.dsk" | awk '/Index bitmap:/ {print $8}')"
mkfs_mfd_fcs_off=$(( (mkfs_iblb + 4) * 512 + 14 ))
mkfs_bitmap_ptr_off=$(( (mkfs_iblb + 2) * 512 + 102 ))
set -- $(od -An -tu1 -N 4 -j "$mkfs_bitmap_ptr_off" "$tmpdir/mkfs.dsk")
mkfs_bitmap_lbn=$(( $1 * 65536 + $3 + 256 * $4 ))
od -An -tu2 -N 8 -j $(( mkfs_bitmap_lbn * 512 + 4 )) "$tmpdir/mkfs.dsk" \
    | tr -s ' ' | sed 's/^ //; s/ *$//' >"$tmpdir/mkfs_bitmap_ctrl.txt"
od -An -tu1 -N 4 -j "$mkfs_mfd_fcs_off" "$tmpdir/mkfs.dsk" \
    | tr -s ' ' | sed 's/^ //; s/ *$//' >"$tmpdir/mkfs_mfd_fcs.txt"
./rsx11tool mkdir "$tmpdir/mkfs.dsk" '[001,123]' >"$tmpdir/mkfs_mkdir.txt"
./rsx11tool add "$tmpdir/mkfs.dsk" "$tmpdir/mkfs_empty.bin" \
    '[001,123]EMPTY.DAT' >"$tmpdir/mkfs_add_empty.txt"
./rsx11tool add "$tmpdir/mkfs.dsk" "$tmpdir/mkfs_input.txt" \
    '[001,123]MKTEST.TXT' >"$tmpdir/mkfs_add.txt"
./rsx11tool ls "$tmpdir/mkfs.dsk" '[001,123]' >"$tmpdir/mkfs_ls_ufd.txt"
./rsx11tool extract "$tmpdir/mkfs.dsk" "$tmpdir/mkfs_empty_out" \
    '[001,123]EMPTY.DAT;1' >"$tmpdir/mkfs_extract_empty.txt"
./rsx11tool extract "$tmpdir/mkfs.dsk" "$tmpdir/mkfs_out" \
    '[001,123]MKTEST.TXT;1' >"$tmpdir/mkfs_extract.txt"
./rsx11tool rm "$tmpdir/mkfs.dsk" '[001,123]EMPTY.DAT;1' \
    >"$tmpdir/mkfs_rm_empty.txt"
./rsx11tool rm "$tmpdir/mkfs.dsk" '[001,123]MKTEST.TXT;1' \
    >"$tmpdir/mkfs_rm.txt"
./rsx11tool rmdir "$tmpdir/mkfs.dsk" '[001,123]' >"$tmpdir/mkfs_rmdir.txt"
./rsx11tool fsck "$tmpdir/mkfs.dsk" >"$tmpdir/mkfs_fsck_after.txt" 2>&1

./rsx11tool mkfs "$tmpdir/chain.dsk" --blocks 2048 --label CHAIN \
    >"$tmpdir/chain_mkfs.txt"
./rsx11tool mkdir "$tmpdir/chain.dsk" '[001,123]' >"$tmpdir/chain_mkdir.txt"
(dd if=/dev/zero bs=512 count=1 2>/dev/null | tr '\0' 'A') >"$tmpdir/chain_a.bin"
printf 'CHAIN-A\n' | dd of="$tmpdir/chain_a.bin" conv=notrunc \
    >/dev/null 2>&1
(dd if=/dev/zero bs=512 count=1 2>/dev/null | tr '\0' 'B') >"$tmpdir/chain_b.bin"
printf 'CHAIN-B\n' | dd of="$tmpdir/chain_b.bin" conv=notrunc \
    >/dev/null 2>&1
cat "$tmpdir/chain_a.bin" "$tmpdir/chain_b.bin" >"$tmpdir/chain_expected.bin"
./rsx11tool add "$tmpdir/chain.dsk" "$tmpdir/chain_a.bin" \
    '[001,123]CHAINA.BIN' >"$tmpdir/chain_add_a.txt"
./rsx11tool add "$tmpdir/chain.dsk" "$tmpdir/chain_b.bin" \
    '[001,123]CHAINB.BIN' >"$tmpdir/chain_add_b.txt"
cat >"$tmpdir/patch_chain.c" <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLK 512u
#define HOME_BLK 1u
#define OFF_IBSZ 0u
#define OFF_IBLB 2u
#define OFF_UFAT 14u
#define OFF_HIBK 4u
#define OFF_EFBK 8u
#define OFF_FFBY 12u

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32hi(const uint8_t *p)
{
    return ((uint32_t)get16(p) << 16) | (uint32_t)get16(p + 2u);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)(v >> 8);
}

static int rad50_enc(int c)
{
    if (c == ' ' || c == '\0') {
        return 0;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 1;
    }
    if (c == '$') {
        return 27;
    }
    if (c == '.') {
        return 28;
    }
    if (c == '%') {
        return 29;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 30;
    }
    return -1;
}

static uint16_t rad50_word(const char *s)
{
    int a = rad50_enc((unsigned char)s[0]);
    int b = rad50_enc((unsigned char)s[1]);
    int c = rad50_enc((unsigned char)s[2]);

    if (a < 0 || b < 0 || c < 0) {
        fprintf(stderr, "invalid RAD50\n");
        exit(1);
    }
    return (uint16_t)(a * 1600 + b * 40 + c);
}

static void rad50_name3(const char *src, uint8_t out[6])
{
    char tmp[10];
    size_t i;

    memset(tmp, ' ', sizeof(tmp));
    memcpy(tmp, src, strlen(src));
    for (i = 0; i < 3u; i++) {
        put16(out + i * 2u, rad50_word(tmp + i * 3u));
    }
}

static void put32hi(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)(v >> 16));
    put16(p + 2u, (uint16_t)(v & 0xffffu));
}

static uint16_t checksum(const uint8_t *buf)
{
    uint16_t sum = 0;
    size_t i;

    for (i = 0; i < 255u; i++) {
        sum = (uint16_t)(sum + get16(buf + i * 2u));
    }
    return sum;
}

static int read_at(FILE *fp, uint64_t off, void *buf, size_t len)
{
    if (fseeko(fp, (off_t)off, SEEK_SET) != 0) {
        return -1;
    }
    return fread(buf, 1, len, fp) == len ? 0 : -1;
}

static int write_at(FILE *fp, uint64_t off, const void *buf, size_t len)
{
    if (fseeko(fp, (off_t)off, SEEK_SET) != 0) {
        return -1;
    }
    return fwrite(buf, 1, len, fp) == len ? 0 : -1;
}

static void header_lbn(const uint8_t *home, uint16_t fnum, uint32_t *lbn_out)
{
    uint16_t ibsz = get16(home + OFF_IBSZ);
    uint32_t iblb = get32hi(home + OFF_IBLB);

    *lbn_out = iblb + ibsz + (uint32_t)fnum - 1u;
}

static void first_extent_lbn(const uint8_t hdr[BLK], uint32_t *lbn_out)
{
    uint8_t mpof = hdr[1];
    const uint8_t *map = hdr + (size_t)mpof * 2u;

    *lbn_out = ((uint32_t)map[10] << 16) | (uint32_t)get16(map + 12u);
}

static int find_record(FILE *fp, uint32_t lbn,
                       const char *name, const char *ext,
                       uint16_t *fnum, uint16_t *seq, uint64_t *rec_off)
{
    uint8_t block[BLK];
    uint8_t name50[6];
    uint16_t ext50;
    size_t off;

    rad50_name3(name, name50);
    ext50 = rad50_word(ext);
    if (read_at(fp, (uint64_t)lbn * BLK, block, sizeof(block)) != 0) {
        return -1;
    }
    for (off = 0; off + 16u <= BLK; off += 16u) {
        if (get16(block + off) == 0u) {
            continue;
        }
        if (memcmp(block + off + 6u, name50, 6u) == 0 &&
            get16(block + off + 12u) == ext50) {
            *fnum = get16(block + off);
            *seq = get16(block + off + 2u);
            *rec_off = (uint64_t)lbn * BLK + off;
            return 0;
        }
    }
    return -1;
}

int main(int argc, char **argv)
{
    FILE *fp;
    uint8_t home[BLK];
    uint8_t mfd_hdr[BLK];
    uint8_t ufd_hdr[BLK];
    uint8_t a_hdr[BLK];
    uint8_t b_hdr[BLK];
    uint8_t zero[16] = { 0 };
    uint32_t mfd_lbn;
    uint32_t ufd_lbn;
    uint32_t a_lbn;
    uint32_t b_lbn;
    uint16_t dir_fnum;
    uint16_t dir_seq;
    uint16_t a_fnum;
    uint16_t a_seq;
    uint16_t b_fnum;
    uint16_t b_seq;
    uint64_t b_rec_off;
    uint8_t *map;

    if (argc != 2) {
        return 2;
    }
    fp = fopen(argv[1], "r+b");
    if (fp == NULL) {
        return 1;
    }
    if (read_at(fp, (uint64_t)HOME_BLK * BLK, home, sizeof(home)) != 0) {
        return 1;
    }
    header_lbn(home, 4u, &mfd_lbn);
    if (read_at(fp, (uint64_t)mfd_lbn * BLK, mfd_hdr, sizeof(mfd_hdr)) != 0) {
        return 1;
    }
    first_extent_lbn(mfd_hdr, &mfd_lbn);
    if (find_record(fp, mfd_lbn, "001123", "DIR",
                    &dir_fnum, &dir_seq, &b_rec_off) != 0) {
        return 1;
    }
    (void)dir_seq;
    header_lbn(home, dir_fnum, &ufd_lbn);
    if (read_at(fp, (uint64_t)ufd_lbn * BLK, ufd_hdr, sizeof(ufd_hdr)) != 0) {
        return 1;
    }
    first_extent_lbn(ufd_hdr, &ufd_lbn);
    if (find_record(fp, ufd_lbn, "CHAINA", "BIN",
                    &a_fnum, &a_seq, &b_rec_off) != 0) {
        return 1;
    }
    if (find_record(fp, ufd_lbn, "CHAINB", "BIN",
                    &b_fnum, &b_seq, &b_rec_off) != 0) {
        return 1;
    }
    (void)a_seq;
    header_lbn(home, a_fnum, &a_lbn);
    header_lbn(home, b_fnum, &b_lbn);
    if (read_at(fp, (uint64_t)a_lbn * BLK, a_hdr, sizeof(a_hdr)) != 0 ||
        read_at(fp, (uint64_t)b_lbn * BLK, b_hdr, sizeof(b_hdr)) != 0) {
        return 1;
    }

    map = a_hdr + (size_t)a_hdr[1] * 2u;
    map[1] = 0u;
    put16(map + 2u, b_fnum);
    put16(map + 4u, b_seq);
    put32hi(a_hdr + OFF_UFAT + OFF_HIBK, 2u);
    put32hi(a_hdr + OFF_UFAT + OFF_EFBK, 2u);
    put16(a_hdr + OFF_UFAT + OFF_FFBY, 512u);
    put16(a_hdr + 510u, checksum(a_hdr));

    map = b_hdr + (size_t)b_hdr[1] * 2u;
    map[0] = 1u;
    map[1] = 0u;
    put16(map + 2u, 0u);
    put16(map + 4u, 0u);
    put16(b_hdr + 510u, checksum(b_hdr));

    if (write_at(fp, (uint64_t)a_lbn * BLK, a_hdr, sizeof(a_hdr)) != 0 ||
        write_at(fp, (uint64_t)b_lbn * BLK, b_hdr, sizeof(b_hdr)) != 0 ||
        write_at(fp, b_rec_off, zero, sizeof(zero)) != 0 ||
        fflush(fp) != 0) {
        return 1;
    }

    fclose(fp);
    return 0;
}
EOF
${CC:-cc} -O2 -Wall -Wextra -o "$tmpdir/patch_chain" "$tmpdir/patch_chain.c"
"$tmpdir/patch_chain" "$tmpdir/chain.dsk"
./rsx11tool ls "$tmpdir/chain.dsk" '[001,123]' >"$tmpdir/chain_ls.txt"
./rsx11tool extract "$tmpdir/chain.dsk" "$tmpdir/chain_out" \
    '[001,123]CHAINA.BIN;1' >"$tmpdir/chain_extract.txt"
./rsx11tool fsck "$tmpdir/chain.dsk" >"$tmpdir/chain_fsck.txt" 2>&1

cat >"$tmpdir/patch_chain_ref.c" <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLK 512u
#define HOME_BLK 1u
#define OFF_IBSZ 0u
#define OFF_IBLB 2u

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32hi(const uint8_t *p)
{
    return ((uint32_t)get16(p) << 16) | (uint32_t)get16(p + 2u);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)(v >> 8);
}

static int rad50_enc(int c)
{
    if (c == ' ' || c == '\0') {
        return 0;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 1;
    }
    if (c == '$') {
        return 27;
    }
    if (c == '.') {
        return 28;
    }
    if (c == '%') {
        return 29;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 30;
    }
    return -1;
}

static uint16_t rad50_word(const char *s)
{
    int a = rad50_enc((unsigned char)s[0]);
    int b = rad50_enc((unsigned char)s[1]);
    int c = rad50_enc((unsigned char)s[2]);

    if (a < 0 || b < 0 || c < 0) {
        fprintf(stderr, "invalid RAD50\n");
        exit(1);
    }
    return (uint16_t)(a * 1600 + b * 40 + c);
}

static void rad50_name3(const char *src, uint8_t out[6])
{
    char tmp[10];
    size_t i;

    memset(tmp, ' ', sizeof(tmp));
    memcpy(tmp, src, strlen(src));
    for (i = 0; i < 3u; i++) {
        put16(out + i * 2u, rad50_word(tmp + i * 3u));
    }
}

static uint16_t checksum(const uint8_t *buf)
{
    uint16_t sum = 0;
    size_t i;

    for (i = 0; i < 255u; i++) {
        sum = (uint16_t)(sum + get16(buf + i * 2u));
    }
    return sum;
}

static int read_at(FILE *fp, uint64_t off, void *buf, size_t len)
{
    if (fseeko(fp, (off_t)off, SEEK_SET) != 0) {
        return -1;
    }
    return fread(buf, 1, len, fp) == len ? 0 : -1;
}

static int write_at(FILE *fp, uint64_t off, const void *buf, size_t len)
{
    if (fseeko(fp, (off_t)off, SEEK_SET) != 0) {
        return -1;
    }
    return fwrite(buf, 1, len, fp) == len ? 0 : -1;
}

static void header_lbn(const uint8_t *home, uint16_t fnum, uint32_t *lbn_out)
{
    uint16_t ibsz = get16(home + OFF_IBSZ);
    uint32_t iblb = get32hi(home + OFF_IBLB);

    *lbn_out = iblb + ibsz + (uint32_t)fnum - 1u;
}

static void first_extent_lbn(const uint8_t hdr[BLK], uint32_t *lbn_out)
{
    uint8_t mpof = hdr[1];
    const uint8_t *map = hdr + (size_t)mpof * 2u;

    *lbn_out = ((uint32_t)map[10] << 16) | (uint32_t)get16(map + 12u);
}

static int header_looks_free(const uint8_t hdr[BLK], uint16_t expect_fnum)
{
    if (checksum(hdr) != get16(hdr + 510u)) {
        return 1;
    }
    if (get16(hdr + 2u) != expect_fnum || get16(hdr + 4u) == 0u ||
        get16(hdr + 6u) != 0401u) {
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    FILE *fp;
    uint8_t home[BLK];
    uint8_t mfd_hdr[BLK];
    uint8_t ufd_hdr[BLK];
    uint8_t file_hdr[BLK];
    uint8_t dirblk[BLK];
    uint8_t *map;
    uint32_t mfd_lbn;
    uint32_t ufd_lbn;
    uint32_t file_hdr_lbn;
    uint16_t fmax;
    uint16_t ufd_fnum;
    uint16_t file_fnum;
    uint16_t bogus_fnum;

    if (argc != 3) {
        return 2;
    }
    fp = fopen(argv[2], "r+b");
    if (fp == NULL) {
        return 1;
    }
    if (read_at(fp, (uint64_t)HOME_BLK * BLK, home, sizeof(home)) != 0) {
        fclose(fp);
        return 1;
    }
    fmax = get16(home + 6u);
    header_lbn(home, 4u, &mfd_lbn);
    if (read_at(fp, (uint64_t)mfd_lbn * BLK, mfd_hdr, sizeof(mfd_hdr)) != 0) {
        fclose(fp);
        return 1;
    }
    first_extent_lbn(mfd_hdr, &mfd_lbn);
    if (read_at(fp, (uint64_t)mfd_lbn * BLK, dirblk, sizeof(dirblk)) != 0) {
        fclose(fp);
        return 1;
    }
    ufd_fnum = get16(dirblk + 80u);
    if (ufd_fnum == 0u) {
        fclose(fp);
        return 1;
    }
    header_lbn(home, ufd_fnum, &ufd_lbn);
    if (read_at(fp, (uint64_t)ufd_lbn * BLK, ufd_hdr, sizeof(ufd_hdr)) != 0) {
        fclose(fp);
        return 1;
    }
    first_extent_lbn(ufd_hdr, &ufd_lbn);
    if (read_at(fp, (uint64_t)ufd_lbn * BLK, dirblk, sizeof(dirblk)) != 0) {
        fclose(fp);
        return 1;
    }
    file_fnum = get16(dirblk + 0u);
    if (file_fnum == 0u) {
        fclose(fp);
        return 1;
    }
    header_lbn(home, file_fnum, &file_hdr_lbn);
    if (read_at(fp, (uint64_t)file_hdr_lbn * BLK, file_hdr, sizeof(file_hdr)) != 0) {
        fclose(fp);
        return 1;
    }
    map = file_hdr + (size_t)file_hdr[1] * 2u;
    if (get16(map + 2u) == 0u) {
        fclose(fp);
        return 1;
    }

    if (strcmp(argv[1], "dir-to-ext") == 0) {
        put16(dirblk + 0u, get16(map + 2u));
        put16(dirblk + 2u, get16(map + 4u));
        put16(dirblk + 4u, map[1]);
        if (write_at(fp, (uint64_t)ufd_lbn * BLK, dirblk, sizeof(dirblk)) != 0) {
            fclose(fp);
            return 1;
        }
    } else if (strcmp(argv[1], "dir-rvn") == 0) {
        put16(dirblk + 4u, 1u);
        if (write_at(fp, (uint64_t)ufd_lbn * BLK, dirblk, sizeof(dirblk)) != 0) {
            fclose(fp);
            return 1;
        }
    } else if (strcmp(argv[1], "dir-mismatch") == 0) {
        rad50_name3("WRONGX", dirblk + 6u);
        put16(dirblk + 12u, rad50_word("TMP"));
        put16(dirblk + 14u, 7u);
        if (write_at(fp, (uint64_t)ufd_lbn * BLK, dirblk, sizeof(dirblk)) != 0) {
            fclose(fp);
            return 1;
        }
    } else if (strcmp(argv[1], "dup-entry") == 0) {
        size_t off;

        for (off = 16u; off + 16u <= BLK; off += 16u) {
            if (get16(dirblk + off) == 0u) {
                memcpy(dirblk + off, dirblk, 16u);
                if (write_at(fp, (uint64_t)ufd_lbn * BLK,
                             dirblk, sizeof(dirblk)) != 0) {
                    fclose(fp);
                    return 1;
                }
                if (fflush(fp) != 0) {
                    fclose(fp);
                    return 1;
                }
                fclose(fp);
                return 0;
            }
        }
        fclose(fp);
        return 1;
    } else if (strcmp(argv[1], "break-next") == 0) {
        for (bogus_fnum = fmax; bogus_fnum > 5u; bogus_fnum--) {
            uint8_t candidate[BLK];
            uint32_t candidate_lbn;

            if (bogus_fnum == file_fnum || bogus_fnum == get16(map + 2u)) {
                continue;
            }
            header_lbn(home, bogus_fnum, &candidate_lbn);
            if (read_at(fp, (uint64_t)candidate_lbn * BLK,
                        candidate, sizeof(candidate)) != 0) {
                fclose(fp);
                return 1;
            }
            if (header_looks_free(candidate, bogus_fnum)) {
                put16(map + 2u, bogus_fnum);
                put16(map + 4u, 1u);
                put16(file_hdr + 510u, checksum(file_hdr));
                if (write_at(fp, (uint64_t)file_hdr_lbn * BLK,
                             file_hdr, sizeof(file_hdr)) != 0) {
                    fclose(fp);
                    return 1;
                }
                fflush(fp);
                fclose(fp);
                return 0;
            }
        }
        fclose(fp);
        return 1;
    } else {
        fclose(fp);
        return 2;
    }

    if (fflush(fp) != 0) {
        fclose(fp);
        return 1;
    }
    fclose(fp);
    return 0;
}
EOF
${CC:-cc} -O2 -Wall -Wextra -o "$tmpdir/patch_chain_ref" \
    "$tmpdir/patch_chain_ref.c"
cp "$tmpdir/chain.dsk" "$tmpdir/nonprimary.dsk"
"$tmpdir/patch_chain_ref" dir-to-ext "$tmpdir/nonprimary.dsk"
set +e
./rsx11tool fsck "$tmpdir/nonprimary.dsk" >"$tmpdir/nonprimary_fsck.txt" 2>&1
status_nonprimary=$?
./rsx11tool fsck "$tmpdir/nonprimary.dsk" --repair \
    >"$tmpdir/nonprimary_repair.txt" 2>&1
status_nonprimary_repair=$?
./rsx11tool fsck "$tmpdir/nonprimary.dsk" >"$tmpdir/nonprimary_after.txt" 2>&1
status_nonprimary_after=$?
set -e
./rsx11tool ls "$tmpdir/nonprimary.dsk" '[001,123]' >"$tmpdir/nonprimary_ls_after.txt"

cp "$tmpdir/chain.dsk" "$tmpdir/rvnfix.dsk"
"$tmpdir/patch_chain_ref" dir-rvn "$tmpdir/rvnfix.dsk"
set +e
./rsx11tool fsck "$tmpdir/rvnfix.dsk" >"$tmpdir/rvnfix_fsck.txt" 2>&1
status_rvnfix=$?
./rsx11tool fsck "$tmpdir/rvnfix.dsk" --repair \
    >"$tmpdir/rvnfix_repair.txt" 2>&1
status_rvnfix_repair=$?
./rsx11tool fsck "$tmpdir/rvnfix.dsk" >"$tmpdir/rvnfix_after.txt" 2>&1
status_rvnfix_after=$?
set -e
./rsx11tool ls "$tmpdir/rvnfix.dsk" '[001,123]' >"$tmpdir/rvnfix_ls_after.txt"

cp "$tmpdir/chain.dsk" "$tmpdir/mismatch.dsk"
"$tmpdir/patch_chain_ref" dir-mismatch "$tmpdir/mismatch.dsk"
set +e
./rsx11tool fsck "$tmpdir/mismatch.dsk" >"$tmpdir/mismatch_fsck.txt" 2>&1
status_mismatch=$?
./rsx11tool fsck "$tmpdir/mismatch.dsk" --repair \
    >"$tmpdir/mismatch_repair.txt" 2>&1
status_mismatch_repair=$?
./rsx11tool fsck "$tmpdir/mismatch.dsk" >"$tmpdir/mismatch_after.txt" 2>&1
status_mismatch_after=$?
set -e
./rsx11tool ls "$tmpdir/mismatch.dsk" '[001,123]' >"$tmpdir/mismatch_ls_after.txt"

cp "$tmpdir/chain.dsk" "$tmpdir/dupentry.dsk"
"$tmpdir/patch_chain_ref" dup-entry "$tmpdir/dupentry.dsk"
set +e
./rsx11tool fsck "$tmpdir/dupentry.dsk" >"$tmpdir/dupentry_fsck.txt" 2>&1
status_dupentry=$?
./rsx11tool fsck "$tmpdir/dupentry.dsk" --repair \
    >"$tmpdir/dupentry_repair.txt" 2>&1
status_dupentry_repair=$?
./rsx11tool fsck "$tmpdir/dupentry.dsk" >"$tmpdir/dupentry_after.txt" 2>&1
status_dupentry_after=$?
set -e
./rsx11tool ls "$tmpdir/dupentry.dsk" '[001,123]' >"$tmpdir/dupentry_ls_after.txt"

cp "$tmpdir/chain.dsk" "$tmpdir/badchain.dsk"
"$tmpdir/patch_chain_ref" break-next "$tmpdir/badchain.dsk"
set +e
./rsx11tool fsck "$tmpdir/badchain.dsk" >"$tmpdir/badchain_fsck.txt" 2>&1
status_badchain=$?
./rsx11tool fsck "$tmpdir/badchain.dsk" --repair \
    >"$tmpdir/badchain_repair.txt" 2>&1
status_badchain_repair=$?
./rsx11tool fsck "$tmpdir/badchain.dsk" >"$tmpdir/badchain_after.txt" 2>&1
status_badchain_after=$?
set -e
./rsx11tool ls "$tmpdir/badchain.dsk" '[001,123]' >"$tmpdir/badchain_ls_after.txt"

./rsx11tool mkfs "$tmpdir/orphan.dsk" --blocks 2048 --label ORPHAN \
    >"$tmpdir/orphan_mkfs.txt"
./rsx11tool mkdir "$tmpdir/orphan.dsk" '[001,123]' >"$tmpdir/orphan_mkdir.txt"
iblb_orphan="$(./rsx11tool info "$tmpdir/orphan.dsk" | awk '/Index bitmap:/ {print $8}')"
mfd_hdr_ptr_off=$(( (iblb_orphan + 4) * 512 + 102 ))
set -- $(od -An -tu1 -N 4 -j "$mfd_hdr_ptr_off" "$tmpdir/orphan.dsk")
mfd_lbn_orphan=$(( $1 * 65536 + $3 + 256 * $4 ))
mfd_dir_slot_offset=$(( mfd_lbn_orphan * 512 + 80 ))
dd if=/dev/zero of="$tmpdir/orphan.dsk" bs=1 seek="$mfd_dir_slot_offset" \
    count=16 conv=notrunc >/dev/null 2>&1
cp "$tmpdir/orphan.dsk" "$tmpdir/orphan_repair.dsk"
set +e
./rsx11tool fsck "$tmpdir/orphan.dsk" >"$tmpdir/orphan_fsck.txt" 2>&1
status_orphan=$?
./rsx11tool fsck "$tmpdir/orphan_repair.dsk" --repair \
    >"$tmpdir/orphan_repair_fsck.txt" 2>&1
status_orphan_repair=$?
set -e
./rsx11tool fsck "$tmpdir/orphan_repair.dsk" \
    >"$tmpdir/orphan_repair_check.txt" 2>&1

cat >"$tmpdir/patch_orphan_chain.c" <<'EOF'
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLK 512u

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32hi(const uint8_t *p)
{
    return ((uint32_t)get16(p) << 16) | (uint32_t)get16(p + 2u);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t checksum(const uint8_t *blk)
{
    uint32_t sum = 0;
    size_t i;

    for (i = 0; i < 510u; i += 2u) {
        sum += get16(blk + i);
    }
    return (uint16_t)sum;
}

static int read_at(FILE *fp, uint64_t off, void *buf, size_t len)
{
    if (fseek(fp, (long)off, SEEK_SET) != 0) {
        return -1;
    }
    return fread(buf, 1, len, fp) == len ? 0 : -1;
}

static int write_at(FILE *fp, uint64_t off, const void *buf, size_t len)
{
    if (fseek(fp, (long)off, SEEK_SET) != 0) {
        return -1;
    }
    return fwrite(buf, 1, len, fp) == len ? 0 : -1;
}

int main(int argc, char **argv)
{
    FILE *fp;
    uint8_t home[BLK];
    uint8_t hdr[BLK];
    uint8_t dirblk[BLK];
    uint8_t zero[16];
    uint8_t *map;
    uint32_t iblb;
    uint16_t fmax;
    uint32_t mfd_lbn;
    uint32_t ufd_lbn;
    uint16_t ufd_fnum;
    uint16_t file_fnum;
    uint16_t bogus_fnum;

    if (argc != 2) {
        return 1;
    }
    memset(zero, 0, sizeof(zero));
    fp = fopen(argv[1], "r+b");
    if (fp == NULL) {
        return 1;
    }
    if (read_at(fp, BLK, home, sizeof(home)) != 0) {
        fclose(fp);
        return 1;
    }
    iblb = get32hi(home + 2u);
    fmax = get16(home + 6u);

    if (read_at(fp, (uint64_t)(iblb + 4u) * BLK, hdr, sizeof(hdr)) != 0) {
        fclose(fp);
        return 1;
    }
    map = hdr + (size_t)hdr[1] * 2u;
    mfd_lbn = ((uint32_t)map[10] << 16) | (uint32_t)get16(map + 12u);
    if (read_at(fp, (uint64_t)mfd_lbn * BLK, dirblk, sizeof(dirblk)) != 0) {
        fclose(fp);
        return 1;
    }
    ufd_fnum = get16(dirblk + 80u);
    if (ufd_fnum == 0u) {
        fclose(fp);
        return 1;
    }

    if (read_at(fp, (uint64_t)(iblb + ufd_fnum) * BLK, hdr, sizeof(hdr)) != 0) {
        fclose(fp);
        return 1;
    }
    map = hdr + (size_t)hdr[1] * 2u;
    ufd_lbn = ((uint32_t)map[10] << 16) | (uint32_t)get16(map + 12u);
    if (read_at(fp, (uint64_t)ufd_lbn * BLK, dirblk, sizeof(dirblk)) != 0) {
        fclose(fp);
        return 1;
    }
    file_fnum = get16(dirblk + 0u);
    bogus_fnum = (uint16_t)(file_fnum + 20u);
    if (file_fnum == 0u || bogus_fnum > fmax) {
        fclose(fp);
        return 1;
    }

    if (read_at(fp, (uint64_t)(iblb + file_fnum) * BLK, hdr, sizeof(hdr)) != 0) {
        fclose(fp);
        return 1;
    }
    map = hdr + (size_t)hdr[1] * 2u;
    map[0] = 0u;
    map[1] = 0u;
    put16(map + 2u, bogus_fnum);
    put16(map + 4u, 1u);
    put16(hdr + 510u, checksum(hdr));

    if (write_at(fp, (uint64_t)(iblb + file_fnum) * BLK, hdr, sizeof(hdr)) != 0 ||
        write_at(fp, (uint64_t)ufd_lbn * BLK, zero, sizeof(zero)) != 0 ||
        fflush(fp) != 0) {
        fclose(fp);
        return 1;
    }

    fclose(fp);
    return 0;
}
EOF
${CC:-cc} -O2 -Wall -Wextra -o "$tmpdir/patch_orphan_chain" \
    "$tmpdir/patch_orphan_chain.c"
./rsx11tool mkfs "$tmpdir/orphan_chain.dsk" --blocks 2048 --label OCHAIN \
    >"$tmpdir/orphan_chain_mkfs.txt"
./rsx11tool mkdir "$tmpdir/orphan_chain.dsk" '[001,123]' \
    >"$tmpdir/orphan_chain_mkdir.txt"
./rsx11tool add "$tmpdir/orphan_chain.dsk" "$tmpdir/input.txt" \
    '[001,123]CHAINX.TXT' >"$tmpdir/orphan_chain_add.txt"
"$tmpdir/patch_orphan_chain" "$tmpdir/orphan_chain.dsk"
set +e
./rsx11tool fsck "$tmpdir/orphan_chain.dsk" >"$tmpdir/orphan_chain_fsck.txt" 2>&1
status_orphan_chain=$?
./rsx11tool fsck "$tmpdir/orphan_chain.dsk" --repair \
    >"$tmpdir/orphan_chain_repair.txt" 2>&1
status_orphan_chain_repair=$?
./rsx11tool fsck "$tmpdir/orphan_chain.dsk" >"$tmpdir/orphan_chain_after.txt" 2>&1
status_orphan_chain_after=$?
set -e

./rsx11tool mkfs "$tmpdir/badslot.dsk" --blocks 2048 --label BADSLOT \
    >"$tmpdir/badslot_mkfs.txt"
iblb_badslot="$(./rsx11tool info "$tmpdir/badslot.dsk" | awk '/Index bitmap:/ {print $8}')"
badslot_fnum=64
badslot_fnum_o="$(printf '%06o' "$badslot_fnum")"
badslot_byte_index=$(( (badslot_fnum - 1) / 8 ))
badslot_bit_index=$(( (badslot_fnum - 1) % 8 ))
badslot_offset=$(( iblb_badslot * 512 + badslot_byte_index ))
badslot_cur_byte="$(od -An -tu1 -N1 -j "$badslot_offset" "$tmpdir/badslot.dsk" | tr -d ' ')"
badslot_new_byte=$(( badslot_cur_byte | (1 << badslot_bit_index) ))
printf '%b' "\\$(printf '%03o' "$badslot_new_byte")" | \
    dd of="$tmpdir/badslot.dsk" bs=1 seek="$badslot_offset" conv=notrunc >/dev/null 2>&1
set +e
./rsx11tool fsck "$tmpdir/badslot.dsk" >"$tmpdir/badslot_fsck.txt" 2>&1
status_badslot=$?
./rsx11tool fsck "$tmpdir/badslot.dsk" --repair >"$tmpdir/badslot_repair.txt" 2>&1
status_badslot_repair=$?
./rsx11tool fsck "$tmpdir/badslot.dsk" >"$tmpdir/badslot_after.txt" 2>&1
status_badslot_after=$?
set -e

./rsx11tool mkfs "$tmpdir/dangling.dsk" --blocks 2048 --label DANGL \
    >"$tmpdir/dangling_mkfs.txt"
./rsx11tool mkdir "$tmpdir/dangling.dsk" '[001,123]' >"$tmpdir/dangling_mkdir.txt"
./rsx11tool add "$tmpdir/dangling.dsk" "$tmpdir/input.txt" \
    '[001,123]BROKEN.TXT' >"$tmpdir/dangling_add.txt"
iblb_dangling="$(./rsx11tool info "$tmpdir/dangling.dsk" | awk '/Index bitmap:/ {print $8}')"
mfd_hdr_ptr_off=$(( (iblb_dangling + 4) * 512 + 102 ))
set -- $(od -An -tu1 -N 4 -j "$mfd_hdr_ptr_off" "$tmpdir/dangling.dsk")
mfd_lbn_dangling=$(( $1 * 65536 + $3 + 256 * $4 ))
mfd_dir_slot_offset=$(( mfd_lbn_dangling * 512 + 80 ))
set -- $(od -An -tu1 -N 4 -j "$mfd_dir_slot_offset" "$tmpdir/dangling.dsk")
ufd_fnum=$(( $1 + 256 * $2 ))
ufd_hdr_ptr_off=$(( (iblb_dangling + ufd_fnum) * 512 + 102 ))
set -- $(od -An -tu1 -N 4 -j "$ufd_hdr_ptr_off" "$tmpdir/dangling.dsk")
ufd_lbn_dangling=$(( $1 * 65536 + $3 + 256 * $4 ))
printf '\0\0' | dd of="$tmpdir/dangling.dsk" bs=1 seek=$(( ufd_lbn_dangling * 512 )) \
    conv=notrunc >/dev/null 2>&1
set +e
./rsx11tool fsck "$tmpdir/dangling.dsk" >"$tmpdir/dangling_fsck.txt" 2>&1
status_dangling=$?
./rsx11tool fsck "$tmpdir/dangling.dsk" --repair \
    >"$tmpdir/dangling_repair.txt" 2>&1
status_dangling_repair=$?
./rsx11tool fsck "$tmpdir/dangling.dsk" >"$tmpdir/dangling_after.txt" 2>&1
status_dangling_after=$?
set -e
./rsx11tool ls "$tmpdir/dangling.dsk" '[001,123]' >"$tmpdir/dangling_ls_after.txt"

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
grep -q "^Supported types:$" "$tmpdir/mkfs_list.txt"
grep -q "^  rk05  disk" "$tmpdir/mkfs_list.txt"
grep -q "^  rl02  disk" "$tmpdir/mkfs_list.txt"
cmp -s "$tmpdir/rsxm26.boot" "$tmpdir/rsxm26.block0"

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
grep -q "created $tmpdir/mkfs_type.dsk (4872 blocks)" "$tmpdir/mkfs_type.txt"
grep -q "created $tmpdir/mkfs_bootcmd.dsk (2048 blocks)" "$tmpdir/mkfs_bootcmd.txt"
grep -q "created $tmpdir/mkfs_bootsrc.dsk (2048 blocks)" "$tmpdir/mkfs_bootsrc.txt"
grep -q "created $tmpdir/mkfs_bootfile.dsk (2048 blocks)" "$tmpdir/mkfs_bootfile.txt"
grep -q "Volume label:   MKTEST" "$tmpdir/mkfs_info.txt"
grep -q "Volume label:   MKTYPE" "$tmpdir/mkfs_type_info.txt"
grep -q "Total blocks:   4872" "$tmpdir/mkfs_type_info.txt"
grep -q "Volume label:   BOOTCMD" "$tmpdir/mkfs_bootcmd_info.txt"
grep -q "Volume label:   BOOTSRC" "$tmpdir/mkfs_bootsrc_info.txt"
grep -q "Volume label:   BOOTFILE" "$tmpdir/mkfs_bootfile_info.txt"
test "$(wc -c < "$tmpdir/mkfs_type.dsk")" -eq 2494464
cmp -s "$tmpdir/rsxm26.boot" "$tmpdir/mkfs_bootcmd.block0"
cmp -s "$tmpdir/rsxm26.boot" "$tmpdir/mkfs_bootsrc.block0"
cmp -s "$tmpdir/rsxm26.boot" "$tmpdir/mkfs_bootfile.block0"
grep -q "MFD.*INDEXF\\.SYS;1" "$tmpdir/mkfs_ls.txt"
grep -q "MFD.*BITMAP\\.SYS;1" "$tmpdir/mkfs_ls.txt"
grep -q "MFD.*BADBLK\\.SYS;1" "$tmpdir/mkfs_ls.txt"
grep -q "MFD.*000000\\.DIR;1" "$tmpdir/mkfs_ls.txt"
grep -q "MFD.*CORIMG\\.SYS;1" "$tmpdir/mkfs_ls.txt"
grep -q "fsck: clean" "$tmpdir/mkfs_fsck.txt"
grep -q "fsck: clean" "$tmpdir/mkfs_type_fsck.txt"
grep -q "fsck: clean" "$tmpdir/mkfs_bootcmd_fsck.txt"
grep -q "fsck: clean" "$tmpdir/mkfs_bootsrc_fsck.txt"
grep -q "fsck: clean" "$tmpdir/mkfs_bootfile_fsck.txt"
test "$frag_count" -gt 3
grep -q "added \\[001,123\\]PAIR\\.BIN;1" "$tmpdir/frag_add_pair.txt"
grep -q "\\[001,123\\].*PAIR\\.BIN;1.*  *2  " "$tmpdir/frag_ls.txt"
grep -q "extracted 1 file(s)" "$tmpdir/frag_extract.txt"
cmp -s "$tmpdir/frag_pair.bin" "$tmpdir/frag_out/001_123/PAIR.BIN;1"
grep -q "fsck: clean" "$tmpdir/frag_fsck.txt"
grep -q "Volume label:   MKTEST" "$tmpdir/mkfs_alt_info.txt"
grep -q "fsck: clean" "$tmpdir/mkfs_alt_fsck.txt"
grep -q "DECFILE11A" "$tmpdir/mkfs_strings.txt"
grep -q "\\[001,001\\]" "$tmpdir/mkfs_strings.txt"
grep -q "^1 0 16 0$" "$tmpdir/mkfs_mfd_fcs.txt"
grep -q "^1894 0 0 2048$" "$tmpdir/mkfs_bitmap_ctrl.txt"
grep -E -q "^Created:        [0-9]{2}[A-Z]{3}[0-9]{2}[0-9]{6}$" \
    "$tmpdir/mkfs_info.txt"
grep -q "created \\[001,123\\]" "$tmpdir/mkfs_mkdir.txt"
grep -q "added \\[001,123\\]EMPTY\\.DAT;1" "$tmpdir/mkfs_add_empty.txt"
grep -q "added \\[001,123\\]MKTEST\\.TXT;1" "$tmpdir/mkfs_add.txt"
grep -q "\\[001,123\\].*EMPTY\\.DAT;1.*  *0  " "$tmpdir/mkfs_ls_ufd.txt"
grep -q "\\[001,123\\].*MKTEST\\.TXT;1" "$tmpdir/mkfs_ls_ufd.txt"
grep -q "extracted 1 file(s)" "$tmpdir/mkfs_extract_empty.txt"
test "$(wc -c < "$tmpdir/mkfs_empty_out/001_123/EMPTY.DAT;1")" -eq 0
grep -q "extracted 1 file(s)" "$tmpdir/mkfs_extract.txt"
cmp -s "$tmpdir/mkfs_input.txt" "$tmpdir/mkfs_out/001_123/MKTEST.TXT;1"
grep -q "removed \\[001,123\\]EMPTY\\.DAT;1" "$tmpdir/mkfs_rm_empty.txt"
grep -q "removed \\[001,123\\]MKTEST\\.TXT;1" "$tmpdir/mkfs_rm.txt"
grep -q "removed \\[001,123\\]" "$tmpdir/mkfs_rmdir.txt"
grep -q "fsck: clean" "$tmpdir/mkfs_fsck_after.txt"
grep -q "\\[001,123\\].*CHAINA\\.BIN;1" "$tmpdir/chain_ls.txt"
grep -q "\\[001,123\\].*CHAINA\\.BIN;1.*  *2  " "$tmpdir/chain_ls.txt"
grep -q "extracted 1 file(s)" "$tmpdir/chain_extract.txt"
test "$(wc -c < "$tmpdir/chain_out/001_123/CHAINA.BIN;1")" -eq 1024
cmp -s "$tmpdir/chain_expected.bin" "$tmpdir/chain_out/001_123/CHAINA.BIN;1"
grep -q "fsck: clean" "$tmpdir/chain_fsck.txt"
test "$status_nonprimary" -eq 2
grep -q "CHAINA\\.BIN;1: directory record points to non-primary header segment" \
    "$tmpdir/nonprimary_fsck.txt"
test "$status_nonprimary_repair" -eq 0
grep -q "fsck: repaired" "$tmpdir/nonprimary_repair.txt"
test "$status_nonprimary_after" -eq 0
grep -q "fsck: clean" "$tmpdir/nonprimary_after.txt"
! grep -q "CHAINA\\.BIN;1" "$tmpdir/nonprimary_ls_after.txt"
test "$status_rvnfix" -eq 2
grep -q "CHAINA\\.BIN;1: directory record FID disagrees with header RVN" \
    "$tmpdir/rvnfix_fsck.txt"
test "$status_rvnfix_repair" -eq 0
grep -q "fsck: repaired" "$tmpdir/rvnfix_repair.txt"
test "$status_rvnfix_after" -eq 0
grep -q "fsck: clean" "$tmpdir/rvnfix_after.txt"
grep -q "CHAINA\\.BIN;1" "$tmpdir/rvnfix_ls_after.txt"
test "$status_mismatch" -eq 2
grep -q "WRONGX\\.TMP;7: directory record name/type/version disagrees with header" \
    "$tmpdir/mismatch_fsck.txt"
test "$status_mismatch_repair" -eq 0
grep -q "fsck: repaired" "$tmpdir/mismatch_repair.txt"
test "$status_mismatch_after" -eq 0
grep -q "fsck: clean" "$tmpdir/mismatch_after.txt"
grep -q "CHAINA\\.BIN;1" "$tmpdir/mismatch_ls_after.txt"
! grep -q "WRONGX\\.TMP;7" "$tmpdir/mismatch_ls_after.txt"
test "$status_dupentry" -eq 2
grep -q "CHAINA\\.BIN;1: directory record is a duplicate of an earlier entry" \
    "$tmpdir/dupentry_fsck.txt"
test "$status_dupentry_repair" -eq 0
grep -q "fsck: repaired" "$tmpdir/dupentry_repair.txt"
test "$status_dupentry_after" -eq 0
grep -q "fsck: clean" "$tmpdir/dupentry_after.txt"
test "$(grep -c 'CHAINA\.BIN;1' "$tmpdir/dupentry_ls_after.txt")" -eq 1
test "$status_badchain" -eq 2
grep -q "CHAINA\\.BIN;1: directory record points to inconsistent header chain" \
    "$tmpdir/badchain_fsck.txt"
test "$status_badchain_repair" -eq 0
grep -q "fsck: repaired" "$tmpdir/badchain_repair.txt"
test "$status_badchain_after" -eq 0
grep -q "fsck: clean" "$tmpdir/badchain_after.txt"
! grep -q "CHAINA\\.BIN;1" "$tmpdir/badchain_ls_after.txt"
test "$status_orphan" -eq 2
grep -q "orphan header 001123\\.DIR;1" "$tmpdir/orphan_fsck.txt"
test "$status_orphan_repair" -eq 0
grep -q "fsck: repaired" "$tmpdir/orphan_repair_fsck.txt"
grep -q "fsck: clean" "$tmpdir/orphan_repair_check.txt"
test "$status_orphan_chain" -eq 2
grep -q "orphan header CHAINX\\.TXT;1" "$tmpdir/orphan_chain_fsck.txt"
grep -q "chain is inconsistent" "$tmpdir/orphan_chain_fsck.txt"
test "$status_orphan_chain_repair" -eq 0
grep -q "fsck: repaired" "$tmpdir/orphan_chain_repair.txt"
test "$status_orphan_chain_after" -eq 0
grep -q "fsck: clean" "$tmpdir/orphan_chain_after.txt"
test "$status_badslot" -eq 2
grep -q "FID $badslot_fnum_o allocated, but header slot is empty or invalid" \
    "$tmpdir/badslot_fsck.txt"
test "$status_badslot_repair" -eq 0
grep -q "fsck: repaired" "$tmpdir/badslot_repair.txt"
test "$status_badslot_after" -eq 0
grep -q "fsck: clean" "$tmpdir/badslot_after.txt"
test "$status_dangling" -eq 2
grep -q "BROKEN\\.TXT;1: directory record references invalid FID" \
    "$tmpdir/dangling_fsck.txt"
test "$status_dangling_repair" -eq 0
grep -q "fsck: repaired" "$tmpdir/dangling_repair.txt"
test "$status_dangling_after" -eq 0
grep -q "fsck: clean" "$tmpdir/dangling_after.txt"
! grep -q "BROKEN\\.TXT;1" "$tmpdir/dangling_ls_after.txt"
test "$status_bad" -eq 2
test "$status_repair" -eq 2
test "$status_after" -eq 2
grep -q "\\[001,123\\]ZZFSCK\\.TXT;1: index bitmap marks 1 header slot(s) free" \
    "$tmpdir/fsck_bad.txt"
grep -q "repaired" "$tmpdir/fsck_repair.txt"
! grep -q "\\[001,123\\]ZZFSCK\\.TXT;1: index bitmap bit is clear" \
    "$tmpdir/fsck_after.txt"

echo "rsx11tool validation passed"
