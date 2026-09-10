/* Existing emulator supplies the APR CSR/byte/W and translation oracle.
 * The RTL test selects entries through canonical physical CSR decode, then
 * reads PAR/PDR serially into testbench latches before checking translation.
 * No processor-mode/privilege/MMR or automatic W timing claim is made here.
 */
#include <stdio.h>
#include <string.h>
#include "../../core/core.c"

int main(void)
{
    regs r;
    unsigned i, state=0x72931b05u;
    const unsigned modes[3]={0,1,3};
    const word bases[3]={0172300,0172200,0177600};
    memset(&r,0,sizeof(r));
    r.model=DCJ11;
    r.mmu_ssr0=1;
    for(i=0;i<65536;i++) {
        unsigned bank=(i/16)%3, mode=modes[bank], space=(i/8)%2, page=i%8;
        unsigned is_pdr, writing=i&1, mark=(i>>1)&1, lanes=(i>>2)&3, map22=(i>>6)&1;
        word address, value, par, pdr, va;
        dword pa;
        int fault, status;
        state^=state<<13; state^=state>>17; state^=state<<5;
        is_pdr=(state>>8)&1; value=(word)(state>>16);
        address=(word)(bases[bank]+space*020+page*2+(is_pdr?0:040));
        r.psw=(word)(mode<<14);
        r.mmu_ssr3=(word)(07|(map22?020:0));
        if(writing && lanes) {
            if(lanes==3)status=mmu_io_write_word(&r,address,value);
            else status=mmu_io_write_byte(&r,(word)(address+(lanes==2)),
                                         (byte)(lanes==2 ? value>>8 : value));
            if(status!=1)return 1;
        } else if(mark)mmu_note_write_pdrw(&r,(int)mode,(int)space,(int)page);
        if(!mmu_io_read_word(&r,(word)(address|040),&par) ||
           !mmu_io_read_word(&r,(word)(address&~040),&pdr))return 1;
        va=(word)((page<<13)|(state&017777));
        status=translate_va_ex(&r,va,0,!space,0,&pa,&fault,NULL,NULL,NULL);
        if((status==0)!=(fault==0))return 1;
        if(status==0 && !map22 && pa>=0760000)pa|=017000000;
        printf("%06x %x %x %04x %04x %04x %04x %u %u %06x\n",
               (unsigned)address|017600000u,(writing<<1)|mark,lanes,
               (unsigned)value,(unsigned)par,(unsigned)pdr,(unsigned)va,
               map22,status!=0,(unsigned)pa);
    }
    return 0;
}
