`include "header.v"
`include "soin_header.v"

module insnCache(
	input						clk,
	input		[31:0]		insnMem_data_w,
	input		[31:0]		fetch_bpredictor_PC,
	input		[7:0]			insnMem_addr_w,
	input 					insnMem_wren,
	output	[31:0]		fetch_bpredictor_inst
);

insnMem insnMem(
	.clock(clk),
	.data(insnMem_data_w),
	.rdaddress(fetch_bpredictor_PC[9:2]),		// using PC[9:2]!
	.wraddress(insnMem_addr_w),
	.wren(insnMem_wren),
	.q(fetch_bpredictor_inst)
);

endmodule


module soin_bpredictor_decode(
	input	[31:0]						inst,
	output	reg							is_branch,
	output	reg							is_cond,
	output	reg							is_ind,
	output	reg							is_call,
	output	reg							is_ret,
	output	reg							is_16,
	output	reg							is_26,
	
	output	reg	[1:0]					is_p_mux,
	output	reg							is_p_uncond,
	output	reg							is_p_ret,
	output	reg							is_p_call
	
);

wire	[5:0]							inst_opcode;
wire	[5:0]							inst_opcode_x_h;

assign inst_opcode						= inst[`BITS_F_OP];
assign inst_opcode_x_h					= inst[`BITS_F_OPXH];

always@( * )
begin
	case (inst_opcode)
		6'h26: begin is_branch			= 1; end
		6'h0e: begin is_branch			= 1; end
		6'h2e: begin is_branch			= 1; end
		6'h16: begin is_branch			= 1; end
		6'h36: begin is_branch			= 1; end
		6'h1e: begin is_branch			= 1; end
		6'h06: begin is_branch			= 1; end
		6'h00: begin is_branch			= 1; end
		6'h01: begin is_branch			= 1; end
		6'h3a:
		begin
			case(inst_opcode_x_h)
				6'h1d: begin is_branch	= 1; end
				6'h01: begin is_branch	= 1; end
				6'h0d: begin is_branch	= 1; end
				6'h05: begin is_branch	= 1; end
				default: begin is_branch= 0; end
			endcase
		end
		default: begin is_branch		= 0; end
	endcase
end

always@( * )
begin
	case (inst_opcode)
		6'h0e: begin is_cond			= 1; end
		6'h16: begin is_cond			= 1; end
		6'h1e: begin is_cond			= 1; end
		6'h26: begin is_cond			= 1; end
		6'h2e: begin is_cond			= 1; end
		6'h36: begin is_cond			= 1; end

		default: begin is_cond			= 0; end
	endcase
end

always@( * )
	is_ind								= inst_opcode == 6'h3A;

