
module tage
(
	input				clk,
	input				reset,
	input	[29:0]	pc,
	input	[11:0]	ch_i			[11:0],
	input [11:0]	ch_t0 		[11:0],
	input	[11:0]	ch_t1			[11:0],
	input	[15:0]	phist,
	
	input	[19:0]	updateData,
	input	[11:0]	updateIndex,
	input	[11:0]	upWren,
	
	output	reg			dir,
	output	reg	[3:0]	provider
);

// Tag bits: 7, 7, 8, 8, 9, 10, 11, 12, 12, 13, 14, 15 
parameter [4*12-1:0]		tagBits = {	4'd15, 4'd14, 4'd13, 4'd12, 4'd12, 4'd11,
												4'd10, 4'd9, 4'd8, 4'd8, 4'd7, 4'd7};

parameter [15*12-1:0]	tagMask = {	15'h7FFF, 15'h3FFF, 15'h1FFF, 15'hFFF, 15'hFFF, 15'h7FF,
												15'h3FF, 15'h1FF, 15'hFF, 15'hFF, 15'h7F, 15'h7F};


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


wire	[11:0] tageIndex		[11:0];
wire	[19:0] tageTableData	[11:0];

gindex gindex(
	.clk			(clk),
	.reset		(reset),
	.pc			(pc),
	.ch_i			(ch_i),
	.phist		(phist),

	.tageIndex	(tageIndex)
);

wire [14:0]		tageTag	[11:0];

gtag gtag(
	.clk			(clk),
	.reset		(reset),
	.pc			(pc),
	.ch_t0		(ch_t0),
	.ch_t1		(ch_t1),

	.tageTag		(tageTag)
);

// 3 types of BRAMs
// 18x1k, 15x2k, 20x512
mem18_1k T1(
	.clock(clk),
	.data(updateData),
	.rdaddress(tageIndex[0]),
	.wraddress(updateIndex),
	.wren(upWren[0]),
	.q(tageTableData[0])
);

mem18_1k T2(
	.clock(clk),
	.data(updateData),
	.rdaddress(tageIndex[1]),
	.wraddress(updateIndex),
	.wren(upWren[1]),
	.q(tageTableData[1])
);

mem15_2k T3(
	.clock(clk),
	.data(updateData),
	.rdaddress(tageIndex[2]),
	.wraddress(updateIndex),
	.wren(upWren[2]),
	.q(tageTableData[2])
);

mem15_2k T4(
	.clock(clk),
	.data(updateData),
	.rdaddress(tageIndex[3]),
	.wraddress(updateIndex),
	.wren(upWren[3]),
	.q(tageTableData[3])
);

mem15_2k T5(
	.clock(clk),
	.data(updateData),
	.rdaddress(tageIndex[4]),
	.wraddress(updateIndex),
	.wren(upWren[4]),
	.q(tageTableData[4])
);

mem15_2k T6(
	.clock(clk),
	.data(updateData),
	.rdaddress(tageIndex[5]),
	.wraddress(updateIndex),
	.wren(upWren[5]),
	.q(tageTableData[5])
);

mem18_1k T7(
	.clock(clk),
	.data(updateData),
	.rdaddress(tageIndex[6]),
	.wraddress(updateIndex),
	.wren(upWren[6]),
	.q(tageTableData[6])
);

mem18_1k T8(
	.clock(clk),
	.data(updateData),
	.rdaddress(tageIndex[7]),
	.wraddress(updateIndex),
	.wren(upWren[7]),
	.q(tageTableData[7])
);

mem18_1k T9(
	.clock(clk),
	.data(updateData),
	.rdaddress(tageIndex[8]),
	.wraddress(updateIndex),
	.wren(upWren[8]),
	.q(tageTableData[8])
);

mem18_1k T10(
	.clock(clk),
	.data(updateData),
	.rdaddress(tageIndex[9]),
	.wraddress(updateIndex),
	.wren(upWren[9]),
	.q(tageTableData[9])
);

mem20_512 T11(
	.clock(clk),
	.data(updateData),
	.rdaddress(tageIndex[10]),
	.wraddress(updateIndex),
	.wren(upWren[10]),
	.q(tageTableData[10])
);

