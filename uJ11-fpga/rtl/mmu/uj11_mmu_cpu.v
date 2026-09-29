`timescale 1ns/1ps
// Independent HC7000 execution profile. The released MMU-less engine is not
// instantiated: VM2 services and bank-select pins do not exist in this core.
module uj11_mmu_cpu #(
    parameter MICROCODE="build/hc7000-mmu-hardware/microcode.mem",
    parameter [15:0] BOOT_PC=16'o004000
)(
    input wire clk, reset, halt_button,
    input wire irq_valid,
    input wire [2:0] irq_priority,
    input wire [15:0] irq_vector,
    output wire irq_ack,
    output reg peripheral_reset,
    output wire mem_request, mem_write, mem_byte,
    output reg mem_lock,
    output wire [21:0] mem_address,
    output wire [15:0] mem_write_data,
    input wire mem_ready, mem_error,
    input wire [15:0] mem_read_data,
    output reg console_active,
    output wire waiting,
    output reg retire,
    output reg [15:0] psw,ir,
    output wire [15:0] mmr0,mmr1,mmr2,mmr3,
    output reg [11:0] upc,
    output wire [53:0] uword,
    output wire [15:0] pc,
    output wire [15:0] debug_register_data,
    input wire [4:0] debug_register_address
);
    wire [53:0] instruction;
    // The generated ROM supplies a build-time constant so the decoder and
    // synthesized FPP hardware always match the selected microcode image.
    wire fpp_enabled,pipeline_enabled;
    uj11_mmu_rom #(.IMAGE(MICROCODE)) microstore(.clk(clk),.enable(reset || advance),
        .address(next),.data(instruction),.fpp_enabled(fpp_enabled),.pipeline_enabled(pipeline_enabled));
    reg [53:0] execution_word;
    always @(posedge clk)execution_word<=instruction;
    assign uword=pipeline_enabled ? execution_word : instruction;
    wire control=uword[35];
    wire [3:0] command=uword[34:31];
    reg [5:0] prepared_control /* synthesis syn_preserve=1 */;
    always @(posedge clk)prepared_control<={
        instruction[35] && instruction[34:31]==2,
        instruction[35] && instruction[34:31]==11,
        instruction[35] && instruction[34:31]==12,
        instruction[35] && instruction[34:31]==14,
        instruction[35] && instruction[34:31]==15,
        instruction[35] && instruction[34:31]==11 && instruction[40] && instruction[39:37]!=5};
    wire fetching=pipeline_enabled ? prepared_control[5] : control && command==2;
    wire reading=pipeline_enabled ? prepared_control[4] : control && command==11;
    wire writing=pipeline_enabled ? prepared_control[3] : control && command==12;
    wire memory_op=fetching || reading || writing;
    wire wait_op=pipeline_enabled ? prepared_control[2] : control && command==14;
    wire trap_op=pipeline_enabled ? prepared_control[1] : control && command==15;
    wire [11:0] target={uword[53],uword[36],uword[20:11]};
    function [3:0] select_register(input [4:0] sel,input [5:0] registers);
        select_register=sel[4] ? {1'b0,sel[0] ? registers[2:0] : registers[5:3]} | {3'b0,sel[1]} : sel[3:0];
    endfunction
    wire [3:0] a=select_register(uword[30:26],{ir[8:6],ir[2:0]}), b=select_register(uword[25:21],{ir[8:6],ir[2:0]});
    wire [3:0] prepare_a=select_register(instruction[30:26],{ir[8:6],ir[2:0]});
    wire [3:0] prepare_b=select_register(instruction[25:21],{ir[8:6],ir[2:0]});
    wire prepare_previous=!instruction[35] && instruction[39];
    wire prepare_direct=instruction[35] && instruction[34:31]==11 && instruction[40] && instruction[39:37]!=5;
    wire byte_instruction=ir[15] && ir[14:12]!=6 && |ir[14:11] && ir[15:12]!=15 &&
        (ir & 16'o077700)!=16'o6500 && (ir & 16'o077700)!=16'o6600;
    wire direct_read=pipeline_enabled ? prepared_control[0] : reading && uword[40] && uword[39:37]!=5;
    wire [2:0] destination=direct_read ? 3'd1 : control && !fetching ? 3'd0 : uword[17:15];
    wire [1:0] flags=control ? 2'd0 : uword[14:13];
    wire byte_operation=!control && byte_instruction &&
        (flags==1 || flags==2 || destination==5 || destination==6);
    wire immediate=uword[12:10]==6;
    wire use_uflags=uword[40];
    wire [3:0] external_d=uword[44:41];
    reg [3:0] uflags;
    wire [3:0] condition_flags=use_uflags ? uflags : psw[3:0];
    reg [15:0] mdr,saved_psw,instruction_pc;
    reg [15:0] d;
    always @* begin
        case(instruction[12:10])
            0:d=0;
            1:d=1;
            2:d=2;
            3:d=fpp_enabled && ir[15:12]==15 ? (prepare_a==7 && ir[5:3]==2 ? 16'd2 : {12'b0,fp_length}) : byte_instruction && prepare_a<6 ? 16'd1 : 16'd2;
            4:d=mdr;
            5:d={{7{ir[7] && !ir[14]}},(ir[7:6] & {2{!ir[14]}}),ir[5:0],1'b0};
            6:d={instruction[52:45],instruction[7:0]};
            7:d=psw;
        endcase
        case(instruction[44:41])
            1:d=ir; 2:d=saved_psw; 3:d=mmr0; 4:d=mmr1;5:d=mmr2;6:d=mmr3;7:d=BOOT_PC;8:d=instruction_pc;
            default:begin end
        endcase
        if(prepare_direct)d=mdr;
    end
    reg [15:0] prepared_d;
    always @(posedge clk)prepared_d<=d;
    // A direct load consumes MDR only after its memory response arrives.
    wire [15:0] execution_d=pipeline_enabled ? (direct_read ? mdr : prepared_d) : d;
    wire [15:0] read_a,read_b,result,q,writeback;
    wire [3:0] nzvc;
    wire unused_datapath=^{q[15:1],writeback[15:5]};
    wire rf_initialized,fp_initialized,rf_write;
    wire initialized=rf_initialized && (!fpp_enabled || fp_initialized);
    // The fast-clock profile separates RF address/read from execution. Only
    // the first cycle of each microinstruction prepares operands; memory
    // transactions still use their existing ready/abort handshake.
    reg operands_valid;
    wire operands_ready=!pipeline_enabled || operands_valid;
    always @(posedge clk)begin
        if(reset || advance)operands_valid<=0;
        else if(initialized)operands_valid<=1;
    end
    // Direct internal loads use MDR as a pipeline register. Otherwise the
    // path RF address -> internal register mux -> ALU -> RF/MMR crosses
    // two register-file reads and exceeds the 24 MHz MachXO2 budget.
    reg turnaround, memory_started, direct_pending;
    wire [2:0] space=uword[39:37];
    wire internal_access=(reading || writing) && space==6;
    wire bus_cycle=memory_op && !internal_access;
    wire request=initialized && operands_ready && bus_cycle && !reset && !turnaround;
    wire [15:0] internal_address=uword[42] ? 16'he0 : uword[41] ? {uword[52:45],uword[7:0]} : read_a;
    wire [15:0] internal_write_data=uword[42] ? {uword[52:45],uword[7:0]} : read_b;
    wire fp_selected=fpp_enabled && internal_access && internal_address[15:6]==10'd2;
    wire wide_selected=fpp_enabled && internal_access && internal_address[15:6]==10'd3;
    wire wide_ready;
    wire [15:0] wide_data;
    uj11_mmu_fp_datapath fp_datapath(.clk(clk),.reset(reset),
        .request(initialized && operands_ready && wide_selected && !turnaround),.writing(writing),
        .address(internal_address[5:1]),.write_data(internal_write_data),.read_data(wide_data),.ready(wide_ready));
    wire fp_ready;
    wire [15:0] fp_read_data;
    uj11_mmu_fp_state fp_state(.clk(clk),.reset(reset),
        .request(initialized && operands_ready && fp_selected && !turnaround),.writing(writing),
        .address(internal_address[5:1]),.write_data(read_b),.read_data(fp_read_data),
        .ready(fp_ready),.initialized(fp_initialized));
    wire internal_ready=initialized && operands_ready && internal_access && !turnaround &&
        (fp_selected ? fp_ready : wide_selected ? wide_ready : 1'b1);
    wire translation_ready;
    wire [2:0] translation_fault;
    wire [15:0] translation_data;
    wire complete=internal_access ? internal_ready : request && translation_ready;
    // MMU ready/fault are registered responses. memory_started holds the
    // transaction identity, avoiding a path through the current READ decode
    // and request fanout into the microsequencer's exception selection.
    wire response_fault=memory_started && translation_ready && !turnaround;
    wire [2:0] fault=(pipeline_enabled ? response_fault : complete && !internal_access) ? translation_fault : 3'd0;
`ifndef SYNTHESIS
    always @(posedge clk)if(!reset && initialized && pipeline_enabled)
        if(fault !== (complete && !internal_access ? translation_fault : 3'd0))
            $fatal(1,"MMU response fault lost transaction alignment");
`endif
    wire memory_done=direct_pending || (complete && (!direct_read || fault!=0));
    wire step=initialized && operands_ready && !reset && (!memory_op || memory_done) && fault==0;
    wire advance=initialized && operands_ready && !reset && (!memory_op || memory_done);
    wire previous_rf=!control && uword[39];
    // J-11 FP addressing commits R0-R6 auto-updates only after successful
    // operand transfers. Forward a pending value for predecrement EA, and
    // discard it on abort. PC stream/pointer increments remain immediate.
    wire fp_defer=fpp_enabled && ir[15:12]==15 && !control && uword[37] && b<7;
    // Architectural RF commits never depend on the wide scratch ready mux.
    // Internal GPR writes are single-cycle accesses by definition (<0x40).
    wire local_step=initialized && operands_ready && !reset && !memory_op;
    wire datapath_step=local_step || (initialized && operands_ready && !reset &&
        (direct_pending || (fetching && request && translation_ready && fault==0)));
    wire debug_write=initialized && operands_ready && !reset && !turnaround && writing &&
        internal_access && internal_address<64;
    wire [4:0] rf_debug_address=internal_access ? internal_address[5:1] : debug_register_address;
    wire [15:0] rf_debug_data;
    // PC has a dedicated asynchronous read for instruction-start metadata and
    // diagnostics; it is not an additional write port.
    uj11_mmu_datapath dp(.clk(clk),.reset(reset),.enable(datapath_step),.a(prepare_a),.b(prepare_b),
        .operation(direct_read ? 4'd0 : command),.pair(direct_read ? 3'd6 : uword[20:18]),.destination(destination),.d(execution_d),
        .carry(condition_flags[0]),.byte_mode(byte_operation),.psw(psw),.previous(prepare_previous),.pipeline_enabled(pipeline_enabled),
        .initialized(rf_initialized),.read_a(read_a),.read_b(read_b),.result(result),.q(q),
        .defer_write(fp_defer),.commit_deferred(local_step && !console_active && event_active==0 &&
            (boundary || (trap_op && ir[15:12]==15))),
        .discard_deferred(fault!=0 || (step && (fetching || trap_op))),
        .nzvc(nzvc),.rf_write(rf_write),.writeback(writeback),
        .debug_write(debug_write),.debug_address(rf_debug_address),.debug_data(read_b),
        .debug_read_data(rf_debug_data),.pc(pc));
    assign debug_register_data=rf_debug_data;
    reg [3:0] fp_length;
    reg [5:0] physical_high;
    reg [2:0] console_space;
    reg [15:0] console_address;
    reg [5:0] console_high;
    reg [1:0] console_kind;
    reg [15:0] internal_data;
    always @* begin
        if(wide_selected)internal_data=wide_data;
        else if(fp_selected)internal_data=fp_read_data;
        else if(internal_address<64)internal_data=rf_debug_data;
        else case(internal_address)
            16'h40:internal_data=psw;
            16'h42:internal_data={10'b0,physical_high};
            16'h44:internal_data={13'b0,console_space};
            16'h46:internal_data=mmr0;
            16'h48:internal_data=mmr1;
            16'h4a:internal_data=mmr2;
            16'h4c:internal_data=mmr3;
            16'h50:internal_data=console_address;
            16'h52:internal_data={10'b0,console_high};
            16'h54:internal_data={14'b0,console_kind};
            16'h58:internal_data=fpp_enabled ? {12'b0,fp_length} : 16'b0;
            16'h5a:internal_data={15'b0,fpp_enabled};
            default:internal_data=0;
        endcase
    end
    wire stream=reading && uword[5] && a==7;
    wire previous_memory=(reading || writing) && (space==3 || space==4);
    // Space 7 is ODT's selected space while in console; CSM uses it to write
    // supervisor D-space before committing the architectural mode/stack.
    wire [1:0] previous_mode=psw[13:12]==2 ? 2'd3 : psw[13:12];
    wire [1:0] access_mode=(reading || writing) && space==2 ? 2'd0 :
        previous_memory ? previous_mode : space==7 ? (console_active ? console_space[2:1] : 2'd1) : psw[15:14];
    // J11 MFPI user -> user reads UD even though the opcode names I space.
    // MTPI still writes UI; the exception applies only to the read direction.
    wire access_data=space==2 ? 1'b1 : previous_memory ?
        (space==4 || ir[15] || (reading && psw[15:14]==3 && previous_mode==3)) :
        space==7 ? (console_active ? console_space[0] : writing) : !(fetching || stream || space==1 || (a==9 && ir[5:0]==6'o27));
    // The emergency stack uses physical low memory even when the failed
    // kernel stack mapping is nonresident. Ordinary instructions cannot
    // select this space; it is private to ODT and the red-stack microcode.
    wire access_physical=(reading || writing) && space==5 && (console_active || red_active);
    wire byte_access=memory_op && !fetching && (uword[6] || (uword[3] && byte_instruction));
    wire [15:0] lane_data=byte_access && read_a[0] ? {read_b[7:0],8'b0} : read_b;
    reg delta_recorded;
    wire read_delta=request && !memory_started && reading && uword[2] && !(fpp_enabled && ir[15:12]==15 && a<7);
    wire alu_delta=step && !control && uword[37] && rf_write && b<8 && !delta_recorded && !fp_defer;
    wire [4:0] read_amount=byte_instruction && a<6 && uword[3] ? 5'd1 : 5'd2;
    // The assembler restricts delta=1 to R := R +/- D. Compute that
    // displacement directly, avoiding ALU -> RF merge -> subtract -> MMR1.
    wire [4:0] alu_amount=command[2] ? -execution_d[4:0] : execution_d[4:0];
    wire [15:0] mapped_data;
    wire mapped_request,mapped_write,mapped_byte,mapped_ready,mapped_error;
    wire [21:0] mapped_address;
    wire [15:0] mapped_write_data;
    uj11_mmu mmu(.clk(clk),.reset(reset),.peripheral_reset(peripheral_reset),
        .request(request),.writing(writing),.byte_access(byte_access),.virtual_address(read_a),
        .write_data(lane_data),.mode(access_mode),.data_space(access_data),
        .physical(access_physical),.console(console_active),.physical_address({red_active ? 6'b0 : uword[40] ? 6'h3f : physical_high,read_a}),
        .instruction_start(request && !memory_started && fetching),.instruction_pc(pc),
        .delta_valid(!console_active && (read_delta || alu_delta)),
        .delta_register(read_delta ? a[2:0] : b[2:0]),.delta_amount(read_delta ? read_amount : alu_amount),
        .ready(translation_ready),.fault(translation_fault),.read_data(translation_data),
        .bus_request(mapped_request),.bus_write(mapped_write),.bus_byte(mapped_byte),
        .bus_address(mapped_address),.bus_data(mapped_write_data),
        .bus_ready(mapped_ready),.bus_error(mapped_error),.bus_read_data(mapped_data),
        .mmr0(mmr0),.mmr1(mmr1),.mmr2(mmr2),.mmr3(mmr3));
    wire psw_selected={mapped_address[21:1],1'b0}==22'o17777776;
    wire cpuerr_selected={mapped_address[21:1],1'b0}==22'o17777766;
    wire pirq_selected={mapped_address[21:1],1'b0}==22'o17777772;
    wire cpu_csr_selected=psw_selected || cpuerr_selected || pirq_selected;
    reg [7:2] cpu_errors;
    reg [7:1] pirq_requests;
    wire [15:0] cpuerr={8'b0,cpu_errors,2'b0};
    wire [2:0] pirq_priority=pirq_requests[7] ? 3'd7 : pirq_requests[6] ? 3'd6 :
        pirq_requests[5] ? 3'd5 : pirq_requests[4] ? 3'd4 : pirq_requests[3] ? 3'd3 :
        pirq_requests[2] ? 3'd2 : pirq_requests[1] ? 3'd1 : 3'd0;
    wire [15:0] pirq={pirq_requests,1'b0,pirq_priority,1'b0,pirq_priority,1'b0};
    assign mem_request=mapped_request && !cpu_csr_selected;
    assign mem_address=mapped_address;
    assign mem_write=mapped_write;
    assign mem_byte=mapped_byte;
    assign mem_write_data=mapped_write_data;
    assign mapped_ready=cpu_csr_selected ? mapped_request : mem_ready;
    assign mapped_error=!cpu_csr_selected && mem_error;
    assign mapped_data=psw_selected ? psw : cpuerr_selected ? cpuerr : pirq_selected ? pirq : mem_read_data;
    // Internal CPU CSRs acknowledge locally in the request cycle. Do not
    // route the unrelated external-peripheral ready mux into PSW writes.
    wire psw_write=mapped_request && mapped_write && psw_selected;
    wire [15:0] psw_lane_mask=mapped_byte ? (mapped_address[0] ? 16'hff00 : 16'h00ff) : 16'hffff;
    wire [15:0] explicit_value=(psw & ~psw_lane_mask) | (mapped_write_data & psw_lane_mask);
    reg explicit_psw,trap_frame,trap_loading,trace_latched,halt_pending,single_step,wait_seen,console_wait;
    reg [1:0] event_active;
    reg fault_repair;
    reg yellow_pending,stack_active,red_active;
    // Only the microcoded predecrement/push operations check the kernel
    // stack, not arbitrary arithmetic on SP. J11 has a fixed 0400 limit
    // (UG 1.8), not the programmable STKLIM of the 11/45 and 11/70.
    // A predecrement by STEP/TWO crosses the yellow zone exactly when
    // (SP - amount) is in 000000..000377, excluding the wrap below zero.
    // Test the old SP directly so the ALU/writeback mux is not on this path.
    wire stack_low=read_a[15:9]==0 && (read_a[8] != (read_a[7:0]>=execution_d[7:0]));
    wire stack_check=local_step && rf_write && !console_active && !stack_active && !red_active &&
        psw[15:14]==0 && !previous_rf && a==6 && b==6 && command==4 && uword[20:18]==2 &&
        (uword[12:10]==2 || uword[12:10]==3) && stack_low;
    wire red_fault=fault!=0 && !console_active && !red_active && trap_frame && writing && a==6 && psw[15:14]==0;
    wire boundary=!control && (uword[9:8]==2 || (uword[9:8]==3 && read_a==1));
    wire debug_pending=!console_active && (halt_pending || (single_step && !wait_op));
    // DEC UG table 1-8: the highest level wins; PIR precedes external IRQ
    // at the same level. A software grant must never acknowledge a device.
    wire pirq_wins=pirq_priority!=0 && (!irq_valid || pirq_priority>=irq_priority);
    wire [2:0] interrupt_priority=pirq_wins ? pirq_priority : irq_priority;
    wire irq_pending=!console_active && (pirq_wins || irq_valid) && interrupt_priority>psw[7:5] && !event_active[1];
    wire yellow_ready=!console_active && yellow_pending && !stack_active && !red_active;
    wire return_trace=uword[0] && !immediate;
    wire trace_pending=!console_active && event_active==0 &&
        (wait_op ? wait_seen && psw[4] : return_trace ? psw[4] && !ir[2] : trace_latched);
    wire interrupt_ack=step && (boundary || wait_op) && irq_pending && !yellow_ready && !trace_pending && !debug_pending;
    assign irq_ack=interrupt_ack && !pirq_wins;
    wire yellow_ack=step && (boundary || wait_op) && yellow_ready && !trace_pending && !debug_pending;
    wire trace_ack=step && (boundary || wait_op) && trace_pending && !debug_pending;
    wire debug_ack=step && (boundary || wait_op) && debug_pending;
    assign waiting=wait_op && operands_ready && !reset;
    wire [15:0] incoming=byte_access ? (read_a[0] ? {8'b0,translation_data[15:8]} : {8'b0,translation_data[7:0]}) : translation_data;
    // Opcode fetches are always words. Bypass the operand byte-lane mux so
    // RF address/bit 0 cannot extend the fetch-to-dispatch timing path.
    wire [15:0] decode_ir=fetching ? translation_data : ir;
    wire [11:0] dispatch;
    uj11_mmu_decode decoder(.ir(decode_ir),.mode(psw[15:14]),.csm_enabled(mmr3[3]),
        .fpp_enabled(fpp_enabled),.entry(dispatch));
    wire [11:0] boundary_target=debug_pending ? 12'h500 : trace_pending ? 12'h024 : yellow_ready ? 12'h015 : irq_pending ? 12'h013 : 12'h020;
    wire [7:0] predicates={1'b0,1'b0,q[0],condition_flags[3:0],1'b1};
    wire condition=predicates[uword[9:7]] ^ uword[10];
    wire [2:0] dispatch_bits=(ir[11:9] & {3{command==5}}) |
        (ir[5:3] & {3{command==6}}) |
        ({1'b0,ir[5:3]==0,ir[11:9]==0} & {3{command==7}}) |
        ({1'b0,read_a[0],byte_instruction} & {3{command==8}}) |
        ({2'b0,~(a[2] & a[1])} & {3{command==9}});
    reg [11:0] links[0:3];reg [2:0] link_sp;
    // Bit 38 is an ALU extension; control memory words use it for space.
    // Share the existing return stack mux with a final ALU operation.
    wire micro_return=control ? command==3 : uword[38];
    wire [11:0] return_target=link_sp!=0 ? links[(link_sp-1'b1)&3] : 12'h7ff;
    reg [11:0] next;
    always @* begin
        next={target[11:3],target[2:0] | dispatch_bits};
        if(control)case(command)
            1:if(!condition)next=upc+1'b1;
            2,4:next=dispatch;
            3:next=return_target;
            10:if(link_sp==4)next=12'h7ff;
            13:next=upc;
            14:next=debug_pending || trace_pending || yellow_ready || irq_pending ? boundary_target : upc;
            default:begin end
        endcase
        else if(micro_return)next=return_target;
        else case(uword[9:8])
            0:next=upc+1'b1;
            1:next={upc[11:8],uword[7:0]};
            2:next=boundary_target;
            3:next=read_a==1 ? boundary_target : upc+1'b1;
        endcase
        if(control && command==0 && uword[2] && console_active && console_wait)next=12'h012;
        if(fault!=0)next=console_active ? 12'h6f0 : red_fault ? 12'h431 :
            (red_active || (trap_frame && !writing)) ? 12'h500 : uword[2] ? target : 12'h015;
        if(fault_repair)next=12'h015;
        if(reset)next=0;
    end
    wire rti_load=upc==12'h036;
    wire [15:0] rti_value=psw[15:14]==0 ? result :
        ((result | (psw & 16'o174000)) & ~16'o340) | (psw & 16'o340);
    // MTPS_APPLY loads PSW at 198. Its microcode preserves T and the high
    // byte; outside kernel the priority bits must also retain their value.
    wire [15:0] psw_load=rti_load ? rti_value :
        upc==12'h198 && psw[15:14]!=0 ? (result & ~16'o340) | (psw & 16'o340) : result;
    always @(posedge clk) begin
        if(reset || advance)upc<=next;
        if(reset)begin
            psw<=16'o340;ir<=0;mdr<=0;saved_psw<=0;uflags<=0;instruction_pc<=0;fp_length<=2;
            turnaround<=0;memory_started<=0;direct_pending<=0;peripheral_reset<=0;retire<=0;mem_lock<=0;
            delta_recorded<=0;physical_high<=6'h3f;console_space<=0;console_active<=0;
            console_address<=0;console_high<=0;console_kind<=0;
            explicit_psw<=0;trap_frame<=0;trap_loading<=0;trace_latched<=0;
            halt_pending<=0;single_step<=0;wait_seen<=0;console_wait<=0;event_active<=0;fault_repair<=0;link_sp<=0;
            yellow_pending<=0;stack_active<=0;red_active<=0;cpu_errors<=0;pirq_requests<=0;
        end else begin
            if(peripheral_reset)pirq_requests<=0;
            else if(mapped_request && mapped_write && pirq_selected && (!mapped_byte || mapped_address[0]))
                pirq_requests<=mapped_write_data[15:9];
            // CPUERR is sticky and clears on either byte or word write.
            // RESET leaves it intact; error causes are recorded before entry.
            if(mapped_request && mapped_write && cpuerr_selected)cpu_errors<=0;
            if(!console_active)begin
                if(fault==1)cpu_errors[6]<=1;
                if(fault==2)begin
                    if(&mapped_address[21:13])cpu_errors[4]<=1;
                    else cpu_errors[5]<=1;
                end
                if(step && fetching && incoming==0 && psw[15:14]!=0)cpu_errors[7]<=1;
            end
            if(stack_check)begin yellow_pending<=1;cpu_errors[3]<=1;end
            peripheral_reset<=step && control && command==0 && uword[0] && !console_active;
            retire<=step && boundary && event_active==0 && !console_active;
            turnaround<=complete;
            direct_pending<=complete && direct_read && fault==0;
            if(turnaround || !memory_op)memory_started<=0;
            else if(request)memory_started<=1;
            if(request && !memory_started && fetching)instruction_pc<=pc;
            if(read_delta)delta_recorded<=1;
            else if(step && !control)delta_recorded<=0;
            if(halt_button)halt_pending<=1;
            if(debug_ack)begin halt_pending<=0;single_step<=0;console_wait<=wait_op;end
            if(fault!=0)begin
                mem_lock<=0;
                if(!console_active)event_active<=2;mdr<=fault==3 ? 16'o250 : 16'o4;
                fault_repair<=!trap_frame && !console_active && reading && uword[2];
                link_sp<=0;
                if(red_fault)begin
                    red_active<=1;stack_active<=1;yellow_pending<=0;cpu_errors[2]<=1;trap_loading<=1;
                end
            end else begin
                if(complete && (reading || fetching))mdr<=internal_access ? internal_data : incoming;
                if(step)begin
                fault_repair<=0;
                wait_seen<=wait_op;
                if(fetching)begin
                    ir<=incoming;trace_latched<=psw[4];explicit_psw<=0;trap_frame<=0;console_wait<=0;
                    // Finish any in-flight DMA, then hold off new grants until
                    // the whole TSTSET completes (including the read/write gap).
                    mem_lock<=(incoming & 16'o177700)==16'o7200 && incoming[5:3]!=0;
                end
                if(boundary || trap_op)mem_lock<=0;
                if(boundary)begin event_active<=0;stack_active<=0;red_active<=0;end
                if(trace_ack)event_active<=3;
                if(interrupt_ack)begin event_active<=1;mdr<=pirq_wins ? 16'o240 : irq_vector;end
                if(yellow_ack)begin event_active<=2;mdr<=4;yellow_pending<=0;stack_active<=1;end
                // Explicit PSW writes suppress only that instruction's ALU flags.
                // An immediately accepted IRQ must still load its vector PSW.
                if(trap_op)begin link_sp<=0;trap_frame<=1;trap_loading<=1;saved_psw<=psw;explicit_psw<=0;end
                if(control && command==10 && link_sp<4)begin links[link_sp[1:0]]<=upc+1'b1;link_sp<=link_sp+1'b1;end
                if(micro_return && link_sp!=0)link_sp<=link_sp-1'b1;
                if(control && command==0 && uword[2:1]!=0)begin
                    console_active<=uword[2:1]==1;
                    if(uword[2:1]==1)begin red_active<=0;stack_active<=0;yellow_pending<=0;end
                    if(uword[2:1]!=1)begin single_step<=uword[2:1]==3;halt_pending<=0;end
                end
                if(flags!=0)begin
                    if(use_uflags)case(flags)
                        1:uflags[3:1]<=nzvc[3:1];2:uflags<=nzvc;3:uflags<=result[3:0];
                    endcase
                    else if(!console_active && !explicit_psw)case(flags)
                        1:psw[3:1]<=nzvc[3:1];
                        2:psw[3:0]<=nzvc;
                        3:begin
                            if(trap_loading)begin
                                // The vector supplies current mode (and thus
                                // the destination stack); only previous mode
                                // comes from the interrupted context.
                                psw<={result[15:14],saved_psw[15:14],result[11],2'b0,result[8:0]};trap_loading<=0;
                            end else psw<=psw_load & 16'hf9ff;
                        end
                    endcase
                end
                if(writing && internal_access)case(internal_address)
                    16'h40:psw<=read_b & 16'hf9ff;
                    16'h42:physical_high<=read_b[5:0];
                    16'h44:console_space<=read_b[2:0];
                    16'h50:console_address<=read_b;
                    16'h52:console_high<=read_b[5:0];
                    16'h54:console_kind<=read_b[1:0];
                    16'h58:if(fpp_enabled)fp_length<=read_b[3:0];
                    default:begin end
                endcase
            end
            end
            if(psw_write)begin
                psw<=(explicit_value & 16'hf9ef) | (psw & 16'h10);explicit_psw<=1;
            end
        end
    end
endmodule
