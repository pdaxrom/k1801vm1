`timescale 1ns/1ps
// Microcoded execution engine. Dispatch is an external, separately measured
// combinational function of dispatch_ir (incoming data during FETCH).
module uj11_engine #(parameter integer ROM_DECODE=0, IRQ_VECTOR_BITS=8, parameter [15:0] UNMASKED_VECTOR=0) (
    input wire clk, reset,
    input wire halt_button, debug_block,
    input wire irq_valid,
    input wire [2:0] irq_priority,
    input wire [IRQ_VECTOR_BITS:1] irq_vector,
    output wire irq_ack, waiting,
    output reg peripheral_reset,
    input wire [9:0] dispatch_address,
    output wire [15:0] dispatch_ir,
    output wire [15:0] mem_addr, mem_write_data,
    output wire mem_request, mem_read, mem_write, mem_byte,
    output wire mem_bank, mem_physical,
    input wire mem_ack, mem_error,
    input wire [15:0] mem_read_data,
    output wire stopped,
    output wire [1:0] fault_code,
    output reg retire,
    output wire [9:0] debug_upc,
    output wire [35:0] debug_uword,
    output reg [15:0] ir, mdr,
    output wire [15:0] psw, q,
    output wire debug_rf_write,
    output wire [3:0] debug_rf_address,
    output wire [15:0] debug_rf_data
);
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
    // Single frozen context. Cold reset clears readiness, not FRAM.
    reg service_mode;
    // CONFIG/STATUS low bits 0..1 retain ABI2 meaning. Bit2 enables debug;
    // legacy service software cannot enter an uninstalled debug vector.
    reg debug_enabled, debug_pending, debug_context, debug_wait, debug_trace;
    wire debug_wanted=debug_enabled && service_ready[0] && debug_pending &&
                      !service_mode && !debug_block;
    wire debug_ack=step && debug_wanted && (alu_boundary || wait_command);
    wire wait_return=service_leave && debug_context && debug_wait;
    always @(posedge clk) begin
        if(reset) begin
            debug_enabled<=0; debug_pending<=0; debug_context<=0;
            debug_wait<=0; debug_trace<=0;
        end else begin
            if(halt_button && debug_enabled && service_ready[0]) debug_pending<=1;
            if(step && service_config) begin
                debug_enabled<=read_a[2];
                debug_pending<=read_a[3];
                debug_wait<=read_a[5];
                debug_trace<=read_a[6];
            end
            if(debug_ack) begin
                debug_pending<=0;
                debug_context<=1;
                debug_wait<=wait_command;
                debug_trace<=trace_pending;
            end
            if(step && service_leave) begin
                debug_context<=0;
                if(debug_context) debug_pending<=0;
                if(ir[2] && debug_enabled && service_ready[0]) debug_pending<=1;
            end
        end
    end
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
            service_mode<=1;
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
        (service_ir[5] ? ~|service_ir[4:3] : |service_ir[4:3]);
    wire [9:0] service_dispatch=
        service_ir[15:12]==4'hf ? (service_mode ? 10'h3ff : service_ready[1] ? S_FP : 10'h042) :
        service_ir==0 ? (service_mode ? 10'h3ff : S_ODT) :
        (!service_mode && service_privileged) ? 10'h042 : dispatch_address;

    wire [35:0] uword;
    wire [9:0] next_address;
    wire control = uword[35];
    wire [3:0] command = uword[34:31];
    wire fetching = control && command==4'd2;
    wire reading = control && command==4'd11;
    wire writing = control && command==4'd12;
    wire stopping = control && command==4'd13;
    wire wait_command = control && command==4'd14;
    reg wait_seen, frame_active, fault_repair, trace_latched;
    // 0 instruction, 1 IRQ frame, 2 memory-fault frame, 3 trace frame.
    reg [1:0] irq_active;
    wire trap_command = control && command==4'd15;
    wire [15:0] resolved_vector={{(15-IRQ_VECTOR_BITS){1'b0}},irq_vector,1'b0};
    // Optional private board firmware assist. External IRQs retain IPL rules.
    wire irq_pending = !service_mode && irq_valid && (irq_priority > psw[7:5] ||
                       (UNMASKED_VECTOR!=0 && resolved_vector==UNMASKED_VECTOR)) && !irq_active[1];
    wire alu_boundary = !control &&
                        (uword[9:8]==2'd2 || (uword[9:8]==2'd3 && read_a==16'd1));
    // A resolved vector is held by the source through the accepting clock.
    // IRQ completes the previous instruction, then reuses MDR for its vector.
    // T is sampled before an instruction; RTI instead uses restored T and
    // RTT suppresses its own trace. WAIT checks T only after its first step.
    wire return_trace = uword[0] && uword[12:10]!=3'd6;
    wire trace_pending = !service_mode && irq_active==0 && (wait_command ? (wait_seen && psw[4]) :
                         (return_trace ? (psw[4] && !ir[2]) : trace_latched));
    wire trace_ack = step && trace_pending && !debug_wanted && (alu_boundary || wait_command);
    assign irq_ack = step && irq_pending && !trace_pending && !debug_wanted && (alu_boundary || wait_command);
    assign waiting = wait_command && !reset && !stopped;
    // Register JUMP.init for peripherals with asynchronous reset inputs.
    // Pulse covers the following settling word; IRQ samples after release.
    always @(posedge clk) begin
        if (reset) peripheral_reset <= 0;
        else peripheral_reset <= running && control && command==4'd0 && uword[0];
    end

    // Synchronous opcode lookup adds one internal clock after a successful
    // fetch. Capture IR/MDR on the physical ACK and issue no repeated beat.
    reg decode_wait;
    wire fetch_capture=ROM_DECODE!=0 && fetching && mem_request && mem_ack && !mem_error;
    wire effective_ack=(ROM_DECODE!=0 && fetching) ?
                       (decode_wait || (mem_ack && mem_error)) : mem_ack;
    wire raw_request, raw_read, raw_write;
    assign mem_request=raw_request && !decode_wait;
    assign mem_read=raw_read && !decode_wait;
    assign mem_write=raw_write && !decode_wait;
    always @(posedge clk)begin
        if(reset || ROM_DECODE==0)decode_wait<=0;
        else decode_wait<=fetch_capture;
    end
    wire memory_op = fetching || reading || writing;
    wire [4:0] a_select = uword[30:26];
    wire [4:0] b_select = uword[25:21];
    function [3:0] select_register;
        input [4:0] selector;
        input [5:0] registers;
        begin
            if (selector[4])
                select_register = {1'b0, selector[0] ? registers[2:0] : registers[5:3]} |
                                  {3'b0,selector[1]};
            else select_register = selector[3:0];
        end
    endfunction
    wire [3:0] a = select_register(a_select,{ir[8:6],ir[2:0]});
    wire [3:0] b = select_register(b_select,{ir[8:6],ir[2:0]});
    wire [15:0] read_a, read_b, result;
    wire [3:0] nzvc;
    reg [15:0] d;
    reg [1:0] fault_latched;
    wire [1:0] bus_fault;
    wire complete;
    wire running = !reset && !stopped;
    wire ready = !memory_op || complete;
    wire step = running && ready && bus_fault==0;
    // Faults redirect the microstore without committing the failed operation.
    // A second fault during vector/frame construction remains terminal.
    wire fault_redirect = running && bus_fault!=0 && !frame_active;
    wire advance = running && ready && !(frame_active && bus_fault!=0);
    // v3 FETCH encodes ADD/AD/RF/TWO directly. Other control words do not
    // commit datapath results, so their overlapping ALU fields are don't-care.
    wire [3:0] operation = uword[34:31];
    wire [2:0] pair = uword[20:18];
    wire [2:0] destination = (control && !fetching) ? 3'd0 : uword[17:15];
    wire [1:0] flags = control ? 2'd0 : uword[14:13];
    // Among accepted high-bit opcodes, SUB and branches remain word-sized.
    // Unsupported opcodes enter a trap; recheck this qualifier when
    // enabling new high-bit system/EIS classes.
    wire byte_instruction = ir[15] && ir[14:12]!=3'd6 && (|ir[14:11]);
    // EA and ordinary RF temporaries are always word. Operand writes and
    // NZV/NZVC flag commits select byte arithmetic for a byte instruction.
    wire byte_operation = !control && byte_instruction &&
                          (flags==2'd1 || flags==2'd2 || destination==3'd5 || destination==3'd6);
    always @* begin
        case (uword[12:10])
            3'd0: d = (uword[7] && uword[9:8]!=1) ? {7'b0,1'b1,!service_ready[1],debug_trace,debug_wait,debug_context,debug_pending,debug_enabled,service_ready} : 16'b0;
            3'd1: d = 1;
            3'd2: d = 2;
            3'd3: d = (byte_instruction && a<4'd6) ? 16'd1 : 16'd2;
            3'd4: d = mdr;
            // DISP is signed branch byte or unsigned SOB six-bit offset.
            // MARK also selects DISP; its IR[7:6]=0 gives an unsigned six-bit offset.
            3'd5: d = {{7{ir[7] && !ir[14]}},(ir[7:6] & {2{!ir[14]}}),ir[5:0],1'b0};
            3'd6: d = {8'b0,uword[7:0]};
            3'd7: d = psw;
        endcase
    end
    uj11_datapath dp(.clk(clk),.reset(reset),.enable(step),.a(a),.b(b),
        .operation(operation),.pair(pair),.destination(destination),.d(d),
        .carry(psw[0]),.byte_mode(byte_operation),.read_a(read_a),.read_b(read_b),.result(result),.q(q),
        .nzvc(nzvc),.rf_write(debug_rf_write),.writeback(debug_rf_data));
    uj11_psw status(.clk(clk),.reset(reset),.enable(step),.update(flags),
        .nzvc(nzvc),.value(result),.psw(psw));
    uj11_mem memory(.active(memory_op && !reset && !stopped),
        .writing(writing),.byte_access(control && (uword[6] || (uword[3] && byte_instruction)) && !fetching),
        .address(read_a),.data(read_b),.ack(effective_ack),.error(mem_error && !decode_wait),
        .request(raw_request),.read(raw_read),.write(raw_write),.byte_word(mem_byte),
        .addr(mem_addr),.write_data(mem_write_data),.complete(complete),.fault(bus_fault));
    // CJUMP and a memory operation are mutually exclusive. Its current
    // bus-error predicate is therefore always zero. Fault redirect/repair
    // remain connected to the real fault; no error is masked or delayed.
    uj11_microseq seq(.clk(clk),.reset(reset),.enable(advance),.uword(uword),.ir(ir),
        .nzvc(psw[3:0]),.dispatch_address(service_dispatch),.address_odd(read_a[0]),
        .byte_instruction(byte_instruction),.selected_a(a),.q0(q[0]),
        .loop_zero(1'b0),.bus_error(1'b0),.a_one(read_a==16'd1),.irq_pending(irq_pending),.trace_pending(trace_pending),.fault_target(service_mode ? 10'h2b6 : 10'h015),.debug_pending(debug_wanted),.wait_return(wait_return),.step_return(service_leave && ir[2]),.fault_redirect(fault_redirect),.fault_repair(fault_repair),.upc(debug_upc),.next_address(next_address));
`ifdef UJ11_VENDOR_ROM
    uj11_rom rom(.clk(clk),.enable(reset || advance),.address(next_address),.data(uword));
