/* CP70 fixtures for the unchanged DCJ11 emulator. Operand semantics and
 * normal operand results/frames come from core_step. Two documented oracle
 * defects use explicitly marked manual expectations below.
 * Output records: 129 hexadecimal words; see tb_fp_transfer_cp70.v. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/core.h"
#include "core/hardware.h"
static byte memory[65536];
static word initial[16][2], writes[8][2], reads[16];
static unsigned ni,nw,nr,count,rng=0x69f011a;
static int fail_kind,failed;
static word fail_address;
static word random_word(void){rng=rng*1664525u+1013904223u;return rng>>8;}
static word get(word a){return memory[a]|((word)memory[(word)(a+1)]<<8);}
static void put(word a,word v){
    if(a&1)return; /* Odd-address probes fault before using these bytes. */
    memory[a]=v;memory[(word)(a+1)]=v>>8;
    for(unsigned i=0;i<ni;i++)if(initial[i][0]==a){initial[i][1]=v;return;}
    if(ni==16)abort();initial[ni][0]=a;initial[ni++][1]=v;
}
static word load(regs *r,word a){
    /* The first read is the architectural opcode fetch. */
    if(nr==16)abort();reads[nr++]=a;
    if(nr>1 && fail_kind==1 && a==fail_address && !failed){failed=1;core_bus_error_trap(r);return 0;}
    return get(a);
}
static void store(regs *r,word a,word v){
    if(fail_kind==2 && a==fail_address && !failed){failed=1;core_bus_error_trap(r);return;}
    if(nw==8)abort();writes[nw][0]=a;writes[nw++][1]=v;
    memory[a]=v;memory[(word)(a+1)]=v>>8;
}
static void fixture(word opcode,unsigned seed,int kind,word fault_at){
    regs r;memset(&r,0,sizeof r);memset(memory,0,sizeof memory);
    ni=nw=nr=0;failed=0;fail_kind=kind;fail_address=fault_at;
    if(hwstub_set_memory(memory,sizeof memory))abort();r.model=DCJ11;
    hwstub_connect(&r);if(core_init(&r))abort();core_reset(&r);
    r.ram_fast=NULL;r.load_word=load;r.store_word=store;
    rng=0x69511a+seed;
    for(unsigned i=0;i<7;i++)r.r[i]=020000+(random_word()&037776);
    r.r[7]=01000;r.psw=0340|(seed&15);
    r.fpu_fps=random_word()&0xcfef;r.fpu_fec=2*(seed%7);r.fpu_fea=random_word();
    /* Make useful operand/pointer words. Aliases with the opcode/extension
     * remain real aliases; the emulator decides their final meaning. */
    unsigned spec=opcode&077,mode=spec>>3,reg=spec&7;
    if(kind==3 && reg<6)r.r[reg]|=1;
    int floating=(opcode&0177400)==0172400 || (opcode&0177400)==0174000;
    if(floating)r.fpu_fps=(r.fpu_fps&~(0200|04000|040000))|((seed&1)?0200:0)|((seed&2)?04000:0)|((seed&4)?040000:0);
    for(unsigned i=0;i<6;i++){r.fpu_fr[i].h=((unsigned)random_word()<<16)|random_word();r.fpu_fr[i].l=((unsigned)random_word()<<16)|random_word();}
    const word heads[]={0,0177,0100000,0100177,0200,077777,0140200,040200};
    word part[4]={heads[(seed>>3)&7],random_word(),random_word(),random_word()};
    if(floating && mode==0 && reg<6){r.fpu_fr[reg].h=((unsigned)part[0]<<16)|part[1];r.fpu_fr[reg].l=((unsigned)part[2]<<16)|part[3];}
    word start=reg==7?01002:r.r[reg],ea=0,step=(opcode&0177700)==0170300?4:2;
    if(floating)step=(r.fpu_fps&0200)?8:4;
    if(mode==2 && reg==7)step=2;
    word target=050000+4*(seed&127),value=random_word();
    if(kind==3 && mode>=6)target|=1;
    switch(mode){
    case 0:if((opcode&0177700)==0170100 && reg<7)r.r[reg]=value;break;
    case 1:case 2:ea=start;put(ea,value);put(ea+2,0xbeef);break;
    case 3:ea=target;put(start,target);put(ea,value);put(ea+2,0xbeef);break;
    case 4:ea=start-step;put(ea,value);put(ea+2,0xbeef);break;
    case 5:ea=target;put(start-2,target);put(ea,value);put(ea+2,0xbeef);break;
    case 6:case 7:
        ea=target;put(01002,(word)(target-(reg==7?01004:start)));
        if(mode==7){put(target,target+01000);ea+=01000;}
        put(ea,value);put(ea+2,0xbeef);break;
    }
    if(floating && mode!=0)for(unsigned i=0;i<step/2;i++)put(ea+2*i,part[i]);
    put(4,01400);put(6,0340);put(0244,01600);put(0246,0340);
    put(01000,opcode);
    word data[129]={0};data[0]=opcode;data[1]=r.fpu_fps;data[2]=r.psw;
    data[3]=r.fpu_fec;data[4]=r.fpu_fea;
    for(unsigned i=0;i<8;i++)data[5+i]=r.r[i];
    data[13]=kind==3?0:kind;data[14]=fault_at;data[15]=ni;
    for(unsigned i=0;i<ni;i++){data[16+2*i]=initial[i][0];data[17+2*i]=initial[i][1];}
    for(unsigned i=0;i<6;i++){
        data[80+4*i]=r.fpu_fr[i].h>>16;data[81+4*i]=r.fpu_fr[i].h;
        data[82+4*i]=r.fpu_fr[i].l>>16;data[83+4*i]=r.fpu_fr[i].l;
    }
    core_step(&r);
    if(floating && mode==0 && reg>=6){
        /* DEC EK-FP11A-UG-001 section 5.2 requires vector 244. The
         * unchanged oracle incorrectly cancels it via fAbort. These rows
         * are explicitly manual expectations, not differential evidence. */
        data[128]=1;
        if(r.fpu_fec!=2 || r.fpu_fea!=01000 || r.r[7]!=01002 || nw)abort();
        if(!(r.fpu_fps&040000)){
            r.r[6]-=2;store(&r,r.r[6],r.psw);
            r.r[6]-=2;store(&r,r.r[6],r.r[7]);
            r.r[7]=01600;r.psw=0340;
        }
    }
    if((opcode&0177400)==0172400 && spec==027 && kind==1 && failed){
        /* The oracle's immediate ReadFP path misses its fAbort check and
         * overwrites AC/FPS after the bus trap. Expected transfer semantics:
         * unsuccessful load leaves AC/FPS intact. Explicit manual rows. */
        data[128]=2;r.fpu_fps=data[1];
        for(unsigned i=0;i<6;i++){
            r.fpu_fr[i].h=((unsigned)data[80+4*i]<<16)|data[81+4*i];
            r.fpu_fr[i].l=((unsigned)data[82+4*i]<<16)|data[83+4*i];
        }
    }
    for(unsigned i=0;i<6;i++){
        data[104+4*i]=r.fpu_fr[i].h>>16;data[105+4*i]=r.fpu_fr[i].h;
        data[106+4*i]=r.fpu_fr[i].l>>16;data[107+4*i]=r.fpu_fr[i].l;
    }
    data[48]=r.fpu_fps;data[49]=r.psw;data[50]=r.fpu_fec;data[51]=r.fpu_fea;
    for(unsigned i=0;i<8;i++)data[52+i]=r.r[i];
    data[60]=nw;
    for(unsigned i=0;i<nw;i++){data[61+2*i]=writes[i][0];data[62+2*i]=writes[i][1];}
    data[77]=failed;data[78]=seed|(kind==3?0x8000:0);data[79]=count++;
    if((kind==1 || kind==2) && !failed){fprintf(stderr,"unused injection %06o %d %06o\n",opcode,kind,fault_at);exit(2);}
    for(unsigned i=0;i<129;i++)printf("%04x%c",data[i],i==128?'\n':' ');
    core_fini(&r);hwstub_clear_memory_binding();
}
static void faults(word op,unsigned seed){
    fixture(op,seed,0,0);
    word read_copy[16],write_copy[8];unsigned rn=nr,wn=nw;
    memcpy(read_copy,reads,sizeof reads);
    for(unsigned i=0;i<wn;i++)write_copy[i]=writes[i][0];
    if((op&077)!=057){
        for(unsigned i=1;i<rn;i++)if(read_copy[i]!=4 && read_copy[i]!=6 && read_copy[i]!=0244 && read_copy[i]!=0246)
            fixture(op,seed,1,read_copy[i]);
        for(unsigned i=0;i<wn;i++)fixture(op,seed,2,write_copy[i]);
    }
}
int main(void){
    for(unsigned group=1;group<=3;group++)for(unsigned spec=0;spec<64;spec++){
        word op=0170000+0100*group+spec;
        for(unsigned seed=0;seed<16;seed++)fixture(op,seed,0,0);
        if((spec>>3)!=0 && ((spec&7)<6 || ((spec&7)==7 && (spec>>3)>=6)))fixture(op,17,3,0);
        faults(op,16);
    }
    const word controls[]={0170000,0170001,0170002,0170011,0170012};
    for(unsigned i=0;i<5;i++)for(unsigned seed=0;seed<256;seed++)fixture(controls[i],seed,0,0);
    for(unsigned group=0;group<2;group++)for(unsigned ac=0;ac<4;ac++)for(unsigned spec=0;spec<64;spec++){
        word op=(group?0174000:0172400)+0100*ac+spec;
        int invalid=spec==6 || spec==7;
        for(unsigned seed=0;seed<(invalid?8:64);seed++)fixture(op,seed,0,0);
        if(invalid)continue;
        if((spec>>3)!=0 && ((spec&7)<6 || ((spec&7)==7 && (spec>>3)>=6)))
            for(unsigned seed=48;seed<50;seed++)fixture(op,seed,3,0);
        faults(op,48);faults(op,49);
    }
    fprintf(stderr,"CP70 DCJ11 oracle + marked DEC invalid-AC expectations: %u cases\n",count);
    return 0;
}
