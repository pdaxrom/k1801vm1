/* Exercise the actual daemon request dispatcher with an in-memory transport. */
#define main hg_daemon_main
#include "hgfsd.c"
#undef main
#include <assert.h>

static uint8_t incoming[HG_HEADER_SIZE + HG_BLOCK_SIZE + 2];
static uint8_t outgoing[HG_BLOCK_SIZE + 4];
static size_t in_pos, out_pos;
static int selected;

int hg_mpsse_open(struct hg_mpsse *l, int v, int p, const char *s,
                  unsigned int i, unsigned int hz)
{
	(void)l;
	(void)v;
	(void)p;
	(void)s;
	(void)i;
	(void)hz;
	return -1;
}
void hg_mpsse_close(struct hg_mpsse *l)
{
	(void)l;
}
int hg_mpsse_jtag_enable(struct hg_mpsse *l, int e)
{
	(void)l;
	(void)e;
	return 0;
}
int hg_mpsse_request_pending(struct hg_mpsse *l)
{
	(void)l;
	return 0;
}
int hg_mpsse_select(struct hg_mpsse *l, int value)
{
	(void)l;
	selected=value;
	return 0;
}
int hg_mpsse_exchange(struct hg_mpsse *l, const uint8_t *tx, uint8_t *rx, size_t n)
{
	(void)l;
	assert(selected);
	if (rx) {
		assert(in_pos+n<=sizeof(incoming));
		memcpy(rx,incoming+in_pos,n);
		in_pos+=n;
	}
	if (tx) {
		assert(out_pos+n<=sizeof(outgoing));
		memcpy(outgoing+out_pos,tx,n);
		out_pos+=n;
	}
	return 0;
}

static void request(unsigned int op, unsigned int block, unsigned int count)
{
	memset(incoming,0,sizeof(incoming));
	memset(outgoing,0,sizeof(outgoing));
	in_pos=out_pos=0;
	incoming[0]='H';
	incoming[1]='G';
	incoming[2]=1;
	incoming[3]=(uint8_t)op;
	incoming[5]=(uint8_t)block;
	incoming[6]=(uint8_t)(block>>8);
	incoming[7]=(uint8_t)count;
	incoming[8]=(uint8_t)(count>>8);
	incoming[9]=hg_header_checksum(incoming);
}

int main(void)
{
	char path[]="/tmp/hg-service.XXXXXX";
	uint8_t original[HG_BLOCK_SIZE], readback[HG_BLOCK_SIZE];
	struct hg_image image= {.fd=-1};
	struct hg_mpsse link= {0};
	struct hg_request served;
	int fd=mkstemp(path);
	assert(fd>=0);
	memset(original,0xa5,sizeof(original));
	assert(write(fd,original,sizeof(original))==(ssize_t)sizeof(original));
	close(fd);
	assert(hg_image_open(&image,path,1)==0);
	for (unsigned int hz=50; hz<=60; hz+=10) {
		request(HG_OP_TIME,hz,HG_TIME_SIZE);
		assert(hg_serve_one(&link,&image,&served)==0);
		assert(!selected && served.operation==HG_OP_TIME && out_pos==9);
		assert(outgoing[0]==HG_STATUS_OK);
		uint16_t sum=hg_data_checksum(outgoing+1,6);
		assert(outgoing[7]==(uint8_t)sum && outgoing[8]==(uint8_t)(sum>>8));
	}
	request(HG_OP_TIME,51,6);
	assert(hg_serve_one(&link,&image,NULL)==0 && out_pos==1);
	assert(outgoing[0]==HG_STATUS_PROTOCOL && !selected);
	request(HG_OP_READ,0,4);
	assert(hg_serve_one(&link,&image,NULL)==0 && out_pos==7);
	assert(outgoing[0]==HG_STATUS_OK && memcmp(outgoing+1,original,4)==0);
	request(HG_OP_WRITE,0,4);
	assert(hg_serve_one(&link,&image,NULL)==0 && out_pos==1);
	assert(outgoing[0]==HG_STATUS_READ_ONLY);
	hg_image_close(&image);
	assert(hg_image_open(&image,path,0)==0);
	request(HG_OP_WRITE,0,4);
	memcpy(incoming+HG_HEADER_SIZE,"TIME",4);
	uint16_t sum=hg_data_checksum(incoming+HG_HEADER_SIZE,4);
	incoming[HG_HEADER_SIZE+4]=(uint8_t)sum;
	incoming[HG_HEADER_SIZE+5]=(uint8_t)(sum>>8);
	assert(hg_serve_one(&link,&image,NULL)==1 && out_pos==2);
	assert(outgoing[0]==0 && outgoing[1]==0);
	assert(hg_image_read(&image,0,readback,sizeof(readback))==0);
	assert(memcmp(readback,"TIME",4)==0);
	assert(memcmp(readback+4,original+4,sizeof(readback)-4)==0);
	request(HG_OP_WRITE,0,4); /* bad checksum must preserve disk */
	incoming[HG_HEADER_SIZE]=1;
	assert(hg_serve_one(&link,&image,NULL)==0 && outgoing[1]==HG_STATUS_CHECKSUM);
	assert(hg_image_read(&image,0,readback,4)==0 && memcmp(readback,"TIME",4)==0);
	hg_image_close(&image);
	unlink(path);
	puts("hg service: TIME read-only, framing, checksum and disk regression passed");
	return 0;
}