always@( * )
	is_call								= (inst_opcode == 6'h00) | ((inst_opcode == 6'h3A) & (inst_opcode_x_h == 6'h1D));

always@( * )
	is_ret								= (inst_opcode == 6'h3A) & (inst_opcode_x_h == 6'h05);

always@( * )
	is_26								= (inst_opcode == 6'h00) | (inst_opcode == 6'h01);

always@( * )
	is_16								= (inst_opcode != 6'h3A) & (~is_26);

always@( * )
	is_p_mux							= inst[31:30];

always@( * )
	is_p_uncond							= inst[29];

always@( * )
	is_p_ret							= is_ret;

always@( * )
	is_p_call							= inst[27];
endmodule


module ras(
	input								clk,
	input								reset,

	input	[31:0]					f_PC4,
	input								f_call,
	input								f_ret,

	input								e_recover,
	input	[3:0]						e_recover_index,
	
	output	reg [3:0]			ras_index,
	output	[31:0]				top_addr
);

MLAB_32_4 ras(
	.clock							(clk),
	.address							(ras_index),
	.q									(top_addr),
	.data								(e_recover_index),
	.wren								(f_call)
);

always@(posedge clk)
begin
	if (reset) begin
		ras_index						<= 'b0;
	end
	else if (e_recover) begin
		ras_index						<= e_recover_index;
	end
	else if (f_call) begin
		ras_index						<= ras_index + 4'h1;
	end
	else if (f_ret) begin
		ras_index						<= ras_index - 4'h1;
	end
	else begin
		ras_index						<= 'b0;
	end
end

endmodule



module bpredTop(
	input	wire					clk,
	input wire					insnMem_wren,
	input wire	[31:0]		insnMem_data_w,
	input wire	[7:0]			insnMem_addr_w,
	output reg	[31:0]		fetch_bpredictor_PC,
	
	input							fetch_redirect,
	input	[31:0]				fetch_redirect_PC,

	input							soin_bpredictor_stall,

	output						bpredictor_fetch_p_dir,

	input							execute_bpredictor_update,
	input	[31:0]				execute_bpredictor_PC4,
	input	[31:0]				execute_bpredictor_target,
	input							execute_bpredictor_dir,
	input							execute_bpredictor_miss,
	input							execute_bpredictor_recover_ras,
	input	[13:0]				execute_bpredictor_meta,
	
	input							reset
);

parameter ghrSize				= 1933;

/*
fetch_bpredictor_PC is to be used before clock edge
fetch_bpredictor_inst is to be used after clock edge
*/

// RAS
wire	[3:0]							ras_index;
wire	[3:0]							ras_top_addr;

wire									is_branch;
wire									is_cond;
wire									is_ind;
wire									is_call;
wire									is_ret;
wire									is_16;
wire									is_26;

// Predecoding
wire	[1:0]							is_p_mux;
wire									is_p_uncond;
wire									is_p_ret;
wire									is_p_call;

wire	[31:0]						OPERAND_IMM16S;
wire	[31:0]						OPERAND_IMM26;
wire	[31:0]						TARGET_IMM16S;
wire	[31:0]						TARGET_IMM26;

reg	[31:0]						PC4;
reg	[31:0]						PC4_r;
reg	[3:0]							PCH4;
reg	[ghrSize-1:0]				GHR;



wire	[11:0]						lu_index;
reg	[11:0]						lu_index_r;
wire	[1:0]							lu_data;

wire	[7:0]							up_index;
wire	[31:0]						up_data;
wire									up_wen;
wire	[3:0]							up_be;

wire	[1:0]							lu_bimodal_data;
reg									lu_bimodal_data_h;
reg	[1:0]							up_bimodal_data;
reg	[7:0]							lu_bimodal_bun;

wire	[31:0]						fetch_bpredictor_inst;



ras ras_inst(
	.clk								(clk),
	.reset							(reset),

	.f_PC4							(PC4),
	.f_call							(is_call),
	.f_ret							(is_ret),

	.e_recover						(execute_bpredictor_recover_ras),
	.e_recover_index				(execute_bpredictor_meta),

	.ras_index						(ras_index),
	.top_addr						(ras_top_addr)
);


wire	[31:0] 	execute_bpredictor_PC	= execute_bpredictor_PC4 - 4;


// ICache, this is a wrapper so the logic here is not included in area
insnCache iCache(
	.clk(clk),
	.insnMem_data_w(insnMem_data_w),
	.fetch_bpredictor_PC(fetch_bpredictor_PC),		// using PC[9:2]!
	.insnMem_addr_w(insnMem_addr_w),
	.insnMem_wren(insnMem_wren),
	.fetch_bpredictor_inst(fetch_bpredictor_inst)
);

integer j;
initial begin
	fetch_bpredictor_PC <= 32'h0;
	PC4_r <= 0;
	PCH4 = 0;
	PC4 <= 4;
end

mem bimodal_mem
(
	.clock							(clk),

	.rdaddress						(lu_index),
	.q									(lu_data),

//	.byteena_a						(up_be),
	.wraddress						(up_index),
	.data								(up_data),
	.wren								(up_wen)
);

//=====================================
// Bimodal Direction
//=====================================

assign lu_index						= fetch_bpredictor_PC[13:2];

assign up_index						= execute_bpredictor_meta[11:0];
assign up_data							= execute_bpredictor_meta[13:12];

wire										lu_data_h;
reg										lu_bimodal_data_h0;
reg										lu_bimodal_data_h1;
reg										lu_bimodal_data_h2;
reg										lu_bimodal_data_h3;

assign lu_data_h = lu_data[1];





//=====================================
// Decoding
//=====================================
soin_bpredictor_decode d_inst(
	.inst								(fetch_bpredictor_inst),

	.is_branch						(is_branch),
	.is_cond							(is_cond),
	.is_ind							(is_ind),
	.is_call							(is_call),
	.is_ret							(is_ret),
	.is_16							(is_16),
	.is_26							(is_26),

	.is_p_mux						(is_p_mux),
	.is_p_uncond					(is_p_uncond),
	.is_p_ret						(is_p_ret),
	.is_p_call						(is_p_call)
);


//=====================================
// Target
//=====================================
assign OPERAND_IMM16S			= {{16{fetch_bpredictor_inst[`BITS_F_IMM16_SIGN]}}, fetch_bpredictor_inst[`BITS_F_IMM16]};
assign OPERAND_IMM26				= {PCH4, fetch_bpredictor_inst[`BITS_F_IMM26], 2'b00};
assign TARGET_IMM16S				= {PC4[31:2] + OPERAND_IMM16S[31:2], 2'b00};
assign TARGET_IMM26				= OPERAND_IMM26;

`define PreDecode

// Output Mux
always@(*)
begin

`ifdef PreDecode

	casex ({fetch_redirect, is_p_mux & {2{is_p_uncond | bpredictor_fetch_p_dir}}})
		3'b1xx:
		begin
			fetch_bpredictor_PC	= fetch_redirect_PC;
		end
		3'b000:
		begin
			fetch_bpredictor_PC	= PC4;
		end
		3'b001:
		begin
			fetch_bpredictor_PC	= ras_top_addr;
		end
		3'b010:
		begin
			fetch_bpredictor_PC	= TARGET_IMM16S;
		end
		3'b011:
		begin
			fetch_bpredictor_PC	= TARGET_IMM26;
		end
		default:
		begin
			fetch_bpredictor_PC	= PC4;
		end
	endcase

`else

	if (fetch_redirect) begin
		fetch_bpredictor_PC = fetch_redirect_PC;
	end
	else if (~bpredictor_fetch_p_dir) begin
		// Not taken or indirect call, the target is not computable
		fetch_bpredictor_PC = PC4;
	end
	else begin
		if (is_ret) begin
			// return, use the stack
			fetch_bpredictor_PC = ras_top_addr;
		end
		else if (is_cond & is_16) begin
			fetch_bpredictor_PC = TARGET_IMM16S;
		end
		else if (is_cond & is_26) begin
			fetch_bpredictor_PC = TARGET_IMM26;
		end
		else begin
			fetch_bpredictor_PC = PC4;
		end
	end
	
`endif
	
end


//=====================================
// TAGE
//=====================================


`ifdef PreDecode
// Predecoding branch direction
assign bpredictor_fetch_p_dir = is_p_uncond | lu_bimodal_data_h;

`else

// Regurlar branch direction
assign bpredictor_fetch_p_dir	= is_branch & (is_cond | is_ret | is_call) & lu_bimodal_data_h;

`endif


// Update TAGE


assign up_wen	= reset | (~soin_bpredictor_stall & execute_bpredictor_update);

always@( * )
begin
	PC4					= PC4_r + 4;
	lu_bimodal_data_h					= lu_data_h;
end

always@(posedge clk)
begin
	if (reset)
	begin
		GHR							<= 0;
	end
	else
	begin
		PCH4				<= fetch_bpredictor_PC[31:28];
		PC4_r				<= fetch_bpredictor_PC;
		
		if (execute_bpredictor_update) begin
			GHR			<= {GHR[ghrSize-1:0], execute_bpredictor_dir};
		end
	end
end


endmodule
