/* Primary-core regression: pending IRQ waits for the first handler instruction
 * when a BUSERR vector restores IPL0; IPL7 defers it through handler RTI. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/core.h"
#include "core/hardware.h"
static byte memory[65536];static int kind,failed,polls;
static word peek(word a){return memory[a]|((word)memory[(word)(a+1)]<<8);}
static void poke(word a,word v){memory[a]=(byte)v;memory[(word)(a+1)]=(byte)(v>>8);}
static word load(regs*r,word a){if(kind==0 && a==0177562 && !failed){failed=1;core_bus_error_trap(r);return 0;}return peek(a);}
static void save(regs*r,word a,word v){if(kind==1 && a==0177562 && !failed){failed=1;core_bus_error_trap(r);return;}poke(a,v);}
static int poll(regs*r,word*v){(void)r;polls++;*v=(7<<9)|0100;return 1;}
static void require(int condition,const char*message){if(!condition){fprintf(stderr,"FAIL fault IRQ oracle: %s kind%d\n",message,kind);exit(1);}}
int main(void){
 for(unsigned low=0;low<2;low++)for(kind=0;kind<4;kind++){
  regs r;memset(&r,0,sizeof r);r.model=DCJ11;memset(memory,0,sizeof memory);
  require(!hwstub_set_memory(memory,sizeof memory),"memory binding");hwstub_connect(&r);require(!core_init(&r),"core init");core_reset(&r);
  word pc=kind==3?0x1001:0x1000;
  poke(pc,kind==1?0010110:0011001);poke(4,0x3000);poke(6,low?0:0340);
  poke(0x3000,0011203);poke(0x3002,2);poke(0100,0x4000);poke(0102,0340);poke(0x4000,1);poke(0x5000,0xbeef);
  r.r[0]=kind==2?0x2001:0177562;r.r[1]=0x1234;r.r[2]=0x5000;r.r[3]=0x5678;r.r[6]=0x6000;r.r[7]=pc;r.psw=3;
  r.ram_fast=NULL;r.load_word=load;r.store_word=save;r.poll_irq=poll;failed=polls=0;
  core_step(&r);
  require(r.r[7]==0x3000 && r.r[6]==0x5ffc && r.psw==(low?0:0340) && r.r[3]==0x5678 && polls==0,"IRQ before handler");
  require(peek(0x5ffe)==3 && peek(0x5ffc)==(kind==3?pc:(word)(pc+2)),"fault frame");
  if(r.fAbort)core_step(&r); // C abort bookkeeping, no architectural instruction.
  core_step(&r);require(r.r[3]==0xbeef,"handler instruction did not execute");
  if(!low){require(r.r[7]==0x3002 && r.r[6]==0x5ffc,"masked handler IRQ");core_step(&r);}
  require(r.r[7]==0x4000 && r.r[6]==(low?0x5ff8:0x5ffc) && r.psw==0340,"IRQ handler frame");
  require(peek(low?0x5ffa:0x5ffe)==(low?8:3) && peek(low?0x5ff8:0x5ffc)==(low?0x3002:kind==3?pc:(word)(pc+2)),"IRQ frame state");
  core_fini(&r);hwstub_clear_memory_binding();
 }
 puts("PASS fault IRQ oracle: 8 DCJ11 bus/odd faults at IPL0/IPL7; handler instruction precedes IRQ");return 0;
}
