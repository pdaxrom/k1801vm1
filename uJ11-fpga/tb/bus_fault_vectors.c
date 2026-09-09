/* Compare actual DCJ11 state at the completed BUSERR vector frame.
 * ACK errors are injected by bus callbacks; odd words use the core's own guard.
 * No instruction or effective-address semantics are copied here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/core.h"
#include "core/hardware.h"
#if ENABLE_MMU
#error "uJ11 v1 has no MMU"
#endif
static byte memory[65536];
static FILE *excluded_file,*continuation_file;
static struct {word wr,a,v;} trace[64];
static unsigned nt,done_nt,done_count,vec_count,vec_seen,attempted_io;
static int fail_at,failed;
static word done_regs[8],done_psw;
static unsigned count,skipped,post_abort_changes,post_abort_accesses,odd_cases,ack_cases;
static word base_word(word a){return 0x8000u|((a^(a>>3)^0x3456u)&0x1ffeu);}
static void poke(word a,word v){memory[a]=(byte)v;memory[(word)(a+1)]=(byte)(v>>8);}
static word peek(word a){return memory[a]|((word)memory[(word)(a+1)]<<8);}
void uj11_oracle_access(unsigned a){if(a>=0160000)attempted_io=1;}
void uj11_oracle_vector(unsigned v){vec_count++;vec_seen=v;}
void uj11_oracle_vector_done(regs *r,unsigned v){
    if(v!=4)return;
    done_count++;done_nt=nt;done_psw=r->psw;memcpy(done_regs,r->r,sizeof done_regs);
}
static int bus(regs *r,word wr,word a,word v){
    if(nt>=64){fprintf(stderr,"fault oracle trace overflow\n");exit(2);}
    int fault=!failed && (int)nt==fail_at;
    trace[nt].wr=wr|(fault?4:0);trace[nt].a=a;trace[nt++].v=fault && !(wr&1)?0:v;
    if(fault){failed=1;core_bus_error_trap(r);return 1;}return 0;
}
static word load(regs*r,word a){word v=peek(a);return bus(r,0,a,v)?0:v;}
static void save(regs*r,word a,word v){if(!bus(r,1,a,v))poke(a,v);}
static byte loadb(regs*r,word a){byte v=memory[a];return bus(r,2,a,v)?0:v;}
static void saveb(regs*r,word a,byte v){if(!bus(r,3,a,v))memory[a]=v;}
static void run(regs*r,word op,word flags,word initial[8],int fail){
    memset(r,0,sizeof *r);r->model=DCJ11;
    if(hwstub_set_memory(memory,sizeof memory))exit(2);hwstub_connect(r);
    if(core_init(r))exit(2);core_reset(r);
    for(unsigned a=0;a<65536;a+=2)poke((word)a,base_word((word)a));
    poke(4,0x3000);poke(6,0357);poke((word)(initial[7]+2),0x20);poke(initial[7],op);
    if(op==2){poke(initial[6],0x3200);poke((word)(initial[6]+2),0357);}
    memcpy(r->r,initial,sizeof r->r);r->psw=flags;
    r->ram_fast=NULL;r->load_word=load;r->store_word=save;r->load_byte=loadb;r->store_byte=saveb;
    nt=done_nt=done_count=vec_count=vec_seen=attempted_io=0;fail_at=fail;failed=0;
    (void)core_step(r);
}
static void finish(regs*r){core_fini(r);hwstub_clear_memory_binding();}
static void exclude(word op,word flags,int fail,const char *reason){
    fprintf(excluded_file,"%06o,%04x,%d,%s,%u,%u,%u,%u\n",op,flags,fail,reason,vec_count,vec_seen,done_count,attempted_io);skipped++;
}
static void emit(word op,word flags,word initial[8],int fail){
    regs r;run(&r,op,flags,initial,fail);
    if(done_count!=1 || vec_count!=1 || vec_seen!=4 || attempted_io ||
       (fail>=0 && !failed) || done_nt<4){exclude(op,flags,fail,"frame_or_io");finish(&r);return;}
    for(unsigned t=0;t<done_nt;t++)if(trace[t].a>=0160000 || ((trace[t].a&1)&&!(trace[t].wr&2))){exclude(op,flags,fail,"frame_or_io");finish(&r);return;}
    unsigned changed=r.psw!=done_psw || memcmp(r.r,done_regs,sizeof done_regs)!=0;
    post_abort_changes+=changed;post_abort_accesses+=nt!=done_nt;
    if(changed || nt!=done_nt){
        unsigned mask=0;for(unsigned k=0;k<8;k++)if(r.r[k]!=done_regs[k])mask|=1u<<k;
        fprintf(continuation_file,"%u,%06o,%04x,%04x,%04x,%04x,%04x,%04x,%02x,%u,%u\n",count,op,done_psw,r.psw,done_regs[7],r.r[7],done_regs[6],r.r[6],mask,done_nt,nt);
    }
    if(fail<0)odd_cases++;else ack_cases++;
    printf("%x %04x %04x",count++,op,flags);
    for(unsigned k=0;k<8;k++)printf(" %04x",initial[k]);
    printf(" %04x %x\n",fail<0?0xffffu:(unsigned)fail,op==2?6:4);
    printf("0004 3000\n0006 00ef\n%04x 0020\n%04x %04x\n",(word)(initial[7]+2),initial[7],op);
    if(op==2)printf("%04x 3200\n%04x 00ef\n",initial[6],(word)(initial[6]+2));
    printf("%04x",done_psw);for(unsigned k=0;k<8;k++)printf(" %04x",done_regs[k]);
    printf(" %x\n",done_nt);
    for(unsigned t=0;t<done_nt;t++)printf("%x %04x %04x\n",trace[t].wr,trace[t].a,trace[t].v);
    finish(&r);
}
static void candidate(word op,word flags,word initial[8]){
    regs r;run(&r,op,flags,initial,-1);
    unsigned normal_nt=nt;int clean=!r.fAbort&&!vec_count&&!attempted_io;
    finish(&r);
    if(clean){for(unsigned beat=1;beat<normal_nt;beat++)emit(op,flags,initial,(int)beat);}
    else if(done_count && vec_seen==4)emit(op,flags,initial,-1);
    else exclude(op,flags,-1,"normal_candidate_outside_profile");
}
int main(void){
    excluded_file=fopen("build/bus-fault-excluded.csv","w");
    continuation_file=fopen("build/bus-fault-continuation.csv","w");
    if(!excluded_file || !continuation_file)return 2;
    fprintf(excluded_file,"opcode,psw,fail_index,reason,vector_count,vector,completed_frames,io\n");
    fprintf(continuation_file,"case,opcode,frame_psw,step_psw,frame_pc,step_pc,frame_sp,step_sp,changed_register_mask,frame_beats,step_beats\n");
    word r[8];
    const word doubles[]={0010000,0020000,0030000,0040000,0050000,0060000,0160000,
                           0110000,0120000,0130000,0140000,0150000};
    const word unary[]={0000300,0005000,0005100,0005200,0005300,0005400,0005500,
                         0005600,0005700,0006000,0006100,0006200,0006300,0006700,
                         0105000,0105100,0105200,0105300,0105400,0105500,0105600,
                         0105700,0106000,0106100,0106200,0106300};
    for(unsigned state=0;state<4;state++){
        word flags=(word)(state==0?0:state==1?3:state==2?010:0357);
        for(unsigned k=0;k<8;k++)r[k]=(word)(0x2000+k*0x200);r[6]=0x6000;r[7]=0x1000;
        for(unsigned op=0;op<sizeof doubles/sizeof *doubles;op++)
        for(unsigned side=0;side<2;side++)for(unsigned mode=1;mode<8;mode++)for(unsigned reg=0;reg<8;reg++){
            word code=doubles[op]|(side?0:(word)((mode*8+reg)<<6))|(side?(word)(mode*8+reg):1);
            candidate(code,flags,r);
            if(reg<6 && mode<6){r[reg]++;candidate(code,flags,r);r[reg]--;}
        }
        for(unsigned op=0;op<sizeof unary/sizeof *unary;op++)
        for(unsigned mode=1;mode<8;mode++)for(unsigned reg=0;reg<8;reg++){
            candidate(unary[op]|(word)(mode*8+reg),flags,r);
            if(reg<6 && mode<6){r[reg]++;candidate(unary[op]|(word)(mode*8+reg),flags,r);r[reg]--;}
        }
        for(unsigned mode=1;mode<8;mode++)for(unsigned reg=0;reg<8;reg++){
            candidate(0000100|(word)(mode*8+reg),flags,r);
            candidate(0004000|(5<<6)|(word)(mode*8+reg),flags,r);
        }
        for(unsigned reg=0;reg<8;reg++)candidate(0000200|(word)reg,flags,r);
        for(unsigned n=0;n<64;n++)candidate(0006400|(word)n,flags,r);
    }
    // Explicit FETCH failures clear fAbort on return in core_step. Completion
    // is established by the audited BUSERR frame, not that transient flag.
    // RTI uses valid T=0 stack data so normal probing cannot become a trace.
    for(unsigned state=0;state<4;state++){
        word flags=(word)(state==0?0:state==1?3:state==2?010:0357);
        for(unsigned k=0;k<8;k++)r[k]=(word)(0x2000+k*0x200);r[6]=0x6000;r[7]=0x1000;
        emit(0010001,flags,r,0);r[7]++;emit(0010001,flags,r,-1);r[7]--;
        emit(0000002,flags,r,1);emit(0000002,flags,r,2);
    }
    fprintf(stderr,"Fault-frame oracle: %u completed frames (%u ACK error, %u odd-word), %u excluded candidates.\n",count,ack_cases,odd_cases,skipped);
    fprintf(stderr,"Original emulator after the captured frame: %u changed register/PSW states, %u additional bus traces; preserved as observed C continuation, not compared as frame entry.\n",post_abort_changes,post_abort_accesses);
    fclose(excluded_file);fclose(continuation_file);
    return count?0:1;
}
