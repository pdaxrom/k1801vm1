/* HC7000 MMU-profile disk IOP with 18-bit physical DMA: RV32I, no runtime/OS or dynamic memory. The PDP-11 bootstrap
 * initializes the same SDHC card as before; this firmware owns only RK commands.
 * Volatile MMIO is the entire interface; sector bytes never pass through SERV.
 */
typedef unsigned int u32;
#define MMIO(a) (*(volatile u32 *)(a))
#define RK(n) MMIO(0x40000000u + 4u*(n))
#define SPI MMIO(0x40000100u)
#define CONTROL MMIO(0x40000104u)
#define DMA_ADDR MMIO(0x40000200u)
#define DMA_COUNT MMIO(0x40000204u)
#define ENGINE MMIO(0x40000208u)
#define TIME MMIO(0x40000300u)
enum { CS1, WC, BA, DA, CS2, DS, ER, MR, DC, DB, STATUS=16, COMPLETE };
enum { RX=1, TX, LOAD, STORE };
enum { ILF=0000001, IAE=0002000, DTE=0020000, DCK=0100000 };

static int elapsed(u32 start, u32 limit)
{
	return ((TIME-start)&65535u)>=limit;
}
static void pause_ms(u32 delay)
{
	u32 start=TIME;
	while(!elapsed(start,delay)) {}
}
/* A write already clocks a byte. Reading SPI clocks another, so command output
 * uses plain writes; input uses reads which clock FF. */
static void close_card(void)
{
	CONTROL=3;
	(void)SPI;
}
static int command(u32 opcode,u32 lba)
{
	CONTROL=2;
	SPI=opcode;
	SPI=lba>>24;
	SPI=lba>>16;
	SPI=lba>>8;
	SPI=lba;
	SPI=1;
	for(u32 n=0; n<16; n++) {
		u32 r=SPI;
		if(!(r&128)) {
			return r==0;
		}
	}
	return 0;
}
static u32 transfer(u32 op)
{
	ENGINE=op;
	u32 status;
	do {
		status=ENGINE;
	} while(status&1);
	return status;
}
static u32 read_sector(u32 lba)
{
	if(!command(0x51,lba)) {
		return DTE;
	}
	u32 start=TIME;
	for(;;) {
		u32 r=SPI;
		if(r==0xfe) {
			break;
		}
		if(r!=0xff || elapsed(start,250)) {
			return DTE;
		}
	}
	u32 crc=transfer(RX)>>16;
	u32 received=SPI<<8;
	received|=SPI;
	if(crc!=received) {
		return DCK;
	}
	close_card();
	return (transfer(STORE)&2) ? DTE : 0;
}
static u32 write_sector(u32 lba)
{
	if(transfer(LOAD)&2) {
		return DTE;
	}
	if(!command(0x58,lba)) {
		return DTE;
	}
	SPI=0xfe;
	u32 crc=transfer(TX)>>16;
	SPI=crc>>8;
	SPI=crc;
	u32 r,start=TIME;
	do {
		r=SPI;
		if(elapsed(start,250)) {
			return DTE;
		}
	} while(r==0xff);
	if((r&31)!=5) {
		return DTE;
	}
	/* The physical card needs programming time with CS low and no clocks. */
	start=TIME;
	do {
		pause_ms(1);
		r=SPI;
		if(elapsed(start,1000)) {
			return DTE;
		}
	} while(r==0);
	close_card();
	return 0;
}
static u32 execute(void)
{
	u32 cs1=RK(CS1),fn=(cs1>>1)&31;
	if(RK(CS2)&7) {
		RK(CS2)|=0010000;
		return 0;
	}
	if(fn==0 || fn==1 || fn==5) {
		return 0;        /* NOP, PACK ACK, RECAL */
	}
	if(fn!=010 && fn!=011) {
		return ILF;
	}
	u32 start=TIME;
	while(!(RK(STATUS)&2))if(elapsed(start,1000)) {
			return DTE;
		}
	do {
		u32 da=RK(DA),dc=RK(DC),wc=RK(WC),ba=RK(BA) | ((RK(CS1)&0001400)<<8);
		u32 head=(da>>8)&7,sector=da&31;
		if(head>=3 || sector>=22 || dc>=815) {
			return IAE;
		}
		u32 lba=dc*66+head*22+sector;
		u32 count=65536u-wc;
		if(count>256) {
			count=256;
		}
		DMA_ADDR=ba;
		DMA_COUNT=count;
		u32 error=fn==010 ? read_sector(lba) : write_sector(lba);
		close_card();
		if(error) {
			return error;
		}
		ba=(ba+2*count)&0x3ffff;
		RK(BA)=ba&65535;
		RK(CS1)=(ba>>8)&0001400;
		RK(WC)=(wc+count)&65535;
		if(++sector==22) {
			sector=0;
			if(++head==3) {
				head=0;
				++dc;
			}
		}
		RK(DA)=(head<<8)|sector;
		RK(DC)=dc;
	} while(RK(WC)!=0);
	return 0;
}
void main(void)
{
	for(;;) {
		while(!(RK(STATUS)&1)) {}
		u32 error=execute();
		// If arbitration timed out, leave the legacy owner's SD transaction alone.
		if(RK(STATUS)&2) {
			close_card();
		}
		RK(COMPLETE)=error;
	}
}
