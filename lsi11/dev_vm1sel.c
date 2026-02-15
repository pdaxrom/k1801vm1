#include "dev_sr.h"
#include "devio.h"
#include "dev_vm1sel.h"

static uint16_t reg_sel1_in = 0;
static uint16_t reg_sel1_out = 0;
static uint16_t reg_sel2_in = 0;
static uint16_t reg_sel2_out = 0;

static uint8_t vm1sel_read8(uint16_t a)
{
    switch(a) {
    case 0177714:
        return reg_sel2_in & 0377;
    case 0177715:
        return reg_sel2_in >> 8;
    case 0177716:
        return reg_sel1_in & 0377;
    case 0177717:
        return reg_sel1_in >> 8;
    }
    return 0;
}

static void vm1sel_write8(uint16_t a, uint8_t v)
{
    switch(a) {
    case 0177714:
        reg_sel2_out = (reg_sel2_in & 0177400) | v;
        return;
    case 0177715:
        reg_sel2_out = (reg_sel2_out & 0377) | (v << 8);
        return;
    case 0177716:
        reg_sel1_out = (reg_sel1_in & 0177400) | v;
        return;
    case 0177717:
        reg_sel1_out = (reg_sel1_out & 0377) | (v << 8);
        return;
    }
}

int vm1sel_init(void)
{
    static const io_range_t r = { 0177714, 0177717, vm1sel_read8, vm1sel_write8, "VM1SEL" };
    if (devio_register(&r) != 0) {
        return -1;
    }
    vm1sel_reset();
    return 0;
}

void vm1sel_reset(void)
{
    reg_sel1_in = 0;
    reg_sel1_out = 0;
    reg_sel2_in = 0;
    reg_sel2_out = 0;
}
