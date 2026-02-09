#include "dev_rh11.h"

#include "bus.h"
#include "devio.h"
#include "irq.h"
#include "irq_latch.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* RH11 (Massbus adapter) base range in 16-bit I/O page view (octal). */
#define RH11_BASE 0177440

/* Registers (octal) */
#define RHCS1 0177440
#define RHWC  0177442
#define RHBA  0177444
#define RHDA  0177446
#define RHCS2 0177450
#define RHDS  0177452
#define RHER  0177454
#define RHAS  0177456
#define RHDC  0177460
#define RHDB  0177462

/* RHCS1 low-byte bits */
#define RHCS1_GO        0000001
#define RHCS1_FUNC_MASK 0000076
#define RHCS1_IE        0000100
#define RHCS1_DONEB     0000200

/* Minimal command subset (phase 1)
 *
 * RT-11 RH bootstrap on this platform uses GO|020 for initial disk read.
 * Keep both read opcodes accepted for compatibility.
 */
#define RH11_FUNC_SEEK   0000004
#define RH11_FUNC_NOP0   0000000
#define RH11_FUNC_NOP2   0000002
#define RH11_FUNC_READ20 0000020
#define RH11_FUNC_WRITE  0000060
#define RH11_FUNC_READ70 0000070

/* Minimal status/error bits */
#define RHDS_DRY   0000200
#define RHDS_0400  0000400
#define RHER_BADFN 0000001
#define RHER_NXM   0000002
#define RHER_NMED  0000004
#define RHER_IO    0000010

/* HKCS2 default status (SIMH-compatible for unit 0 present/online). */
#define RHCS2_BASE_0100 0000100
/* HKDS default ready/online signature used by RT-11 bootstrap paths. */
#define RHDS_BASE_100701 0100701

/* HK/RK06/RK07 geometry used by RT-11 RH bootstrap. */
#define RH11_HEADS_PER_CYL     0000003
#define RH11_SECTORS_PER_TRACK 0000026
#define RH11_WORDS_PER_SECTOR  0000400
#define RH11_SECTOR_BYTES      0001000
#define HKDA_SEC_MASK          0000037
#define HKDA_HEAD_MASK         0003400
#define HKDA_HEAD_SHIFT        8

static uint16_t rhcs1, rhwc, rhba, rhda, rhcs2, rhds, rher, rhas, rhdc, rhdb;
static irq_latch_t rh_l;
static FILE *rh_fp = NULL;
static int rh_debug = 0;
static int rh_debug_active = 0;
static int rh_sec_one_based = 0;

static uint16_t *rh_reg_ptr(uint16_t addr) {
  switch (addr & 0177776) {
  case RHCS1:
    return &rhcs1;
  case RHWC:
    return &rhwc;
  case RHBA:
    return &rhba;
  case RHDA:
    return &rhda;
  case RHCS2:
    return &rhcs2;
  case RHDS:
    return &rhds;
  case RHER:
    return &rher;
  case RHAS:
    return &rhas;
  case RHDC:
    return &rhdc;
  case RHDB:
    return &rhdb;
  default:
    return NULL;
  }
}

static void rh_sync_cs1_bits(void) {
  rhcs1 &= (uint16_t)~RHCS1_DONEB;
  if (rh_l.done)
    rhcs1 |= RHCS1_DONEB;

  rhcs1 &= (uint16_t)~RHCS1_IE;
  if (rh_l.ie)
    rhcs1 |= RHCS1_IE;
}

static uint16_t rh_da_get_sec(uint16_t da) {
  return (uint16_t)(da & HKDA_SEC_MASK);
}

static uint16_t rh_da_get_head(uint16_t da) {
  return (uint16_t)((da & HKDA_HEAD_MASK) >> HKDA_HEAD_SHIFT);
}

static uint16_t rh_da_set_head_sec(uint16_t da, uint16_t head, uint16_t sec) {
  da &= (uint16_t)~(HKDA_HEAD_MASK | HKDA_SEC_MASK);
  da |= (uint16_t)(((head & 07) << HKDA_HEAD_SHIFT) | (sec & HKDA_SEC_MASK));
  return da;
}

static int rh_lba(uint16_t dc, uint16_t da, uint32_t *lba_out) {
  uint16_t sec = rh_da_get_sec(da);
  uint16_t head = rh_da_get_head(da);

  if (rh_sec_one_based) {
    if (sec == 0) {
      sec = RH11_SECTORS_PER_TRACK;
    }
    sec--;
  }

  if (sec >= RH11_SECTORS_PER_TRACK || head >= RH11_HEADS_PER_CYL) {
    return -1;
  }

  *lba_out =
      (((uint32_t)dc * RH11_HEADS_PER_CYL) + (uint32_t)head) * RH11_SECTORS_PER_TRACK +
      (uint32_t)sec;
  return 0;
}

