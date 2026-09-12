    // CP57 single frozen context. Cold reset clears readiness, not FRAM.
    reg service_mode;
    reg [1:0] service_ready;
    wire [1:0] service_space=(reading || writing) ? uword[8:7] : 2'd0;
    wire service_enter=control && command==0 && uword[2:1]==1;
    wire service_leave=control && command==0 && uword[2:1]==2;
    wire service_config=control && command==0 && uword[2:1]==3;
    // ACTIVE selects current CPU space. GUEST is logical bank zero;
    // UPPER/LOWER bypass every CPU ROM/CSR overlay for raw physical RAM.
    // CPC/CPSW are ordinary UPPER memory words, addressed by microcode in T5.
    assign mem_bank=(service_mode || service_space[1]) && !service_space[0];
    assign mem_physical=service_space[1];
    always @(posedge clk) begin
        if(reset)begin
            service_mode<=0;
            service_ready<=0;
        end else if(step)begin
            if(service_enter)service_mode<=1;
            if(service_leave)service_mode<=0;
            if(service_config)service_ready<=read_a[1:0];
        end
    end
    // Use the opcode prefix instead of comparing ten microcode entry addresses.
    // Logic decode consumes incoming data; EBR decode has already captured IR.
    wire [15:0] service_ir=ROM_DECODE!=0 ? ir : dispatch_ir;
    wire service_privileged=service_ir[15:6]==0 &&
        (service_ir[5] ? service_ir[4:1]==2 : |service_ir[4:3]);
    wire [9:0] service_dispatch=
        service_ir[15:12]==4'hf ? (service_mode ? 10'h3ff : service_ready[1] ? S_FP : 10'h042) :
        service_ir==0 ? (service_mode ? 10'h3ff : service_ready[0] ? S_ODT : 10'h018) :
        (!service_mode && service_privileged) ? 10'h042 : dispatch_address;
