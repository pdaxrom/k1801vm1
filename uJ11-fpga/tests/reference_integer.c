/* Execute guest programs on the unmodified DCJ11 reference. The RTL consumes
 * the same initial RAM and compares state before the next instruction fetch.
 * No FPGA implementation details or precomputed ALU answers are used here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/core.h"
#include "core/hardware.h"
static byte memory[1u<<22];
static regs cpu;
static unsigned addresses[256],values[256],probes[64],nwords,nprobes,pos,cases,nins;
static void put(unsigned a,word v)
{
	memory[a]=v;
	memory[a+1]=v>>8;
	for(unsigned i=0; i<nwords; i++)if(addresses[i]==a) {
			values[i]=v;
			return;
		}
	if(nwords==256) {
		abort();
	}
	addresses[nwords]=a;
	values[nwords++]=v;
}
static word get(unsigned a)
{
	return memory[a]|((word)memory[a+1]<<8);
}
static void emit(word v)
{
	put(pos,v);
	pos+=2;
}
static void mov(word v,word a)
{
	emit(012737);
	emit(v);
	emit(a);
	nins++;
}
static void reg(unsigned r,word v)
{
	emit(012700|r);
	emit(v);
	nins++;
}
static void probe(unsigned a)
{
	if(nprobes==64) {
		abort();
	}
	probes[nprobes++]=a;
}
static void begin(void)
{
	memset(&cpu,0,sizeof cpu);
	hwstub_set_memory(memory,sizeof memory);
	cpu.model=DCJ11;
	hwstub_connect(&cpu);
	if(core_init(&cpu)) {
		abort();
	}
	core_reset(&cpu);
	cpu.r[7]=04000;
	nwords=nprobes=nins=0;
	pos=04000;
	/* Explicitly initialize both banks: the C fixture lazily initializes
	 * an untouched alternate bank, while FPGA reset clears physical slots. */
	mov(04000,0177776);
	for(unsigned r=0; r<6; r++) {
		reg(r,02000+r*020);
	}
	mov(0,0177776);
	reg(6,02000);
	mov(040000,0177776);
	reg(6,02600);
	mov(0140000,0177776);
	reg(6,03000);
	mov(0,0177776);
	for(unsigned r=0; r<6; r++) {
		reg(r,01000+r*020);
	}
	/* Unexpected traps stop at a distinct PC, rather than silently looping. */
	for(unsigned a=4; a<=034; a+=4) {
		put(a,07000);
		put(a+2,0340);
	}
	put(0250,07000);
	put(0252,0340);
	put(07000,1);
	for(unsigned a=01774; a<02000; a+=2) {
		probe(a);
	}
	for(unsigned a=02574; a<02600; a+=2) {
		probe(a);
	}
	for(unsigned a=02774; a<03000; a+=2) {
		probe(a);
	}
}
static void finish(const char *name)
{
	nins++; /* The tested instruction follows the setup MOVs. */
	for(unsigned i=0; i<nins; i++)if(core_step(&cpu)) {
			abort();
		}
	printf("%x %x %x %x %s\n",cases++,nwords,nprobes,nins,name);
	for(unsigned i=0; i<nwords; i++) {
		printf("%x %x\n",addresses[i],values[i]);
	}
	for(unsigned i=0; i<8; i++) {
		printf("%x ",cpu.r[i]);
	}
	printf("%x %x %x\n",cpu.psw,cpu.J11_CPUERR,cpu.J11_PIRQ);
	for(unsigned i=0; i<nprobes; i++) {
		printf("%x %x\n",probes[i],get(probes[i]));
	}
	core_fini(&cpu);
}
int main(void)
{
	const word input[]= {0,1,0100000,0177777};
	for(unsigned cm=0; cm<4; cm++)if(cm!=2)
			for(unsigned ipl=0; ipl<8; ipl++)for(unsigned v=0; v<4; v++)for(unsigned form=0; form<3; form++) {
						begin();
						reg(0,input[v]);
						reg(1,01001);
						put(01000,input[v]<<8);
						mov((cm<<14)|(ipl<<5)|017,0177776);
						emit(0106400|(form==0?0:form==1?027:011));
						if(form==1) {
							emit(input[v]);
						}
						finish("mtps");
					}
	/* Separate all three D spaces from I. PM=2 is the J11 previous-user alias.
	 * MTPI user->user must remain UI; the MFPI exception must not leak to it. */
	for(unsigned cm=0; cm<4; cm++)if(cm!=2)for(unsigned pm=0; pm<4; pm++)
				for(unsigned family=0; family<4; family++) {
					begin();
					reg(1,01000);
					for(unsigned m=0; m<4; m++)if(m!=2) {
							unsigned csr=m==0?0172300:m==1?0172200:0177600;
							mov(0177406,csr);
							mov(0,csr+040);
							mov(0177406,csr+020);
							mov((m+1)*010000,csr+060);
							mov(0177406,csr+036);
							mov(0177600,csr+076);
							put((m+1)*01000000+01000,01111*(m+1));
							probe((m+1)*01000000+01000);
							unsigned sp=m==0?02000:m==1?02600:03000;
							put((m+1)*01000000+sp,065432);
							probe((m+1)*01000000+sp-2);
						}
					put(01000,076543);
					probe(01000);
					mov(027,0172516);
					mov(1,0177572);
					mov((cm<<14)|(pm<<12)|1,0177776);
					emit((family==0?006500:family==1?0106500:family==2?006600:0106600)|011);
					finish("previous-space");
				}
	for(unsigned family=0; family<2; family++)for(unsigned spec=0; spec<64; spec++)
			for(unsigned v=0; v<4; v++)for(unsigned carry=0; carry<2; carry++) {
					begin();
					unsigned mode=spec>>3,r=spec&7;
					/* Cover R0 aliases, SP and PC, including register-form rejection. */
					reg(0,input[v]);
					if(r!=0 && r!=7) {
						reg(r,01200);
					}
					unsigned rv=r==0?input[v]:01200;
					mov(carry|012,0177776);
					unsigned op_pc=pos;
					if(r==7) {
						rv=op_pc+2;
					}
					unsigned ea=rv;
					if(mode==2) {
						ea=rv;
					}
					if(mode==3) {
						put(rv&~1u,01400);
						ea=01400;
					}
					if(mode==4) {
						ea=(word)(rv-2);
					}
					if(mode==5) {
						put((word)(rv-2)&~1u,01400);
						ea=01400;
					}
					if(mode==6) {
						ea=(word)(rv+(r==7?2:0)+020);
					}
					if(mode==7) {
						put((word)(rv+(r==7?2:0)+020)&~1u,01400);
						ea=01400;
					}
					/* Avoid operands inside I/O space: those are CSR tests, not RAM. */
					if(ea<0160000 && mode) {
						put(ea&~1u,input[v]);
						probe(ea&~1u);
					}
					emit((family?007300:007200)|spec);
					if(mode==6||mode==7) {
						emit(020);
					}
					/* PC-as-destination modes 2/3 advance over an inline word. */
					if(r==7 && (mode==2||mode==3)) {
						emit(mode==3?01400:input[v]);
					}
					/* Stop before fetching the next opcode, including self-modifying
					 * PC addressing forms and odd-address exceptions. */
					finish(family?"wrtlck":"tstset");
				}
	for(unsigned family=0; family<2; family++)for(unsigned protection=0; protection<2; protection++) {
			begin();
			reg(0,012345);
			reg(1,020000);
			put(020000,0100001);
			probe(020000);
			mov(0177406,0172300);
			mov(0,0172340);
			mov(protection?0177402:0,0172302);
			mov(0200,0172342);
			mov(0177406,0172316);
			mov(0177600,0172356);
			mov(020,0172516);
			mov(1,0177572);
			mov(017,0177776);
			emit((family?007300:007200)|021);
			finish(protection?"locked-write-abort":"locked-read-abort");
		}
	for(unsigned from=0; from<4; from++)if(from!=2)
			for(unsigned to=0; to<4; to++)if(to!=2)for(unsigned bank=0; bank<2; bank++) {
						begin();
						mov((from<<14)|3,0177776);
						put(014,07000);
						put(016,(to<<14)|(bank<<11)|0340);
						emit(3);
						finish("trap-vector-mode");
					}
	/* PIRQ readback encodes the highest request twice. All request patterns,
	 * all IPLs and every nonreserved mode exercise both masking and vectoring. */
	for(unsigned req=0; req<128; req++) {
		begin();
		mov(0340,0177776);
		mov(req<<9,0177772);
		emit(013700);
		emit(0177772);
		finish("pirq-readback");
		for(unsigned ipl=0; ipl<8; ipl++) {
			begin();
			mov(0340,0177776);
			put(0240,07000);
			put(0242,0340);
			mov(req<<9,0177772);
			emit(012737);
			emit(ipl<<5);
			emit(0177776);
			finish("pirq-ipl");
		}
	}
	for(unsigned cm=1; cm<=3; cm+=2) {
		begin();
		mov(0340,0177776);
		put(0240,07000);
		put(0242,0340);
		mov(010000,0177772);
		emit(012737);
		emit(cm<<14);
		emit(0177776);
		finish("pirq-mode");
	}
	for(unsigned lane=0; lane<2; lane++)for(unsigned value=0; value<256; value+=17) {
			begin();
			mov(0340,0177776);
			mov(0124000,0177772);
			emit(0112737);
			emit(value);
			emit(0177772+lane);
			nins++;
			emit(013700);
			emit(0177772);
			finish("pirq-byte");
		}
	for(unsigned cm=0; cm<4; cm++)if(cm!=2)
			for(unsigned sp=0376; sp<=0404; sp+=2)for(unsigned kind=0; kind<10; kind++) {
					begin();
					mov((cm<<14)|3,0177776);
					reg(6,sp);
					put(sp-2,01200);
					put(01200,012345);
					for(unsigned a=sp-014; a<=sp; a+=2) {
						probe(a);
					}
					switch(kind) {
					case 0:
						emit(012746);
						emit(012345);
						break; /* MOV #v,-(SP) */
					case 1:
						emit(014600);
						break; /* MOV -(SP),R0 */
					case 2:
						emit(015600);
						break; /* MOV @-(SP),R0 */
					case 3:
						emit(012756);
						emit(012345);
						break; /* MOV #v,@-(SP) */
					case 4:
						emit(004737);
						emit(01200);
						break; /* JSR PC,@#1200 */
					case 5:
						emit(006500);
						break; /* MFPI R0 push */
					case 6:
						emit(0106500);
						break; /* MFPD R0 push */
					case 7:
						emit(3);
						break; /* BPT pushes kernel stack */
					case 8:
						emit(0162706);
						emit(2);
						break; /* SUB #2,SP does not check */
					default:
						emit(0114600);
						break; /* MOVB -(SP),R0 still decrements by 2 */
					}
					finish("stack-reference");
				}
	/* RTT installs T without tracing itself. The following push must take
	 * trace first, then yellow, before fetching either handler instruction. */
	begin();
	reg(6,0374);
	put(0374,pos+2);
	put(0376,020);
	emit(6);
	nins++;
	put(014,07200);
	put(016,0340);
	for(unsigned a=0366; a<0400; a+=2) {
		probe(a);
	}
	emit(012746);
	emit(012345);
	finish("trace-before-yellow");
	/* Kernel trap push aborts in split D space. Emergency accesses must
	 * reach physical low memory independently of that failed mapping. */
	for(unsigned protect=0; protect<2; protect++) {
		begin();
		reg(6,020004);
		mov(0177406,0172300);
		mov(0,0172340);
		mov(0177406,0172320);
		mov(0,0172360);
		mov(protect?0177402:0,0172322);
		mov(0200,0172362);
		mov(0177406,0172336);
		mov(0177600,0172376);
		mov(4,0172516);
		mov(1,0177572);
		mov(3,0177776);
		probe(0);
		probe(2);
		probe(020000);
		probe(020002);
		emit(3);
		finish("red-stack-mmu");
	}
	/* CSR clearing and privileged HALT causes must survive RESET. */
	for(unsigned clear=0; clear<4; clear++) {
		begin();
		mov(0140003,0177776);
		emit(0);
		nins++;
		/* The trap vector leads here; continue the test in its handler. */
		pos=07000;
		if(clear==0) {
			emit(5);
			nins++;
		} else {
			emit(clear==1?012737:0112737);
			emit(0177777);
			emit(0177766+(clear==3));
			nins++;
		}
		emit(013700);
		emit(0177766);
		finish("cpuerr-clear-reset");
	}
	/* A user-mode vector with a nonresident user stack must take an ordinary
	 * MMU trap using kernel SP; it must not create the kernel emergency stack. */
	begin();
	mov(0140340,0177776);
	reg(6,020004);
	mov(0340,0177776);
	put(014,07200);
	put(016,0140340);
	mov(0177406,0172300);
	mov(0,0172340);
	mov(0177406,0172316);
	mov(0177600,0172356);
	mov(0,0177602);
	mov(1,0177572);
	mov(3,0177776);
	probe(0);
	probe(2);
	emit(3);
	finish("user-stack-abort-not-red");
	fprintf(stderr,"DCJ11 integer reference: %u programs\n",cases);
	return 0;
}
