/* CP24 XOR: independently fail each destination/pointer/extension bus beat. */
#define main uj11_previous_fault_fixture_main
#include "bus_fault_vectors.c"
#undef main
int main(void){
 excluded_file=fopen("build/eis-xor-fault-excluded.csv","w");
 continuation_file=fopen("build/eis-xor-fault-continuation.csv","w");
 if(!excluded_file || !continuation_file)return 2;
 fprintf(excluded_file,"opcode,psw,fail_index,reason,vector_count,vector,completed_frames,io\n");
 fprintf(continuation_file,"case,opcode,frame_psw,step_psw,frame_pc,step_pc,frame_sp,step_sp,changed_register_mask,frame_beats,step_beats\n");
 word r[8];
 for(unsigned state=0;state<4;state++){
  word flags=(word)(state==0?0:state==1?3:state==2?010:0357);
  for(unsigned k=0;k<8;k++)r[k]=(word)(0x2000+k*0x200);
  r[6]=0x6000;r[7]=0x1000;
  for(unsigned rs=0;rs<8;rs++)for(unsigned mode=1;mode<8;mode++)for(unsigned rd=0;rd<8;rd++){
   word op=(word)(0074000|(rs<<6)|(mode<<3)|rd);
   candidate(op,flags,r);
   if(rd<6 && mode<6){r[rd]++;candidate(op,flags,r);r[rd]--;}
  }
 }
 fprintf(stderr,"XOR fault oracle: %u completed frames (%u ACK error, %u odd-word), %u excluded candidates.\n",count,ack_cases,odd_cases,skipped);
 fprintf(stderr,"Original emulator after the captured frame: %u changed register/PSW states, %u additional bus traces; preserved as observed C continuation, not compared as frame entry.\n",post_abort_changes,post_abort_accesses);
 fclose(excluded_file);fclose(continuation_file);return count?0:1;
}
