/* All CC encodings and MFPT through the unchanged DCJ11 executor. */
#include "trace_oracle.h"
#ifndef ORACLE_TRACE_PROFILE
#error "Trace/IRQ audit required"
#endif
static void run(unsigned op_index,unsigned psw,unsigned priority,unsigned seed){
 word r[8],p[4][2]={{014,0x3000},{016,0345},{0100,0x4000},{0102,0343}};
 for(unsigned k=0;k<6;k++)r[k]=(word)(0x8765u+seed*31u+k*0x1111u);
 r[6]=0x6000;r[7]=(word)(0x1000+2*(seed%64));
 trace_steps=trace_retirements=1;
 irq_request=priority?((priority<<9)|0100):0;
 expected_vector_count=(psw&16)|| (priority>(psw>>5))?1:0;
 expected_vectors[0]=(psw&16)?014:0100;
 fixture(op_index==32?000007:(word)(000240|op_index),(word)psw,r,4,p);
}
int main(void){
 trace_exclusions=fopen("build/system_flags-excluded.csv","w");if(!trace_exclusions)return 2;
 fprintf(trace_exclusions,"candidate,opcode,psw,reason_mask,actual_vectors,expected_vectors,R0,R1,R2,R3,R4,R5,SP,PC\n");
 /* Every mask/set combination, every NZVC/T/IPL value, with and without IRQ. */
 for(unsigned op=0;op<33;op++)for(unsigned st=0;st<256;st++)for(unsigned pending=0;pending<2;pending++)
  run(op,st,pending?(1+(op+st)%7):0,count);
 /* Full IPL x IRQ-priority matrix at both trace states for each encoding. */
 for(unsigned op=0;op<33;op++)for(unsigned ipl=0;ipl<8;ipl++)for(unsigned pri=0;pri<8;pri++)for(unsigned t=0;t<2;t++)
  run(op,(ipl<<5)|(t<<4)|((op+ipl+pri)&15),pri,count);
 fprintf(stderr,"System-flags DCJ11: %u completed cases; %u excluded. Reasons abort=%u vector=%u I/O=%u odd=%u\n",count,skipped,excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3]);
 fclose(trace_exclusions);return count==21120 && skipped==0?0:1;
}