`else
    uj11_rom #(.IMAGE("build/hardware/m0.mem")) rom(
        .clk(clk),.enable(reset || advance),.address(next_address),.data(uword));
`endif
    assign dispatch_ir = fetching ? mem_read_data : ir;
    assign stopped = (fault_latched!=0) || stopping;
    assign fault_code = fault_latched!=0 ? fault_latched : stopping ? 2'd3 : 2'd0;
    assign debug_uword = uword;
    assign debug_rf_address = b;
    always @(posedge clk) begin
        if (reset) begin
            ir <= 0;
            mdr <= 0;
            fault_latched <= 0;
            retire <= 0;
            wait_seen <= 0;
            irq_active <= 0;
            frame_active <= 1;
            fault_repair <= 0;
            trace_latched <= 0;
        end else begin
            retire <= step && ((alu_boundary && irq_active==0) || (wait_command && !wait_seen) || (service_leave && ir[2]));
            if (step) wait_seen <= wait_command;
            if (step && service_leave) begin
                trace_latched <= debug_context ? debug_trace : psw[4];
                if(wait_return) wait_seen<=1;
            end
            // These READ continuations contain exactly one architectural +1/+2.
            // Defer only that delta until after a failed bus edge; preserve the
            // successful stream-read/PC-update order and its prefetch hit.
            if (fault_redirect) fault_repair <= reading && uword[2];
            else if (step) fault_repair <= 0;
            if (fault_redirect) irq_active <= 2'd2;
            else if (trace_ack) irq_active <= 2'd3;
            else if (irq_ack) irq_active <= 2'd1;
            else if (step && alu_boundary) irq_active <= 0;
            // Board firmware service may use a private vector in 16-bit ROM.
            // The default 8-bit external interrupt-vector interface is unchanged.
            if (irq_ack) mdr <= resolved_vector;
            if (bus_fault!=0 && frame_active) fault_latched <= bus_fault;
            // Guard incomplete entry/context and fault-vector construction through
            // the first successful handler opcode fetch. A fault while guarded
            // remains terminal; SEL174/274 escalation is not implemented.
            if ((step && (trap_command || service_enter)) || (fault_redirect && service_mode)) frame_active <= 1;
            else if (service_mode ? ((step && fetching && ROM_DECODE==0) || fetch_capture) : (step && alu_boundary)) frame_active <= 0;
            if ((step && (reading || (fetching && ROM_DECODE==0))) || fetch_capture) mdr <= mem_read_data;
            if ((step && fetching && ROM_DECODE==0) || fetch_capture) begin
                ir <= mem_read_data;
                trace_latched <= psw[4];
            end
        end
    end
endmodule
