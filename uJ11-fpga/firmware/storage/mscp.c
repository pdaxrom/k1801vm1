#include "storage.h"

/* UQSSP/MSCP service. IP/SA and interrupts are software state in controllers.c;
 * packet/ring state and commands execute here on SERV. Disk data stays in
 * the sector engine, and packet scratch reuses the startup label buffer. */
enum { RQ_S1,RQ_WRAP,RQ_S2,RQ_S3,RQ_PURGE,RQ_PURGE_IP,RQ_S4,RQ_UP,RQ_DEAD };
static struct {
	u32 comm;
	uint16_t length[2],index[2],s1;
	uint8_t state,online,swp,credits;
} rq;
struct rq_geometry {
	u32 blocks,media;
	uint16_t model;
	uint8_t sectors,tracks;
	uint16_t rct;
};
static const struct rq_geometry rq_types[]= {
	{311200,0x25644036,13,17,15,7}, /* RD54 */
	{138672,0x25644035,9,17,8,5},   /* RD53 */
	{60480,0x25644034,8,17,8,4},    /* RD52 */
	{21600,0x25644033,6,18,4,36},   /* RD51 */
	{41560,0x2564401f,12,17,4,3},   /* RD31 */
	{83204,0x25644020,15,17,6,4},   /* RD32 */
	{237212,0x25641050,1,31,14,0},  /* RA80 */
	{547041,0x25641046,18,33,11,198},/* RA70 */
	{891072,0x25641051,5,51,14,2856},/* RA81 */
	{1216665,0x25641052,11,57,15,3420}, /* RA82 */
	{1367310,0x25641047,40,51,14,1428} /* RA71 */
};
static const struct rq_geometry *rq_type(u32 blocks)
{
	const struct rq_geometry *best=rq_types;
	u32 distance=0xffffffffu;
	for(unsigned i=0; i<sizeof(rq_types)/sizeof(*rq_types); i++) {
		u32 n=rq_types[i].blocks,d=blocks>n?blocks-n:n-blocks;
		if(d<distance) {
			best=rq_types+i;
			distance=d;
		}
	}
	return best;
}
static u32 packet32(const uint16_t *p,unsigned n)
{
	return p[n]|((u32)p[n+1]<<16);
}
static void set32(uint16_t *p,unsigned n,u32 v)
{
	p[n]=v;
	p[n+1]=v>>16;
}
static int rq_word(u32 address,unsigned write,uint16_t *value)
{
	if(!active() || (address&1) || address>=0x200000u) {
		return 0;
	}
	DMA_MODE=1;
	DMA_ADDR=address;
	if(write) {
		DIRECT=*value;
	}
	u32 result=transfer(write?7:6);
	if((result&2) || !active()) {
		return 0;
	}
	if(!write) {
		*value=DIRECT;
	}
	return 1;
}
static int rq_words(u32 address,unsigned write,uint16_t *p,unsigned count)
{
	for(unsigned i=0; i<count; i++)if(!rq_word(address+2*i,write,p+i)) {
			return 0;
		}
	return 1;
}
static int rq_store(u32 address,uint16_t value)
{
	return rq_word(address,1,&value);
}
static void rq_fail(u32 code)
{
	rq.state=RQ_DEAD;
	R(2)=0x8000u|code;
	rq_interrupt();
}
static u32 rq_size(const struct sd_partition *p,const struct rq_geometry *t)
{
	return p->blocks<t->blocks?p->blocks:t->blocks;
}
static u32 rq_data(struct sd_partition *p,uint16_t *cmd,uint16_t *rsp)
{
	u32 op=cmd[6]&255,unit=cmd[4],bytes=packet32(cmd,8),ba=packet32(cmd,10),lbn=packet32(cmd,16),done=0;
	set32(rsp,8,0);
	set32(rsp,16,lbn);
	if(!p) {
		return 3|32;
	}
	if(!(rq.online&(1u<<unit))) {
		return 4;
	}
	if(ba&1) {
		return 9|32;
	}
	if(bytes&1) {
		return 9|64;
	}
	if(bytes&0xf0000000u) {
		return 1|(12<<8);
	}
	if(!bytes) {
		return 0;
	}
	const struct rq_geometry *t=rq_type(p->blocks);
	u32 blocks=rq_size(p,t),count=(bytes+511)>>9;
	if(lbn>=blocks) {
		if(lbn-blocks>=t->rct || lbn>=p->blocks) {
			return 1|(28<<8);
		}
		if(bytes!=512) {
			return 1|(12<<8);
		}
	} else if(count>blocks-lbn) {
		return 1|(12<<8);
	}
	if(op==042) {
		if(lbn>=blocks) {
			return 1|(28<<8);
		}
		if(rq.swp&(1u<<unit)) {
			return 6|(128<<5);
		}
		if(p->flags&SD_READONLY) {
			return 6|(256<<5);
		}
	}
	u32 status=0;
	DMA_MODE=1;
	while(done<bytes && active()) {
		u32 chunk=bytes-done;
		if(chunk>512) {
			chunk=512;
		}
		if(ba>=0x200000u || chunk>0x200000u-ba) {
			status=9|(3<<5);
			break;
		}
		DMA_ADDR=ba;
		u32 error=sector_io(p->start+lbn,op==042?1:op==040?2:0,chunk>>1,0,256);
		close_card();
		if(error) {
			status=error==NXM?9|(3<<5):error==DCK && op==040?7:18;
			break;
		}
		done+=chunk;
		ba+=chunk;
		++lbn;
	}
	set32(rsp,8,done);
	return status;
}
static void rq_command(uint16_t *cmd,uint16_t *rsp)
{
	u32 op=cmd[6]&255,unit=cmd[4],mod=cmd[7],status=0,len=12;
	if(op==3 && (mod&1) && unit>=4) {
		unit=0;
	}
	struct sd_partition *p=find_drive(4,unit);
	for(unsigned i=0; i<32; i++) {
		rsp[i]=0;
	}
	rsp[2]=cmd[2];
	rsp[3]=cmd[3];
	rsp[4]=unit;
	rsp[6]=op|0200;
	if(op==2) {
		len=20;        /* synchronous commands: no outstanding reference */
	} else if(op==4) {
		len=32;
		if(cmd[8]) {
			status=1|(12<<8);
		} else {
			rsp[9]=0x8000|(cmd[9]&0xf0);
			rsp[10]=120;
			rsp[11]=0x0103;
			rsp[15]=0x0113; /* RQDX3, MSCP controller class */
		}
	} else if(op==3 || op==011 || op==012) {
		len=op==3?48:44;
		status=!p?3|32:0;
		if(p && op==3 && !(rq.online&(1u<<unit))) {
			status=4;
		}
		if(p && op!=3) {
			if(op==011 && (rq.online&(1u<<unit))) {
				status=8<<5;
			} else {
				if(op==011) {
					rq.online|=1u<<unit;
				}
				if((mod&4) && (cmd[9]&0x1000)) {
					rq.swp|=1u<<unit;
				} else {
					rq.swp&=~(1u<<unit);
				}
			}
		}
		const struct rq_geometry *t=rq_type(p?p->blocks:311200);
		rsp[8]=unit;
		rsp[9]=0x8000 | (p && (p->flags&SD_READONLY)?0x2000:0) |
		       (unit<4 && (rq.swp&(1u<<unit))?0x1000:0);
		rsp[12]=unit;
		rsp[15]=0x0200|t->model;
		set32(rsp,16,t->media);
		rsp[18]=unit;
		if(op==3) {
			rsp[20]=t->sectors;
			rsp[21]=t->tracks;
			rsp[22]=1;
			rsp[24]=t->rct;
			rsp[25]=t->rct?0x0101:0;
		} else {
			set32(rsp,20,p?rq_size(p,t):0);
			set32(rsp,22,01234+unit);
		}
	} else if(op==010) {
		if(!p) {
			status=3|32;
		} else {
			rq.online&=~(1u<<unit);
			rq.swp&=~(1u<<unit);
		}
	} else if(op==020) {
		if(!p) {
			status=3|32;
		}
	} else if(op==040 || op==041 || op==042) {
		len=32;
		status=rq_data(p,cmd,rsp);
	} else {
		status=1|(8<<8);
	}
	rsp[0]=len;
	rsp[1]=1+rq.credits;
	rq.credits=0;
	rsp[7]=status;
	if((status&31)==18) {
		rsp[6]|=0x1000;
	}
}
static u32 rq_slot(unsigned ring)
{
	return rq.comm+(ring?rq.length[0]:0)+rq.index[ring];
}
static int rq_release(unsigned ring,u32 descriptor)
{
	uint16_t words[2];
	set32(words,0,(descriptor&0x7fffffffu)|0x40000000u);
	if(!rq_words(rq_slot(ring),1,words,2) || !rq_store(rq.comm-(ring?4:2),1)) {
		return 0;
	}
	rq.index[ring]=(rq.index[ring]+4)&(rq.length[ring]-1);
	return 1;
}
static int rq_poll(void)
{
	uint16_t desc[4];
	if(!rq_words(rq_slot(1),0,desc+2,2)) {
		rq_fail(1);
		return 0;
	}
	if(!(desc[3]&0x8000)) {
		return 0;
	}
	if(!rq_words(rq_slot(0),0,desc,2)) {
		rq_fail(2);
		return 0;
	}
	if(!(desc[1]&0x8000)) {
		return 0;
	}
	u32 response=packet32(desc,0),command=packet32(desc,2);
	u32 ca=command&0x3ffffe,ra=response&0x3ffffe;
	if(ca<4 || ra<4) {
		rq_fail(010);
		return 0;
	}
	uint16_t *cmd=scratch.live.packet[0],*rsp=scratch.live.packet[1];
	if(!rq_words(ca-4,0,cmd,2)) {
		rq_fail(1);
		return 0;
	}
	u32 length=cmd[0];
	if(length<12 || length>60 || (length&1) || (cmd[1]&0xfff0)) {
		rq_fail(1);
		return 0;
	}
	for(unsigned i=2; i<32; i++) {
		cmd[i]=0;
	}
	if(!rq_words(ca,0,cmd+2,length>>1)) {
		rq_fail(1);
		return 0;
	}
	rq_command(cmd,rsp);
	if(!rq_words(ra-4,1,rsp,(rsp[0]+4)>>1)) {
		rq_fail(2);
		return 0;
	}
	if(!rq_release(1,command) || !rq_release(0,response)) {
		rq_fail(010);
		return 0;
	}
	rq_interrupt();
	return 1; /* yield between packets; a subsequent pass checks ownership */
}
static void rq_step4(void)
{
	rq.length[0]=4u<<((rq.s1>>8)&7);
	rq.length[1]=4u<<((rq.s1>>11)&7);
	rq.index[0]=rq.index[1]=0;
	if(rq.comm<4) {
		rq_fail(7);
		return;
	}
	for(u32 a=rq.comm-4; a<rq.comm+rq.length[0]+rq.length[1]; a+=2)
		if(!rq_store(a,0)) {
			rq_fail(7);
			return;
		}
	rq.state=RQ_S4;
	R(2)=0x4133;
	rq_interrupt();
}
u32 rq_service(void)
{
	u32 event=R(1),v=R(0);
	if(event==0) {
		rq.state=RQ_S1;
		rq.online=rq.swp=0;
		rq.credits=7;
		rq.comm=0;
		rq.s1=0;
		R(2)=0x0b40;
		R(3)=0;
	} else if(event==2) {
		if(rq.state==RQ_PURGE_IP) {
			rq_step4();
		} else if(rq.state==RQ_UP) {
			return rq_poll();
		}
	} else switch(rq.state) {
		case RQ_S1:
		case RQ_WRAP:
			if(v&0x4000) {
				R(2)=v;
				rq.state=RQ_WRAP;
			} else if(v&0x8000) {
				rq.s1=v;
				R(3)=v&255;
				R(2)=0x1000|(v>>8);
				rq.state=RQ_S2;
				rq_interrupt();
			}
			break;
		case RQ_S2:
			rq.comm=v&0xfffe;
			R(2)=0x2000|(rq.s1&255);
			rq.state=RQ_S3;
			rq_interrupt();
			break;
		case RQ_S3:
			rq.comm|=(v&0x7fff)<<16;
			if(v&0x8000) {
				R(2)=0;
				rq.state=RQ_PURGE;
				rq_interrupt();
			} else {
				rq_step4();
			}
			break;
		case RQ_PURGE:
			if(!v) {
				rq.state=RQ_PURGE_IP;
			}
			break;
		case RQ_S4:
			if(v&1) {
				rq.state=RQ_UP;
				R(2)=0;
			}
			break;
		default:
			break;
		}
	return 0;
}