static void rh_advance_sector(uint16_t *dc, uint16_t *da) {
  uint16_t sec = rh_da_get_sec(*da);
  uint16_t head = rh_da_get_head(*da);

  sec++;
  if (sec >= RH11_SECTORS_PER_TRACK) {
    sec = 0;
    head++;
    if (head >= RH11_HEADS_PER_CYL) {
      head = 0;
      *dc = (uint16_t)(*dc + 1);
    }
  }

  *da = rh_da_set_head_sec(*da, head, sec);
}

static void rh_set_error(uint16_t bit) {
  rher |= bit;
  rhcs2 |= 0000001;
}

static int rh_dma_read_word(uint16_t addr, uint16_t *w) {
  paddr_t pa = (paddr_t)addr;
  if (!bus_addr_is_ram(pa) || !bus_addr_is_ram((paddr_t)(pa + 1)))
    return -1;
  if (bus_is_nxm(pa) || bus_is_nxm((paddr_t)(pa + 1)))
    return -1;
  *w = bus_read16(pa);
  return 0;
}

static int rh_dma_write_word(uint16_t addr, uint16_t w) {
  paddr_t pa = (paddr_t)addr;
  if (!bus_addr_is_ram(pa) || !bus_addr_is_ram((paddr_t)(pa + 1)))
    return -1;
  if (bus_is_nxm(pa) || bus_is_nxm((paddr_t)(pa + 1)))
    return -1;
  bus_write16(pa, w);
  return 0;
}

static void rh_finish_command(void) {
  rhcs1 &= (uint16_t)~RHCS1_GO;
  rhds |= RHDS_DRY;
  irq_latch_event_set_done(&rh_l);
  rh_sync_cs1_bits();

  if (rh_debug && rh_debug_active) {
    fprintf(stderr,
            "RH11 DONE cs1=%06o wc=%06o ba=%06o dc=%06o da=%06o cs2=%06o er=%06o\n",
            rhcs1, rhwc, rhba, rhdc, rhda, rhcs2, rher);
    rh_debug_active = 0;
  }
}

static void rh_transfer(int is_write) {
  int16_t wc = (int16_t)rhwc;
  int words = (wc < 0) ? -wc : 0;
  uint16_t cur_wc = rhwc;
  uint16_t cur_ba = rhba;
  uint16_t cur_dc = rhdc;
  uint16_t cur_da = rhda;
  int w_in_sec = 0;

  if (!rh_fp) {
    rh_set_error(RHER_NMED);
    rh_finish_command();
    return;
  }

  if (words <= 0) {
    rh_finish_command();
    return;
  }

  for (int i = 0; i < words; i++) {
    uint32_t lba = 0;
    uint32_t off = lba * RH11_SECTOR_BYTES + (uint32_t)w_in_sec * 2u;
    if (rh_lba(cur_dc, cur_da, &lba) != 0) {
      rh_set_error(RHER_IO);
      break;
    }
    off = lba * RH11_SECTOR_BYTES + (uint32_t)w_in_sec * 2u;

    if (fseek(rh_fp, (long)off, SEEK_SET) != 0) {
      rh_set_error(RHER_IO);
      break;
    }

    if (is_write) {
      uint16_t w = 0;
      uint8_t lo, hi;
      if (rh_dma_read_word(cur_ba, &w) != 0) {
        rh_set_error(RHER_NXM);
        break;
      }
      lo = (uint8_t)(w & 000377);
      hi = (uint8_t)((w >> 8) & 000377);
      if (fwrite(&lo, 1, 1, rh_fp) != 1 || fwrite(&hi, 1, 1, rh_fp) != 1) {
        rh_set_error(RHER_IO);
        break;
      }
    } else {
      uint8_t lo, hi;
      uint16_t w;
      if (fread(&lo, 1, 1, rh_fp) != 1 || fread(&hi, 1, 1, rh_fp) != 1) {
        rh_set_error(RHER_IO);
        break;
      }
      w = (uint16_t)(lo | ((uint16_t)hi << 8));
      if (rh_dma_write_word(cur_ba, w) != 0) {
        rh_set_error(RHER_NXM);
        break;
      }
    }

    cur_ba = (uint16_t)(cur_ba + 2);
    cur_wc = (uint16_t)(cur_wc + 1);

    w_in_sec++;
    if (w_in_sec >= RH11_WORDS_PER_SECTOR) {
      w_in_sec = 0;
      rh_advance_sector(&cur_dc, &cur_da);
    }
  }

  if (is_write) {
    fflush(rh_fp);
  }

  rhba = cur_ba;
  rhwc = cur_wc;
  rhdc = cur_dc;
  rhda = cur_da;
  rh_finish_command();
}

