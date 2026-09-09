/* MFPS/MTPS through the unchanged DCJ11 executor; no copied EA implementation. */
#include "trace_oracle.h"
#ifndef ORACLE_PSW_PROFILE
#error "PSW transfer profile required"
#endif
static void init(word r[8],word p[][2],unsigned priority){
 for(unsigned k=0;k<6;k++)r[k]=(word)(0x4000+k*0x100);
 r[6]=0x6000;r[7]=0x1000;
 p[0][0]=014;p[0][1]=0x3000;p[1][0]=016;p[1][1]=0345;
 p[2][0]=0100;p[2][1]=0x3800;p[3][0]=0102;p[3][1]=0343;
 p[4][0]=0x1002;p[4][1]=0x0020;
 trace_steps=trace_retirements=1;irq_request=priority?((priority<<9)|0100):0;
}
int main(void){
 trace_exclusions=fopen("build/psw_transfer-excluded.csv","w");if(!trace_exclusions)return 2;
 fprintf(trace_exclusions,"candidate,opcode,psw,reason_mask,actual_vectors,expected_vectors,R0,R1,R2,R3,R4,R5,SP,PC\n");
 word r[8],p[16][2];
 /* MFPS every PSW low byte: register, even/odd memory and PC destinations. */
 const unsigned mfps_dst[]={0,010,010,7};
 for(unsigned kind=0;kind<4;kind++)for(unsigned psw=0;psw<256;psw++)for(unsigned pending=0;pending<2;pending++){
  init(r,p,pending?7:0);if(kind==2)r[0]|=1;
  fixture((word)(0106700|mfps_dst[kind]),(word)psw,r,5,p);
 }
 /* MTPS every source byte x old T/IPL, IRQ absent or priority7. High byte must vanish. */
 for(unsigned value=0;value<256;value++)for(unsigned ipl=0;ipl<8;ipl++)for(unsigned t=0;t<2;t++)for(unsigned pending=0;pending<2;pending++){
  init(r,p,pending?7:0);r[0]=(word)(0xa500|value);
  fixture(0106400,(word)((ipl<<5)|(t<<4)|((value+7)&15)),r,5,p);
 }
 /* Both operations: every EA mode and register, representative flags/IPL/T. */
 const word states[]={0,1,017,020,077,0177,0200,0377};
 for(unsigned op=0;op<2;op++)for(unsigned mode=0;mode<8;mode++)for(unsigned reg=0;reg<8;reg++)for(unsigned st=0;st<8;st++)for(unsigned irq=0;irq<2;irq++){
  init(r,p,irq?1+(mode+reg+st)%7:0);
  fixture((word)((op?0106400:0106700)|(mode<<3)|reg),states[st],r,5,p);
 }
 /* Explicit extension words: immediate/absolute, signed PC-relative, wrap, odd byte EA. */
 const word exts[]={0,1,0x7f,0x80,0xff,0x2000,0xffe0,0xfffe};
 const unsigned specs[]={027,037,067,077,060,070};
 for(unsigned op=0;op<2;op++)for(unsigned spec=0;spec<6;spec++)for(unsigned ext=0;ext<8;ext++)for(unsigned st=0;st<8;st++){
  init(r,p,st&1?7:0);p[4][1]=exts[ext];
  if(spec>=4)r[0]=ext<4?0xfff0:0x0020;
  fixture((word)((op?0106400:0106700)|specs[spec]),states[st],r,5,p);
 }
 fprintf(stderr,"PSW-transfer DCJ11: %u completed of %u candidates; %u excluded. Reasons abort=%u vector=%u I/O=%u odd=%u stack=%u\n",count,count+skipped,skipped,excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3],excluded_stack);
 fclose(trace_exclusions);return count?0:1;
}
