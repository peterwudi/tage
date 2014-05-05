#ifndef BPRED_H
#define BPRED_H
#endif

#define dassert(a) assert(a)

#include <stdio.h>

/* the includes */
//#include <inttypes.h>



/* ======================== TAGE ============================= */
#define TAGE 1 

//a limit predictor for 256Kbits: no AHEAD pipelining, 13 components

#define NHIST 12                //12 tagged components
#define LOGB 14                 // log of the number of entries in the base bimodal predictor
#define HYSTSHIFT 2             // sharing an hysteris bit between 4 bimodal predictor entries
#define LOGG (LOGB-3)           // base 2 logarithm of number of entries  on each tagged component
#define TBITS 7                 // minimum tag width (shortest history length table)
#define MINHIST 4               // shortest history length
#define MAXHIST 640             // longest history length

#define LOGL 8                  //256 entries loop predictor
#define WIDTHNBITERLOOP 14

#define EPOCH	1000	//each epoch consists of 1000 branch accesses
#define WHICHTABLE 12 

/*
 Total storage for the submitted predictor
 TAGE
 Table T0: 20Kbits (16K prediction + 4K hysteresis)
 Tables T1 and T2: 12Kbits each; 
 Tables T3 and T4: 26 Kbits each; 
 Table T5: 28Kbits; 
 Table  T6: 30Kbits; 
 Table T7: 16 Kbits  ;  
 Tables T8 and  T9: 17 Kbits each, 
 Table T10: 18 Kbits;  
 Table T11: 9,5 Kbits; 
 Table T12 10 Kbits
 Total of storage for the TAGE tables: 239,5 Kbits
 Loop predictor: 256 entries * 52 bits= 13 Kbits
 Total of storage for the TAGE tables: 241,5 Kbits + 13 Kbits = 260,608 bits
 Extra storage:  2*640 history bits + 2*16 path history bits + 
 4 bits for  USE_ALT_ON_NA + 19 bits for the TICK counter + 
 2 bits for Seed in the "pseudo-random" counter= 1,337 bits + 
 7bits on the WITHLOOP counter = 1,344 bits
 Total storage= 260,608 + 1,344 = 261,952 bits
 */

#define CWIDTH 3                // predictor counter width on the tagged tables

#define BUFFERHIST 128 * 1024
//size of the history buffer: to allow fast management we use a very large buffer
//we replace a global shift by a pointer management

/* utility class for index computation */

/* this is the cyclic shift register for folding 
 * a long global history into a smaller number of bits; see P. Michaud's PPM-like predictor at CBP-1
 */

/* ********************* */
/* class folded _history */
/* ********************* */
/* *** the structs *** */
typedef struct tage_folded_history_struct {
	unsigned comp;
	int CLENGTH;
	int OLENGTH;
	int OUTPOINT;
} tage_folded_history;

/* class bentry */
typedef struct bentry {
	int hyst;
	int pred;
} bentry;

/* class gentry */
typedef struct gentry {
	int ctr;
	unsigned int tag;
	int u;
} gentry;

/* class my_predictor */
typedef struct my_predictor {
	int USE_ALT_ON_NA;              // "Use alternate prediction on newly allocated":  a 4-bit counter  to determine whether the newly allocated entries should be considered as  valid or not for delivering  the prediction

	int TICK, LOGTICK;              //control counter for the smooth resetting of useful counters

	int phist;                      // use a path history as on  the OGEHL predictor
	//for managing global history  
	unsigned int *GHIST;
	unsigned int *ghist;
	int ptghist;

	tage_folded_history ch_i[NHIST + 1];  //utility for computing TAGE indices
	tage_folded_history ch_t[2][NHIST + 1];       //utility for computing TAGE tags

	bentry *btable;         //bimodal TAGE table
	gentry *gtable[NHIST + 1];      // tagged TAGE tables

	int TB[NHIST + 1];              // tag width for the different tagged tables
	int m[NHIST + 1];               // used for storing the history lengths
	int logg[NHIST + 1];            // log of number entries of the different tagged tables
	int GI[NHIST + 1];              // indexes to the different tables are computed only once  
	int GTAG[NHIST + 1];            // tags for the different tables are computed only once  
	int BI;                 // index of the bimodal table
	int Seed;                       // for the pseudo-random number generator
	int pred_taken;         // prediction
	int alttaken;           // alternate  TAGEprediction
	int tage_pred;          // TAGE prediction
	int HitBank;                    // longest matching bank
	int AltBank;                    // alternate matching bank

	char c;

	FILE *log;
	unsigned int counters[NHIST + 1];
	unsigned int clock;
	unsigned int g_clock;
	unsigned int tag_needed;
} my_predictor;

/* *** function declarations *** */
tage_folded_history *new_tage_folded_history();
void init_tage_folded_history(tage_folded_history *f, int original_length, int compressed_length);
void update_tage_folded_history(tage_folded_history *f, unsigned int *h);
bentry *new_bentry();
void init_bentry(bentry *b);
gentry *new_gentry();
int bindex (int pc);
int F (my_predictor *p, int A, int size, int bank);
int gindex (my_predictor *p, int pc, int bank);
unsigned int gtag (my_predictor *p, int pc, int bank);
void ctrupdate (int *ctr, int taken, int nbits);
int getbim (my_predictor *p, int pc);
void baseupdate (my_predictor *p, int pc, int Taken);
int MYRANDOM (my_predictor *p);
void updateghist (my_predictor *p, unsigned int **h, int dir, unsigned int *tab, int *PT);

my_predictor *new_my_predictor();
char *my_predictor_predict (my_predictor *p, int pc);
void my_predictor_update (my_predictor *p, int pc, int taken);
void my_predictor_update_histories(my_predictor *p, int pc, int taken);


/* ======================== TAGE ============================= */
