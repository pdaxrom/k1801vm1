/* Use the existing C MMU without changing it. Including the translation unit
 * exposes its static translate_va_ex helper solely to this test executable.
 * Compare success/fault and successful PA; DEC tests separately check all
 * simultaneous abort bits, which this emulator reports as a single cause.
 */
#include <stdio.h>
#include <string.h>
#include "../../core/core.c"

int main(void)
{
    regs r;
    unsigned i, state=0x75d83219u;
    memset(&r,0,sizeof(r));
    r.model=DCJ11;
    for(i=0;i<262144;i++) {
        const unsigned enabled=(i>>16)&1u, map22=(i>>17)&1u;
        unsigned mode, space, page, writing, fetch, control;
        word va, par, pdr;
        dword pa;
        int fault, status;
        state^=state<<13; state^=state>>17; state^=state<<5;
        va=(word)state; par=(word)i; pdr=(word)(state>>16);
        mode=(state>>18)&3u; writing=(state>>20)&1u; fetch=(state>>21)&1u;
        /* Enable split I/D for each valid mode to exercise selected APRs. */
        r.psw=(word)(mode<<14);
        r.mmu_ssr0=(word)enabled; r.mmu_ssr3=(word)((map22?020:0)|07);
        page=(va>>13)&7; space=(!fetch && mode!=2) ? 1u : 0u;
        r.mmu_par[mode][space][page]=par;
        r.mmu_pdr[mode][space][page]=pdr;
        status=translate_va_ex(&r,va,writing,fetch,0,&pa,&fault,NULL,NULL,NULL);
        if((status==0)!=(fault==0))return 1;
        /* Core exposes local PA16/18 to its bus callbacks. Normalize only the
         * architectural I/O page to the canonical PA22 used by the RTL. */
        if(status==0) {
            if(!enabled && pa>=0160000)pa|=017600000;
            else if(enabled && !map22 && pa>=0760000)pa|=017000000;
        }
        control=(enabled<<3)|(map22<<2)|(writing<<1)|(mode==2);
        printf("%x %04x %04x %04x %u %06x\n",control,(unsigned)va,
               (unsigned)par,(unsigned)pdr,status!=0,(unsigned)pa);
    }
    return 0;
}
