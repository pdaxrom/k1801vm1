module miter(input clk,reset,enable,stopped,
input [35:0] uword,input [15:0] ir,read_a,read_b,input [3:0] nzvc,selected_a,
input [9:0] dispatch_address,fault_target,
input effective_ack,mem_error,decode_wait,byte_instruction,address_odd,
q0,loop_zero,a_one,irq_pending,trace_pending,fault_redirect,fault_repair,step_return,
output ok);
wire [1:0] bus_fault; wire raw_request,raw_read,raw_write,mem_byte,complete;
wire [15:0] mem_addr,mem_write_data;
    wire control = uword[35];
    wire [3:0] command = uword[34:31];
    wire fetching = control && command==4'd2;
    wire reading = control && command==4'd11;
    wire writing = control && command==4'd12;
    wire memory_op = fetching || reading || writing;
    uj11_mem memory(.active(memory_op && !reset && !stopped),
        .writing(writing),.byte_access(control && (uword[6] || (uword[3] && byte_instruction)) && !fetching),
        .address(read_a),.data(read_b),.ack(effective_ack),.error(mem_error && !decode_wait),
        .request(raw_request),.read(raw_read),.write(raw_write),.byte_word(mem_byte),
        .addr(mem_addr),.write_data(mem_write_data),.complete(complete),.fault(bus_fault));
wire [9:0] gold_pc,gold_next;wire[10:0] gold_link;
gold gold_inst(.clk(clk),.reset(reset),.enable(enable),.uword(uword),.ir(ir),.nzvc(nzvc),.dispatch_address(dispatch_address),.fault_target(fault_target),.step_return(step_return),.address_odd(address_odd),.byte_instruction(byte_instruction),.selected_a(selected_a),.q0(q0),.loop_zero(loop_zero),.a_one(a_one),.irq_pending(irq_pending),.trace_pending(trace_pending),.fault_redirect(fault_redirect),.fault_repair(fault_repair),.bus_error(bus_fault!=0),.upc(gold_pc),.next_address(gold_next),.saved_link(gold_link));
wire [9:0] gate_pc,gate_next;wire[10:0] gate_link;
gate gate_inst(.clk(clk),.reset(reset),.enable(enable),.uword(uword),.ir(ir),.nzvc(nzvc),.dispatch_address(dispatch_address),.fault_target(fault_target),.step_return(step_return),.address_odd(address_odd),.byte_instruction(byte_instruction),.selected_a(selected_a),.q0(q0),.loop_zero(loop_zero),.a_one(a_one),.irq_pending(irq_pending),.trace_pending(trace_pending),.fault_redirect(fault_redirect),.fault_repair(fault_repair),.bus_error(1'b0),.upc(gate_pc),.next_address(gate_next),.saved_link(gate_link));
assign ok={gold_pc,gold_next,gold_link}=={gate_pc,gate_next,gate_link};
endmodule
