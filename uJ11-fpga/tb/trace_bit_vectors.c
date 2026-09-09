/* T-bit, RTI/RTT and trace/IRQ priority through unchanged DCJ11 core_step. */
#include "trace_oracle.h"
#ifndef ORACLE_TRACE_PROFILE
#error "Trace profile required"
#endif
static word state(unsigned n){return (word)((n&31)|((n>>5)<<5));}
static void init(word r[8]){
 for(unsigned i=0;i<8;i++)r[i]=(word)(0x2000+i*0x100);
 r[6]=0x6000;r[7]=0x1000;trace_steps=trace_retirements=1;irq_request=0;expected_vector=0;
}
static void vectors(unsigned n,word a,word b){expected_vector_count=n;expected_vectors[0]=a;expected_vectors[1]=b;}
static unsigned patch_vectors(word p[][2],word newpsw){
 p[0][0]=014;p[0][1]=0x3000;p[1][0]=016;p[1][1]=newpsw;
 p[2][0]=0100;p[2][1]=0x4000;p[3][0]=0102;p[3][1]=0343;
 return 4;
}
int main(void){
 trace_exclusions=fopen("build/trace_bit-excluded.csv","w");if(!trace_exclusions)return 2;
 fprintf(trace_exclusions,"candidate,opcode,psw,reason_mask,actual_vectors,expected_vectors,R0,R1,R2,R3,R4,R5,SP,PC\n");
 word r[8],p[16][2];
 const word rr[]={0010001,0020001,0030001,0040001,0050001,0060001,0160001,
                 0110001,0120001,0130001,0140001,0150001,0000301,0006701};
 /* Every NZVC/T/IPL combination, including traced SOB taken/fall-through. */
 for(unsigned kind=0;kind<17;kind++)for(unsigned st=0;st<256;st++){
  init(r);word op=kind<14?rr[kind]:kind<16?0077101:(word)(0000230|(st&7));
  if(kind==14)r[1]=1;else if(kind==15)r[1]=2;
  unsigned n=patch_vectors(p,state((st*37+13)&255));
  vectors((st&16)?1:0,014,0);fixture(op,state(st),r,n,p);
 }
 /* All unary word/byte operations, all modes with R0/R6/R7. */
 for(unsigned by=0;by<2;by++)for(unsigned fn=0;fn<12;fn++)for(unsigned mode=0;mode<8;mode++)for(unsigned reg=0;reg<3;reg++){
  init(r);unsigned rn=reg==0?0:reg==1?6:7;unsigned n=patch_vectors(p,0340);
  vectors(1,014,0);fixture((word)((by?0100000:0)|0005000|(fn<<6)|(mode<<3)|rn),16|(fn&15),r,n,p);
 }
 /* Every source/destination mode pair, with ordinary and PC source/destination. */
 for(unsigned by=0;by<2;by++)for(unsigned fn=1;fn<=(by?5:6);fn++)for(unsigned sm=0;sm<8;sm++)for(unsigned dm=0;dm<8;dm++)for(unsigned pc=0;pc<4;pc++){
  init(r);unsigned n=patch_vectors(p,0340);unsigned sr=pc&1?7:0,dr=pc&2?7:1;
  vectors(1,014,0);fixture((word)((by?0100000:0)|(fn<<12)|(sm<<9)|(sr<<6)|(dm<<3)|dr),16|(sm+dm)%16,r,n,p);
 }
 /* Branch predicates use old NZVC, trace stacks the actual resulting PC. */
 for(unsigned branch=1;branch<16;branch++)for(unsigned flags=0;flags<16;flags++)for(unsigned disp=0;disp<3;disp++){
  init(r);unsigned n=patch_vectors(p,0340);vectors(1,014,0);
  word op=(word)(((branch&8)?0100000:0)|((branch&7)<<8)|(disp==0?0:disp==1?0177:0200));
  fixture(op,16|flags,r,n,p);
 }
 /* RTI uses restored T; RTT suppresses its own trace. IRQ tests restored IPL. */
 for(unsigned op=2;op<=6;op+=4)for(unsigned oldt=0;oldt<2;oldt++)for(unsigned st=0;st<256;st++)for(unsigned pri=0;pri<2;pri++){
  init(r);unsigned n=patch_vectors(p,state((st*13+5)&255));
  p[n][0]=0x6000;p[n++][1]=0x1800;p[n][0]=0x6002;p[n++][1]=state(st);
  irq_request=pri?((7<<9)|0100):0;
  unsigned tracing=op==2 && (st&16),irq=pri && (st>>5)<7;
  vectors(tracing||irq?1:0,tracing?014:0100,0);
  fixture((word)op,(word)(0340|(oldt?16:0)|((st+5)&15)),r,n,p);
 }
 /* Software trap followed by trace depends on pre-instruction T, not vector T. */
 const word traps[]={000003,000004,0104007,0104777,000100,0004000,0075040};
 const word vecs[]={014,020,030,034,004,010,010};
 for(unsigned op=0;op<7;op++)for(unsigned oldt=0;oldt<2;oldt++)for(unsigned newt=0;newt<2;newt++)for(unsigned flags=0;flags<16;flags++){
  init(r);unsigned n=patch_vectors(p,(word)(newt*16|flags));
  if(vecs[op]!=014){p[n][0]=vecs[op];p[n++][1]=0x3200;p[n][0]=vecs[op]+2;p[n++][1]=(word)(newt*16|flags);}
  irq_request=(7<<9)|0100;vectors(2,vecs[op],oldt?014:0100);
  fixture(traps[op],(word)(oldt*16|(flags^15)),r,n,p);
 }
 /* WAIT retires once. T is considered on the later waiting step, after the
  * first-step IRQ opportunity. At that later step trace precedes IRQ. */
 for(unsigned st=0;st<256;st++)for(unsigned pri=0;pri<2;pri++){
  init(r);unsigned n=patch_vectors(p,0340);irq_request=pri?((7<<9)|0100):0;
  unsigned irq=pri && (st>>5)<7;
  trace_steps=((st&16)&&!irq)?2:1;
  vectors(irq||(st&16)?1:0,irq?0100:014,0);fixture(1,state(st),r,n,p);
 }
 fprintf(stderr,"Trace-bit DCJ11: %u completed cases; %u excluded. Reasons abort=%u vector=%u I/O=%u odd=%u\n",count,skipped,excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3]);
 fclose(trace_exclusions);
 return count==11196 && skipped==276?0:1;
}
