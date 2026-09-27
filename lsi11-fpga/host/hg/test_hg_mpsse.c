/* Test the actual GPIO commands without opening a USB device. */
#define ftdi_write_data mock_write
#define ftdi_read_data mock_read
#define ftdi_set_bitmode mock_bitmode
#define ftdi_usb_close mock_close
#define ftdi_free mock_free
#include "hg_mpsse.c"
#include <assert.h>
#include <stdio.h>

static uint8_t value, direction;
static int bad_readback, commands;
int mock_write(struct ftdi_context *f, const unsigned char *p, int n)
{
    (void)f;
    if (n == 3 && p[0] == SET_BITS_LOW) {
        value=p[1]; direction=p[2]; commands++;
        assert(!(value & HG_PIN_JTAGENB)); /* Never drive the jumper high. */
    }
    return n;
}
int mock_read(struct ftdi_context *f, unsigned char *p, int n)
{
    (void)f; assert(n == 1);
    *p=(value & direction) | (HG_PIN_JTAGENB & ~direction);
    if (bad_readback) *p ^= HG_PIN_JTAGENB;
    return 1;
}
int mock_bitmode(struct ftdi_context *f, unsigned char mask, unsigned char mode)
{ (void)f; (void)mask; (void)mode; return 0; }
int mock_close(struct ftdi_context *f) { (void)f; return 0; }
void mock_free(struct ftdi_context *f) { (void)f; }

int main(void)
{
    struct hg_mpsse link={.ftdi=(struct ftdi_context *)1,
        .low_direction=HG_MPSSE_DIRECTION};
    assert(hg_mpsse_jtag_enable(&link,1)==0 && direction==0x0b);
    assert(hg_mpsse_jtag_enable(&link,0)==0 && direction==0x8b);
    assert(hg_mpsse_select(&link,1)==0 && direction==0x8b && value==8);
    assert(hg_mpsse_select(&link,0)==0 && direction==0x8b && value==0);
    assert(hg_mpsse_jtag_enable(&link,1)==0 && direction==0x0b);
    bad_readback=1;
    assert(hg_mpsse_jtag_enable(&link,0)==-1 && errno==EIO);
    bad_readback=0;
    hg_mpsse_close(&link);
    assert(direction==0 && value==0 && link.ftdi==NULL && commands>=8);
    puts("hg MPSSE: ADBUS7 low/release, selection preservation, readback failure and cleanup passed");
    return 0;
}
