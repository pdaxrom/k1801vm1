#ifndef UJ11_TRACE_ORACLE_H
#define UJ11_TRACE_ORACLE_H
/* Shared oracle: actual DCJ11 core_step with bus callbacks, no copied ISA logic.
 * stdout: variable-length hex records (init patches, final registers, bus trace).
 * Non-completing/trapping cases are counted separately, not called compatible.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/core.h"
#include "core/hardware.h"
#if ENABLE_MMU
#error "uJ11 v1 is strictly without MMU"
#endif
static byte memory[65536];
static struct {word wr,addr,data;} trace[32];
static unsigned nt,count,skipped;
#ifdef ORACLE_RESET_PROFILE
static unsigned reset_received,reset_irq_policy;
static void reset_devices(regs *r){(void)r;reset_received++;}
#endif
static unsigned completed_by_op[16],excluded_by_op[16],excluded_reason[4];
#ifdef ORACLE_PSW_PROFILE
static unsigned excluded_stack;
#endif
#ifdef ORACLE_ACCESS_AUDIT
static unsigned attempted_io;
void uj11_oracle_access(unsigned address) {if(address>=0160000)attempted_io=1;}
#endif
#ifdef ORACLE_VECTOR_AUDIT
static unsigned attempted_vector;
#ifdef ORACLE_TRAP_PROFILE
static unsigned expected_vector,vector_count,observed_vector;
#endif
#ifdef ORACLE_TRACE_PROFILE
static unsigned trace_steps=1,trace_retirements=1,expected_vectors[4],expected_vector_count;
static unsigned observed_vectors[4];
static FILE *trace_exclusions;
#endif
void uj11_oracle_vector(unsigned vector) {
    (void)vector;attempted_vector=1;
#ifdef ORACLE_TRAP_PROFILE
#ifdef ORACLE_TRACE_PROFILE
    if(vector_count>=4){fprintf(stderr,"trace vector overflow\n");exit(2);}
    observed_vectors[vector_count]=vector;
#endif
    vector_count++;observed_vector=vector;
#endif
}
#endif
#ifdef ORACLE_IRQ_PROFILE
static word irq_request;
static unsigned irq_polled;
static int irq_poll(regs *r,word *vector) {
    (void)r;
    #ifdef ORACLE_RESET_PROFILE
    if(reset_irq_policy && reset_received)return 0;
#endif
    if(!irq_request || irq_polled++)return 0;
    *vector=irq_request;return 1;
}
#endif
static word base_word(word a) {return 0x8000u|((a^(a>>3)^0x3456u)&0x1ffeu);}
static void poke(word a,word v) {memory[a]=(byte)v;memory[(word)(a+1)]=(byte)(v>>8);}
static word peek(word a) {return memory[a]|((word)memory[(word)(a+1)]<<8);}
static void record(word wr,word a,word v) {
    if(nt>=32) {fprintf(stderr,"oracle trace overflow\n");exit(2);}
    trace[nt].wr=wr;trace[nt].addr=a;trace[nt].data=v;nt++;
}
static word load(regs *r,word a) {(void)r;word v=peek(a);record(0,a,v);return v;}
static void save(regs *r,word a,word v) {(void)r;record(1,a,v);poke(a,v);}
static byte loadb(regs *r,word a) {(void)r;byte v=memory[a];record(2,a,v);return v;}
static void saveb(regs *r,word a,byte v) {(void)r;record(3,a,v);memory[a]=v;}
static void fixture(word op,word flags,word initial[8],unsigned np,word patches[][2]) {
    regs r;memset(&r,0,sizeof r);r.model=DCJ11;
    if(hwstub_set_memory(memory,sizeof memory)) exit(2);
    hwstub_connect(&r);if(core_init(&r))exit(2);core_reset(&r);
    for(unsigned a=0;a<65536;a+=2)poke((word)a,base_word((word)a));
    for(unsigned p=0;p<np;p++)poke(patches[p][0],patches[p][1]);
    poke(initial[7],op);
    memcpy(r.r,initial,sizeof r.r);r.psw=flags;
    r.ram_fast=NULL;r.load_word=load;r.store_word=save;r.load_byte=loadb;r.store_byte=saveb;
#ifdef ORACLE_RESET_PROFILE
    r.reset=reset_devices;reset_received=0;
#endif
#ifdef ORACLE_IRQ_PROFILE
    irq_polled=0;r.poll_irq=irq_poll;
#endif
    nt=0;
#ifdef ORACLE_ACCESS_AUDIT
    attempted_io=0;
#endif
#ifdef ORACLE_VECTOR_AUDIT
    attempted_vector=0;
#ifdef ORACLE_TRAP_PROFILE
    vector_count=0;observed_vector=0;
#endif
#endif
#ifdef ORACLE_TRACE_PROFILE
    for(unsigned step=0;step<trace_steps;step++)(void)core_step(&r);
#else
    (void)core_step(&r);
#endif
#ifdef ORACLE_PSW_PROFILE
    /* This fixture delegates operand EA and post-MTPS IPL to the actual C
     * executor. Only a single TRACE or IRQ frame is in its completed profile.
     * Abort/I/O cases still go through the explicit exclusion audit below. */
    expected_vector_count=vector_count;
    for(unsigned v=0;v<vector_count;v++)expected_vectors[v]=observed_vectors[v];
