#include "storage.h"
/* Every PDP-11 register and its side effects live in SERV RAM. The FPGA only
 * holds the bus response, one interrupt vector and DMA/SD ownership controls. */
struct controller *current;
uint16_t *regs,boot_status;
unsigned service_bank;
static uint16_t generation,cache_control;
#define xp_drive scratch.live.xp
static const uint8_t xp_reg[]= {3,14,15,13,6,5};
static uint8_t card_control;
static uint8_t devices_ready;
static const struct {
	uint16_t base;
	uint8_t count,vector;
} ports[]= {
	{017440,16,0210/4},{014400,5,0160/4},{016700,22,0254/4},
	{017400,8,0220/4},{012150,2,0}
};
static void xp_save(void)
{
	struct controller *c=controllers+XP_BANK;
	for(unsigned i=0; i<6; i++) {
		xp_drive[c->r[4]&7][i]=c->r[xp_reg[i]];
	}
}
static void xp_load(void)
{
	struct controller *c=controllers+XP_BANK;
	for(unsigned i=0; i<6; i++) {
		c->r[xp_reg[i]]=xp_drive[c->r[4]&7][i];
	}
}
static unsigned attention(void)
{
	xp_save();
	unsigned result=0;
	for(unsigned i=0; i<8; i++)if(xp_drive[i][5]&0100000) {
			result|=1u<<i;
		}
	return result;
}
void controller_reset(unsigned bank)
{
	struct controller *c=controllers+bank;
	if(bank==XP_BANK) {
		xp_save();
	}
	for(unsigned i=0; i<22; i++) {
		c->r[i]=0;
	}
	c->busy=c->irq=0;
	++c->epoch;
	if(bank==XP_BANK) {
		for(unsigned i=0; i<8; i++) {
			xp_drive[i][4]=0;
		}
		xp_load();
	}
	if(bank<=XP_BANK) {
		c->r[0]=0200;
	}
	if(bank==RQ_BANK) {
		c->r[2]=0x0b40;
		c->busy=1;
	}
}
static void publish_irq(void)
{
	/* Same BR5 daisy-chain order as the board, selected in software. */
	static const uint8_t order[]= {RL_BANK,RH_BANK,XP_BANK,RK_BANK,RQ_BANK};
	unsigned vector=0;
	for(unsigned i=0; i<NCONTROLLERS; i++) {
		unsigned b=order[i];
		struct controller *c=controllers+b;
		if(c->irq) {
			vector=b==RQ_BANK?(c->r[3]&127):ports[b].vector;
			break;
		}
	}
	BUS_IRQ=vector?0x10000u|(vector<<2):0;
}
void sd_ownership(unsigned take)
{
	card_control=take ? 1u|((service_bank!=XP_BANK && service_bank!=RQ_BANK)?2:0)|(current?8:0) : 0;
	BUS_CONTROL=card_control;
	if(take)while(!(BUS_STATUS&8)) {
			poll_io();
		}
}
void service_begin(unsigned bank)
{
	service_bank=bank;
	current=controllers+bank;
	regs=current->r;
	generation=current->epoch;
	sd_ownership(1);
}
int active(void)
{
	poll_io();
	return current && current->busy && current->epoch==generation;
}
void rq_interrupt(void)
{
	struct controller *c=controllers+RQ_BANK;
	if(c->r[3]&128) {
		c->irq=1;
	}
}
void controller_finish(u32 error)
{
	struct controller *c=current;
	if(active()) {
		if(service_bank==RQ_BANK) {
			c->busy=error || c->r[5];
			c->r[1]=2;
			c->r[5]=0;
		} else {
			c->busy=0;
			if(service_bank==RK_BANK) {
				c->r[1]|=error;
				c->irq=(c->r[2]>>6)&1;
			} else {
				if(service_bank==RL_BANK) {
					c->r[0]=(c->r[0]&01776)|0200|error;
				} else {
					c->r[6]=error;
					c->r[0]=(c->r[0] & (service_bank==XP_BANK?0176:03576)) | 0200 |
					        ((error || (c->r[4]&0177400))?(service_bank==XP_BANK?0140000:0100000):0);
					if(service_bank==RH_BANK && ((c->r[0]>>1)&31)==5) {
						c->r[0]|=040000;
					}
				}
				c->irq=(c->r[0]>>6)&1;
			}
		}
	}
	current=0;
	sd_ownership(0);
	publish_irq();
}
static u32 value(unsigned bank,unsigned a)
{
	struct controller *c=controllers+bank;
	uint16_t *r=c->r;
	unsigned unit=bank==RK_BANK?r[5]>>13:bank==RL_BANK?(r[0]>>8)&3:r[4]&7;
	unsigned present=(c->present>>unit)&1,ro=(c->ro>>unit)&1;
	if(bank==RQ_BANK) {
		return a?r[2]:0;
	}
	if(bank==RK_BANK) {
		if(a==0) {
			return (unit<<13)|(r[5]&15)|04420|(present?(c->busy?0200:0300):0)|(((c->ro|r[20])>>unit)&1?040:0);
		}
		if(a==2) {
			return (r[2]&027776)|(c->busy?1:0200)|(r[1]?0100000:0)|((r[1]&0177740)?040000:0);
		}
	}
	if(bank==RL_BANK && a==0) {
		return (r[0]&~061u)|((r[4]&3)<<4)|(present && !c->busy);
	}
	if(bank==RH_BANK && a==5) {
		return present?0100701|(ro?04000:0):0;
	}
	if(bank==XP_BANK)switch(a) {
		case 0:
			return (r[0]&~0101400u)|((r[20]&3)<<8)|04000|((r[0]&040000)||attention()?0100000:0);
		case 4:
			return r[4]|0300;
		case 5:
			return present?010400|(c->busy?0:0200)|(r[5]&0102100)|(ro?04000:0)|(r[6]?040000:0):0;
		case 7:
			return attention();
		case 8:
			return (r[3]&63)<<6;
		case 11:
			return 020027;
		case 12:
			return 021+unit;
		case 21:
			return r[0]&0100;
		default:
			break;
		}
	return r[a];
}
static void start(struct controller *c)
{
	c->busy=1;
	if(c!=controllers+RQ_BANK) {
		c->irq=0;
	}
	++c->epoch;
}
/* Return false only for a deferred SA write while its previous event runs. */
static int access(unsigned bank,unsigned a,unsigned lanes,unsigned writing,u32 data,u32 *result)
{
	struct controller *c=controllers+bank;
	uint16_t *r=c->r;
	u32 old=value(bank,a),mask=(lanes&1?255u:0)|(lanes&2?65280u:0),v=(old&~mask)|(data&mask);
	*result=old;
	if(bank==RQ_BANK) {
		if(writing) {
			if(!a) {
				controller_reset(bank);
			} else {
				if(c->busy) {
					return 0;
				}
				/* Finish the SA state transition before acknowledging this
				 * write: secondary boot loaders inspect SA immediately. */
				if(r[7]) {
					r[7]=0;
				} else {
					r[6]=(r[6]&~mask)|(data&mask);
					if(lanes&2) {
						r[0]=r[6];
						r[1]=1;
						r[7]=1;
						start(c);
						return 0;
					}
				}
			}
		} else if(!a) {
			if(c->busy) {
				r[5]=1;
			} else {
				r[1]=2;
				start(c);
			}
		}
		return 1;
	}
	if(!writing) {
		if(bank==RL_BANK && a==3 && (lanes&2) && r[6]) {
			if(r[6]==1) {
				r[3]=0;
			}
			if(r[6]==2) {
				r[3]=r[5];
			}
			r[6]=(r[6]+1)&3;
		}
		return 1;
	}
	/* Clear bits are write strobes, not the merged readback value: CS1 bit 15
	 * also reports ERR and must survive a write to the other byte. */
	if((bank==RH_BANK && a==0 && (lanes&2) && (data&0100000)) ||
	                ((bank==RH_BANK || bank==XP_BANK) && a==4 && (lanes&1) && (data&040)) ||
	                (bank==RK_BANK && a==2 && (lanes&1) && (v&017)==1)) {
		controller_reset(bank);
		if(bank==RK_BANK) {
			r[2]=v&07500;
			c->irq=(v>>6)&1;
		}
		if(c==current) {
			BUS_CONTROL=card_control|4;
		}
		return 1;
	}
	unsigned cs=bank==RK_BANK?2:0,was_busy=c->busy;
	if(a==cs || (bank==XP_BANK && a==21)) {
		unsigned previous=r[cs];
		if(lanes&1) {
			r[cs]=(previous&~0100u)|(v&0100);
			if(!(v&0100)) {
				c->irq=0;
			} else if(!c->busy && (bank==RK_BANK || (previous&0200)) &&
			                ((bank!=XP_BANK && bank!=RK_BANK) || !(previous&0100))) {
				c->irq=1;
			}
			if(!c->busy && a!=21) {
				if(bank==RL_BANK) {
					r[0]=(v&01776)|0200;
					r[4]=(r[4]&~3u)|((v>>4)&3);
					if(!(v&0200)) {
						r[0]&=~0200u;
						r[6]=0;
						start(c);
					}
				} else if(bank==RK_BANK) {
					r[2]=v&07776;
					if(v&1) {
						r[1]&=0177774;
						start(c);
					}
				} else {
					if(bank==XP_BANK) {
						r[0]=(v&0176)|(previous&0140200);
					}
					if(v&1) {
						r[0]=v & (bank==XP_BANK?0177:03577);
						r[6]=0;
						r[4]&=bank==XP_BANK?037:7;
						start(c);
					}
				}
			}
		}
		if(lanes&2) {
			if(bank==RK_BANK) {
				r[2]=(r[2]&0377)|(v&07400);
			} else if(!was_busy) {
				if(bank==RH_BANK) {
					r[0]=(r[0]&~01400u)|(v&01400);
				}
				if(bank==RL_BANK) {
					r[0]=(r[0]&~01400u)|(v&01400);
				}
				if(bank==XP_BANK) {
					r[20]=(r[20]&~3u)|((v>>8)&3);
					if(v&040000) {
						r[0]&=037777;
						r[4]&=037;
					}
				}
			}
		}
	} else if(bank==XP_BANK && a==7) {
		if(lanes&1) {
			xp_save();
			for(unsigned u=0; u<8; u++)if(data&(1u<<u)) {
					xp_drive[u][5]&=~0100000u;
				}
			xp_load();
		}
	} else if(!c->busy) {
		static const uint8_t first[]= {0,10,15,37};
		static const uint8_t count[]= {10,5,22,8};
		static const uint16_t writable[]= {
			0,0177777,0177776,0177777,7,0,0,0177777,0177777,0177777,
			0,0177776,0177777,0177777,077,
			0,0177777,0177776,037777,037,0,0,0,0,0177777,0,0,0,016377,01777,0,0,0,0,0,077,0,
			0,0,0,0177777,0177776,0177777,0,0177777
		};
		if(a<count[bank]) {
			unsigned bits=writable[first[bank]+a];
			if(bank==XP_BANK && a==4) {
				xp_save();
			}
			r[a]=(r[a]&~bits)|(v&bits);
			if(bank==XP_BANK && a==4) {
				xp_load();
			}
			if(bank==RL_BANK && a==3) {
				r[6]=0;
			}
		}
	}

	return 1;
}
void poll_io(void)
{
	u32 status=BUS_STATUS;
	if(status&2) {
		if(devices_ready)for(unsigned b=0; b<NCONTROLLERS; b++) {
				controller_reset(b);
			}
		cache_control=0;
		BUS_STATUS=6;
		BUS_CONTROL=card_control|4;
		return;
	}
	if(status&4) {
		for(unsigned b=0; b<NCONTROLLERS; b++) {
			unsigned v=b==RQ_BANK?(controllers[b].r[3]&127):ports[b].vector;
			if(controllers[b].irq && v==((status>>8)&127)) {
				controllers[b].irq=0;
				break;
			}
		}
		BUS_STATUS=4;
		publish_irq();
	}
	if(!(status&1)) {
		return;
	}
	u32 request=BUS_REQUEST,address=request&8191,writing=(request>>13)&1,lanes=(request>>14)&3,data=request>>16;
	u32 result=0x10000;
	if(address==017504) {
		result=boot_status;
	} else if(address==017506) {
		result=!devices_ready?0:((u32)controllers[RK_BANK].present<<8)|controllers[RQ_BANK].present;
	} else if(address==017744) {
		result=0;
	} else if(address==017746) {
		result=cache_control;
		if(writing) {
			cache_control=(cache_control&~((lanes&1?255:0)|(lanes&2?65280:0)))|(data&((lanes&1?255:0)|(lanes&2?65280:0)));
		}
	} else if(devices_ready)for(unsigned b=0; b<NCONTROLLERS; b++) {
			unsigned a=(address-ports[b].base)>>1;
			if(a<ports[b].count && controllers[b].present) {
				if(!access(b,a,lanes,writing,data,&result)) {
					return;
				}
				break;
			}
		}
	if(devices_ready && writing) {
		publish_irq();
	}
	BUS_RESPONSE=result;
}

void controllers_init(void)
{
	for(unsigned i=0; i<sizeof(scratch); i++) {
		((uint8_t *)&scratch)[i]=0;
	}
	for(unsigned b=0; b<NCONTROLLERS; b++) {
		controller_reset(b);
	}
	devices_ready=1;
}
