/* HALT restart profile and RESET through the unchanged project's DCJ11.
 * HALT here is deliberately not a model of the full chip console ODT. */
#include "trace_oracle.h"
#ifndef ORACLE_RESET_PROFILE
#error "Reset callback audit required"
#endif
static void run(word op,unsigned psw,unsigned priority,unsigned clear){
 word r[8];unsigned seed=count;
 const word restart[]={0x3000,0x3001,0xff00,0xff01,0,1,0x1000,0x1001};
 word p[6][2]={{4,restart[seed%8]},{6,(word)(0xa500|seed)},
               {014,0x3800},{016,0345},{0100,0x4000},{0102,0343}};
 for(unsigned k=0;k<6;k++)r[k]=(word)(0x8765u+seed*31u+k*0x1111u);
 r[6]=(word)(0x6000+2*(seed%32));r[7]=(word)(0x1000+2*(seed%32));
 trace_steps=trace_retirements=1;irq_request=priority?((priority<<9)|0100):0;
 reset_irq_policy=clear;fixture(op,(word)psw,r,6,p);
}
int main(void){
 trace_exclusions=fopen("build/system_control-excluded.csv","w");if(!trace_exclusions)return 2;
 fprintf(trace_exclusions,"candidate,opcode,psw,reason_mask,actual_vectors,expected_vectors,R0,R1,R2,R3,R4,R5,SP,PC\n");
 for(unsigned psw=0;psw<256;psw++)for(unsigned priority=0;priority<8;priority++){
  run(0,(word)psw,priority,0);
  run(5,(word)psw,priority,0);
  run(5,(word)psw,priority,1);
 }
 fprintf(stderr,"System-control DCJ11 restart profile: %u completed of %u candidates; %u excluded. Reasons abort=%u vector=%u I/O=%u odd=%u stack=%u\n",count,count+skipped,skipped,excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3],excluded_stack);
 fclose(trace_exclusions);return count==6144 && skipped==0?0:1;
}
