/* CP74 fixtures for the private DCJ11/FP11-A reference. Operand semantics and
 * normal operand results/frames come from core_step. Documented oracle
 * defects use explicitly marked manual expectations below.
 * Output records: 129 hexadecimal words; see tb_fp_unary_cp71.v. */
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
static unsigned trap_writes;
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
    int family=opcode&0177400,unary=family==0170400;
    int floating=unary || family==0172400 || family==0173400 || family==0174000 || family==0172000 || family==0173000 || family==0171000 || family==0174400;
    if(floating)r.fpu_fps=(r.fpu_fps&~(0200|04000|040000))|((seed&1)?0200:0)|((seed&2)?04000:0)|((seed&4)?040000:0);
    for(unsigned i=0;i<6;i++){r.fpu_fr[i].h=((unsigned)random_word()<<16)|random_word();r.fpu_fr[i].l=((unsigned)random_word()<<16)|random_word();}
    const word heads[]={0,0177,0100000,0100177,0200,077777,0140200,040200};
    word part[4]={heads[(seed>>3)&7],random_word(),random_word(),random_word()};
    if(floating && mode==0 && reg<6){r.fpu_fr[reg].h=((unsigned)part[0]<<16)|part[1];r.fpu_fr[reg].l=((unsigned)part[2]<<16)|part[3];}
    if(family==0172000 || family==0173000){
        /* Exercise every rounding/individual-exception/FID combination. */
        r.fpu_fps=(r.fpu_fps&~(040|01000|02000))|((seed&8)?040:0)|((seed&16)?01000:0)|((seed&32)?02000:0);
        if(seed>>9){
            const uint64_t pairs[][2]={
                {0x4080000000000000ULL,0x4080000000000000ULL}, /* 1 +/- 1 */
                {0x4080000000000000ULL,0xc080000000000000ULL},
                {0x4080000000000001ULL,0xc080000000000000ULL}, /* long cancellation */
                {0x4080000000000000ULL,0xc07fffffffffffffULL},
                {0x4080000000000000ULL,0x3500000000000000ULL}, /* F half ULP */
                {0x4080000000000000ULL,0x34ffffffffffffffULL},
                {0x4080000000000000ULL,0x3500000000000001ULL},
                {0x4080000000000000ULL,0x2500000000000000ULL}, /* D half ULP */
                {0x4080000000000000ULL,0x24ffffffffffffffULL},
                {0x4080000000000000ULL,0x2500000000000001ULL},
                {0x7fffffffffffffffULL,0x7fffffffffffffffULL}, /* exponent overflow */
                {0xffffffffffffffffULL,0xffffffffffffffffULL},
                {0x0080000000000001ULL,0x8080000000000000ULL}, /* exponent underflow */
                {0x0100000000000000ULL,0x80ffffffffffffffULL},
                {0x0100000000000000ULL,0x8080000000000000ULL},
                {0x0080000000000000ULL,0x8080000000000000ULL},
                {0x7fffffffffffffffULL,0x7480000000000000ULL}, /* F rounding overflow */
                {0x7fffffffffffffffULL,0x6480000000000000ULL}, /* D rounding overflow */
                {0x00ffffffffffffffULL,0x8000000000000000ULL},
                {0x8000000000000000ULL,0x00ffffffffffffffULL},
                {0x4000000000000001ULL,0xb47fffffffffffffULL},
                {0x4000000000000001ULL,0xa47fffffffffffffULL},
                {0x4000000000000001ULL,0xa27fffffffffffffULL},
                {0x4000000000000001ULL,0xa07fffffffffffffULL},
                {0x4000000000000000ULL,0x8080000000000000ULL},
                {0x407fffffffffffffULL,0x3780000000000000ULL},
                {0x407fffffffffffffULL,0x407fffffffffffffULL},
                {0x407fffffffffffffULL,0xbfffffffffffffffULL},
                {0x6fd05fd921c28504ULL,0xd0eafd9adde9a1f2ULL}, /* three vs seven guards */
                {0x7f3165870e6da564ULL,0xe5ccf2cfefee55d0ULL},
                {0xce2b4b0b5d6c84f4ULL,0x3a2cd7962aaf854fULL},
                {0x00c0000000000000ULL,0x8080000000000000ULL}, /* underflow packs as zero */
                {0x80c0000000000000ULL,0x0080000000000000ULL},
            };
            unsigned pair=(seed>>9)-1,ac=(opcode>>6)&3;
            if(pair>=sizeof(pairs)/sizeof(pairs[0]))abort();
            uint64_t a=pairs[pair][0],b=pairs[pair][1];
            r.fpu_fr[ac].h=a>>32;r.fpu_fr[ac].l=a;
            for(unsigned j=0;j<4;j++)part[j]=b>>(48-16*j);
            if(mode==0 && reg<6){r.fpu_fr[reg].h=b>>32;r.fpu_fr[reg].l=b;}
        }
    }
    if(family==0171000 || family==0174400){
        r.fpu_fps=(r.fpu_fps&~(040|01000|02000))|((seed&8)?040:0)|((seed&16)?01000:0)|((seed&32)?02000:0);
        if(seed>>9){
            const uint64_t pairs[][2]={
                {0x4080000000000000ULL,0x4080000000000000ULL},
                {0x4080000000000000ULL,0xc080000000000000ULL},
                {0xc080000000000000ULL,0xc080000000000000ULL},
                {0x4080000000000000ULL,0x4140000000000000ULL}, /* 1 and 3 */
                {0x4140000000000000ULL,0x4080000000000000ULL},
                {0x4080000000000000ULL,0x4000000000000000ULL}, /* ratio >= 1 */
                {0x4000000000000000ULL,0x4080000000000000ULL},
                {0x4080000000000000ULL,0}, /* divisor zero */
                {0,0x4080000000000000ULL},
                {0,0},
                {0x4080000000000000ULL,0x807fffffffffffffULL}, /* dirty negative zero */
                {0x807fffffffffffffULL,0x4080000000000000ULL},
                {0x7fffffffffffffffULL,0x4100000000000000ULL}, /* overflow/wrap */
                {0xffffffffffffffffULL,0x4100000000000000ULL},
                {0x7fffffffffffffffULL,0x0080000000000000ULL},
                {0xffffffffffffffffULL,0x0080000000000000ULL},
                {0x0080000000000000ULL,0x4000000000000000ULL}, /* underflow/wrap zero */
                {0x8080000000000000ULL,0x4000000000000000ULL},
                {0x0080000000000000ULL,0x4100000000000000ULL},
                {0x8080000000000000ULL,0x4100000000000000ULL},
                {0x0080000000000000ULL,0x7fffffffffffffffULL},
                {0x0080000000000000ULL,0x0080000000000000ULL},
                {0x7fffffffffffffffULL,0x7fffffffffffffffULL},
                {0x40ffffffffffffffULL,0x40ffffffffffffffULL},
                {0x4080000000000001ULL,0x40fffffffffffffeULL}, /* guard/tie boundaries */
                {0x4080000000000001ULL,0x40c0000000000000ULL},
                {0x4080000100000000ULL,0x40c0000000000000ULL},
                {0x4080000100000000ULL,0x40ffffff00000000ULL},
                {0x4080000000000000ULL,0x40ffffffffffffffULL},
                {0x4080000000000000ULL,0x40ffffff00000000ULL},
                {0x407fffffffffffffULL,0x407fffffffffffffULL},
                {0x407fffff00000000ULL,0x407fffff00000000ULL},
            };
            unsigned pair=(seed>>9)-1,ac=(opcode>>6)&3;
            if(pair>=sizeof(pairs)/sizeof(pairs[0]))abort();
            uint64_t a=pairs[pair][0],b=pairs[pair][1];
            r.fpu_fr[ac].h=a>>32;r.fpu_fr[ac].l=a;
            for(unsigned j=0;j<4;j++)part[j]=b>>(48-16*j);
            if(mode==0 && reg<6){r.fpu_fr[reg].h=b>>32;r.fpu_fr[reg].l=b;}
        }
    }
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
    uint32_t data[129]={0};data[0]=opcode;data[1]=r.fpu_fps;data[2]=r.psw;
    data[3]=r.fpu_fec;data[4]=r.fpu_fea;
    for(unsigned i=0;i<8;i++)data[5+i]=r.r[i];
    data[13]=kind==3?0:kind;data[14]=fault_at;data[15]=ni;
    for(unsigned i=0;i<ni;i++){data[16+2*i]=initial[i][0];data[17+2*i]=initial[i][1];}
    for(unsigned i=0;i<6;i++){
        data[80+4*i]=r.fpu_fr[i].h>>16;data[81+4*i]=r.fpu_fr[i].h&65535;
        data[82+4*i]=r.fpu_fr[i].l>>16;data[83+4*i]=r.fpu_fr[i].l&65535;
    }
    if(family==0173400){
        /* Deliberate equal, low-word-only, opposite-sign and dirty-zero
         * pairs complement random pairs; retain real AC self-aliases. */
        unsigned ac=(opcode>>6)&3;
        if(mode!=0 || reg!=ac){
            switch((seed>>6)&7){
            case 1:r.fpu_fr[ac].h=((unsigned)part[0]<<16)|part[1];r.fpu_fr[ac].l=((unsigned)part[2]<<16)|part[3];break;
            case 2:r.fpu_fr[ac].h=((unsigned)part[0]<<16)|part[1];r.fpu_fr[ac].l=(((unsigned)part[2]<<16)|part[3])^1;break;
            case 3:r.fpu_fr[ac].h=(((unsigned)part[0]<<16)|part[1])^0x80000000u;break;
            case 4:r.fpu_fr[ac].h=0x807fffffu;r.fpu_fr[ac].l=0xffffffffu;break;
            case 5:r.fpu_fr[ac].h=((unsigned)part[0]<<16)|(part[1]^1);r.fpu_fr[ac].l=((unsigned)part[2]<<16)|part[3];break;
            }
        }
        for(unsigned i=0;i<6;i++){
            data[80+4*i]=r.fpu_fr[i].h>>16;data[81+4*i]=r.fpu_fr[i].h&65535;
            data[82+4*i]=r.fpu_fr[i].l>>16;data[83+4*i]=r.fpu_fr[i].l&65535;
        }
    }
    core_step(&r);
    if(floating && mode==0 && reg>=6){
        /* DEC EK-FP11A-UG-001 section 5.2 requires vector 244. The
         * unchanged oracle incorrectly cancels it via fAbort. These rows
         * are explicitly manual expectations, not differential evidence. */
        data[128]=1;
        r.fpu_fps=(r.fpu_fps&~15)|(data[1]&15);
        if(r.fpu_fec!=2 || r.fpu_fea!=01000 || r.r[7]!=01002 || nw)abort();
        if(!(r.fpu_fps&040000)){
            r.r[6]-=2;store(&r,r.r[6],r.psw);
            r.r[6]-=2;store(&r,r.r[6],r.r[7]);
            r.r[7]=01600;r.psw=0340;
        }
    }
    if(floating && !((opcode&0177700)==0170400) && family!=0174000 && spec==027 && kind==1 && failed){
        /* The oracle's immediate ReadFP path misses its fAbort check and
         * overwrites AC/FPS after the bus trap. Expected unary semantics:
         * unsuccessful load leaves all FP state intact. DIV also overwrites
         * FEC/FEA with divide-zero after the failed read. Explicit manual rows. */
        data[128]=2;r.fpu_fps=data[1];r.fpu_fec=data[3];r.fpu_fea=data[4];
        for(unsigned i=0;i<6;i++){
            r.fpu_fr[i].h=((unsigned)data[80+4*i]<<16)|data[81+4*i];
            r.fpu_fr[i].l=((unsigned)data[82+4*i]<<16)|data[83+4*i];
        }
    }
    if(!data[128] && unary && r.fAbort && ((opcode&0177700)==0170400 || (kind==2 && failed))){
        /* Our handler commits FPS only after successful operand accesses.
         * CLR and ABS/NEG write-abort paths in the oracle update it anyway. */
        data[128]=3;r.fpu_fps=data[1];
    }
    if((opcode&0177700)==0170500 && mode && !r.fAbort && (data[1]&04000) && r.fpu_fec==014 && r.fpu_fea==01000){
        /* FP11-A section 5.3.8: TST completes flags before UV exception.
         * DCJ11 (and this oracle) stops before updating FPS condition codes. */
        data[128]=4;r.fpu_fps=(r.fpu_fps&~15)|014;
    }
    for(unsigned i=0;i<6;i++){
        data[104+4*i]=r.fpu_fr[i].h>>16;data[105+4*i]=r.fpu_fr[i].h&65535;
        data[106+4*i]=r.fpu_fr[i].l>>16;data[107+4*i]=r.fpu_fr[i].l&65535;
    }
    data[48]=r.fpu_fps;data[49]=r.psw;data[50]=r.fpu_fec;data[51]=r.fpu_fea;
    for(unsigned i=0;i<8;i++)data[52+i]=r.r[i];
    trap_writes=(r.r[7]==01600 || r.r[7]==01400)?2:0;
    if(trap_writes>nw)abort();
    data[60]=nw;
    for(unsigned i=0;i<nw;i++){data[61+2*i]=writes[i][0];data[62+2*i]=writes[i][1];}
    data[77]=failed;data[78]=seed|(kind==3?0x8000:0);data[79]=count++;
    if((kind==1 || kind==2) && !failed){fprintf(stderr,"unused injection %06o %d %06o\n",opcode,kind,fault_at);exit(2);}
    for(unsigned i=0;i<129;i++)printf("%04x%c",data[i],i==128?'\n':' ');
    core_fini(&r);hwstub_clear_memory_binding();
}
static void faults(word op,unsigned seed){
    fixture(op,seed,0,0);
    word read_copy[16],write_copy[8];unsigned rn=nr,wn=nw-trap_writes;
    memcpy(read_copy,reads,sizeof reads);
    for(unsigned i=0;i<wn;i++)write_copy[i]=writes[i][0];
    if((op&077)!=057){
        for(unsigned i=1;i<rn;i++)if(read_copy[i]!=4 && read_copy[i]!=6 && read_copy[i]!=0244 && read_copy[i]!=0246)
            fixture(op,seed,1,read_copy[i]);
        /* Nested trap-frame bus failures are terminal in uJ11; the event
         * harness checks those separately. Inject into operand writes here. */
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
    for(unsigned group=0;group<7;group++)for(unsigned ac=0;ac<4;ac++)for(unsigned spec=0;spec<64;spec++){
        word op=(group==0?0172400:group==1?0174000:group==2?0173400:group==3?0172000:group==4?0173000:group==5?0171000:0174400)+0100*ac+spec;
        int invalid=spec==6 || spec==7;
        for(unsigned seed=0;seed<(invalid?8:64);seed++)fixture(op,seed,0,0);
        if(invalid)continue;
        if((spec>>3)!=0 && ((spec&7)<6 || ((spec&7)==7 && (spec>>3)>=6)))
            for(unsigned seed=48;seed<50;seed++)fixture(op,seed,3,0);
        faults(op,48);faults(op,49);
    }
    for(unsigned base=0170400;base<=0170700;base+=0100)for(unsigned spec=0;spec<64;spec++){
        word op=base+spec;int invalid=spec==6 || spec==7;
        for(unsigned seed=0;seed<(invalid?8:64);seed++)fixture(op,seed,0,0);
        if(invalid)continue;
        if((spec>>3)!=0 && ((spec&7)<6 || ((spec&7)==7 && (spec>>3)>=6)))
            for(unsigned seed=48;seed<50;seed++)fixture(op,seed,3,0);
        faults(op,48);faults(op,49);
    }
    /* CMP exact/equality/LSW/sign/zero pairs in each memory mode and AC. */
    for(unsigned ac=0;ac<4;ac++)for(unsigned spec=0;spec<64;spec++)if(spec!=6 && spec!=7)
        for(unsigned pair=1;pair<=5;pair++)for(unsigned value=0;value<8;value++)for(unsigned d=0;d<2;d++)
            fixture(0173400+0100*ac+spec,64*pair+8*value+d,0,0);
    /* Explicit arithmetic boundaries in AC and autoincrement memory. */
    for(unsigned group=0;group<2;group++)for(unsigned mem=0;mem<2;mem++)
        for(unsigned pair=1;pair<=33;pair++)for(unsigned flags=0;flags<64;flags++)
            fixture((group?0173000:0172000)+0200+(mem?022:05),512*pair+flags,0,0);
    for(unsigned group=0;group<2;group++)for(unsigned mem=0;mem<2;mem++)
        for(unsigned pair=1;pair<=32;pair++)for(unsigned flags=0;flags<64;flags++)
            fixture((group?0174400:0171000)+0200+(mem?022:05),512*pair+flags,0,0);
    /* UV unary completion includes writes, followed by the 244 frame. */
    for(unsigned base=0170600;base<=0170700;base+=0100)for(unsigned spec=010;spec<0100;spec++)
        for(unsigned seed=18;seed<24;seed++)faults(base+spec,seed);
    fprintf(stderr,"CP74 DCJ11 oracle + marked DEC invalid-AC expectations: %u cases\n",count);
    return 0;
}