mem20_512 T12(
	.clock(clk),
	.data(updateData),
	.rdaddress(tageIndex[11]),
	.wraddress(updateIndex),
	.wren(upWren[11]),
	.q(tageTableData[11])
);

// Tag comparison
wire	[11:0]	hitBank;

genvar i;
generate
	for (i = 0; i < 12; i = i + 1) begin: tagComparison
		always @(*) begin
			hitBank[i] = (tageTag[i] == ((tageTableData[i] >> 3) & tagMask[15*(i+1)-1:15*i])) ? 1:0;
		end
	end
endgenerate


always @(*) begin
	if (hitBank[11]) begin
		dir 		= (tageTableData[11] >> 19);
		provider = 'd11;
	end
	else if (hitBank[10]) begin
		dir = (tageTableData[10] >> 18);
		provider = 'd10;
	end
	else if (hitBank[9]) begin
		dir = (tageTableData[9] >> 17);
		provider = 'd9;
	end
	else if (hitBank[8]) begin
		dir = (tageTableData[8] >> 16);
		provider = 'd8;
	end
	else if (hitBank[7]) begin
		dir = (tageTableData[7] >> 16);
		provider = 'd7;
	end
	else if (hitBank[6]) begin
		dir = (tageTableData[6] >> 15);
		provider = 'd6;
	end
	else if (hitBank[5]) begin
		dir = (tageTableData[5] >> 14);
		provider = 'd5;
	end
	else if (hitBank[4]) begin
		dir = (tageTableData[4] >> 13);
		provider = 'd4;
	end
	else if (hitBank[3]) begin
		dir = (tageTableData[3] >> 12);
		provider = 'd3;
	end
	else if (hitBank[2]) begin
		dir = (tageTableData[2] >> 12);
		provider = 'd2;
	end
	else if (hitBank[1]) begin
		dir = (tageTableData[1] >> 11);
		provider = 'd1;
	end
	else begin // if (hitBank[0])
		dir = (tageTableData[0] >> 11);
		provider = 'd0;
	end
end

	
endmodule


module gindex
(
	input					clk,
	input					reset,
	input		[29:0]	pc,
	input		[11:0]	ch_i			[11:0],
	input		[15:0]	phist,
	
	output	[11:0]	tageIndex	[11:0]
);

// abs(p->logg[bank] - bank) + 1
parameter [4*12-1:0]	logg_m_bank = {4'd5, 4'd3, 4'd1, 4'd2, 4'd3, 4'd4,
												4'd6, 4'd7, 4'd8, 4'd9, 4'd9, 4'd10};
												
// See module F
parameter [11*12-1:0]	loggMask = {11'h1FF, 11'h1FF, 11'h3FF, 11'h3FF, 11'h3FF, 11'h3FF,
												11'h7FF, 11'h7FF, 11'h7FF, 11'h7FF, 11'h3FF, 11'h3FF};
												
wire	[15:0]	fOut	[11:0];

F F(
	.clk			(clk),
	.reset		(reset),
	.A				(phist),

	.AxOut		(fOut)
);


genvar i;
generate
	for (i = 0; i < 12; i = i + 1) begin: indexComputation
		assign tageIndex[i] =	(pc ^ (pc >> logg_m_bank[4*(i+1)-1:4*i]) ^
										ch_i[i] ^ fOut[i]) & loggMask[11*(i+1)-1:11*i];
	end
endgenerate

endmodule

/*
// gindex computes a full hash of pc, ghist and phist
int gindex (my_predictor *p, address_t pc, int bank)
{
    int index;
    int M = (p->m[bank] > 16) ? 16 : p->m[bank];

        index =
        pc ^ (pc >> (abs(p->logg[bank] - bank) + 1)) ^
        p->ch_i[bank].comp ^ F (p, p->phist, M, bank);

    return (index & ((1 << (p->logg[bank])) - 1));
}
*/


module F
(
	input						clk,
	input						reset,
	input		[15:0]		A,
	
	output	[15:0]		AxOut [11:0]
);

