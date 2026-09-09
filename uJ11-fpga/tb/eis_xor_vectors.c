/* CP24: actual DCJ11 XOR, including late source reads and destination writes. */
#include "trace_oracle.h"
static void init(word r[8],word p[][2],unsigned priority){
 for(unsigned k=0;k<6;k++)r[k]=(word)(0x4000+k*0x100);
 r[6]=0x6000;r[7]=0x1000;
 p[0][0]=014;p[0][1]=0x3000;p[1][0]=016;p[1][1]=0345;
 p[2][0]=0100;p[2][1]=0x3800;p[3][0]=0102;p[3][1]=0343;
 p[4][0]=0x1002;p[4][1]=0x0020;
 trace_steps=trace_retirements=1;irq_request=priority?((priority<<9)|0100):0;
}
int main(void){
 trace_exclusions=fopen("build/eis_xor-excluded.csv","w");if(!trace_exclusions)return 2;
 fprintf(trace_exclusions,"candidate,opcode,psw,reason_mask,actual_vectors,expected_vectors,R0,R1,R2,R3,R4,R5,SP,PC\n");
 word r[8],p[16][2];
 const word values[]={0,1,2,0x7fff,0x8000,0x8001,0xffff,0xfffe,0x4000,0xc000,0x5555,0xaaaa};
 for(unsigned a=0;a<12;a++)for(unsigned b=0;b<12;b++)for(unsigned flags=0;flags<16;flags++){
  init(r,p,0);r[0]=values[a];r[1]=values[b];fixture(0074001,(word)flags,r,5,p);
 }
 /* All 512 encodings, every source/destination register and destination mode. */
 const word states[]={0,1,017,020,077,0177,0200,0377};
 for(unsigned rs=0;rs<8;rs++)for(unsigned mode=0;mode<8;mode++)for(unsigned rd=0;rd<8;rd++)for(unsigned st=0;st<8;st++)for(unsigned irq=0;irq<2;irq++){
  init(r,p,irq?7:0);fixture((word)(0074000|(rs<<6)|(mode<<3)|rd),states[st],r,5,p);
 }
 /* Aliased source/EA register: expose both increment and decrement ordering,
  * including the sign boundary and SP, with independently initialized data. */
 for(unsigned rs=0;rs<7;rs++)for(unsigned mode=1;mode<8;mode++)for(unsigned st=0;st<8;st++){
  init(r,p,st&1?7:0);r[rs]=(word)(st&2?0x8000:rs==6?0x6000:0x4000);
  p[4][1]=0;unsigned np=5;
  word a=(word)(r[rs]-((mode==4||mode==5)?2:0));
  p[np][0]=a;p[np++][1]=(mode==3||mode==5||mode==7)?0x4200:0xa55a;
  if(mode==3||mode==5||mode==7){p[np][0]=0x4200;p[np++][1]=0xa55a;}
  fixture((word)(0074000|(rs<<6)|(mode<<3)|rs),(word)(st&4?037:017),r,np,p);
 }
 /* PC source sees the PC after extension fetch; immediate destination is a
  * real read/modify/write of the instruction stream, never a read-only operand. */
 const unsigned specs[]={027,037,067,077,060,070};
 const word exts[]={0,2,0x7ffe,0x8000,0x2000,0xffe0,0xfffe};
 for(unsigned spec=0;spec<6;spec++)for(unsigned ext=0;ext<7;ext++)for(unsigned rs=0;rs<8;rs++){
  init(r,p,0);p[4][1]=exts[ext];
  if(spec>=4)r[0]=ext<3?0xfff0:0x0020;
  fixture((word)(0074000|(rs<<6)|specs[spec]),017,r,5,p);
 }
 fprintf(stderr,"XOR DCJ11: %u completed of %u candidates; %u excluded. Reasons abort=%u vector=%u I/O=%u odd=%u stack=%u\n",count,count+skipped,skipped,excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3],excluded_stack);
 fclose(trace_exclusions);return count?0:1;
}
