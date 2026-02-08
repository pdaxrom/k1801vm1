#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adapter_core.h"
#include "bus.h"
#include "dev_rk11.h"
#include "dev_sr.h"

/* core headers (read-only) */
#include "../core/core.h" /* TODO: replace with actual header providing regs + cpu step/run */
#include "../core/disas.h"

static int parse_cpu_model(const char *name, byte *model) {
  if (!name || !model)
    return -1;

  if (!strcmp(name, "dcj11") || !strcmp(name, "11/03")) {
    *model = DCJ11;
    return 0;
  }
  if (!strcmp(name, "k1801vm1") || !strcmp(name, "vm1")) {
    *model = K1801VM1;
    return 0;
  }
  if (!strcmp(name, "k1801vm1g") || !strcmp(name, "vm1g")) {
    *model = K1801VM1G;
    return 0;
  }
  if (!strcmp(name, "k1801vm2") || !strcmp(name, "vm2")) {
    *model = K1801VM2;
    return 0;
  }
  if (!strcmp(name, "k1806vm2")) {
    *model = K1806VM2;
    return 0;
  }

  return -1;
}

static void usage(const char *argv0) {
#if defined(LSI11_TARGET_1134)
  const char *target = "pdp1134";
#else
  const char *target = "lsi11";
#endif

  fprintf(stderr,
          "Usage:\n"
          "  %s -rk <rk05.img> [-bootcopy|-bootrt11] [-cpu <model>]\n"
          "\n"
          "Target profile:\n"
          "  %s\n"
          "\n"
          "Options:\n"
          "  -cpu <model>    CPU model: dcj11 (default), 11/03, "
          "k1801vm1, k1801vm1g, k1801vm2, k1806vm2\n"
          "  -rk <path>      Attach RK05 image\n"
          "  -bootcopy       Copy first 010000 bytes from RK image into RAM at "
          "000000\n"
          "  -bootrt11       Copy first 01000 bytes (or 2nd block if empty) "
          "into RAM at 000000 and jump to 000000\n"
          "  -trace          Trace each instruction\n"
          "  -trace-regs     With -trace, also dump registers\n"
          "  -traceirq       Trace delivered IRQ vectors\n"
          "  -tracenxm       Trace NXM traps\n"
          "  -exit-on-abort  Exit emulator on HALT/abort\n"
          "  -check-config   Validate machine config and exit\n",
          argv0, target);
  fprintf(stderr,
          "  -load <file>    Load binary file into RAM\n"
          "  -addr <oct>     Load address (octal) for -load (default 0)\n"
          "  -pc <oct>       Set initial PC (R7) to octal address\n"
          "  -sr <oct>       Set SR switch register (0177570) value\n"
#if defined(LSI11_TARGET_1134)
          "  -ram <kb>       RAM size in KB (default 4096, must be multiple of 8)\n"
          "  -dl11-alias     Enable DL11 alias 0176500..0176507\n"
          "  -no-dl11-alias  Disable DL11 alias 0176500..0176507 (default)\n");
#else
          "  -dl11-alias     Keep DL11 alias enabled (default)\n"
          "  -no-dl11-alias  Disable DL11 alias (non-standard for this target)\n");
#endif
}

