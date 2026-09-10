/* Existing DCJ11 translation helper, valid kernel I-space PDRs only.
 * CP44 compares addresses; it does not implement the emulator's PDR checks.
 */
#include <stdio.h>
#include <string.h>
#include "../../core/core.c"
int main(void)
{
    regs r;
    unsigned i, state=0x4ee29a31u;
    memset(&r,0,sizeof(r));r.model=DCJ11;
    for(i=0;i<262144;i++) {
        unsigned enabled=(i>>16)&1u,map22=(i>>17)&1u;
        word va,par=(word)i;
        dword pa;
        int fault;
        state^=state<<13;state^=state>>17;state^=state<<5;
        va=(word)state;
        r.mmu_ssr0=(word)enabled;r.mmu_ssr3=(word)(map22?020:0);
        r.mmu_par[0][0][va>>13]=par;r.mmu_pdr[0][0][va>>13]=077406;
        if(translate_va_ex(&r,va,0,0,0,&pa,&fault,NULL,NULL,NULL) || fault)return 1;
        if(!enabled && pa>=0160000)pa|=017600000;
        else if(enabled && !map22 && pa>=0760000)pa|=017000000;
        printf("%x %04x %04x %06x\n",enabled|(map22<<1),(unsigned)va,(unsigned)par,(unsigned)pa);
    }
    return 0;
}
