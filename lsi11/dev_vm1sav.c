#include "dev_sr.h"
#include "devio.h"
#include "dev_vm1sav.h"

static uint16_t reg_old_pc = 0;
static uint16_t reg_old_psw = 0;

static uint8_t vm1sav_read8(uint16_t a)
{
    switch(a) {
    case 0177674:
        return reg_old_pc & 0377;
    case 0177675:
        return reg_old_pc >> 8;
    case 0177676:
        return reg_old_psw & 0377;
    case 0177677:
        return reg_old_psw >> 8;
    }
    return 0;
}

static void vm1sav_write8(uint16_t a, uint8_t v)
{
    switch(a) {
    case 0177674:
        reg_old_pc = (reg_old_pc & 0177400) | v;
        return;
    case 0177675:
        reg_old_pc = (reg_old_pc & 0377) | (v << 8);
        return;
    case 0177676:
        reg_old_psw = (reg_old_psw & 0177400) | v;
        return;
    case 0177677:
        reg_old_psw = (reg_old_psw & 0377) | (v << 8);
        return;
    }
}

int vm1sav_init(void)
{
    static const io_range_t r = { 0177674, 0177677, vm1sav_read8, vm1sav_write8, "VM1SAV" };
    if (devio_register(&r) != 0) {
        return -1;
    }
    vm1sav_reset();
    return 0;
}

void vm1sav_reset(void)
{
    reg_old_pc = 0;
    reg_old_psw = 0;
}