static uint8_t rh_read8(uint16_t addr) {
  uint16_t *rp = rh_reg_ptr(addr);
  uint16_t v;

  if (!rp)
    return 0;

  if ((addr & 0177776) == RHCS1)
    rh_sync_cs1_bits();

  v = *rp;
  if (addr & 1)
    return (uint8_t)((v >> 8) & 000377);
  return (uint8_t)(v & 000377);
}

static void rh_write8(uint16_t addr, uint8_t b) {
  uint16_t *rp = rh_reg_ptr(addr);
  uint16_t old;
  uint16_t v;

  if (!rp)
    return;

  old = *rp;
  if (addr & 1)
    v = (uint16_t)((old & 000377) | ((uint16_t)b << 8));
  else
    v = (uint16_t)((old & 0177400) | b);
  *rp = v;

  if ((addr & 0177776) == RHCS1 && !(addr & 1)) {
    int old_go = (old & RHCS1_GO) ? 1 : 0;
    int new_go = (rhcs1 & RHCS1_GO) ? 1 : 0;

    irq_latch_set_ie(&rh_l, (rhcs1 & RHCS1_IE) ? 1 : 0);

    if (new_go && !old_go) {
      irq_latch_sw_clear_done(&rh_l);
      rher = 0;
      rhcs2 = RHCS2_BASE_0100;
      rhds &= (uint16_t)~RHDS_DRY;
      rh_sync_cs1_bits();
      if (rh_debug) {
        rh_debug_active = 1;
        fprintf(stderr,
                "RH11 GO cs1=%06o wc=%06o ba=%06o dc=%06o da=%06o func=%03o\n",
                rhcs1, rhwc, rhba, rhdc, rhda, rhcs1 & RHCS1_FUNC_MASK);
      }
    }
  }
}

int rh11_irq_pending(void) { return rh_l.irq_req ? 1 : 0; }

void rh11_irq_ack(void) { irq_latch_ack(&rh_l); }

int rh11_init(void) {
  static const io_range_t r = {RH11_BASE, RHDB, rh_read8, rh_write8, "RH11"};
  static const irq_source_t s = {"RH11", 000254, 5, rh11_irq_pending,
                                  rh11_irq_ack};

  rh_debug = (getenv("RH11_DEBUG") != NULL);
  rh_sec_one_based = (getenv("RH11_SECTOR_ONE_BASED") != NULL);
  if (devio_register(&r) != 0)
    return -1;
  if (irq_register(&s) != 0)
    return -1;

  rh11_reset();
  return 0;
}

void rh11_reset(void) {
  rhcs1 = 0;
  rhwc = 0;
  rhba = 0;
  rhda = 0;
  rhcs2 = RHCS2_BASE_0100;
  rhds = RHDS_BASE_100701;
  rher = 0;
  rhas = 0;
  rhdc = 0;
  rhdb = 0;

  irq_latch_reset(&rh_l);
  rh_l.done = 0;
  rh_l.irq_armed = 1;
  irq_latch_event_set_done(&rh_l);
  rh_sync_cs1_bits();
}

void rh11_poll(void) {
  uint16_t func;

  if (!(rhcs1 & RHCS1_GO))
    return;

  func = (uint16_t)(rhcs1 & RHCS1_FUNC_MASK);
  switch (func) {
  case RH11_FUNC_NOP0:
  case RH11_FUNC_NOP2:
    rh_finish_command();
    return;
  case RH11_FUNC_READ20:
  case RH11_FUNC_READ70:
    rh_transfer(0);
    return;
  case RH11_FUNC_WRITE:
    rh_transfer(1);
    return;
  case RH11_FUNC_SEEK:
    rh_finish_command();
    return;
  default:
    /* RT-11 probe/maintenance sequences may issue non-data commands here. */
    rh_finish_command();
    return;
  }
}

int rh11_open_image(const char *path) {
  if (rh_fp)
    fclose(rh_fp);
  rh_fp = fopen(path, "r+b");
  if (!rh_fp)
    return -1;
  return 0;
}

void rh11_close_image(void) {
  if (rh_fp) {
    fclose(rh_fp);
    rh_fp = NULL;
  }
}

int rh11_boot_copy(void *dest, size_t len) {
  if (!rh_fp)
    return -1;
  if (fseek(rh_fp, 0, SEEK_SET) != 0)
    return -1;
  return (fread(dest, 1, len, rh_fp) == len) ? 0 : -1;
}
