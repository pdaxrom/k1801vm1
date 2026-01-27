#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adapter_core.h"
#include "bus.h"
#include "dev_rk11.h"

/* core headers (read-only) */
#include "../core/core.h" /* TODO: replace with actual header providing regs + cpu step/run */

static void usage(const char *argv0) {
  fprintf(stderr,
          "Usage:\n"
          "  %s -rk <rk05.img> [-bootcopy]\n"
          "\n"
          "Options:\n"
          "  -rk <path>      Attach RK05 image\n"
          "  -bootcopy       Copy first 010000 bytes from RK image into RAM at "
          "000000\n"
          "  -traceirq       Trace delivered IRQ vectors\n"
          "  -tracenxm       Trace NXM traps\n",
          argv0);
}

int main(int argc, char **argv) {
  const char *rk_path = NULL;
  int do_bootcopy = 0;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-rk") && i + 1 < argc) {
      rk_path = argv[++i];
    } else if (!strcmp(argv[i], "-bootcopy")) {
      do_bootcopy = 1;
    } else if (!strcmp(argv[i], "-traceirq")) {
      lsi11_set_trace_irq(1);
    } else if (!strcmp(argv[i], "-tracenxm")) {
      lsi11_set_trace_nxm(1);
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  regs r;
  memset(&r, 0, sizeof(r));

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

  r.reset(&r);

  if (do_bootcopy) {
    /* Copy first 010000 bytes (4 KB) into RAM[000000..007777] by default.
       Adjust if your bootstrap needs a different size. */
    const size_t n = 010000;
    if (rk11_boot_copy(&ram[0], n) != 0) {
      fprintf(stderr, "rk11_boot_copy failed (need -rk <image>)\n");
      r.fini(&r);
      return 1;
    }
    /* start execution at 000000 (common simple bootstrap scenario) */
    r.r[7] = 000000;
  }

  /* -------- main emulation loop --------
     Replace cpu_step(&r) with your core's actual stepping API. */
  for (;;) {
    /* Run a chunk of CPU steps, poll devices between chunks */
    for (int k = 0; k < 1000; k++) {
      /* TODO: replace with your core single-instruction executor */
      core_step(&r); /* must exist in your core */
      if (r.fAbort)
        break;
    }

    lsi11_poll_devices();

    if (r.fAbort) {
      /* core set fAbort on traps/halt; decide how to handle */
      /* For now: break */
      break;
    }
  }

  r.fini(&r);
  rk11_close_image();
  return 0;
}
