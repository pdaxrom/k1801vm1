#include "dev_dl11.h"
#include "dev_kw11.h"
#include "dev_rk11.h"
#include "dev_lp11.h"
#include "dev_sr.h"
#include "dev_vm1sel.h"
#include "dev_vm1sav.h"
#include "util_term.h"

int lsi11_machine_init(void)
{
    if (util_term_init_raw() != 0) {
        return -1;
    }

    if (dl11_init() != 0) {
        return -1;
    }
    if (kw11_init() != 0) {
        return -1;
    }
    if (rk11_init() != 0) {
        return -1;
    }
    if (lp11_init() != 0) {
        return -1;
    }
    if (sr_init()  != 0) {
        return -1;
    }
    if (vm1sel_init()  != 0) {
        return -1;
    }
    if (vm1sav_init()  != 0) {
        return -1;
    }

    return 0;
}

void lsi11_machine_reset(void)
{
    dl11_reset();
    kw11_reset();
    rk11_reset();
    lp11_reset();
    sr_reset();
    vm1sel_reset();
    vm1sav_reset();
}

void lsi11_machine_poll(void)
{
    dl11_poll_input();
    kw11_poll();
    rk11_poll();
    lp11_poll();
}
