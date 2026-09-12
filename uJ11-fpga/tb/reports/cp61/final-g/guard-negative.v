module cone0(input [15:0] service_ir,input [1:0] service_ready,
input service_mode,input [9:0] dispatch_address,output [9:0] result);
    localparam [9:0] S_FP=10'h02d;
    localparam [9:0] S_ODT=10'h053;
    localparam [9:0] S_START=10'h10e;
    localparam [9:0] S_RCPC=10'h116;
    localparam [9:0] S_RCPS=10'h11e;
    localparam [9:0] S_WCPC=10'h126;
    localparam [9:0] S_WCPS=10'h12e;
    localparam [9:0] S_UPPER_READ=10'h057;
    localparam [9:0] S_UPPER_WRITE=10'h06b;
    localparam [9:0] S_CONFIG=10'h06f;
    localparam [9:0] S_STATUS=10'h0a1;
    localparam [9:0] S_LOWER_READ=10'h0a5;
    localparam [9:0] S_LOWER_WRITE=10'h0a9;
    localparam [9:0] S_MFUS=10'h0ad;
    localparam [9:0] S_MTUS=10'h136;
    localparam [9:0] S_RSEL=10'h13e;
    wire service_privileged=service_ir[15:6]==0 &&
        (service_ir[5] ? service_ir[4:1]==2 : |service_ir[4:3]);
    wire [9:0] service_dispatch=
        service_ir[15:12]==4'hf ? (service_mode ? 10'h3ff : service_ready[1] ? S_FP : 10'h042) :
        service_ir==0 ? (service_mode ? 10'h3ff : S_ODT) :
        (!service_mode && service_privileged) ? 10'h042 : dispatch_address;


assign result=service_dispatch;
endmodule
module cone1(input [15:0] service_ir,input [1:0] service_ready,
input service_mode,input [9:0] dispatch_address,output [9:0] result);
    localparam [9:0] S_FP=10'h02d;
    localparam [9:0] S_ODT=10'h053;
    localparam [9:0] S_START=10'h10e;
    localparam [9:0] S_RCPC=10'h116;
    localparam [9:0] S_RCPS=10'h11e;
    localparam [9:0] S_WCPC=10'h126;
    localparam [9:0] S_WCPS=10'h12e;
    localparam [9:0] S_UPPER_READ=10'h057;
    localparam [9:0] S_UPPER_WRITE=10'h06b;
    localparam [9:0] S_CONFIG=10'h06f;
    localparam [9:0] S_STATUS=10'h0a1;
    localparam [9:0] S_LOWER_READ=10'h0a5;
    localparam [9:0] S_LOWER_WRITE=10'h0a9;
    localparam [9:0] S_MFUS=10'h0ad;
    localparam [9:0] S_MTUS=10'h136;
    localparam [9:0] S_RSEL=10'h13e;
    wire service_privileged=service_ir[15:6]==0 &&
        (service_ir[5] ? service_ir[4:1]==2 : |service_ir[4:3]);
    wire [9:0] service_dispatch=
        service_ir[15:12]==4'hf ? (service_mode ? 10'h3ff : service_ready[1] ? S_FP : 10'h042) :
        service_ir==0 ? (service_mode ? 10'h3ff : S_ODT) :
        (!service_mode && service_privileged) ? 10'h042 : dispatch_address;


assign result=service_dispatch;
endmodule
module test;
reg[15:0] ir;reg[1:0] ready;reg mode;wire[9:0] expected,observed;
wire [9:0] normal;
uj11_decode decode(ir,normal);
integer op,m,r,count=0;
cone0 reference_cone(ir,ready,mode,normal,expected);cone1 dut(ir,ready,mode,normal,observed);
initial begin for(r=0;r<4;r=r+1)for(m=0;m<2;m=m+1)for(op=0;op<65536;op=op+1)begin
ir=op;ready=r;mode=m;#1;
if(m==0 && op>=32 && op<=35)begin
 if(observed!==10'h42)$fatal(1,"guest installation opcode escaped %o",ir);
end else if(observed!==expected)$fatal(1,"unrelated dispatch changed %o mode%0d",ir,m);
count=count+1;
end
$display("PASS CP61 dispatch: %0d opcode/mode/ready combinations",count);$finish;end
endmodule