#endif
    unsigned bad=(!!r.fAbort)|((!!r.fTrap)<<1);
#ifdef ORACLE_PSW_PROFILE
    if(vector_count>1 || (vector_count && observed_vectors[0]!=014 && observed_vectors[0]!=0100))bad|=2;
    if(r.J11_CPUERR & 0000014)bad|=16; /* primary DCJ11 red/yellow stack-limit status */
#endif
#ifdef ORACLE_VECTOR_AUDIT
#ifdef ORACLE_TRAP_PROFILE
#ifdef ORACLE_TRACE_PROFILE
    if(vector_count!=expected_vector_count)bad|=2;
    for(unsigned v=0;v<vector_count;v++)if(observed_vectors[v]!=expected_vectors[v])bad|=2;
#else
    if(vector_count!=(expected_vector?1u:0u) || (expected_vector && observed_vector!=expected_vector))bad|=2;
#endif
#else
    if(attempted_vector)bad|=2;
#endif
#endif
    for(unsigned t=0;t<nt;t++) {
        if(trace[t].addr>=0160000)bad|=4;
        if((trace[t].addr&1) && !(trace[t].wr&2))bad|=8;
    }
#ifdef ORACLE_ACCESS_AUDIT
    if(attempted_io)bad|=4;
#endif
    if(bad){
#ifdef ORACLE_TRACE_PROFILE
#ifdef ORACLE_PSW_PROFILE
        excluded_stack+=!!(bad&16);
        if(!(bad&(5|16))){
#else
        if(!(bad&5)){
#endif
        fprintf(stderr,"unexpected trace vector without abort or I/O: op%06o PSW%06o vectors%u first%06o SP%06o PC%06o\n",op,flags,vector_count,observed_vectors[0],r.r[6],r.r[7]);exit(2);}
        fprintf(trace_exclusions,"%u,%06o,%06o,%x,%u,%u",count+skipped,op,flags,bad,vector_count,expected_vector_count);
        for(unsigned k=0;k<8;k++)fprintf(trace_exclusions,",%04x",initial[k]);
        fputc('\n',trace_exclusions);
        fprintf(stderr,"trace excluded op%06o flags%06o bad%x vectors%u/%u PC%06o\n",op,flags,bad,vector_count,expected_vector_count,r.r[7]);
#endif
        skipped++;excluded_by_op[op>>12]++;
        for(unsigned reason=0;reason<4;reason++)if(bad&(1u<<reason))excluded_reason[reason]++;
        core_fini(&r);hwstub_clear_memory_binding();return;}
    completed_by_op[op>>12]++;
    printf("%x %04x %04x",count++,op,flags);
    for(unsigned k=0;k<8;k++)printf(" %04x",initial[k]);
    /* The opcode was poked after patches above. Preserve that precedence
     * when a stack/data patch overlaps it; keep older disjoint fixtures byte-identical. */
    unsigned opcode_overlap=0;
    for(unsigned p=0;p<np;p++)
        if(patches[p][0]==initial[7] || (word)(patches[p][0]+1)==initial[7] ||
           patches[p][0]==(word)(initial[7]+1))opcode_overlap=1;
    printf(" %x\n",np+1);
#ifdef ORACLE_IRQ_PROFILE
#ifdef ORACLE_TRACE_PROFILE
    unsigned accepted_irq=0;
    for(unsigned v=0;v<vector_count;v++)if(observed_vectors[v]==0100)accepted_irq++;
    #ifdef ORACLE_RESET_PROFILE
    printf("%04x %x %x %x %x %x\n",irq_request,accepted_irq,trace_retirements,!!r.fWait,reset_received,reset_irq_policy);
#else
    printf("%04x %x %x %x\n",irq_request,accepted_irq,trace_retirements,!!r.fWait);
#endif
#else
    printf("%04x %x\n",irq_request,expected_vector?1:0);
#endif
#endif
    if(!opcode_overlap)printf("%04x %04x\n",initial[7],op);
    for(unsigned p=0;p<np;p++)printf("%04x %04x\n",patches[p][0],patches[p][1]);
    if(opcode_overlap)printf("%04x %04x\n",initial[7],op);
    printf("%04x",r.psw);for(unsigned k=0;k<8;k++)printf(" %04x",r.r[k]);
    printf(" %x\n",nt);
    for(unsigned t=0;t<nt;t++)printf("%x %04x %04x\n",trace[t].wr,trace[t].addr,trace[t].data);
    core_fini(&r);hwstub_clear_memory_binding();
}

#endif
