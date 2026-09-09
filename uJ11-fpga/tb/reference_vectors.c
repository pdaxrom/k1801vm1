/* Differential oracle: uses the existing DCJ11 core, not duplicated formulas.
 * stdout: IR, pre-PSW, 8 pre-registers, post-PSW, 8 post-registers, abort, trap.
 * RR/BR must not alter memory. Built separately with ENABLE_MMU=0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/core.h"
#include "core/hardware.h"
#if ENABLE_MMU
#error "uJ11 v1 oracle must be built without memory management"
#endif
static byte memory[65536], before[65536];
static unsigned rng=0x1801u;
static unsigned count;
static word random_word(void) { rng=rng*1664525u+1013904223u; return (word)(rng>>8); }
static void fixture(word opcode,word psw,word values[8])
{
    regs r;
    memset(&r,0,sizeof r);
    memset(memory,0,sizeof memory);
    if(hwstub_set_memory(memory,sizeof memory)) exit(2);
    r.model=DCJ11;
    hwstub_connect(&r);
    if(core_init(&r)) exit(2);
    core_reset(&r);
    memcpy(r.r,values,sizeof r.r);
    r.psw=psw;
    memory[values[7]]=(byte)opcode;
    memory[(word)(values[7]+1)]=(byte)(opcode>>8);
    memcpy(before,memory,sizeof memory);
    printf("%04x %04x",opcode,psw);
    for(int k=0;k<8;k++) printf(" %04x",values[k]);
    (void)core_step(&r);
    printf(" %04x",r.psw);
    for(int k=0;k<8;k++) printf(" %04x",r.r[k]);
    printf(" %04x %04x\n",!!r.fAbort,!!r.fTrap);
    if(memcmp(before,memory,sizeof memory) || r.fAbort || r.fTrap) {
        fprintf(stderr,"Unexpected oracle trap/memory change case %u opcode %06o PC %06o\n",count,opcode,values[7]);
        exit(3);
    }
    count++;
    core_fini(&r);
    hwstub_clear_memory_binding();
}
int main(void)
{
    const word edges[]={0,1,2,0x7ffe,0x7fff,0x8000,0x8001,0xfffe,0xffff};
    const word ops[]={0010000,0020000,0060000};
    word r[8];
    for(unsigned op=0;op<3;op++) for(unsigned s=0;s<8;s++) for(unsigned d=0;d<8;d++)
        for(unsigned e=0;e<9;e++) for(unsigned c=0;c<2;c++) {
            for(int k=0;k<8;k++) r[k]=random_word();
            if(s!=7) r[s]=edges[e];
            if(d!=7) r[d]=edges[(e+3)%9];
            r[7]=01000;
            fixture((word)(ops[op] | (s<<6) | d),(word)((e*2 & 14)|c),r);
        }
    for(unsigned i=0;i<2048;i++) {
        for(int k=0;k<8;k++) r[k]=random_word();
        r[7]=01000;
        unsigned s=random_word()&7, d=random_word()&7;
        fixture((word)(ops[i%3] | (s<<6) | d),random_word()&15,r);
    }
    for(unsigned base=0;base<3;base++) for(unsigned offset=0;offset<256;offset++) {
        for(int k=0;k<8;k++) r[k]=random_word();
        r[7]=base==0 ? 0 : base==1 ? 01000 : 0157776;
        fixture((word)(0000400 | offset),random_word()&15,r);
    }
    // Append CP8 coverage, preserving the exact original 6272 cases above.
    const word extra[]={0030000,0040000,0050000,0160000};
    for(unsigned op=0;op<4;op++) for(unsigned s=0;s<8;s++) for(unsigned d=0;d<8;d++)
        for(unsigned e=0;e<9;e++) for(unsigned c=0;c<2;c++) {
            for(int k=0;k<8;k++) r[k]=random_word();
            if(s!=7) r[s]=edges[e];
            if(d!=7) r[d]=edges[(e+3)%9];
            r[7]=01000;
            fixture((word)(extra[op] | (s<<6) | d),(word)((e*2 & 14)|c),r);
        }
    for(unsigned i=0;i<2048;i++) {
        for(int k=0;k<8;k++) r[k]=random_word();
        r[7]=01000;
        unsigned s=random_word()&7,d=random_word()&7;
        fixture((word)(extra[i%4] | (s<<6) | d),random_word()&15,r);
    }
    fprintf(stderr,"DCJ11 ENABLE_MMU=0: %u single-instruction reference vectors; no memory changes/traps\n",count);
    return 0;
}
