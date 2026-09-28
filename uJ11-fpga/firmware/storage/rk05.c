#include "storage.h"
/* RK05 commands share the SD staging engine with the other controllers. */
u32 rk05_command(struct sd_partition *p)
{
	u32 cs=R(2),fn=(cs>>1)&7,da=R(5),unit=da>>13;
	if(fn==6) {
		R(5)=da&0160000;
		R(2)=cs|0020000;
		return 0;
	}
	if(fn==7) {
		R(20)|=1u<<unit;
		return 0;
	}
	if(!p) {
		return 0000200;        /* NXD */
	}
	u32 cyl=(da>>5)&255,head=(da>>4)&1,sec=da&15;
	if(cyl>202) {
		return 0000100;
	}
	if(sec>=12) {
		return 0000040;
	}
	if(fn==4) {
		R(2)=cs|0020000;
		return 0;
	}
	if(fn!=1 && fn!=2 && fn!=3 && fn!=5) {
		return 0004000;
	}
	if((cs&0002000) && (fn==3 || fn==5)) {
		return 0004000;
	}
	if((fn==1 || fn==3) && ((current->ro|R(20))&(1u<<unit))) {
		return 0020000;
	}
	u32 wc=R(3),left=(-wc)&65535,ba=R(4)|((cs&060)<<12);
	u32 lba=(cyl*2+head)*12+sec,errors=0;
	DMA_MODE=(cs&0004000)?2:0;
	while(left && active()) {
		u32 count=left>256?256:left,error;
		if(lba>=p->blocks) {
			return errors|0040000;
		}
		DMA_ADDR=ba;
		if(fn==5) {
			error=fetch_sector(p->start+lba);        /* READ CHECK has no DMA */
		} else {
			error=sector_io(p->start+lba,fn==1?1:fn==3?2:0,count,0,fn==1?count:256);
		}
		close_card();
		if(!active()) {
			return 0;
		}
		if(error==DCK && fn==3) {
			errors|=1;
		} else if(error) {
			return errors|(error==NXM?0002000:error==DCK?2:0001000);
		}
		if(!(cs&0004000)) {
			ba=(ba+2*count)&0x3ffff;
		}
		left-=count;
		wc=(wc+count)&65535;
		R(3)=wc;
		R(4)=ba&65535;
		R(2)=(R(2)&~060u)|((ba>>12)&060);
		if(count==256) {
			++lba;
			if(++sec==12) {
				sec=0;
				if(++head==2) {
					head=0;
					++cyl;
				}
			}
			R(5)=(unit<<13)|(cyl<<5)|(head<<4)|sec;
		}
		if(errors && (cs&0000400)) {
			break;
		}
	}
	return errors;
}
