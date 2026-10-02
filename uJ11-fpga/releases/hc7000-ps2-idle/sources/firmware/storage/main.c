#include "storage.h"
#include "terminal.h"
#include "keyboard.h"
/* SDHC service and scheduler for independent PDP-11 disk controllers. */

static int elapsed(u32 start, u32 limit)
{
	return ((TIME-start)&65535u)>=limit;
}
static void pause_ms(u32 delay)
{
	u32 start=TIME;
	while(!elapsed(start,delay)) {
		poll_io();
	}
}
/* A write already clocks a byte. Reading SPI clocks another, so command output
 * uses plain writes; input uses reads which clock FF. */
static u32 speed;
u32 dma_mode;
union storage_scratch scratch;
#define sector scratch.sector
static struct sd_label label;
static uint16_t rl_position[4];
static uint8_t rl_rotation[4];
void close_card(void)
{
	CONTROL=speed|1;
	(void)SPI;
}
static u32 send(u32 opcode,u32 arg,u32 crc)
{
	CONTROL=speed;
	SPI=opcode;
	SPI=arg>>24;
	SPI=arg>>16;
	SPI=arg>>8;
	SPI=arg;
	SPI=crc;
	for(u32 n=0; n<16; n++) {
		u32 r=SPI;
		if(!(r&128)) {
			return r;
		}
	}
	return 255;
}
static int command(u32 opcode,u32 lba)
{
	return send(opcode,lba,1)==0;
}
static int token(void)
{
	u32 start=TIME;
	for(;;) {
		poll_io();
		u32 r=SPI;
		if(r==0xfe) {
			return 1;
		}
		if(r!=0xff || elapsed(start,250)) {
			return 0;
		}
	}
}
static int receive(uint8_t *p,u32 n)
{
	if(!token()) {
		return 0;
	}
	u32 crc=0;
	for(u32 i=0; i<n; i++) {
		u32 byte=SPI;
		p[i]=byte;
		crc^=byte<<8;
		poll_io();
		for(unsigned b=0; b<8; b++) {
			crc=((crc<<1)^((crc&0x8000)?0x1021:0))&65535;
		}
	}
	u32 actual=SPI<<8;
	actual|=SPI;
	return crc==actual;
}
static u32 initialize(void)
{
	/* Firmware owns the byte port throughout startup; no guest SRAM is used. */
	sd_ownership(1);
	pause_ms(10);
	CONTROL=1;
	for(unsigned n=0; n<10; n++) {
		SPI=255;
	}
	if(send(0x40,0,0x95)!=1) {
		return 1;
	}
	close_card();
	if(send(0x48,0x1aa,0x87)!=1) {
		return 2;
	}
	u32 echo=0;
	for(unsigned n=0; n<4; n++) {
		echo=(echo<<8)|SPI;
	}
	close_card();
	if(echo!=0x1aa) {
		return 2;
	}
	u32 start=TIME;
	for(;;) {
		u32 r=send(0x77,0,1);
		close_card();
		if(r>1) {
			return 3;
		}
		r=send(0x69,0x40000000,1);
		close_card();
		if(!r) {
			break;
		}
		if(r!=1 || elapsed(start,2000)) {
			return 3;
		}
	}
	if(send(0x7a,0,1)!=0) {
		return 4;
	}
	u32 ocr=SPI;
	for(unsigned n=0; n<3; n++) {
		(void)SPI;
	}
	close_card();
	if((ocr&0xc0)!=0xc0) {
		return 4;
	}
	speed=2;
	if(!command(0x49,0) || !receive(sector,16)) {
		return 5;
	}
	close_card();
	if((sector[0]>>6)!=1) {
		return 5;
	}
	u32 csize=((u32)(sector[7]&63)<<16)|((u32)sector[8]<<8)|sector[9];
	/* v1 has a 32-bit sector count; saturate a 2 TiB SDXC card. */
	u32 capacity=csize==0x3fffff ? 0xffffffffu : (csize+1)<<10;
	unsigned good=0;
	for(unsigned copy=0; copy<2; copy++) {
		int valid=command(0x51,copy) && receive(sector,512);
		close_card();
		if(valid && sd_label_decode(sector,capacity,&label)) {
			good=1;
			break;
		}
	}
	if(!good) {
		return 6;
	}
	controllers_init();
	u32 boot=0xffffffffu;
	for(unsigned i=0; i<label.count; i++) {
		struct sd_partition *p=label.part+i;
		int supported=(p->kind==2 && p->media==3 && p->mode==0) ||
		              (p->kind==5 && (p->media==6 || p->media==7)) || (p->kind==3 && p->media==4) ||
		              (p->kind==1 && p->media==1) || (p->kind==4 && p->media==5);
		unsigned bank=p->kind==2 ? 0 : p->kind==5 ? 1 : p->kind==3 ? 2 : p->kind==1 ? 3 : 4;
		if(p->flags&SD_BOOT) {
			if(!supported) {
				return 7;
			}
			if(p->kind!=2 && !(label.features&SD_MENU)) {
				return 8;
			}
			boot=p->unit | ((p->kind==2 ? 0 : p->kind)<<3);
		}
		if(supported) {
			controllers[bank].present|=1u<<p->unit;
			if(p->flags&SD_READONLY) {
				controllers[bank].ro|=1u<<p->unit;
			}
		}
	}
	if(boot==0xffffffffu) {
		if(!(label.features&SD_MENU)) {
			return 8;
		}
		boot=0x1000; /* menu without an automatic/default drive */
	}
	close_card();
	boot_status=0x8000u|boot|((label.features&SD_MENU)?0x2000:0);
	return 0;
}
u32 transfer(u32 op)
{
	ENGINE=op;
	u32 status;
	while((status=ENGINE)&1) {
		poll_io();
	}
	return status;
}
u32 fetch_sector(u32 lba)
{
	if(!command(0x51,lba)) {
		return DTE;
	}
	u32 start=TIME;
	for(;;) {
		poll_io();
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
	return 0;
}
static u32 write_sector(u32 lba)
{
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
/* Operation 0 reads, 1 writes, 2 compares. A 256-byte RL sector shares
 * its SD sector with a neighbour; read/modify/write preserves that neighbour. */
u32 sector_io(u32 lba,u32 op,u32 count,u32 offset,u32 span)
{
	u32 address=DMA_ADDR,end=offset+span,status=0;
	if(op!=1 || span!=256) {
		u32 error=fetch_sector(lba);
		if(error) {
			return error;
		}
	}
	while(count) {
		if(!active()) {
			return DTE;
		}
		u32 chunk=count,physical=address;
		unsigned mapped=service_bank!=XP_BANK && service_bank!=RQ_BANK && (BUS_STATUS&BUS_MAP_ENABLED);
		if(mapped) {
			u32 page=(address>>13)&31,within=address&8191;
			/* UNIBUS page 31 is I/O, never an alias of installed SRAM. */
			if(page==31) {
				return NXM;
			}
			physical=(ubmap.base[page]+within)&0x3fffff;
			if(!(dma_mode&2) && chunk>(8192-within)/2) {
				chunk=(8192-within)/2;
			}
		}
		DMA_MODE=dma_mode|mapped;
		DMA_ADDR=physical;
		DMA_COUNT=chunk;
		/* LOAD pads only the final fragment; earlier fragments must leave
		 * the rest of this SD sector available to the next mapped page. */
		WINDOW=offset|((chunk==count?end:offset+chunk)<<8);
		status|=transfer(op==1?LOAD:op==2?COMPARE:STORE);
		if(status&2) {
			return status&8?NXM:DTE;
		}
		count-=chunk;
		offset+=chunk;
		if(!(dma_mode&2)) {
			address+=2*chunk;
		}
	}
	if(!active()) {
		return DTE;
	}
	return op==1?write_sector(lba):status&4?DCK:0;
}
struct sd_partition *find_drive(u32 kind,u32 unit)
{
	for(unsigned i=0; i<label.count; i++)
		if(label.part[i].kind==kind && label.part[i].unit==unit) {
			return label.part+i;
		}
	return 0;
}
static u32 disks(struct sd_partition *p,u32 xp)
{
	u32 fn=(R(CS1)>>1)&31,unit=R(CS2)&7,dc_reg=xp ? 14 : DC;
	if(!p) {
		R(CS2)|=0010000;
		return 0;
	}
	if(xp && fn<024) {
		if(fn==010) {
			R(DA)=0;
			R(14)=0;
			R(13)=0;
		}
		if(fn==3) {
			R(14)=0;
		}
		if(fn==2 || fn==014 || fn==3) {
			if(R(14)>=823) {
				return IAE;
			}
			R(15)=R(14);
			R(DS)|=0100000;
		} else if(fn==010 || fn==011) {
			R(DS)|=0100;
		} else if(fn==4) {
			R(DS)&=~0100000u;
		} else if(fn!=0 && fn!=1 && fn!=5 && fn!=6 && fn!=7) {
			return ILF;
		}
		return 0;
	}
	if(!xp && (fn==0 || fn==1 || fn==5)) {
		return 0;
	}
	u32 op=xp ? (fn==030 || fn==031 ? 1 : fn==024 ? 2 : 0) : fn==011;
	if(xp ? (fn!=024 && fn!=030 && fn!=031 && fn!=034 && fn!=035) : (fn!=010 && fn!=011)) {
		return ILF;
	}
	if(op==1 && (p->flags&SD_READONLY)) {
		return WLE;
	}
	u32 per_track=xp ? 32 : 22,heads=xp ? 19 : 3;
	u32 da=R(DA),dc=R(dc_reg),head=(da>>8)&63,sec=da&63;
	if(head>=heads || sec>=per_track || dc>=(xp ? 823u : 815u)) {
		return IAE;
	}
	u32 lba=(dc*heads+head)*per_track+sec;
	u32 wc=R(WC),left=65536u-wc;
	if(xp && !wc) {
		return 0;
	}
	if(lba>=p->blocks || ((left+255)>>8)>p->blocks-lba) {
		return IAE;
	}
	u32 ba=R(BA) | (xp ? R(20)<<16 : (R(CS1)&0001400)<<8);
	u32 inhibit=xp && (R(CS2)&8);
	dma_mode=xp | (inhibit<<1);
	while(left && active()) {
		u32 count=left>256 ? 256 : left;
		DMA_ADDR=ba;
		/* RM05 short writes preserve the untouched tail, as the reference
		 * controller does. RK07 retains its legacy zero padding. */
		u32 error=sector_io(p->start+lba,op,count,0,xp && op==1 ? count : 256);
		close_card();
		if(!active()) {
			return DTE;
		}
		if(xp && error==NXM) {
			R(CS2)|=0004000;
			return 0;
		}
		if(xp && op==2 && error==DCK) {
			R(CS2)|=0040000;
			return 0;
		}
		if(error) {
			return error;
		}
		if(!inhibit) {
			ba=(ba+2*count)&(xp ? 0x3fffffu : 0x3ffffu);
		}
		left-=count;
		R(WC)=(wc+=count)&65535;
		R(BA)=ba&65535;
		if(xp) {
			R(20)=ba>>16;
		} else {
			R(CS1)=(R(CS1)&~0001400u)|((ba>>8)&0001400);
		}
		++lba;
		if(++sec==per_track) {
			sec=0;
			if(++head==heads) {
				head=0;
				++dc;
			}
		}
		if(xp && lba==p->blocks) {
			dc=822;
			head=18;
			sec=31;
			R(DS)|=02000;
		}
		R(DA)=(head<<8)|sec;
		R(dc_reg)=dc;
		if(xp) {
			R(15)=dc;
		}
	}
	(void)unit;
	return 0;
}
static u32 rl_error(u32 code)
{
	return 0100000u | (code<<10);
}
static u32 rl_command(struct sd_partition *p)
{
	u32 cs=R(0),fn=(cs>>1)&7,unit=(cs>>8)&3,da=R(2);
	if(!fn) {
		return 0;
	}
	if(!p) {
		return rl_error(5);
	}
	u32 max_cyl=p->media==7 ? 511 : 255;
	u32 position=rl_position[unit];
	if(fn==2) {
		if((da&7)!=3) {
			return rl_error(1);
		}
		R(3)=035 | ((position&64) ? 0100 : 0) | (p->media==7 ? 0200 : 0) |
		     ((p->flags&SD_READONLY) ? 020000 : 0);
		return 0;
	}
	if(fn==3) {
		if((da&3)!=1) {
			return rl_error(1);
		}
		u32 cyl=position>>7,diff=da>>7;
		if(da&4) {
			cyl+=diff;
			if(cyl>max_cyl) {
				cyl=max_cyl;
			}
		} else {
			cyl=diff>cyl ? 0 : cyl-diff;
		}
		rl_position[unit]=(cyl<<7)|((da&020)?64:0);
		return 0;
	}
	if(fn==4) {
		u32 header=position|rl_rotation[unit],crc=0,data=header;
		for(unsigned i=0; i<32; i++) {
			u32 bit=(crc^data)&1;
			crc=(crc>>1)^(bit?0120001:0);
			data>>=1;
		}
		R(3)=header;
		R(5)=crc;
		R(6)=1;
		rl_rotation[unit]=(rl_rotation[unit]+1)%40;
		return 0;
	}
	if(fn==5 && (p->flags&SD_READONLY)) {
		return rl_error(1)|040000;
	}
	if(fn==7) {
		da=position|rl_rotation[unit];
	} else if((da>>7)!=(position>>7)) {
		return rl_error(5);
	}
	u32 sec=da&63,cyl=da>>7,head=(da>>6)&1;
	if(sec>=40 || cyl>max_cyl) {
		return rl_error(5);
	}
	u32 left=(-R(3))&65535,maximum=(40-sec)*128,overrun=left>maximum;
	if(overrun) {
		left=maximum;
	}
	u32 ba=R(1)|(R(4)<<16),mp=R(3),original=R(2),done=0;
	dma_mode=1;
	while(left && active()) {
		u32 span=(sec&1)?128:256,count=left>span?span:left;
		if(count<=128) {
			span=128;
		}
		u32 lba=(cyl*2+head)*40+sec;
		DMA_ADDR=ba;
		u32 error=sector_io(p->start+(lba>>1),fn==5 ? 1 : fn==1 ? 2 : 0,
		                    count,(sec&1)?128:0,span);
		close_card();
		if(!active()) {
			return 0;
		}
		if(error) {
			return rl_error(error==DCK ? 2 : error==NXM ? 8 : 4);
		}
		left-=count;
		done+=count;
		mp=(mp+count)&65535;
		ba=(ba+2*count)&0x3fffff;
		sec+=(count+127)>>7;
		R(3)=mp;
		R(1)=ba&65535;
		R(4)=ba>>16;
		R(2)=(original+((done+127)>>7))&65535;
	}
	rl_position[unit]=(cyl<<7)|(head<<6);
	rl_rotation[unit]=sec>=40?0:sec;
	return overrun ? rl_error(1) : 0;
}
void main(void)
{
	keyboard_reset();
	terminal_init();
	bootstrap();
	u32 error=initialize();
	if(error) {
		controllers_init();
		close_card();
		boot_status=0xc000u|error;
	}
	sd_ownership(0);
	for(;;) {
		for(unsigned bank=0; bank<NCONTROLLERS; bank++) {
			poll_io();
			if(!controllers[bank].busy) {
				continue;
			}
			service_begin(bank);
			error=0;
			if(active()) {
				if(bank==RQ_BANK) {
					error=rq_service();
				} else {
					u32 unit=bank==RL_BANK?(R(0)>>8)&3:bank==RK_BANK?R(5)>>13:R(CS2)&7;
					u32 kind=bank==RH_BANK?2:bank==RL_BANK?5:bank==XP_BANK?3:1;
					struct sd_partition *p=find_drive(kind,unit);
					if(!(current->present&(1u<<unit))) {
						p=0;
					}
					error=bank==RL_BANK?rl_command(p):bank==RK_BANK?rk05_command(p):disks(p,bank==XP_BANK);
				}
			}
			close_card();
			controller_finish(error);
		}
#ifdef UJ11_KEYBOARD
		/* Leave raw scans in hardware while a disk or bridge event needs
		 * service. One scan or one ASCII byte per idle scheduler pass. */
		unsigned busy=0;
		for(unsigned bank=0; bank<NCONTROLLERS; bank++) {
			busy|=controllers[bank].busy;
		}
		if(!busy) {
			u32 status=BUS_STATUS;
			if(!(status&7)) {
				keyboard_poll(status);
			}
		}
#endif
		terminal_service();
	}
}
