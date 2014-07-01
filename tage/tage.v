
module tage
{
	input				clk,
	input				reset,
	input	[31:0]	pc,
	input	[11:0]	ch_i		[11:0],
	input [11:0]	ch_t0 	[11:0],
	input	[11:0]	ch_t1		[11:0],
	
	output			res
};

// Total storage for the submitted predictor
// TAGE		2-bit ctr, 3-bit u
// Table T0: 20Kbits (16K prediction + 4K hysteresis)
// Tables T1 and T2: 12Kbits each; 			1k entries, 7 tag
// Tables T3 and T4: 26 Kbits each; 			2k entries, 8 tag
// Table T5: 28Kbits; 								2k entries, 9 tag
// Table  T6: 30Kbits; 							2k entries, 10 tag
// Table T7: 16 Kbits  ;  						1k entries, 11 tag
// Tables T8 and  T9: 17 Kbits each,			1k entries, 12 tag
// Table T10: 18 Kbits;  							1k entries, 13 tag
// Table T11: 9,5 Kbits; 							512 entries, 14 tag
// Table T12 10 Kbits								512 entries, 15 tag
// 
// Total of storage for the TAGE tables: 239,5 Kbits
// Extra storage:  2*640 history bits + 2*16 path history bits + 
// 4 bits for  USE_ALT_ON_NA + 19 bits for the TICK counter + 
// 2 bits for Seed in the "pseudo-random" counter= 1,337
// Total storage= 239,5Kbits + 1,337 = 246585 bits = 240.8kbits = 30.1kB


wire	[11:0] gindex [11:0];

tageHash tageHash
{
	.clk		(clk),
	.reset	(reset),
	.ch_i		(ch_i),
	.ch_t0	(ch_t0),
	.ch_t1	(ch_t1),

	.gindex	(gindex)
}




// 3 types of BRAMs
// 18x1k, 16x2k, 20x512
mem18_1k T1(
	.clock (clk),
	.data(),
	.rdaddress(),
	.wraddress(),
	.wren(),
	.q()
);


always @(posedge clk) begin
	if (!reset) begin
		clock 	<= 0;
	end
	else begin
		if (clock == 1000)
		begin
			// Reached a epoch, reset
			clock		<= clock + 1;
	
	
	
	end
end


	
endmodule


module tageHash
{
	input					clk,
	input					reset,
	input		[11:0]	ch_i		[11:0],
	input		[11:0]	ch_t0 	[11:0],
	input		[11:0]	ch_t1		[11:0],

	output	[11:0]	gindex	[11:0]
};

parameter [4*11-1:0] M = {4'b}






endmodule

