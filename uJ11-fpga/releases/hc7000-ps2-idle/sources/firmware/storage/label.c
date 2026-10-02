#include "label.h"
static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static unsigned get16(const uint8_t *p)
{
	return p[0] | (unsigned)p[1]<<8;
}
int sd_label_decode(const uint8_t raw[512], uint32_t capacity, struct sd_label *out)
{
	static const uint8_t magic[8]= {'U','J','1','1','S','D',0,0};
	static const uint8_t kinds[8]= {1,2,2,3,4,5,5,6};
	static const uint32_t sizes[8]= {4872,27126,53790,500384,0,10240,20480,0};
	uint32_t crc=~0u, blocks=get32(raw+24), reserved=get32(raw+28);
	unsigned count=get16(raw+14), boots=0;
	for(unsigned i=0; i<8; i++)if(raw[i]!=magic[i]) {
			return 0;
		}
	if(get16(raw+8)!=1 || get16(raw+10)!=64 || get16(raw+12)!=32 || count>14 ||
	                get32(raw+32)!=1 || (get32(raw+36)&~SD_MENU) ||
	                ((get32(raw+36)&SD_MENU) && reserved<19) || reserved<2 || reserved>=blocks || blocks>capacity) {
		return 0;
	}
	for(unsigned i=40; i<64; i++)if(raw[i]) {
			return 0;
		}
	for(unsigned i=64+32*count; i<512; i++)if(raw[i]) {
			return 0;
		}
	for(unsigned i=0; i<512; i++) {
		crc^=(i>=20 && i<24) ? 0 : raw[i];
		for(unsigned b=0; b<8; b++) {
			crc=(crc>>1)^((0u-(crc&1))&0xedb88320u);
		}
	}
	if(~crc!=get32(raw+20)) {
		return 0;
	}
	for(unsigned i=0; i<count; i++) {
		const uint8_t *p=raw+64+32*i;
		struct sd_partition *q=out->part+i;
		q->kind=p[0];
		q->unit=p[2];
		q->media=p[3];
		q->flags=p[4];
		q->mode=p[6];
		q->start=get32(p+8);
		q->blocks=get32(p+12);
		if(p[1] || p[5] || p[7] || q->media<1 || q->media>8 ||
		                kinds[q->media-1]!=q->kind || q->unit>=((q->kind==4 || q->kind==5)?4:8) ||
		                q->flags&~3u || q->mode>(q->kind==2?1:0) ||
		                !q->blocks || q->start<reserved || q->start>=blocks || q->blocks>blocks-q->start ||
		                (sizes[q->media-1] && sizes[q->media-1]!=q->blocks)) {
			return 0;
		}
		uint32_t low=get32(p+16),high=get32(p+20);
		uint32_t caplow=q->blocks<<9,caphigh=q->blocks>>23;
		if(high>caphigh || (high==caphigh && low>caplow) ||
		                (q->kind!=6 && (high!=caphigh || low!=caplow))) {
			return 0;
		}
		unsigned zero=0;
		for(unsigned n=24; n<32; n++) {
			if(!p[n]) {
				zero=1;
			} else if(zero || p[n]<32 || p[n]>126) {
				return 0;
			}
		}
		boots+=!!(q->flags&SD_BOOT);
		for(unsigned j=0; j<i; j++) {
			const struct sd_partition *r=out->part+j;
			if((q->kind==r->kind && (q->unit==r->unit || q->mode!=r->mode)) ||
			                (q->start<r->start+r->blocks && r->start<q->start+q->blocks)) {
				return 0;
			}
		}
	}
	if(boots>1) {
		return 0;
	}
	out->blocks=blocks;
	out->features=get32(raw+36);
	out->count=count;
	return 1;
}
