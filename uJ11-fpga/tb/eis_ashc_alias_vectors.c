/* ASHC regression: count EA precedes destination capture; sign-boundary
 * cases are ordinary positive C comparisons, with no accepted divergences. */
#define main uj11_ashc_normal_fixture_main
#include "eis_ashc_vectors.c"
#undef main
int main(void){
 trace_exclusions=fopen("build/eis_ashc_alias-excluded.csv","w");if(!trace_exclusions)return 2;
 fprintf(trace_exclusions,"candidate,opcode,psw,reason_mask,actual_vectors,expected_vectors,R0,R1,R2,R3,R4,R5,SP,PC\n");
 word r[8],p[16][2];
 for(unsigned rs=0;rs<2;rs++)for(unsigned direction=0;direction<2;direction++)for(unsigned n=0;n<64;n++){
  init(r,p,0);r[rs]=(word)(direction?0x8000:0x7ffe);if(!rs)r[1]=1;
  p[5][0]=0x7ffe;p[5][1]=(word)(0xa500|n);
  fixture((word)(0073000|(rs<<6)|((direction?4:2)<<3)|rs),017,r,6,p);
 }
 fprintf(stderr,"ASHC alias diagnostic: %u completed of %u; %u excluded\n",count,count+skipped,skipped);
 fclose(trace_exclusions);return skipped?1:0;
}
