// Included by test_mmu_basic.py in the full CPU/SERV/SD/SRAM/UART harness.
// No register, memory or device-response substitution.
    integer ready_count=0,option_count=0;
    reg [47:0] basic_window=0;
    always @(posedge clk) if(dut.request && dut.ready && dut.writing &&
                            dut.address==22'o17777566)begin
        basic_window={basic_window[39:0],dut.write_data[7:0]};
        if(basic_window==48'h52454144590d)ready_count++;
        if(basic_window=="DUAL)?")option_count++;
    end
    task basic(input string command,input bit options=0);
        integer before_count;
        begin
            before_count=options ? option_count : ready_count;segment="";
            for(integer i=0;i<command.len();i++)send_byte(command[i]);send_byte(13);
            if(options)wait(option_count>before_count);else wait(ready_count>before_count);
            wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);
        end
    endtask
    task test_basic(input string name,input bit double_precision);
        begin
            basic({"RUN ",name},1);basic("A");
            basic("RUN B81TST");contains("CHECKS= 29  ERRORS= 0");contains("CP81 PASS");
            if(double_precision)begin basic("RUN B81DBL");contains("CP81 DOUBLE PASS");end
            basic("PRINT 1/0");contains("?DIVISION BY ZERO");
            basic("PRINT 2+2");contains(" 4 ");
            basic("PRINT SQR(-1)");contains("?NEGATIVE SQUARE ROOT");
            basic("PRINT 2+2");contains(" 4 ");
            basic("PRINT 1E30*1E30");contains("?FLOATING OVERFLOW");
            basic("PRINT 2+2");contains(" 4 ");
            basic("PRINT 1/0");contains("?DIVISION BY ZERO");
            basic("PRINT 2+2");contains(" 4 ");shell("BYE");
        end
    endtask
