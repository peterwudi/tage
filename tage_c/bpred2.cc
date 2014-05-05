#include "bpred2.h"
#include <stdio.h>
#include <typeinfo>

extern "C" {
#include "machine.h"
}

unsigned long long bpred2::global_history = 0LL;
unsigned long long bpred2::global_history_r = 0LL;


void bpred2::print_stats()
{
	//printf ("%s.lookups\t\t%u\n", get_name(), n_lookups);
	//printf ("%s.updates\t\t%u\n", get_name(), n_updates);
	//printf ("%s.hits   \t\t%u\n", get_name(), n_hits);
	//printf ("%s.size   \t\t%u\n", get_name(), get_size());
	printf ("%s\t%u\t%.2f\t%u\t%u\n", get_name(), n_hits, (n_updates-n_hits)*100 / (double)n_updates, get_size(), n_lookups);
}
bool bpred2::lookup(bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	//predict not taken
	n_lookups++;
	int hit = 0;
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
		hit = 1;
	else
		hit = taken ? 0 : 1;
		
	n_hits += hit;
	return (MD_OP_FLAGS(op) & F_UNCOND) ? 1 : 0;
}
bool bpred2::update(e_update_mode mode, bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	n_updates++;
	//static predictors don't update.
	return 0;
}

void bpred2::update_ghist(bool taken)
{
	global_history = (global_history<<1) | (taken ? 1LL : 0LL);
	global_history_r = (global_history_r>>1) | (taken ? 0x8000000000000000ULL : 0LL);	
}	



bpred2::bpred2()
{
	n_updates = 0;
	n_lookups = 0;
	n_hits = 0;
}


// Basic static predictors
bpred2_static::bpred2_static(enum bpred2_static::bpstatic_type type)
{
	bp_type = type;
}
bpred2* bpred2_static::clone()
{
	return new bpred2_static(bp_type);
}

bool bpred2_static::lookup(bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	n_lookups++;
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
	{
		n_hits ++;
		return 1;
	}
	
	int ptaken = 0;
	switch (bp_type)
	{
		case bptype_taken:    ptaken = 1; break;
		case bptype_nottaken: ptaken = 0; break;
		case bptype_btfn: ptaken = (((int)(target_addr-baddr) > 0) ? 0 : 1); break;
	}
	
	n_hits += (ptaken == taken);
	return ptaken;
}
bool bpred2_static::update(e_update_mode mode, bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	//static predictors don't update.
	n_updates++;
	bool ptaken = 0;
	switch (bp_type)
	{
		case bptype_taken:    ptaken = 1; break;
		case bptype_nottaken: ptaken = 0; break;
		case bptype_btfn: ptaken = (((int)(target_addr-baddr) > 0) ? 0 : 1); break;
	}
	return ptaken;
}
const char* bpred2_static::get_name()
{
	switch (bp_type)
	{
		case bptype_taken: return "static_taken";
		case bptype_nottaken: return "static_nottaken";
		case bptype_btfn: return "static_btfn";
		default: return "static_invalid";
	}
}
int bpred2_static::get_config(char* out)
{
	int outlen = 0;
	outlen += sprintf (out, "bpred2_static ");
	switch (bp_type)
	{
		case bptype_taken: outlen += sprintf (out+outlen, "taken\n"); break;
		case bptype_nottaken: outlen += sprintf (out+outlen, "nottaken\n"); break;
		case bptype_btfn: outlen += sprintf (out+outlen, "btfn\n"); break;
		default: fatal ("Unknown bpred2_static type");
	}
	return outlen;
}





// Two-level predictor. Implements [PG]A[pg] and gshare predictors.


static unsigned char automata[8] = 
{ 0, 1,
  0, 2,
  1, 3,
  2, 3
};

const char* bpred2_twolev::get_name()
{ 
	static char str[32]; 
	sprintf (str, "twolev(%d,%d,%d,%d,%d)", log_table_size1, log_table_size2, log_hysteresis_size, history_bits, addr_share_bits); 
	return str; 
}

int bpred2_twolev::get_size()
{
	return (1<<log_table_size1)*history_bits + (1<<log_table_size2) + (1<<log_hysteresis_size); //history and 2bc.
}

int bpred2_twolev::get_config(char* out)
{
	int outlen = 0;
	outlen += sprintf (out, "bpred2_twolev %d %d %d %d %d ", 
		log_table_size1, log_table_size2, log_hysteresis_size, history_bits, addr_share_bits);
		
	if (hash == bpred2_vote::tlhash_trunc) outlen += sprintf (out+outlen, "bpred2_vote::tlhash_trunc\n"); 
	else if (hash == bpred2_vote::tlhash0) outlen += sprintf (out+outlen, "bpred2_vote::tlhash0\n");
	else if (hash == bpred2_vote::tlhash1) outlen += sprintf (out+outlen, "bpred2_vote::tlhash1\n");
	else if (hash == bpred2_vote::tlhash2) outlen += sprintf (out+outlen, "bpred2_vote::tlhash2\n");
	else if (hash == NULL) outlen += sprintf (out+outlen, "null\n"); 
	else fatal ("Unknown bpred2_twolev hash type");
	
	return outlen;
}


bpred2_twolev::bpred2_twolev(int log_table_size1, int log_table_size2, int log_hysteresis_size, int history_bits, int addr_share_bits, int (*hash)(unsigned int baddr, unsigned long long history, int table_size, int history_bits))
{
	if (log_table_size1 < 0) fatal ("twolev log_table_size1 must be >= 0");
	if (log_table_size2 < 0) fatal ("twolev log_table_size2 must be >= 0");
	if (log_table_size1 > 30) fatal ("twolev log_table_size1 %d too big", log_table_size1);
	if (log_table_size2 > 30) fatal ("twolev log_table_size2 %d too big", log_table_size2);

	if (log_hysteresis_size < 0) fatal ("twolev log_hysteresis_size must be >= 0");
	if (log_hysteresis_size > log_table_size2) fatal ("twolev log_hysteresis_size must be <= log_table_size2");
	
	if (history_bits < 0 || history_bits > 63) fatal ("twolev history_bits must be 0 to 63");
	if (addr_share_bits < 0 || addr_share_bits > history_bits) fatal ("twolev addr_share_bits must be 0 to history_bits");
	
	this->log_table_size1 = log_table_size1;
	this->log_table_size2 = log_table_size2;	
	this->log_hysteresis_size = log_hysteresis_size;
	this->addr_share_bits = addr_share_bits;	
	this->history_bits = history_bits;
	
	int table_size1 = 1 << log_table_size1;
	int table_size2 = 1 << log_table_size2;
	
	table1 = new unsigned long long[table_size1]; memset (table1, 0, sizeof(unsigned long long)*table_size1);
	table2 = new unsigned char[table_size2];
	for (int i=0;i<table_size2;i++)
		table2[i] = 1 + (i&1);
	
	this->hash = hash;
	
	mask1 = (history_bits <= log_table_size2) ? ((1<<(log_table_size2 - history_bits + addr_share_bits)) -1) : ((1<< addr_share_bits)-1);
	mask2 = ((1<<log_table_size2)-1);
	history_mask = (1LL<<history_bits)-1;
	
	ghist_bits = 0;
}
bpred2* bpred2_twolev::clone()
{
	bpred2_twolev *p = new bpred2_twolev(log_table_size1, log_table_size2, log_hysteresis_size, history_bits, addr_share_bits, hash);
	return p;
}

void bpred2_twolev::free_storage()
{
	if (table1) delete[] table1;
	if (table2) delete[] table2;
	table1 = NULL;
	table2 = NULL;
}
bpred2_twolev::~bpred2_twolev()
{
	if (table1) delete[] table1;
	if (table2) delete[] table2;
}

