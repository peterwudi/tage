`timescale 1ns / 100ps 

module test(); 
reg					clk;
reg					insnMem_wren = 1'b0;
reg	[31:0]		insnMem_data_w = 32'b0;
reg	[7:0]			insnMem_addr_w = 8'b11111111;
reg	[31:0]		fetch_bpredictor_PC;
reg					fetch_redirect = 0;
reg	[31:0]		fetch_redirect_PC = 'b0;


reg					soin_bpredictor_stall = 1'b0;

wire					bpredictor_fetch_p_dir;

reg					execute_bpredictor_update = 1'b0;
reg	[31:0]		execute_bpredictor_PC4 = 32'd128;
reg	[31:0]		execute_bpredictor_target = 32'b0;
reg					execute_bpredictor_dir = 1'b1;
reg					execute_bpredictor_miss = 1'b0;
reg	[95:0]		execute_bpredictor_data = 'hFFFF;
	
reg	[31:0]		soin_bpredictor_debug_sel = 2'b00;

reg					reset = 1'b0;
reg					execute_bpredictor_recover_ras = 0;
reg	[3:0]			execute_bpredictor_meta = 'b0;



bpredTop dut(.*);


//clock pulse with a 20 ns period 
always begin   
   #5  clk = ~clk; 
end

initial begin 
	$timeformat(-9, 1, " ns", 6); 
	clk = 1'b0;    // time = 0
	reset = 1'b1;
	
	@(negedge clk);
	reset = 1'b0;
	for (int i = 0; i < 32; i++) begin
		@(negedge clk);
	end
	
	$stop(0);
	
	// pc <= 32'h0;
	// beq, I-type, PC <- PC + 4 + IMM16
	// IMM16 = dec 0016
	// btb[1] = 16
	//insn <= 32'b10000100110;
	
	//#15
	//pc <= 32'd16;
	
	// call, J-type, PC <- IMM26 << 2
	// call IMM26 = dec 8, calculated result = dec 32
	// btb[4] = 32
	//insn <= 32'b1000000000;

	//#10
	// callr, R-type, use btb
	// IMM16 = dec 1234
	// pc <= 32'd32;
	// insn <= 32'h3EB43A;
	
	// btb[8] = 48
	//insn <= 32'b 00001000001111101110100000111010;
/*	
	#10
	// bne, I-type, IMM16 = dec 4936, PC <- PC + 4 + IMM16
	//insn <= 32'b00000000000000010011010010011110;
	
	// IMM16 = 16
	insn <= 32'b10000011110;
	
	#10
	// bltu, I-type, IMM16 = dec 4936, PC <- PC + 4 + IMM16
	//pc <= 32'h0;
	insn <= 32'b00000000000000010011010010110110;
*/	
	//#10
	//taken <= 1'b0;
	//insn <= 32'h0;
	
	
end



endmodule

