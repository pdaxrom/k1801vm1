`timescale 1ns/1ps
// BK AY attachment: inverted word selects R0..R15, inverted byte writes data.
// Four-command asynchronous FIFO decouples native CPU writes from the fixed
// 48 MHz audio domain, which continues during turbo changes and disk waits.
module bk_audio #(
    // 1.7734 MHz AY master; reduced ratio for master/8 from 48 MHz.
    parameter TICK_NUMERATOR = 8867, TICK_DENOMINATOR = 1920000
) (
    input wire cpu_clk, clk48, reset, port_write,
    input wire [1:0] lanes,
    input wire [15:0] data,
    input wire beeper,
    output wire audio
);
    reg [8:0] commands[0:3];
    reg [2:0] write_pointer = 0, write_gray = 0, read_pointer = 0, read_gray = 0;
    reg [2:0] write_meta = 0, write_sync = 0, read_meta = 0, read_sync = 0 /* synthesis syn_preserve=1 */;
    reg [2:0] audio_reset_pipe = 7 /* synthesis syn_preserve=1 */;
    reg port_previous = 0;
    wire [2:0] write_next = write_pointer + 1'b1;
    wire [2:0] read_next = read_pointer + 1'b1;
    wire full = write_gray == {~read_sync[2:1],read_sync[0]};
    wire audio_reset = audio_reset_pipe[2];
    wire command_valid = write_sync != read_gray;
    wire [8:0] command = commands[read_pointer[1:0]];
    reg [3:0] selected = 0;
    reg [1:0] beeper_sync = 0 /* synthesis syn_preserve=1 */;
    localparam PHASE_BITS = $clog2(TICK_DENOMINATOR);
    reg [PHASE_BITS-1:0] phase = 0;
    wire tick = phase >= TICK_DENOMINATOR - TICK_NUMERATOR;
    wire [8:0] ay_pcm;
    reg [8:0] mixed = 0;
    reg [9:0] accumulator = 0;

    always @(posedge clk48 or posedge reset)
        if(reset) audio_reset_pipe <= 7;
        else audio_reset_pipe <= {audio_reset_pipe[1:0],1'b0};

    always @(posedge cpu_clk or posedge reset) begin
        if(reset) begin
            write_pointer <= 0; write_gray <= 0; port_previous <= 0; read_meta <= 0; read_sync <= 0;
        end else begin
            read_meta <= read_gray; read_sync <= read_meta;
            port_previous <= port_write;
            if(port_write && !port_previous && lanes != 0) begin
                if(!full) begin
                    commands[write_pointer[1:0]] <= {lanes == 3,~(lanes[0] ? data[7:0] : data[15:8])};
                    write_pointer <= write_next; write_gray <= (write_next >> 1) ^ write_next;
                end
`ifndef SYNTHESIS
                if(full) $fatal(1,"AY command FIFO overflow");
`endif
            end
        end
    end
    always @(posedge clk48) begin
        if(audio_reset) begin
            read_pointer <= 0; read_gray <= 0; write_meta <= 0; write_sync <= 0;
            selected <= 0; phase <= 0; beeper_sync <= 0; mixed <= 0; accumulator <= 0;
        end else begin
            write_meta <= write_gray; write_sync <= write_meta;
            beeper_sync <= {beeper_sync[0],beeper};
            if(command_valid) begin
                read_pointer <= read_next; read_gray <= (read_next >> 1) ^ read_next;
                if(command[8]) selected <= command[3:0];
            end
            if(tick) phase <= phase - (TICK_DENOMINATOR - TICK_NUMERATOR);
            else phase <= phase + TICK_NUMERATOR;
            // Maximum 3*127+128=509: no clipping in the nine-bit mixer.
            mixed <= ay_pcm + (beeper_sync[1] ? 9'd128 : 9'd0);
            accumulator <= {1'b0,accumulator[8:0]} + {1'b0,mixed};
        end
    end
    bk_ay psg(clk48,audio_reset,tick,command_valid && !command[8],selected,command[7:0],ay_pcm);
    assign audio = accumulator[9] && !reset && !audio_reset;
endmodule