bool bpred2_twolev::lookup(bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
	{
		n_hits++;
		return 1;
	}


	n_lookups++;
	t1_index = (baddr>>MD_BR_SHIFT) & ((1<<log_table_size1)-1);
	unsigned long long history = table1[ t1_index ]; 
	
	
	t2_index = 0;		// for log_table_size == 0
	
	if (log_table_size2 > 0)
	{
		if (!hash)
		{
			if (history_bits <= log_table_size2)
			{
				t2_index = (( ((baddr>>(16+MD_BR_SHIFT))^(baddr>>MD_BR_SHIFT)) & mask1 ) ^
					       (history << (log_table_size2-history_bits))) & mask2;
			}
			else
			{
				t2_index = ( (((baddr>>16)^baddr)>>MD_BR_SHIFT) & mask1 );
				register unsigned long long history2 = history;
				register int bits;
				for (bits = history_bits; bits > log_table_size2; bits-= log_table_size2)
				{
					t2_index ^= history2;
					history2 >>= log_table_size2;
				}
				t2_index ^= (history2 << (log_table_size2-bits));
			
				t2_index &= mask2;
			}
		}
		else
		{
			t2_index = hash(baddr>>MD_BR_SHIFT, history & history_mask, log_table_size2, history_bits);
		}
	}
		
	counter = table2[ t2_index ];
	if (log_hysteresis_size < log_table_size2)	// smaller hysteresis table
		counter = (counter & 2) | ( table2[ t2_index & ((1<<log_hysteresis_size)-1) ] & 1 );


	
	n_hits += ( !taken == !(counter&2) );
	

	return (counter&2)>>1;
}
bool bpred2_twolev::update(e_update_mode mode, bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{

	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
		return 1;

	if (mode & update_history)
	{
		unsigned long long history = table1[ t1_index ]; 
		table1[t1_index] = (history << 1) | (!!taken);
	}
	
	if (mode & update_predictor)
	{
		n_updates++;		//this counter is starting to become bogus...
		// counter is saved state.
		table2[t2_index] = automata[counter*2 + !!taken]; 
	
		if (log_hysteresis_size < log_table_size2)		// If smaller hysteresis table, update the proper hysteresis bit.
			table2[ t2_index & ((1<<log_hysteresis_size)-1) ] = 
				(table2[ t2_index & ((1<<log_hysteresis_size)-1) ] & 2) | (automata[counter*2 + !!taken] & 1);

		return (automata[counter*2+!!taken] & 2)>>1;
	}			
	return (counter&2)>>1;
}	
	
	
	
// Global history predictors. One-level specialization of twolev for reduced runtime.
const char* bpred2_ghist::get_name()
{ 
	static char str[32]; 
	sprintf (str, "ghist(%d,%d,%d,%d)", log_table_size, log_hysteresis_size, history_bits, addr_share_bits); 
	return str; 
}

int bpred2_ghist::get_size()
{
	return history_bits + (1<<log_table_size) + (1<<log_hysteresis_size); //history and 2bc.
}

int bpred2_ghist::get_config(char* out)
{
	int outlen = 0;
	outlen += sprintf (out, "bpred2_ghist %d %d %d %d ", 
		log_table_size, log_hysteresis_size, history_bits, addr_share_bits);
		
	if (hash == bpred2_vote::tlhash_trunc) outlen += sprintf (out+outlen, "bpred2_vote::tlhash_trunc\n"); 
	else if (hash == bpred2_vote::tlhash0) outlen += sprintf (out+outlen, "bpred2_vote::tlhash0\n");
	else if (hash == bpred2_vote::tlhash1) outlen += sprintf (out+outlen, "bpred2_vote::tlhash1\n");
	else if (hash == bpred2_vote::tlhash2) outlen += sprintf (out+outlen, "bpred2_vote::tlhash2\n");
	else if (hash == NULL) outlen += sprintf (out+outlen, "null\n"); 
	else fatal ("Unknown bpred2_ghist hash type");
			

	return outlen;
}


bpred2_ghist::bpred2_ghist(int log_table_size, int log_hysteresis_size, int history_bits, int addr_share_bits, int (*hash)(unsigned int baddr, unsigned long long history, int table_size, int history_bits))
{
	if (log_table_size < 0) fatal ("ghist log_table_size must be >= 0");
	if (log_table_size > 30) fatal ("ghist log_table_size %d too big", log_table_size);

	if (log_hysteresis_size < 0) fatal ("ghist log_hysteresis_size must be >= 0");
	if (log_hysteresis_size > log_table_size) fatal ("ghist log_hysteresis_size must be <= log_table_size");
	
	if (history_bits < 0 || history_bits > 63) fatal ("ghist history_bits must be 0 to 63");
	if (addr_share_bits < 0 || addr_share_bits > history_bits) fatal ("ghist addr_share_bits must be 0 to history_bits");
	
	this->log_table_size = log_table_size;	
	this->log_hysteresis_size = log_hysteresis_size;
	this->addr_share_bits = addr_share_bits;	
	this->history_bits = history_bits;
	
	int table_size = 1 << log_table_size;
	
	table = new unsigned char[table_size];
	for (int i=0;i<table_size;i++)
		table[i] = 1 + (i&1);
	
	this->hash = hash;
	
	mask1 = (history_bits <= log_table_size) ? ((1<<(log_table_size - history_bits + addr_share_bits)) -1) : ((1<< addr_share_bits)-1);
	mask2 = ((1<<log_table_size)-1);
	history_mask = (1LL<<history_bits)-1;
}

bpred2_ghist::~bpred2_ghist()
{
	if (table) delete[] table;
}
void bpred2_ghist::free_storage()
{
	if (table) delete[] table;
	table = NULL;
}
bpred2* bpred2_ghist::clone()
{
	bpred2_ghist *p = new bpred2_ghist(log_table_size, log_hysteresis_size, history_bits, addr_share_bits, hash);
	return p;
}

bool bpred2_ghist::lookup(bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
	{
		n_hits++;
		return 1;
	}

	n_lookups++;

	t_index = 0;		// for log_table_size == 0
	
	if (log_table_size > 0)
	{
		if (!hash)
		{
			if (history_bits <= log_table_size)
			{
				t_index = (( ((baddr>>(16+MD_BR_SHIFT))^(baddr>>MD_BR_SHIFT)) & mask1) ^
					       (global_history << (log_table_size-history_bits))) & mask2;
			}
			else
			{
				t_index = ( ((baddr>>(16+MD_BR_SHIFT))^(baddr>>MD_BR_SHIFT)) & mask1 );
				register unsigned long long history = global_history;
				register int bits;
				for (bits = history_bits; bits > log_table_size; bits-= log_table_size)
				{
					t_index ^= history;
					history >>= log_table_size;
				}
				t_index ^= (history << (log_table_size-bits));
			
				t_index &= mask2;
			}
		}
		else
		{
			t_index = hash(baddr>>MD_BR_SHIFT, global_history & history_mask, log_table_size, history_bits);
		}
	}
	
	counter = table[ t_index ];
	if (log_hysteresis_size < log_table_size)	// smaller hysteresis table
		counter = (counter & 2) | ( table[ t_index & ((1<<log_hysteresis_size)-1) ] & 1 );


	n_hits += ( !taken == !(counter&2) );
	
	return (counter&2)>>1;
}
bool bpred2_ghist::update(e_update_mode mode, bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{

	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
		return 1;

	if (mode & update_predictor)
	{
		n_updates++;
		// counter is saved state.
		char new_counter = automata[counter*2 + !!taken]; 
		table[t_index] = new_counter;
	
		if (log_hysteresis_size < log_table_size)		// If smaller hysteresis table, update the proper hysteresis bit.
			table[ t_index & ((1<<log_hysteresis_size)-1) ] = 
				(table[ t_index & ((1<<log_hysteresis_size)-1) ] & 2) | (new_counter & 1);

		return (new_counter & 2)>>1;
	}			
	return (counter&2)>>1;
}		
	

	


	
// Combining predictor.
bpred2_comb::bpred2_comb()
{
	b1 = b2 = bc = NULL;
	skip_b1 = skip_b2 = skip_bc = false;
	update_type = update_all;
}

bpred2_comb::bpred2_comb(bpred2 *b1, bpred2 *b2, bpred2 *bc, e_update_type update_type)
{
	skip_b1 = skip_b2 = skip_bc = false;
	this->b1 = b1;
	this->b2 = b2;
	this->bc = bc;
	this->update_type = update_type;
}
bpred2_comb::~bpred2_comb()
{
	if (b1 && !skip_b1) delete b1;
	if (b2 && !skip_b2) delete b2;
	if (bc && !skip_bc) delete bc;
	b1 = b2 = bc = NULL;
}
void bpred2_comb::free_storage()
{
	if (b1 && !skip_b1) b1->free_storage();
	if (b2 && !skip_b2) b2->free_storage();
	if (bc && !skip_bc) bc->free_storage();
}
bpred2* bpred2_comb::clone()
{
	bpred2 *b1n = NULL, *b2n = NULL, *bcn = NULL;
	if (b1) b1n = b1->clone();
	if (b2) b2n = b2->clone();
	if (bc) bcn = bc->clone();
	
	bpred2_comb *p = new bpred2_comb(b1n, b2n, bcn, update_type);
	return p;
}

const char* bpred2_comb::get_name()
{
	static char str[96];
	char n1[32]="NULL", n2[32]="NULL";
	if (b1) strcpy (n1, b1->get_name());
	if (b2) strcpy (n2, b2->get_name());	
	
	sprintf (str, "comb%s[%s; %s][%s]", update_type == update_ev8used ? ".u" : "", n1, n2, bc ? bc->get_name() : "NULL");
	return str;
}

int bpred2_comb::get_size()
{
	int sz = 0;
	if (!skip_b1 && b1) sz += b1->get_size();
	if (!skip_b2 && b2) sz += b2->get_size();
	if (!skip_bc && bc) sz += bc->get_size();
	return sz;
}

int bpred2_comb::get_config(char* out)
{
	int outlen = 0;
	
	if (b1) { outlen += sprintf (out+outlen, "    "); outlen += b1->get_config(out+outlen); } else { outlen += sprintf (out+outlen, "    null\n"); }
	if (b2) { outlen += sprintf (out+outlen, "    "); outlen += b2->get_config(out+outlen); } else { outlen += sprintf (out+outlen, "    null\n"); }
	if (bc) { outlen += sprintf (out+outlen, "    "); outlen += bc->get_config(out+outlen); } else { outlen += sprintf (out+outlen, "    null\n"); }
	
	outlen += sprintf (out+outlen, "bpred2_comb %s\n", 
		update_type == update_all ? "update_all" : "update_ev8used");
		
	return outlen;
}


bool bpred2_comb::lookup(bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	n_lookups++;
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
	{
		n_hits++;
		return 1;
	}	
	
	c1 = b1->lookup (taken, op, baddr, target_addr);	// combining predictor updates both predictors
	c2 = b2->lookup (taken, op, baddr, target_addr);
	cc = bc->lookup (true, op, baddr, target_addr);
	
	bool ptaken = cc ? c1 : c2;
	n_hits += (ptaken == taken);

	//no updating here

	return ptaken;
}

bool bpred2_comb::update(e_update_mode mode, bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	n_updates++;
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
		return 1;
	
	bool newc1 = c1, newc2 = c2, newcc = cc;
	if (update_type == update_all)
	{
		if (!skip_b1) newc1 = b1->update (mode, taken, op, baddr, target_addr);	// combining predictor updates both predictors
		if (!skip_b2) newc2 = b2->update (mode, taken, op, baddr, target_addr);
		if (!skip_bc)
		{
			if (c1 != c2)	//If predictors predicted differently
				newcc = bc->update ((e_update_mode)(mode & ~update_history), c1==taken, op, baddr, target_addr);		// Update the meta predictor
			bc->update (update_history, taken, op, baddr, target_addr);		// Update the meta global history
		}
			
	}
	else if (update_type == update_ev8used)
	{
		if (!skip_bc)	// If predictors predicted differently
		{
			if (c1 != c2)	// If predictors predicted differently
				newcc = bc->update ((e_update_mode)(mode & ~update_history), c1==taken, op, baddr, target_addr);		// Update the meta predictor
			bc->update (update_history, taken, op, baddr, target_addr);		// Update the meta global history
		}

		if ( (newcc ? c1: c2) == taken)	// new prediction is correct, update only the used table
		{
			// update the predictor that will be used next time
			if (!skip_b1) newc1 = b1->update ((newcc) ? mode : (e_update_mode)(mode&~update_predictor), taken, op, baddr, target_addr);	
			if (!skip_b2) newc2 = b2->update ((!newcc) ? mode : (e_update_mode)(mode&~update_predictor), taken, op, baddr, target_addr);
		}
		else	//new prediction is wrong. Update all tables
		{
			if (!skip_b1) newc1 = b1->update (mode, taken, op, baddr, target_addr);	// update the predictor that will be used next time
			if (!skip_b2) newc2 = b2->update (mode, taken, op, baddr, target_addr);
		}
	}
	else
	{
		fatal ("unknown bpred2_comb update type %d", update_type);
	}
	
	return newcc ? newc1 : newc2;

}





bpred2_vote::bpred2_vote()
{
	update_type = update_all;
}
bpred2_vote::~bpred2_vote()
{
	for (vector< pair<bpred2*,bool> >::iterator i = vsubpred.begin(); i != vsubpred.end(); ++i)
	{
		if (!i->second) delete i->first;
	}
	vsubpred.clear();
}
void bpred2_vote::free_storage()
{
	for (vector< pair<bpred2*,bool> >::iterator i = vsubpred.begin(); i != vsubpred.end(); ++i)
	{
		if (!i->second) i->first->free_storage();
	}
}
bpred2* bpred2_vote::clone()
{
	bpred2_vote *p = new bpred2_vote();
	p->update_type = update_type;
	
	for (vector< pair<bpred2*,bool> >::iterator i = vsubpred.begin(); i != vsubpred.end(); ++i)
	{
		bpred2 *ps = NULL;
		if (i->first) ps = i->first->clone();
		
		p->vsubpred.push_back (make_pair(ps, false));
	}
	return p;
}

const char* bpred2_vote::get_name()
{
	static char name[1024];	//hope this doesn't overflow...
	sprintf (name, "vote%s[ ", update_type == update_ev8used ? ".ev8" : update_type==update_used ? ".u" : "");
	for (vector< pair<bpred2*,bool> >::iterator i = vsubpred.begin(); i != vsubpred.end(); ++i)
	{
		strcat (name, (*i).first->get_name());
		strcat (name, " ");
	}
	strcat (name, "]");
	return name;
}

int bpred2_vote::get_size()
{
	int sz = 0;
	for (vector< pair<bpred2*,bool> >::iterator i = vsubpred.begin(); i != vsubpred.end(); ++i)
		if (!(*i).second) sz += (*i).first->get_size();

	return sz;
}

int bpred2_vote::get_config(char* out)
{
	int outlen = 0;
	unsigned int disable_mask = 0;
	if (vsubpred.size() > 20) fatal ("bpred2_vote has too many subpredictors");
	
	for (int i=0;i<vsubpred.size();++i)
	{
		outlen += sprintf (out+outlen, "    "); outlen += vsubpred[i].first->get_config(out+outlen);
		disable_mask |= (vsubpred[i].second ? (1<<i) : 0);
	}
	
	outlen += sprintf (out+outlen, "bpred2_vote %llu 0x%08x ", (unsigned long long) vsubpred.size(), disable_mask); 
	
	switch (update_type)
	{
		case update_all: outlen += sprintf (out+outlen, "update_all"); break;
		case update_used: outlen += sprintf (out+outlen, "update_used"); break;
		case update_ev8used: outlen += sprintf (out+outlen, "update_ev8used"); break;
		default: fatal ("Unknown bpred2_vote update_type %d", update_type);
	}
	outlen += sprintf (out+outlen, "\n");
		
	return outlen;
}

bool bpred2_vote::lookup(bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	n_lookups++;
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
	{
		n_hits++;
		return 1;
	}	

	sum = 0;
	subpred.reserve( vsubpred.size() );
	int j = 0;
	for (vector< pair<bpred2*,bool> >::iterator i = vsubpred.begin(); i != vsubpred.end(); ++i, ++j)
	{
		sum += (subpred[j] = ((*i).first->lookup (taken, op, baddr, target_addr) << 1) - 1); 
	}
	
	n_hits += (taken == (sum > 0));		//if tied, choose not-taken
	
	return (sum > 0);
}
bool bpred2_vote::update(e_update_mode mode, bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	n_updates++;
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
		return 1;
	
	int newsum = sum;
	vector<char>::iterator j;
	vector< pair<bpred2*,bool> >::iterator i;
	
	if (update_type == update_all)
	{
		newsum = 0;
		for (i = vsubpred.begin(), j=subpred.begin(); i != vsubpred.end(); ++i, ++j)
			if (!(*i).second) newsum += ((*i).first->update (mode, taken, op, baddr, target_addr) << 1) - 1;
			else newsum += (*j);
	}
	else if (update_type == update_ev8used || update_type == update_used)
	{
		bool skip_update = false;
		if (update_type == update_ev8used)
		{
			int vsize = vsubpred.size();	// subpred might be bigger than vsubpred
			skip_update = (sum == (taken? vsize : -vsize));
		}
		
		if (!skip_update)	// at least one component mispredicted, so do update. all_correct is also false if update_used.
		{
			newsum = 0;
			if ((sum > 0) == taken) // correct overall prediction: update just the correct ones
			{
				for (i = vsubpred.begin(), j=subpred.begin(); i != vsubpred.end(); ++i, ++j)
					if (!(*i).second) 
						newsum += ((*i).first->update (((*j) == taken) ? mode : (e_update_mode)(mode &~ update_predictor), taken, op, baddr, target_addr) << 1) - 1;
					else { newsum += (*j); }

			}
			else	// mispredict: update all components
			{
				for (i = vsubpred.begin(), j=subpred.begin(); i != vsubpred.end(); ++i, ++j)
					if (!(*i).second) newsum += ((*i).first->update (mode, taken, op, baddr, target_addr) << 1) - 1;
					else { newsum += (*j); }
			}
		}
		else
		{
			newsum = sum;
			for (i = vsubpred.begin(); i != vsubpred.end(); ++i)
				if (!(*i).second) (*i).first->update ((e_update_mode)(mode&~update_predictor), taken, op, baddr, target_addr);
		}
	}
	else
	{
		fatal ("Unknown bpred2_vote update type %d", update_type);
	}

	return (newsum > 0);
}


// Hash functions for gskew and e-gskew.
int bpred2_vote::tlhash_trunc (unsigned int baddr, unsigned long long history, int table_size, int history_bits)
{
	return (baddr^(baddr>>16)) & ((1<<table_size)-1);
}
int bpred2_vote::tlhash0 (unsigned int baddr, unsigned long long history, int table_size, int history_bits)
{
	unsigned long long v1v2 = (((unsigned long long)baddr << history_bits ) | (history & ((1LL<<(history_bits))-1))) & ((1LL<<(table_size<<1))-1);

	unsigned int v1 = v1v2 & ((1<<table_size)-1);
	unsigned int v2 = v1v2 >> table_size;
	v1 = (v1 >> 1) | (((v1&1) << (table_size-1)) ^ (v1 & (1<<(table_size-1))));
	v1 ^= v2;
	v2 = ((v2 << 1)&((1<<table_size)-1)) | (((v2 ^ (v2<<1)) >> (table_size-1)) & 1);
	v1 ^= v2;
	return v1;
}
int bpred2_vote::tlhash1 (unsigned int baddr, unsigned long long history, int table_size, int history_bits)
{
	//printf ("baddr=%08x history=%08llx table_size=%d  history_bits=%d\n", baddr, history, table_size, history_bits);
	unsigned long long v1v2 = (((unsigned long long)baddr << history_bits ) | (history & ((1LL<<(history_bits))-1))) & ((1LL<<(table_size<<1))-1);
	//printf ("v1v2 = %08llx, mask = %08llx, mask+1 = %08llx\n", v1v2, ((1LL<<(table_size<<1))-1), (1LL<<(table_size<<1)));

	unsigned int v1 = v1v2 & ((1<<table_size)-1);
	unsigned int v2 = v1v2 >> table_size;
	
	//printf ("  before: v1, v2 = %08x, %08x\n", v1, v2);
	
	v2 = ((v2 << 1)&((1<<table_size)-1)) | (((v2 ^ (v2<<1)) >> (table_size-1)) & 1);
	//printf ("  H-1 v2: %08x\n", v2);
	v2 ^= v1;
	v1 = (v1 >> 1) | (((v1&1) << (table_size-1)) ^ (v1 & (1<<(table_size-1))));
	//printf ("  H v1: %08x\n", v1);
	v2 ^= v1;
	//printf ("  hash: %08x\n", v2);
	return v2;
}
int bpred2_vote::tlhash2 (unsigned int baddr, unsigned long long history, int table_size, int history_bits)
{
	unsigned long long v1v2 = (((unsigned long long)baddr << history_bits ) | (history & ((1LL<<(history_bits))-1))) & ((1LL<<(table_size<<1))-1);

	unsigned int v1 = v1v2 & ((1<<table_size)-1);
	unsigned int v2 = v1v2 >> table_size;
	v1 = ((v1 << 1)&((1<<table_size)-1)) | (((v1 ^ (v1<<1)) >> (table_size-1)) & 1);
	v1 ^= v2;
	v2 = (v2 >> 1) | (((v2&1) << (table_size-1)) ^ (v2 & (1<<(table_size-1))));
	v1 ^= v2;
	return v1;
}



template <typename T>
lru_table<T>::lru_table(int sets, int assoc, int tag_bits, int free_tag_bits, int data_bits)
{
	if (sets <= 0) fatal ("sets must be > 0");
	if (assoc <= 0) fatal ("assoc must be > 0");
	if (sets*assoc > (1<<27) || ((sets*assoc) < 0)) fatal ("sets * assoc = %d really big. Are you sure?", sets*assoc);
	
	if (tag_bits < 0) fatal ("tag bits must be >= 0");
	if (tag_bits > 64) fatal ("tag bits must be <= 64");
	if (free_tag_bits < 0) fatal ("free_tag_bits must be >= 0");
	if (free_tag_bits > tag_bits) fatal ("free_tag_bits(%d) must be <= tag_bits(%d)", free_tag_bits, tag_bits);	
	if (data_bits <= 0) fatal ("data bits must be >= 0");
	if (data_bits > 8*sizeof(T)) ("data bits exceeds table storage type %s", typeid(T).name());
	
	this->sets = sets;
	this->assoc = assoc;
	this->tag_bits = tag_bits;
	this->free_tag_bits = free_tag_bits;
	this->data_bits = data_bits;
	
	table = new map<unsigned long long, pair<T,int> > *[sets];
	lrustack = new s_entry[sets*assoc];
	lruheadtail = new s_entry[sets];
	
	for (int s = 0; s < sets; s++)
	{
		table[s] = new map<unsigned long long, pair<T,int> >;
		lruheadtail[s].next = s*assoc;
		lruheadtail[s].prev = (s+1)*assoc - 1;
		for (int a = 0; a < assoc; a++)
		{
			typename m_entry::iterator mi = table[s]->insert( make_pair(a, make_pair((T)0, s*assoc+a)) ).first;		// Bogus entry a -> (0, lru entry)
			lrustack[s*assoc+a].next = s*assoc+a+1;
			lrustack[s*assoc+a].prev = s*assoc+a-1;
			lrustack[s*assoc+a].entry = mi;
		}
		lrustack[s*assoc].prev = -1;
		lrustack[(s+1)*assoc-1].next = -1;
	}
}

template <typename T>
lru_table<T>::~lru_table()
{
	if (table) delete [] table;
	table = NULL;
}

template <typename T>
void lru_table<T>::free_storage()
{
	delete [] table;
	table = NULL;
}
		
template <typename T>
int lru_table<T>::get_size()
{
	return sets*assoc*(tag_bits-free_tag_bits+data_bits);
}
		
template <typename T>
pair<T, int> lru_table<T>::lookup (int s, unsigned long long tag)	// returns table value and how many bits of the tag matched.
{
	m_entry *set_map = table[s];
	typename m_entry::iterator e2 = set_map->lower_bound(tag);
	if (e2 != set_map->end() && e2->first == tag)
	{
		return make_pair(e2->second.first, 0);
	}
	else
	{
		typename m_entry::iterator e1 = e2; --e1;	//e1 = preceding element
		if ( e2 == set_map->end() || (e2->first ^ tag) > (e1->first ^ tag) )	// if e2 matches fewer bits
			return make_pair(e1->second.first, e1->first ^ tag);
		else
			return make_pair(e2->second.first, e2->first ^ tag);	// Let caller deal with the bits that mismatch
	}
}

template <typename T>
void lru_table<T>::update (int s, unsigned long long tag, int tag_fuzziness, T new_value)
{
	m_entry *set_map = table[s];
	typename m_entry::iterator e2 = set_map->lower_bound(tag);
	typename m_entry::iterator new_entry;

	//assert(set_map->size() == assoc);
	
	//printf ("Update tag=%llx fuzz=%d value=%d\n", tag,tag_fuzziness,new_value);
	//printf ("  set_map->size = %llu\n", set_map->size());
	//printf ("  lower_bound tag=%llx, value=%d lru=%d\n", e2->first, e2->second.first, e2->second.second);

	if (e2 != set_map->end() && e2->first == tag)
	{
		//printf ("  exact match\n");
		new_entry = e2;
		e2->second.first = new_value;
	}
	else
	{
		//printf ("  no exact match\n");
		
		typename m_entry::iterator e1 = e2; --e1;	//e1 = preceding element
		if (e2 == set_map->end() || (e2->first ^ tag) > (e1->first ^ tag) )	// if e2 matches fewer bits, use e1.
		{
			//printf ("  choosing e1\n");
			//printf ("    tag=%llx, value=%d lru=%d\n", e1->first, e1->second.first, e1->second.second);
			if ((e1->first^tag) >= (1<<tag_fuzziness)) 	//fail fuzziness check: use a new entry
			{
				//printf ("    fuzziness not ok, replacing LRU\n");
				set_map->erase(lrustack[ lruheadtail[s].prev ].entry);
				new_entry = lrustack[lruheadtail[s].prev].entry = set_map->insert( make_pair(tag, make_pair((T)new_value, lruheadtail[s].prev)) ).first;
			}
			else
			{
				//printf ("     fuzziness ok, updating entry e1\n");
				new_entry = e1;
				e1->second.first = new_value;
			}
		}
		else
		{
			//printf ("  choosing e2\n");
			//printf ("    tag=%llx, value=%d lru=%d\n", e2->first, e2->second.first, e2->second.second);
			if ((e2->first^tag) >= (1<<tag_fuzziness)) 	//fail fuzziness check: use a new entry
			{
				//printf ("    fuzziness not ok, replacing LRU\n");
				set_map->erase(lrustack[ lruheadtail[s].prev ].entry);
				new_entry = lrustack[lruheadtail[s].prev].entry = set_map->insert( make_pair(tag, make_pair((T)new_value, lruheadtail[s].prev)) ).first;
			}
			else
			{
				//printf ("     fuzziness ok, updating entry e1\n");
				new_entry = e2;
				e2->second.first = new_value;
			}
		}			
	}
	
	// new_entry contains iterator to new table entry with new_value already updated.
	// Adjust LRU bits here.
	//assert(set_map->size() == assoc);
	
	int lru_index = new_entry->second.second;
	if (lrustack[lru_index].prev >= 0)		// If this is the top element, nothing to do
	{
		lrustack[lrustack[lru_index].prev].next = lrustack[lru_index].next;
		if (lrustack[lru_index].next >= 0)	// If not bottom element...
			lrustack[lrustack[lru_index].next].prev = lrustack[lru_index].prev;
		else							// else this is bottom element
			lruheadtail[s].prev = lrustack[lru_index].prev;
		
		// Stick this element at the top
		lrustack[lru_index].prev = -1;
		lrustack[lru_index].next = lruheadtail[s].next;
		lrustack[ lruheadtail[s].next ].prev = lru_index;
		lruheadtail[s].next = lru_index;
	}	
	
}

bpred2_lru::bpred2_lru(int log_sets, int assoc, int history_bits, int free_tag_bits)
{
	if (history_bits < 0) fatal ("bpred2_lru history_bits must be >= 0");
	if (history_bits > 64) fatal ("bpred2_lru history_bits must be < 64");
	
	int tag_bits = 29 + history_bits - log_sets;
	if (tag_bits > 64)
	{
		free_tag_bits -= (tag_bits-64);
		tag_bits = 64;
	}
	
	table = new lru_table<char>(1<<log_sets, assoc, tag_bits, free_tag_bits, 2);
	this->log_sets = log_sets;
	this->history_bits = history_bits;
	this->assoc = assoc;
	this->free_tag_bits = free_tag_bits;
	baddr_mask = (1<<log_sets)-1;
}
bpred2_lru::~bpred2_lru()
{
	if (table) delete table;
}
void bpred2_lru::free_storage()
{
	if (table) table->free_storage();
}
bpred2* bpred2_lru::clone()
{
	bpred2_lru *p = new bpred2_lru(log_sets, assoc, history_bits, free_tag_bits);
	return p;
}
		
const char* bpred2_lru::get_name()
{
	static char str[16];
	
	sprintf (str, "lru[%d,%d,%d,%d]", log_sets, assoc, history_bits, free_tag_bits);
	return str;	
}

int bpred2_lru::get_size()
{
	return table->get_size();
}

int bpred2_lru::get_config(char* out)
{
	int outlen = 0;
	outlen += sprintf (out, "bpred2_lru %d %d %d %d \n", 
		log_sets, assoc, history_bits, free_tag_bits);

	return outlen;
}

bool bpred2_lru::lookup(bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	n_lookups++;
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
	{
		n_hits++;
		return 1;
	}
	
	baddr >>= MD_BR_SHIFT;
	unsigned int btag = baddr >> log_sets;
	i_set = baddr & baddr_mask;
	
	tag = (global_history_r >> (64-history_bits)) | ((unsigned long long)btag<<history_bits);
	pair <char, int> r = table->lookup(i_set, tag);
	
	counter = r.first;
	if (!!taken == ((r.first&2)>>1))
		n_hits++;
	
	return ((r.first) & 2) >> 1;
}

bool bpred2_lru::update(enum e_update_mode mode, bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
		return 1;
		
	if (mode & update_predictor)
	{
		n_updates++;
		char new_counter = automata[counter*2 + !!taken]; 
		table->update(i_set, tag, 0, new_counter);
		return (new_counter & 2)>>1;
	}			
	return (counter&2)>>1;
}







// 2bc-gskew

bpred2_2bcgskew::bpred2_2bcgskew(e_update_type update_type)
{
	this->update_type = update_type;
}
bpred2_2bcgskew::~bpred2_2bcgskew()
{
	for (int i=0;i<4;i++) {
		if (subpreds[i]) delete subpreds[i];
		subpreds[i] = NULL;
	}
}
void bpred2_2bcgskew::free_storage()
{
	for (int i=0;i<4;i++) {
		subpreds[i]->free_storage();
	}
}
bpred2* bpred2_2bcgskew::clone()
{
	bpred2_2bcgskew *p = new bpred2_2bcgskew(update_type);
	
	for (int i=0;i<4;i++)
	{
		if (subpreds[i])
			p->subpreds[i] = subpreds[i]->clone();
		else
			p->subpreds[i] = NULL;
	}
	return p;
}

		
		
const char* bpred2_2bcgskew::get_name()
{

	static char name[1024];
	sprintf (name, "2bcgskew%s[ ", update_type == update_ev8used ? ".ev8" : update_type==update_used ? ".used" : ".all");
	for (int i=0;i<4;i++)
	{
		strcat (name, subpreds[i]->get_name());
		strcat (name, " ");
	}
	strcat (name, "]");
	return name;		
}

int bpred2_2bcgskew::get_size()
{
	return subpreds[0]->get_size() + subpreds[1]->get_size() + subpreds[2]->get_size() + subpreds[3]->get_size();
}

int bpred2_2bcgskew::get_config(char* out)
{
	int outlen = 0;
	
	outlen += sprintf (out+outlen, "    "); outlen += subpreds[0]->get_config(out+outlen);
	outlen += sprintf (out+outlen, "    "); outlen += subpreds[1]->get_config(out+outlen);
	outlen += sprintf (out+outlen, "    "); outlen += subpreds[2]->get_config(out+outlen);
	outlen += sprintf (out+outlen, "    "); outlen += subpreds[3]->get_config(out+outlen);
	
	outlen += sprintf (out+outlen, "bpred2_2bcgskew %s\n", 
		update_type == update_all ? "update_all" : 
		update_type == update_used ? "update_used" : 
		update_type == update_ev8used? "update_ev8used" : "unknown");
		
	return outlen;
}

bool bpred2_2bcgskew::lookup(bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{

	n_lookups++;
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
	{
		n_hits++;
		return 1;
	}	

	sum = 0;
	
	for (int i=0;i<3;i++)
		sum += (subpred_res[i] = subpreds[i]->lookup (taken, op, baddr, target_addr));
	subpred_res[3] = subpreds[3]->lookup (true, op, baddr, target_addr);
	
	if (subpred_res[3])	// 1 = bimodal
	{
		n_hits += (taken == subpred_res[0]);
		return prediction = subpred_res[0];
	}
	else	//gskew vote
	{
		n_hits += (taken == (sum > 1));
		return prediction = (sum>1);
	}

}
bool bpred2_2bcgskew::update(enum e_update_mode mode, bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{

	n_updates++;
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
		return 1;
	
	int newsum = sum;
	vector<char>::iterator j;
	vector< pair<bpred2*,bool> >::iterator i;
	
	if (update_type == update_all || mode == update_history)		//just update everything if we're updating only history.
	{
		newsum = 0;
		bool newbim;
		
		newsum += (newbim = subpreds[0]->update (mode, taken, op, baddr, target_addr));
		newsum += subpreds[1]->update (mode, taken, op, baddr, target_addr);
		newsum += subpreds[2]->update (mode, taken, op, baddr, target_addr);
		bool newmeta = subpred_res[3];
		 
		if ((sum>1) != subpred_res[0])
		{
			newmeta = subpreds[3]->update ((e_update_mode)(mode &~update_history), taken==subpred_res[0], op, baddr, target_addr);
			subpreds[3]->update (update_history, taken, op, baddr, target_addr);
		}
		else
			subpreds[3]->update ((e_update_mode)(mode &~ update_predictor), taken, op, baddr, target_addr);	
			
		return newmeta ? newbim : (newsum > 1); 
	}
	else if (update_type == update_used)
	{
		newsum = 0;
		bool newbim = subpred_res[0];
		
		if (taken == prediction)
		{
			if (subpred_res[3] || taken == subpred_res[0]) newsum += (newbim = subpreds[0]->update (mode, taken, op, baddr, target_addr));
				else newsum += subpreds[0]->update ((e_update_mode)(mode &~ update_predictor), taken, op, baddr, target_addr);
			if (!subpred_res[3] && taken == subpred_res[1]) newsum += subpreds[1]->update (mode, taken, op, baddr, target_addr);
				else newsum += subpreds[1]->update ((e_update_mode)(mode &~ update_predictor), taken, op, baddr, target_addr);
			if (!subpred_res[3] && taken == subpred_res[2]) newsum += subpreds[2]->update (mode, taken, op, baddr, target_addr);
				else newsum += subpreds[2]->update ((e_update_mode)(mode &~ update_predictor), taken, op, baddr, target_addr);
		}
		else
		{		
			newsum += (newbim = subpreds[0]->update (mode, taken, op, baddr, target_addr));
			newsum += subpreds[1]->update (mode, taken, op, baddr, target_addr);
			newsum += subpreds[2]->update (mode, taken, op, baddr, target_addr);
		}
		
		bool newmeta = subpred_res[3];
		
		if ((sum>1) != subpred_res[0])
		{
			newmeta = subpreds[3]->update ((e_update_mode)(mode &~update_history), taken==subpred_res[0], op, baddr, target_addr);
			subpreds[3]->update (update_history, taken, op, baddr, target_addr);
		}
		else
			subpreds[3]->update ((e_update_mode)(mode &~ update_predictor), taken, op, baddr, target_addr);	
				
		return newmeta ? newbim : (newsum > 1); 		
	}
	else if (update_type == update_ev8used)
	{
		newsum = 0;
		bool newbim = subpred_res[0];
		bool update[3];
		

		bool newmeta = subpred_res[3];
		
		if ((sum>1) != subpred_res[0])
		{
			newmeta = subpreds[3]->update ((e_update_mode)(mode &~update_history), taken==subpred_res[0], op, baddr, target_addr);
			subpreds[3]->update (update_history, taken, op, baddr, target_addr);
		}
		else
			subpreds[3]->update ((e_update_mode)(mode &~ update_predictor), taken, op, baddr, target_addr);	


		if (taken == prediction && (sum == 3*taken))
		{
			update[0] = update[1] = update[2] = 0;
		}
		else if (!(taken == prediction) && (taken != (newmeta ? subpred_res[0] : (sum > 1))))
		{
			update[0] = update[1] = update[2] = 1;					// Still bad. Update all.
		}		
		else
		{
			update[0] = (newmeta || taken == subpred_res[0]);		// Correct prediction with new meta
			update[1] = (!newmeta && taken == subpred_res[1]);
			update[2] = (!newmeta && taken == subpred_res[2]);
		}
		
		if (update[0]) newsum += (newbim = subpreds[0]->update (mode, taken, op, baddr, target_addr));
			else newsum += subpreds[0]->update ((e_update_mode)(mode &~ update_predictor), taken, op, baddr, target_addr);
		if (update[1]) newsum += subpreds[1]->update (mode, taken, op, baddr, target_addr);
			else newsum += subpreds[1]->update ((e_update_mode)(mode &~ update_predictor), taken, op, baddr, target_addr);
		if (update[2]) newsum += subpreds[2]->update (mode, taken, op, baddr, target_addr);
			else newsum += subpreds[2]->update ((e_update_mode)(mode &~ update_predictor), taken, op, baddr, target_addr);

		return newmeta ? newbim : (newsum > 1); 				
	}
	else
	{
		fatal ("Unknown bpred2_2bcgskew update type %d", update_type);
	}

	return (newsum > 0);

}

bpred2_yags::bpred2_yags(//int c_size, int c_hyssize, int c_histbits, int c_sharebits, 
		bpred2 *bp2_choice, int dc_size, int dc_hyssize, int dc_histbits, int dc_sharebits, int dc_tagabits, int dc_taghbits, int dc_tagashift, int dc_taghshift)
{
	/*if (c_size < 0) fatal ("bpred2_yags c_size %d must be > 0", c_size);
	if (c_size > 28) fatal ("bpred2_yags c_size %d must be < 28", c_size);
	if (c_hyssize < 0) fatal ("bpred2_yags c_hyssize %d must be > 0", c_hyssize);
	if (c_hyssize > c_size) fatal ("bpred2_yags c_hyssize %d must be < c_size", c_hyssize);
	if (c_histbits < 0) fatal ("bpred2_yags c_histbits %d must be > 0", c_histbits);
	if (c_histbits > 63) fatal ("bpred2_yags c_histbits %d must be < 64", c_histbits);
	if (c_sharebits < 0) fatal ("bpred2_yags c_sharebits %d must be > 0", c_sharebits);
	if (c_sharebits > c_histbits) fatal ("bpred2_yags c_sharebits %d must be < c_histbits", c_sharebits);
	*/
	if (!bp2_choice) fatal ("bpred2_yags bp2_choice must be a valid predictor.");
	if (dc_size < 0) fatal ("bpred2_yags dc_size %d must be > 0", dc_size);
	if (dc_size > 28) fatal ("bpred2_yags dc_size %d must be < 28", dc_size);
	if (dc_hyssize < 0) fatal ("bpred2_yags dc_hyssize %d must be > 0", dc_hyssize);
	if (dc_hyssize > dc_size) fatal ("bpred2_yags dc_hyssize %d must be < dc_size", dc_hyssize);
	if (dc_histbits < 0) fatal ("bpred2_yags dc_histbits %d must be > 0", dc_histbits);
	if (dc_histbits > 63) fatal ("bpred2_yags dc_histbits %d must be < 64", dc_histbits);
	if (dc_sharebits < 0) fatal ("bpred2_yags dc_sharebits %d must be > 0", dc_sharebits);
	if (dc_sharebits > dc_histbits) fatal ("bpred2_yags dc_sharebits %d must be < dc_histbits", dc_sharebits);
	if (dc_tagabits < 0) fatal ("bpred2_yags dc_tagabits %d must be > 0", dc_tagabits);
	if (dc_taghbits < 0) fatal ("bpred2_yags dc_taghbits %d must be > 0", dc_taghbits);
	//if (dc_taghbits + dc_tagabits > 32) fatal ("bpred2_yags dc_tagabits %d + dc_taghbits %d must be <= 32", dc_tagabits, dc_taghbits);
	if (dc_taghbits + dc_tagabits > 32) {printf ("bpred2_yags dc_tagabits %d + dc_taghbits %d must be <= 32\n", dc_tagabits, dc_taghbits); dc_taghbits = 32-dc_tagabits; }
	if (dc_tagashift < 0) fatal ("bpred2_yags dc_tagashift %d must be > 0", dc_tagashift);
	if (dc_tagashift > 28) fatal ("bpred2_yags dc_tagashift %d too big...", dc_tagashift);	
	if (dc_taghshift < 0) fatal ("bpred2_yags dc_taghshift %d must be > 0", dc_taghshift);
	if (dc_taghshift > 28) fatal ("bpred2_yags dc_taghshift %d too big...", dc_taghshift);	
	

	/*this->c_size = c_size;
	this->c_hyssize = c_hyssize;
	this->c_histbits = c_histbits;
	this->c_sharebits = c_sharebits;*/
	this->dc_size = dc_size;
	this->dc_hyssize = dc_hyssize;
	this->dc_histbits = dc_histbits;
	this->dc_sharebits = dc_sharebits;
	this->dc_tagabits = dc_tagabits;
	this->dc_taghbits = dc_taghbits;
	this->dc_tagashift = dc_tagashift;
	this->dc_taghshift = dc_taghshift;
	
	mask_a = (1<<dc_tagabits)-1;
	mask_h = (1<<dc_taghbits)-1;

	
	this->bp2_choice = bp2_choice;
	bp2_t = new bpred2_ghist(dc_size, dc_hyssize, dc_histbits, dc_sharebits, NULL);		// Use default hash
	bp2_nt = new bpred2_ghist(dc_size, dc_hyssize, dc_histbits, dc_sharebits, NULL);	
	tag_t = new unsigned int[1<<dc_size];
	tag_nt = new unsigned int[1<<dc_size];
	
}

bpred2_yags::~bpred2_yags()
{
	if (bp2_choice) delete bp2_choice;
	if (bp2_t) delete bp2_t;
	if (bp2_nt) delete bp2_nt;
	if (tag_t) delete [] tag_t;
	if (tag_nt) delete [] tag_nt;
	tag_t = tag_nt = NULL;
	bp2_t = bp2_nt = NULL;
	bp2_choice = NULL;
}
void bpred2_yags::free_storage()
{
	if (bp2_choice) bp2_choice->free_storage();
	if (bp2_t) bp2_t->free_storage();
	if (bp2_nt) bp2_nt->free_storage();
	if (tag_t) delete [] tag_t;
	if (tag_nt) delete [] tag_nt;
	tag_t = tag_nt = NULL;
}
bpred2* bpred2_yags::clone()
{
	bpred2 *newchoice = bp2_choice->clone();
	bpred2 *newyags = new bpred2_yags(newchoice, dc_size, dc_hyssize, dc_histbits, dc_sharebits, dc_tagabits, dc_taghbits, dc_tagashift, dc_taghshift);
	return newyags;
}

const char* bpred2_yags::get_name()
{
	static char name[1024];
	sprintf (name, "bpred2_yags(%d,%d,%d,%d,%d,%d,%d,%d)[%s]", 
		dc_size, dc_hyssize, dc_histbits, dc_sharebits, dc_tagabits, dc_taghbits, dc_tagashift, dc_taghshift,
		bp2_choice->get_name());
	return name;
}
int bpred2_yags::get_size()
{
	int bp_size = bp2_choice->get_size();
	return bp_size + dc_histbits + 2* ((1<<dc_size)*(dc_tagabits + dc_taghbits+1) + (1<<dc_hyssize));
}

int bpred2_yags::get_config(char *out)
{
	int len = sprintf (out, "    ");
	len += bp2_choice->get_config(out+len);
	len += sprintf (out+len, "bpred2_yags %d %d %d %d %d %d %d %d\n", 
			dc_size, dc_hyssize, dc_histbits, dc_sharebits, dc_tagabits, dc_taghbits, dc_tagashift, dc_taghshift);
	return len;
}

bool bpred2_yags::lookup(bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	n_lookups++;
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
	{
		n_hits++;
		return 1;
	}	

	s_choice_result = bp2_choice->lookup (taken, op, baddr, target_addr);
	s_t_pred = bp2_t->lookup(taken, op, baddr, target_addr);
	s_nt_pred = bp2_nt->lookup(taken, op, baddr, target_addr);
	
	s_tag = ((baddr>>(MD_BR_SHIFT + dc_tagashift))&mask_a) | (((bpred2::global_history>>dc_taghshift)&mask_h)<<dc_tagabits);
	
	if (tag_t[bp2_t->t_index] != s_tag)
		s_t_pred = -1;
	if (tag_nt[bp2_nt->t_index] != s_tag)
		s_nt_pred = -1;
	
	//if (bp2_t->t_index != bp2_nt->t_index)
	//	fatal ("YAGS: Why do the taken and not taken direction caches have different table indices? %d %d", bp2_t->t_index, bp2_nt->t_index);
	
	s_result = s_choice_result;
	if (s_choice_result)
	{
		if (s_nt_pred >= 0) s_result = s_nt_pred;
	}
	else
	{
		if (s_t_pred >= 0) s_result = s_t_pred;
	}
	
	if (!s_result == !taken) n_hits++;
	
	return s_result;
}

bool bpred2_yags::update(enum e_update_mode mode, bool taken, enum md_opcode op, md_addr_t baddr, md_addr_t target_addr)
{
	n_updates++;
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
		return 1;
	
	// update bp2_choice if:
	// - it is correct
	// - or it is incorrect *and* direction cache was also wrong.
	bool new_choice = s_choice_result;
	if (! ( taken != s_choice_result && taken == s_result ) )
		new_choice = bp2_choice->update(mode, taken, op, baddr, target_addr);
	
	
	// update chosen direction cache if:
	// - bp2_choice was incorrect or
	// - the lookup produced a hit in direction cache.
	
	bool tag_hit = (s_choice_result ? s_nt_pred : s_t_pred) >= 0;
	if ( taken != s_choice_result || tag_hit )
	{
		//if (bp2_t->t_index != bp2_nt->t_index)
		//	fatal ("YAGS: Why do the taken and not taken direction caches have different table indices? %d %d", bp2_t->t_index, bp2_nt->t_index);

		bp2_nt->update( (bpred2::e_update_mode)(mode & (s_choice_result ? update_normal : update_history)), taken, op, baddr, target_addr);
		bp2_t->update ( (bpred2::e_update_mode)(mode & (s_choice_result ? update_history : update_normal)), taken, op, baddr, target_addr);	

		if (!tag_hit && (mode & update_predictor))
		{
			if (s_choice_result) bp2_nt->table[bp2_nt->t_index] = 0x1;	//weakly not-taken
			else bp2_t->table[bp2_t->t_index] = 0x2;						//weakly taken
			unsigned int* tag_table = s_choice_result ? tag_nt : tag_t;
			tag_table[bp2_t->t_index] = s_tag;							// tag miss: Allocate new entry.
		}
		
		/*if (tag_hit)
		{
			unsigned int* tag_table = s_choice_result ? tag_nt : tag_t;
			if (tag_table[bp2_t->t_index] != s_tag) fatal ("bpred2_yags::update: Tag hit, but tags differ");
		}*/

	}
	

	bool new_result = new_choice;
	if (new_choice)
	{
		if (tag_nt[bp2_nt->t_index] == s_tag)
			new_result = bp2_nt->table[bp2_nt->t_index] >> 1;
	}
	else
	{
		if (tag_t[bp2_t->t_index] == s_tag) 
			new_result = bp2_t->table[bp2_t->t_index] >> 1;
	}
	
	return new_result;
}











// Pulled out perturb section here.

void bpred2::perturb()
{
}
void bpred2_static::perturb()
{
	bp_type = (bpstatic_type)(rand()%3);		
}
void bpred2_twolev::perturb()
{
	log_table_size1 = max(1, min(24, log_table_size1 + rand()%3 - rand()%3)); 
	int t = rand()%3 - rand()%3;
	log_table_size2 = max(0, min(20, log_table_size2 + t)); 
	log_hysteresis_size = max(0, min(log_table_size2, log_hysteresis_size + rand()%2 - rand()%2 + t));
	t = rand()%3 - rand()%3;
	history_bits = max(1, min(50, history_bits + t)); 
	addr_share_bits = max(0, min(history_bits, addr_share_bits + t + rand()%2 - rand()%2));
}
void bpred2_ghist::perturb()
{
	int t = rand()%3 - rand()%3;
	log_table_size = max(0, min(19, log_table_size + t)); 
	log_hysteresis_size = max(0, min(log_table_size, log_hysteresis_size + rand()%2 - rand()%2 + t));
	t = rand()%3 - rand()%3;
	history_bits = max(0, min(50, history_bits + t)); 
	addr_share_bits = max(0, min(history_bits, addr_share_bits + t + rand()%2 - rand()%2));
}
void bpred2_comb::perturb()
{
	int r = rand()%4;
	switch (r)
	{
		case 0: b1->perturb(); break;
		case 1: b2->perturb(); break;
		case 2: bc->perturb(); break;
		case 3: update_type = (e_update_type)(update_all + update_ev8used - update_type); break;
	}
}
void bpred2_vote::perturb()
{
	int r = rand() % vsubpred.size();
	vsubpred[r].first->perturb();

	r = rand()%6;	
	if (r == update_all || r == update_used || r == update_ev8used)
		update_type = (e_update_type)r;
	
}
void bpred2_lru::perturb()
{
	int t = rand()%3 - rand()%3;
	log_sets = max(0, min(30, log_sets + t));
	history_bits = max(0, min(30+2*log_sets, history_bits + rand()%2 - rand()%2 + t));
	//Don't touch assoc for now. It'll just converge towards increasing associativity that's hard to build.
}
void bpred2_2bcgskew::perturb()
{
	int r = rand()%6;
	if (r == update_all || r == update_used || r == update_ev8used)
		update_type = (e_update_type)r;

	r = rand()%4;
	subpreds[r]->perturb();
}
void bpred2_yags::perturb()
{
	int r = rand()%3;
	if (r <= 1) bp2_choice->perturb();
	if (r >= 1)
	{
		int t = rand()%3 - rand()%3;
		dc_size = max(0, dc_size + t); 
		dc_hyssize = max(0, min(dc_size, dc_hyssize + rand()%2 - rand()%2 + t));

		t = rand()%3 - rand()%3;
		dc_histbits = max(0, min(50, dc_histbits + t));
		dc_sharebits = max(0, min(dc_histbits, dc_sharebits + t + rand()%2 - rand()%2));
		
		dc_tagabits = max(0, min(28, dc_tagabits + rand()%2 - rand()%2));
		dc_taghbits = max(0, min(32-dc_tagabits, dc_taghbits + rand()%2 - rand()%2));
		dc_tagashift = max(0, min(28, dc_tagashift + rand()%2 - rand()%2));
		dc_taghshift = max(0, min(36, dc_taghshift + rand()%2 - rand()%2));
	}
}