// History length: 4, 6, 10, 16, 16, 16, 16, 16, 16, 16, 16, 16
parameter [16*12-1:0]	M = {	16'hFFFF, 16'hFFFF, 16'hFFFF, 16'hFFFF, 16'hFFFF, 16'hFFFF,
										16'hFFFF, 16'hFFFF, 16'hFFFF, 16'h3FF, 16'h3F, 16'hF};
// logg: log of number entries of the different tagged tables, LOGG = 11
// 10, 10, 11, 11, 11, 11, 10, 10, 10, 10, 9, 9
parameter [4*12-1:0]		logg = {	4'd9, 4'd9, 4'd10, 4'd10, 4'd10, 4'd10,
											4'd11, 4'd11, 4'd11, 4'd11, 4'd10, 4'd10};

parameter [11*12-1:0]	loggMask = {11'h1FF, 11'h1FF, 11'h3FF, 11'h3FF, 11'h3FF, 11'h3FF,
												11'h7FF, 11'h7FF, 11'h7FF, 11'h7FF, 11'h3FF, 11'h3FF};


wire	[15:0]	Ax_1	[11:0];
wire	[15:0]	A1		[11:0];
wire	[15:0]	A2_1	[11:0];
wire	[15:0]	A2_2	[11:0];
wire	[15:0]	Ax_2	[11:0];

genvar i;
generate 
	for (i = 0; i < 12; i = i + 1) begin: fComputation
		assign	Ax_1[i]	=	A & M[16*(i+1)-1:16*i];
		assign	A1[i]		=	Ax_1[i] & loggMask[11*(i+1)-1:11*i];
		assign	A2_1[i]	=	Ax_1[i] >> logg[4*(i+1)-1:4*i];
		assign	A2_2[i]	= 	((A2_1[i] << i) & loggMask[11*(i+1)-1:11*i])
									+ (A2_1[i] >> (logg[4*(i+1)-1:4*i] - i));
		assign	Ax_2[i]	=	A1[i] ^ A2_2[i];
		assign	AxOut[i]	=	((Ax_2[i] << i) & loggMask[11*(i+1)-1:11*i])
									+ (Ax_2[i] >> (logg[4*(i+1)-1:4*i] - i));

	end
endgenerate

endmodule

/*
int F (my_predictor *p, int A, int size, int bank)
{
    int A1, A2;
        int Ax = A;

    Ax = Ax & ((1 << size) - 1);
    A1 = (Ax & ((1 << p->logg[bank]) - 1));
    A2 = (Ax >> p->logg[bank]);
    A2 =
        ((A2 << bank) & ((1 << p->logg[bank]) - 1)) + (A2 >> (p->logg[bank] - bank));
    Ax = A1 ^ A2;
    Ax = ((Ax << bank) & ((1 << p->logg[bank]) - 1)) + (Ax >> (p->logg[bank] - bank));
    return (Ax);
}
*/

module gtag
(
	input						clk,
	input						reset,
	input		[29:0]		pc,
	input		[11:0]		ch_t0 	[11:0],
	input		[11:0]		ch_t1		[11:0],

	output	[14:0]		tageTag	[11:0]
);

// Tag bits: 7, 7, 8, 8, 9, 10, 11, 12, 12, 13, 14, 15 
parameter [15*12-1:0]	tagMask = {	15'h7FFF, 15'h3FFF, 15'h1FFF, 15'hFFF, 15'hFFF, 15'h7FF,
												15'h3FF, 15'h1FF, 15'hFF, 15'hFF, 15'h7F, 15'h7F};

genvar i;
generate
	for (i = 0; i < 12; i = i + 1) begin: tagComputation
		assign tageTag[i] =	(pc ^ ch_t0[i] ^ (ch_t1[i] << 1)) & tagMask[15*(i+1)-1:15*i];
	end
endgenerate	

endmodule

/*
//  tag computation
uint16_t gtag (my_predictor *p, address_t pc, int bank)
{
        int tag = 0;
        tag = pc ^ p->ch_t[0][bank].comp ^ (p->ch_t[1][bank].comp << 1);
		  
    return (tag & ((1 << p->TB[bank]) - 1));
}
*/





