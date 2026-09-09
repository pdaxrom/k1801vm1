/* The decoder candidate set includes missing ISA. Only actual successful
 * DCJ11 vector004/010 cases enter the compatibility fixtures. */
#include "trace_oracle.h"
#ifndef ORACLE_TRAP_PROFILE
#error "Reserved-instruction suite requires vector audit"
#endif
int main(void) {
    FILE *f=fopen("build/illegal-opcodes.txt","r");unsigned op,vec,candidates=0;
    if(!f)return 2;
    while(fscanf(f,"%x %x",&op,&vec)==2) {
        for(unsigned state=0;state<2;state++) {
            word r[8],p[2][2];
            for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+k*0x100);
            r[6]=0x6000;r[7]=0x1000;
            p[0][0]=(word)vec;p[0][1]=0x3000;
            p[1][0]=(word)(vec+2);p[1][1]=(word)(0340|(state?15:0));
            expected_vector=vec;
            fixture((word)op,(word)(state?0357:0),r,2,p);
            candidates++;
        }
    }
    if(!feof(f))return 2;fclose(f);
    fprintf(stderr,"Reserved/invalid-mode DCJ11 kernel/T=0 ENABLE_MMU=0: %u completed of %u candidates; %u excluded.\n",count,candidates,skipped);
    fprintf(stderr,"Reasons (may overlap): abort=%u different/no-vector=%u I/O=%u odd-word=%u\n",excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3]);
    return count?0:1;
}