int main(int argc, char **argv) {
  const char *rk_path = NULL;
  const char *load_path = NULL;
  int do_bootcopy = 0;
  int do_bootrt11 = 0;
  long load_addr = 0;
  long start_pc = -1;
  long sr_value = -1;
  long ram_kb_arg = -1;
  int force_dl11_alias = -1;
  byte cpu_model = DCJ11;
  int trace = 0;
  int trace_regs = 0;
  int exit_on_abort = 0;
  int check_config_only = 0;
  char cfg_err[160] = {0};

#if defined(LSI11_TARGET_1134)
  const lsi11_machine_t machine_kind = LSI11_MACHINE_1134;
#else
  const lsi11_machine_t machine_kind = LSI11_MACHINE_1104;
#endif

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-rk") && i + 1 < argc) {
      rk_path = argv[++i];
    } else if (!strcmp(argv[i], "-bootcopy")) {
      do_bootcopy = 1;
    } else if (!strcmp(argv[i], "-bootrt11")) {
      do_bootrt11 = 1;
    } else if (!strcmp(argv[i], "-traceirq")) {
      lsi11_set_trace_irq(1);
    } else if (!strcmp(argv[i], "-tracenxm")) {
      lsi11_set_trace_nxm(1);
    } else if (!strcmp(argv[i], "-trace")) {
      trace = 1;
    } else if (!strcmp(argv[i], "-trace-regs")) {
      trace = 1;
      trace_regs = 1;
    } else if (!strcmp(argv[i], "-exit-on-abort")) {
      exit_on_abort = 1;
    } else if (!strcmp(argv[i], "-check-config")) {
      check_config_only = 1;
    } else if (!strcmp(argv[i], "-load") && i + 1 < argc) {
      load_path = argv[++i];
    } else if (!strcmp(argv[i], "-addr") && i + 1 < argc) {
      load_addr = strtol(argv[++i], NULL, 8);
    } else if (!strcmp(argv[i], "-pc") && i + 1 < argc) {
      start_pc = strtol(argv[++i], NULL, 8);
    } else if (!strcmp(argv[i], "-sr") && i + 1 < argc) {
      sr_value = strtol(argv[++i], NULL, 8);
    } else if (!strcmp(argv[i], "-ram") && i + 1 < argc) {
      ram_kb_arg = strtol(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "-dl11-alias")) {
      force_dl11_alias = 1;
    } else if (!strcmp(argv[i], "-no-dl11-alias")) {
      force_dl11_alias = 0;
    } else if (!strcmp(argv[i], "-cpu") && i + 1 < argc) {
      if (parse_cpu_model(argv[++i], &cpu_model) != 0) {
        fprintf(stderr, "Unknown CPU model: %s\n", argv[i]);
        usage(argv[0]);
        return 2;
      }
    } else {
      usage(argv[0]);
      return 2;
    }
  }

#if !defined(LSI11_TARGET_1134)
  if (ram_kb_arg >= 0) {
    fprintf(stderr,
            "This lsi11 target is fixed 56KB RAM; -ram is not supported.\n");
    return 2;
  }
