/* Actual DCJ11 ASHC oracle; all 64 counts with independent operand edges. */
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
 trace_exclusions=fopen("build/eis_ashc-excluded.csv","w");if(!trace_exclusions)return 2;
 fprintf(trace_exclusions,"candidate,opcode,psw,reason_mask,actual_vectors,expected_vectors,R0,R1,R2,R3,R4,R5,SP,PC\n");
 word r[8],p[16][2];
 const dword values[]={0,1,2,0xffff,0x10000,0x7fffffff,0x80000000,0x80000001,0xffffffff,0xfffffffe,0x40000000,0xc0000000,0x5555aaaa,0xaaaa5555,0x00008000};
 for(unsigned v=0;v<sizeof(values)/sizeof(values[0]);v++)for(unsigned n=0;n<64;n++)for(unsigned flags=0;flags<16;flags++){
  init(r,p,0);r[0]=(word)(values[v]>>16);r[1]=(word)values[v];r[2]=(word)(0xa500|n);fixture(0073002,(word)flags,r,5,p);
 }
 /* Independently vary ignored count bits 15:6, including bits 6 and 7. */
 const word upper[]={0,0x0040,0x0080,0xffc0};
 const dword edge[]={0,1,0x80000000,0xffffffff};
 for(unsigned hi=0;hi<4;hi++)for(unsigned n=0;n<64;n++)for(unsigned v=0;v<4;v++){
  init(r,p,0);r[0]=(word)(edge[v]>>16);r[1]=(word)edge[v];r[2]=(word)(upper[hi]|n);fixture(0073002,0,r,5,p);
 }
 /* All 512 encodings; aliases, SP/PC, T/IPL and eligible/masked IRQ. */
 const word states[]={0,1,017,020,077,0177,0200,0377};
 for(unsigned rs=0;rs<8;rs++)for(unsigned mode=0;mode<8;mode++)for(unsigned rd=0;rd<8;rd++)for(unsigned st=0;st<8;st++)for(unsigned irq=0;irq<2;irq++){
  init(r,p,irq?7:0);fixture((word)(0073000|(rs<<6)|(mode<<3)|rd),states[st],r,5,p);
 }
 /* Every six-bit count in every mode, controlled memory/pointer/extension.
  * Upper count bits are deliberately nonzero; four boundary T/IRQ profiles. */
 for(unsigned mode=0;mode<8;mode++)for(unsigned n=0;n<64;n++)for(unsigned st=0;st<4;st++){
  init(r,p,st&1?7:0);r[0]=(word)(values[n%15]>>16);r[1]=(word)values[n%15];r[2]=0x4000;word shift=(word)(0xa500|n);unsigned np=5;
  switch(mode){
   case 0:r[2]=shift;break;
   case 1:case 2:p[np][0]=0x4000;p[np++][1]=shift;break;
   case 3:p[np][0]=0x4000;p[np++][1]=0x4200;p[np][0]=0x4200;p[np++][1]=shift;break;
   case 4:p[np][0]=0x3ffe;p[np++][1]=shift;break;
   case 5:p[np][0]=0x3ffe;p[np++][1]=0x4200;p[np][0]=0x4200;p[np++][1]=shift;break;
   case 6:p[np][0]=0x4020;p[np++][1]=shift;break;
   case 7:p[np][0]=0x4020;p[np++][1]=0x4200;p[np][0]=0x4200;p[np++][1]=shift;break;
  }
  fixture((word)(0073002|(mode<<3)),(word)(st<2?017:037),r,np,p);
 }
 /* Explicit Rs==EA register with zero count exposes snapshot ordering. */
 for(unsigned rs=0;rs<7;rs++)for(unsigned mode=1;mode<8;mode++)for(unsigned st=0;st<4;st++){
  init(r,p,st&1?7:0);word base=rs==6?0x6000:0x4000;r[rs]=base;p[4][1]=0;unsigned np=5;
  word address=(word)(base-((mode==4||mode==5)?2:0));
  p[np][0]=address;p[np++][1]=(mode==3||mode==5||mode==7)?0x4200:0;
  if(mode==3||mode==5||mode==7){p[np][0]=0x4200;p[np++][1]=0;}
  fixture((word)(0073000|(rs<<6)|(mode<<3)|rs),(word)(st<2?0:020),r,np,p);
 }
 /* Immediate, absolute and signed/wrapping PC-relative displacements. */
 const unsigned specs[]={027,037,067,077,060,070};
 const word exts[]={0,1,0x7f,0x80,0xff,0x2000,0xffe0,0xfffe};
 for(unsigned spec=0;spec<6;spec++)for(unsigned ext=0;ext<8;ext++)for(unsigned st=0;st<8;st++){
  init(r,p,st&1?7:0);p[4][1]=exts[ext];
  if(spec>=4)r[0]=ext<4?0xfff0:0x0020;
  fixture((word)(0073000|(2<<6)|specs[spec]),states[st],r,5,p);
 }
 /* Odd Rs duplicates the operand, and the final low store wins. */
 const word odd_edges[]={0,1,0x7fff,0x8000,0x8001,0xffff,0x5555,0xaaaa};
 for(unsigned v=0;v<8;v++)for(unsigned n=0;n<64;n++)for(unsigned f=0;f<4;f++){
  init(r,p,0);r[1]=odd_edges[v];r[2]=(word)(0xa500|n);fixture(0073102,(word)(f*5),r,5,p);
 }
 /* A count EA may modify the low register as well as the high register. */
 for(unsigned rs=0;rs<6;rs+=2)for(unsigned mode=2;mode<=5;mode++)for(unsigned f=0;f<4;f++){
  init(r,p,0);r[rs]=0x8001;r[rs+1]=0x4000;unsigned np=5;
  p[np][0]=(word)(mode>=4?0x3ffe:0x4000);p[np++][1]=(mode==3||mode==5)?0x4200:0;
  if(mode==3||mode==5){p[np][0]=0x4200;p[np++][1]=0;}
  fixture((word)(0073000|(rs<<6)|(mode<<3)|(rs+1)),(word)(f*5),r,np,p);
 }
 fprintf(stderr,"ASHC DCJ11: %u completed of %u candidates; %u excluded. Reasons abort=%u vector=%u I/O=%u odd=%u stack=%u\n",count,count+skipped,skipped,excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3],excluded_stack);
 fclose(trace_exclusions);return count?0:1;
}
