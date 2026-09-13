/* Reused CP30 oracle for CP68 software execution.
 * Read-only use of the existing DCJ11 FP11 oracle; no host float is involved. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/core.h"
#include "core/hardware.h"
#if ENABLE_MMU
#error MMU must be disabled
#endif
static byte memory[65536],before[65536];
static unsigned rng=0x30f011a,count;
static word random_word(void){rng=rng*1664525u+1013904223u;return (word)(rng>>8);}
static void fixture(word op,word fps,word psw,word value){
    regs r;memset(&r,0,sizeof r);memset(memory,0,sizeof memory);
    if(hwstub_set_memory(memory,sizeof memory))exit(2);
    r.model=DCJ11;hwstub_connect(&r);if(core_init(&r))exit(2);core_reset(&r);
    r.fpu_fps=fps&0xcfef;r.psw=psw;
    for(unsigned i=0;i<7;i++)r.r[i]=random_word();
    if((op&077770)==070100 && (op&7)!=7)r.r[op&7]=value;
    r.r[7]=01000;
    memory[01000]=(byte)op;memory[01001]=(byte)(op>>8);memcpy(before,memory,sizeof memory);
    printf("%04x %04x %04x",op,(word)r.fpu_fps,psw);
    for(unsigned i=0;i<8;i++)printf(" %04x",r.r[i]);
    core_step(&r);
    printf(" %04x %04x",(word)r.fpu_fps,r.psw);
    for(unsigned i=0;i<8;i++)printf(" %04x",r.r[i]);
    printf("\n");
    if(r.fAbort||r.fTrap||memcmp(before,memory,sizeof memory)){fprintf(stderr,"Unexpected trap/write op %06o case %u\n",op,count);exit(3);}
    core_fini(&r);hwstub_clear_memory_binding();count++;
}
int main(void){
    /* Every possible LDFPS input, including all reserved-bit combinations. */
    for(unsigned i=0;i<65536;i++)fixture(0170100,(word)~i,0340|(i&15),(word)i);
    const word control[]={0170000,0170001,0170002,0170011,0170012};
    for(unsigned op=0;op<5;op++)for(unsigned bits=0;bits<4096;bits++)
        fixture(control[op],random_word(),((bits&7)<<5)|(bits&15),0);
    for(unsigned d=0;d<8;d++)for(unsigned i=0;i<256;i++){
        fixture(0170100|d,random_word(),i&15,random_word());
        fixture(0170200|d,random_word(),i&15,0);
    }
    fprintf(stderr,"DCJ11 FP control oracle: %u cases; no traps/memory writes\n",count);
}