#endif

  if (ram_kb_arg < 0) {
    ram_kb_arg = 0;
  }

  if (lsi11_machine_configure(machine_kind, (uint32_t)ram_kb_arg, cfg_err,
                              sizeof(cfg_err)) != 0) {
    fprintf(stderr, "Machine configuration error: %s\n", cfg_err);
    return 2;
  }
  if (force_dl11_alias >= 0) {
    lsi11_set_dl11_alias(force_dl11_alias);
  }

  if (check_config_only) {
    const char *m = (lsi11_machine_current() == LSI11_MACHINE_1134) ? "pdp1134"
                                                                     : "lsi11";
    fprintf(stderr, "CONFIG machine=%s ram_kb=%u dl11_alias=%d\n", m,
            lsi11_machine_ram_kb(), lsi11_dl11_alias());
    return 0;
  }

  regs r;
  memset(&r, 0, sizeof(r));
  r.model = cpu_model;

  lsi11_hw_connect(&r);

  /* core init will init devices etc. */
  if (r.init(&r) != 0) {
    fprintf(stderr, "init failed\n");
    return 1;
  }

  /* Attach RK image if provided */
  if (rk_path) {
    if (rk11_open_image(rk_path) != 0) {
      fprintf(stderr, "rk11_open_image failed: %s\n", rk_path);
      r.fini(&r);
      return 1;
    }
  }

  core_reset(&r);

  if (sr_value >= 0) {
    sr_set((uint16_t)sr_value);
  }

  if (do_bootcopy) {
    /* Copy first 010000 bytes (4 KB) into RAM[000000..007777] by default.
       Adjust if your bootstrap needs a different size. */
    const size_t n = 010000;
    uint8_t *ram0 = bus_ram_ptr(0);
    if (!ram0 || !bus_range_is_ram(0, n)) {
      fprintf(stderr, "bootcopy destination is outside RAM\n");
      r.fini(&r);
      return 1;
    }
    if (rk11_boot_copy(ram0, n) != 0) {
      fprintf(stderr, "rk11_boot_copy failed (need -rk <image>)\n");
      r.fini(&r);
      return 1;
    }
    /* start execution at 000000 (common simple bootstrap scenario) */
    r.r[7] = 000000;
  }

  if (do_bootrt11) {
    if (!rk_path) {
      fprintf(stderr, "-bootrt11 requires -rk <image>\n");
      r.fini(&r);
      return 1;
    }
    FILE *f = fopen(rk_path, "rb");
    if (!f) {
      fprintf(stderr, "Cannot open RK image: %s\n", rk_path);
      r.fini(&r);
      return 1;
    }
    const size_t n = 01000;
    uint8_t buf[01000];
    size_t got = fread(buf, 1, n, f);
    int all_zero = 1;
    if (got == n) {
      for (size_t i = 0; i < n; i++) {
        if (buf[i]) { all_zero = 0; break; }
      }
    }
    if (got != n || all_zero) {
      if (fseek(f, (long)n, SEEK_SET) == 0) {
        got = fread(buf, 1, n, f);
        if (got == n) {
          all_zero = 0;
        }
      }
    }
    fclose(f);
    if (got != n || all_zero) {
      fprintf(stderr, "RT11 boot block not found in image\n");
      r.fini(&r);
      return 1;
    }
    {
      uint8_t *ram0 = bus_ram_ptr(0);
      if (!ram0 || !bus_range_is_ram(0, n)) {
        fprintf(stderr, "bootrt11 destination is outside RAM\n");
        r.fini(&r);
        return 1;
      }
      memcpy(ram0, buf, n);
    }
    r.r[7] = 000000;
  }

  if (load_path) {
    FILE *fload = fopen(load_path, "rb");
    if (!fload) {
      fprintf(stderr, "Cannot open file: %s\n", load_path);
      r.fini(&r);
      return 1;
    }

    if (fseek(fload, 0, SEEK_END) != 0) {
      fprintf(stderr, "Seek failed: %s\n", load_path);
      fclose(fload);
      r.fini(&r);
      return 1;
    }
    long fsize = ftell(fload);
    fseek(fload, 0, SEEK_SET);

    if (load_addr < 0 || fsize < 0 ||
        !bus_range_is_ram((paddr_t)load_addr, (size_t)fsize)) {
      fprintf(stderr, "Load address/size out of RAM bounds\n");
      fclose(fload);
      r.fini(&r);
      return 1;
    }

    {
      uint8_t *dst = bus_ram_ptr((paddr_t)load_addr);
      if (!dst) {
        fprintf(stderr, "Load destination is outside RAM\n");
        fclose(fload);
        r.fini(&r);
        return 1;
      }
      if (fread(dst, 1, fsize, fload) != (size_t)fsize) {
        fprintf(stderr, "Read failed: %s\n", load_path);
        fclose(fload);
        r.fini(&r);
        return 1;
      }
    }
    fclose(fload);
    fprintf(stderr, "Loaded %ld bytes from %s to octal %lo\n", fsize, load_path,
            load_addr);
  }

  if (start_pc >= 0) {
    r.r[7] = (uint16_t)start_pc;
    fprintf(stderr, "Set PC to octal %lo\n", start_pc);
  }

  /* -------- main emulation loop --------
     Replace cpu_step(&r) with your core's actual stepping API. */
  for (;;) {
    /* Run a chunk of CPU steps, poll devices between chunks */
    for (int k = 0; k < 1000; k++) {
      if (trace) {
        char buf[128];
        word pc = r.r[7];
        word tmp = pc;
        disas(&r, &tmp, buf);
        fprintf(stderr, "%06o %s\n", pc, buf);
        if (trace_regs) {
          fprintf(stderr,
                  "R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PS=%06o\n",
                  r.r[0], r.r[1], r.r[2], r.r[3], r.r[4], r.r[5], r.r[6],
                  r.psw);
        }
      }
      /* TODO: replace with your core single-instruction executor */
      core_step(&r); /* must exist in your core */
      if (r.fAbort)
        break;
    }

    lsi11_poll_devices();

    if (r.fAbort) {
      if (exit_on_abort) {
        break;
      }
      /* RT-11 and some monitor code may use HALT/abort vector path. */
      r.fAbort = 0;
    }
  }

  r.fini(&r);
  rk11_close_image();
  return 0;
}
