#include "label.h"
/* Autonomous SDHC initialization and partitioned RK07 service. */
typedef unsigned int u32;
#define MMIO(a) (*(volatile u32 *)(a))
#define RK(n) MMIO(0x40000000u + 4u*(n))
#define SPI MMIO(0x40000100u)
#define CONTROL MMIO(0x40000104u)
#define DMA_ADDR MMIO(0x40000200u)
#define DMA_COUNT MMIO(0x40000204u)
#define ENGINE MMIO(0x40000208u)
#define TIME MMIO(0x40000300u)
enum { CS1, WC, BA, DA, CS2, DS, ER, MR, DC, DB, STATUS=16, COMPLETE, PRESENT, PROTECTED, BOOT_STATUS };
enum { RX=1, TX, LOAD, STORE };
enum { ILF=0000001, IAE=0002000, DTE=0020000, WLE=0004000, DCK=0100000 };

static int elapsed(u32 start, u32 limit) { return ((TIME-start)&65535u)>=limit; }
static void pause_ms(u32 delay) { u32 start=TIME; while(!elapsed(start,delay)) {} }
/* A write already clocks a byte. Reading SPI clocks another, so command output
 * uses plain writes; input uses reads which clock FF. */
static u32 speed;
static uint8_t sector[512];
static struct sd_label label;
static u32 unit_base[8], present, protected;
static void close_card(void) { CONTROL=speed|1; (void)SPI; }
static u32 send(u32 opcode,u32 arg,u32 crc) {
    CONTROL=speed;
    SPI=opcode; SPI=arg>>24; SPI=arg>>16; SPI=arg>>8; SPI=arg; SPI=crc;
    for(u32 n=0;n<16;n++) {
        u32 r=SPI;
        if(!(r&128))return r;
    }
    return 255;
}
static int command(u32 opcode,u32 lba) { return send(opcode,lba,1)==0; }
static int token(void) {
    u32 start=TIME;
    for(;;) {
        u32 r=SPI;
        if(r==0xfe)return 1;
        if(r!=0xff || elapsed(start,250))return 0;
    }
}
static int receive(uint8_t *p,u32 n) {
    if(!token())return 0;
    u32 crc=0;
    for(u32 i=0;i<n;i++) {
        u32 byte=SPI;p[i]=byte;crc^=byte<<8;
        for(unsigned b=0;b<8;b++)crc=((crc<<1)^((crc&0x8000)?0x1021:0))&65535;
    }
    u32 actual=SPI<<8;actual|=SPI;
    return crc==actual;
}
static u32 initialize(void) {
    /* Firmware owns the byte port throughout startup; no guest SRAM is used. */
    while(!(RK(STATUS)&2)) {}
    pause_ms(10);CONTROL=1;
    for(unsigned n=0;n<10;n++)SPI=255;
    if(send(0x40,0,0x95)!=1)return 1;
    close_card();
    if(send(0x48,0x1aa,0x87)!=1)return 2;
    u32 echo=0;
    for(unsigned n=0;n<4;n++)echo=(echo<<8)|SPI;
    close_card();if(echo!=0x1aa)return 2;
    u32 start=TIME;
    for(;;) {
        u32 r=send(0x77,0,1);close_card();if(r>1)return 3;
        r=send(0x69,0x40000000,1);close_card();
        if(!r)break;
        if(r!=1 || elapsed(start,2000))return 3;
    }
    if(send(0x7a,0,1)!=0)return 4;
    u32 ocr=SPI;for(unsigned n=0;n<3;n++)(void)SPI;
    close_card();if((ocr&0xc0)!=0xc0)return 4;
    speed=2;
    if(!command(0x49,0) || !receive(sector,16))return 5;
    close_card();if((sector[0]>>6)!=1)return 5;
    u32 csize=((u32)(sector[7]&63)<<16)|((u32)sector[8]<<8)|sector[9];
    /* v1 has a 32-bit sector count; saturate a 2 TiB SDXC card. */
    u32 capacity=csize==0x3fffff ? 0xffffffffu : (csize+1)<<10;
    unsigned good=0;
    for(unsigned copy=0;copy<2;copy++) {
        int valid=command(0x51,copy) && receive(sector,512);
        close_card();
        if(valid && sd_label_decode(sector,capacity,&label)) { good=1;break; }
    }
    if(!good)return 6;
    u32 boot=0xffffffffu;
    for(unsigned i=0;i<label.count;i++) {
        struct sd_partition *p=label.part+i;
        int supported=p->kind==2 && p->media==3 && p->mode==0;
        if(p->flags&SD_BOOT) {
            if(!supported)return 7;
            boot=p->unit;
        }
        if(supported) {
            present|=1u<<p->unit;unit_base[p->unit]=p->start;
            if(p->flags&SD_READONLY)protected|=1u<<p->unit;
        }
    }
    if(boot==0xffffffffu) {
        if(!(label.features&SD_MENU))return 8;
        boot=0x1000; /* menu without an automatic/default drive */
    }
    RK(PRESENT)=present;RK(PROTECTED)=protected;
    close_card();RK(BOOT_STATUS)=0x8000u|boot|((label.features&SD_MENU)?0x2000:0);
    return 0;
}
static u32 transfer(u32 op) {
    ENGINE=op;
    u32 status;
    do { status=ENGINE; } while(status&1);
    return status;
}
static u32 read_sector(u32 lba) {
    if(!command(0x51,lba))return DTE;
    u32 start=TIME;
    for(;;) {
        u32 r=SPI;
        if(r==0xfe)break;
        if(r!=0xff || elapsed(start,250))return DTE;
    }
    u32 crc=transfer(RX)>>16;
    u32 received=SPI<<8;
    received|=SPI;
    if(crc!=received)return DCK;
    close_card();
    return (transfer(STORE)&2) ? DTE : 0;
}
static u32 write_sector(u32 lba) {
    if(transfer(LOAD)&2)return DTE;
    if(!command(0x58,lba))return DTE;
    SPI=0xfe;
    u32 crc=transfer(TX)>>16;
    SPI=crc>>8;SPI=crc;
    u32 r,start=TIME;
    do { r=SPI; if(elapsed(start,250))return DTE; } while(r==0xff);
    if((r&31)!=5)return DTE;
    /* The physical card needs programming time with CS low and no clocks. */
    start=TIME;
    do {
        pause_ms(1);
        r=SPI;
        if(elapsed(start,1000))return DTE;
    } while(r==0);
    close_card();
    return 0;
}
static u32 execute(void) {
    u32 cs1=RK(CS1),fn=(cs1>>1)&31;
    u32 unit=RK(CS2)&7;
    if(!(present&(1u<<unit))) { RK(CS2)|=0010000;return 0; }
    if(fn==0 || fn==1 || fn==5)return 0; /* NOP, PACK ACK, RECAL */
    if(fn!=010 && fn!=011)return ILF;
    if(fn==011 && (protected&(1u<<unit)))return WLE;
    /* Reject the entire request before writing if it crosses this drive. */
    u32 first=RK(DC)*66+((RK(DA)>>8)&7)*22+(RK(DA)&31);
    u32 sectors=(65536u-RK(WC)+255)>>8;
    if(first>=53790 || sectors>53790-first)return IAE;
    u32 start=TIME;
    while(!(RK(STATUS)&2))if(elapsed(start,1000))return DTE;
    do {
        u32 da=RK(DA),dc=RK(DC),wc=RK(WC),ba=RK(BA) | ((RK(CS1)&0001400)<<8);
        u32 head=(da>>8)&7,sector=da&31;
        if(head>=3 || sector>=22 || dc>=815)return IAE;
        u32 lba=unit_base[unit]+dc*66+head*22+sector;
        u32 count=65536u-wc;
        if(count>256)count=256;
        DMA_ADDR=ba;DMA_COUNT=count;
        u32 error=fn==010 ? read_sector(lba) : write_sector(lba);
        close_card();
        if(error)return error;
        ba=(ba+2*count)&0x3ffff;
        RK(BA)=ba&65535;
        RK(CS1)=(ba>>8)&0001400;
        RK(WC)=(wc+count)&65535;
        if(++sector==22) { sector=0;if(++head==3) { head=0;++dc; } }
        RK(DA)=(head<<8)|sector;RK(DC)=dc;
    } while(RK(WC)!=0);
    return 0;
}
void main(void) {
    u32 error=initialize();
    if(error) { present=0;protected=0;close_card();RK(BOOT_STATUS)=0xc000u|error; }
    for(;;) {
        while(!(RK(STATUS)&1)) {}
        u32 error=execute();
        // If arbitration timed out, leave the legacy owner's SD transaction alone.
        if(RK(STATUS)&2)close_card();
        RK(COMPLETE)=error;
    }
}
