#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

//#include "tage.h"

/* class bentry */
typedef struct bentry {
	int hyst;
	int pred;
} bentry;

typedef struct gentry {
	int ctr;
	unsigned int tag;
	int u;
} gentry;


typedef struct tage_folded_history_struct {
	unsigned comp;
	int CLENGTH;
	int OLENGTH;
	int OUTPOINT;
} tage_folded_history;

#define numTageTables 4
//#define NHIST 12                //12 tagged components
#define LOGB 14                 // log of the number of entries in the base bimodal predictor
#define HYSTSHIFT 2             // sharing an hysteris bit between 4 bimodal predictor entries
#define LOGG (LOGB-3)           // base 2 logarithm of number of entries  on each tagged component
#define TBITS 7                 // minimum tag width (shortest history length table)
#define MINHIST 4               // shortest history length
#define MAXHIST 640             // longest history length

// See if these are useful
int tag_needed = 0;
int clock = 0;
int g_clock = 0;
int USE_ALT_ON_NA = 0;
int Seed = 0;
int counters[numTageTables] = {0}; 
int LOGTICK = 0;
int TICK = 0;
int phist = 0;                      // use a path history as on  the OGEHL predictor
int ptghist = 0;

tage_folded_history ch_i[numTageTables];  //utility for computing TAGE indices
tage_folded_history ch_t[2][numTageTables];       //utility for computing TAGE tags


bentry *btable;         //bimodal TAGE table
gentry *gtable[numTageTables];      // tagged TAGE tables

int TB[numTageTables];              // tag width for the different tagged tables
int m[numTageTables];               // used for storing the history lengths
int logg[numTageTables];            // log of number entries of the different tagged tables
int GI[numTageTables];              // indexes to the different tables are computed only once  
int GTAG[numTageTables];            // tags for the different tables are computed only once  
int BI;								// index of the bimodal table
int Seed;							// for the pseudo-random number generator
int pred_taken;						// prediction
int alttaken;						// alternate  TAGEprediction
int tage_pred;						// TAGE prediction
int HitBank;						// longest matching bank
int AltBank;						// alternate matching bank

void main()
{
	int dir = 0;

	// These are fake
	int baddr = 0;
	int taken = 0;

	tage_init();
	for (;;)
	{
		dir = tage_predict(tage, baddr);
		tage_update(tage, baddr, taken);
		tage_update_histories(tage, baddr, taken);
	}
}

/* *********************** TAGE ************************* */
void tage_init()
{
	int i = 0;
	int j = 0;

	tag_needed = 0;
	clock = 0;
	g_clock = 0;

	for (i = 0; i < numTageTables; i++)
	{
		counters[i] = 0;
	}

	USE_ALT_ON_NA = 0;
	Seed = 0;
	LOGTICK = 19;                //log of the period for useful tag smooth resetting
	TICK = (1 << (LOGTICK - 1));      //initialize the resetting counter to the half of the period

	phist = 0;	
	ptghist = 0;

	// computes the geometric history lengths   
	m[0] = MINHIST;
	m[numTageTables-1] = MAXHIST;

	for (i = 1; i < numTageTables; i++)
	{
		m[i]=
			(int) (((double) MINHIST *
			pow ((double) (MAXHIST) / (double) MINHIST,
			(double) (i - 1) / (double) ((numTageTables - 1)))) + 0.5);
	}

	//widths of the partial tags
	TB[0] = TBITS;
	TB[1] = TBITS;
	TB[2] = TBITS + 1;
	TB[3] = TBITS + 1;
	//TB[5] = TBITS + 2;
	//TB[6] = TBITS + 3;
	//TB[7] = TBITS + 4;
	//TB[8] = TBITS + 5;
	//TB[9] = TBITS + 5;
	//TB[10] = TBITS + 6;
	//TB[11] = TBITS + 7;
	//TB[12] = TBITS + 8;

	// log2 of number entries in the tagged components
	
	logg[0] = LOGG-1;
	logg[1] = LOGG-1;
	logg[2] = LOGG;
	logg[3] = LOGG;

	//for (i = 1; i <= 2; i++)
	//		logg[i] = LOGG - 1;
	//for (i = 3; i <= 6; i++)
	//		logg[i] = LOGG;
	//for (i = 7; i <= 10; i++)
	//		logg[i] = LOGG - 1;
	//for (i = 11; i <= 12; i++)
	//		logg[i] = LOGG - 2;

	//initialisation of index and tag computation functions
	for (i = 0; i < numTageTables; i++)
	{
		init_tage_folded_history (&(ch_i[i]), m[i], (logg[i]));
		init_tage_folded_history (&(ch_t[0][i]), ch_i[i].OLENGTH, TB[i]);
		init_tage_folded_history (&(ch_t[1][i]), ch_i[i].OLENGTH, TB[i] - 1);
	}

	//allocation of the predictor tables
	btable = (bentry *)malloc((1 << LOGB) * sizeof(bentry));
	for (i = 0; i < (1 << LOGB); i++)
	{
		btable[i].pred = 0;
		btable[i].hyst = 1;
	}

	for (i = 0; i < numTageTables; i++)
	{
		gtable[i] = (gentry *)malloc((1 << (logg[i])) * sizeof(gentry));
		for (j = 0; j < (1 << logg[i]); j++)
		{
			gtable[i][j].ctr = 0;
			gtable[i][j].tag = 0;
			gtable[i][j].u = 0;
		}
	}
}

tage_folded_history *new_tage_folded_history()
{
        tage_folded_history *f = (tage_folded_history *)(malloc(sizeof(tage_folded_history)));
        return f;
}

void init_tage_folded_history(tage_folded_history *f, int original_length, int compressed_length)
{
    f->comp = 0;
    f->OLENGTH = original_length;
    f->CLENGTH = compressed_length;
    f->OUTPOINT = f->OLENGTH % f->CLENGTH;
}

void update_tage_folded_history(tage_folded_history *f, int *h)
{
    f->comp = (f->comp << 1) | h[0];
    f->comp ^= h[f->OLENGTH] << f->OUTPOINT;
    f->comp ^= (f->comp >> f->CLENGTH);
    f->comp &= (1 << f->CLENGTH) - 1;

}
/* ********************* */
/* ********************* */

/* ****************** */
/* class my_predictor */
/* ****************** */
bentry *new_bentry()
{
        bentry* b = (bentry *)malloc(sizeof(struct bentry));
        b->pred = 0;
        b->hyst = 1;

        return b;
}

void init_bentry(bentry *b)
{
        //bentry* b = (bentry *)malloc(sizeof(struct bentry));
        b->pred = 0;
        b->hyst = 1;
}



gentry *new_gentry()
{
        gentry* g= (gentry *)malloc(sizeof(gentry));
        g->ctr = 0;
        g->tag = 0;
        g->u = 0;

        return g;
}



// index function for the bimodal table
int bindex (int pc)
{
        return ((pc) & ((1 << (LOGB)) - 1));
}

// the index functions for the tagged tables uses path history as in the OGEHL predictor
//F serves to mix path history
int F (int A, int size, int bank)
{
    int A1, A2;
        int Ax = A;

    Ax = Ax & ((1 << size) - 1);
    A1 = (Ax & ((1 << logg[bank]) - 1));
    A2 = (Ax >> logg[bank]);
    A2 =
        ((A2 << bank) & ((1 << logg[bank]) - 1)) + (A2 >> (logg[bank] - bank));
    Ax = A1 ^ A2;
    Ax = ((Ax << bank) & ((1 << logg[bank]) - 1)) + (Ax >> (logg[bank] - bank));
    return (Ax);
}

// gindex computes a full hash of pc, ghist and phist
int gindex (int pc, int bank)
{
    int index;
    int M = (m[bank] > 16) ? 16 : m[bank];

        index =
        pc ^ (pc >> (abs(p->logg[bank] - bank) + 1)) ^
        ch_i[bank].comp ^ F (phist, M, bank);

    return (index & ((1 << (logg[bank])) - 1));
}

//  tag computation
unsigned int gtag (int pc, int bank)
{
        int tag = 0;
        tag = pc ^ ch_t[0][bank].comp ^ (p->ch_t[1][bank].comp << 1);

    return (tag & ((1 << p->TB[bank]) - 1));
}

// up-down saturating counter
void ctrupdate (int *ctr, int taken, int nbits)
{
    if (taken)
        {
                if ((*ctr) < ((1 << (nbits - 1)) - 1))
                        (*ctr)++;
        }
    else
        {
                if ((*ctr) > -(1 << (nbits - 1)))
                        (*ctr)--;
        }
}

int getbim (my_predictor *p, int pc)
{
    return (p->btable[p->BI].pred > 0);
}

// update the bimodal predictor: a hysteresis bit is shared among 4 prediction bits
void baseupdate (my_predictor *p, int pc, int Taken)
{
    int inter = (p->btable[p->BI].pred << 1) + p->btable[p->BI >> HYSTSHIFT].hyst;
    if (Taken)
        {
                if (inter < 3)
                        inter += 1;
        }
    else if (inter > 0)
                inter--;
    p->btable[p->BI].pred = inter >> 1;
    p->btable[p->BI >> HYSTSHIFT].hyst = (inter & 1);
}

//just a simple pseudo random number generator: a 2-bit counter, used to avoid ping-pong phenomenom on tagged entry allocations
int MYRANDOM (my_predictor *p)
{
    p->Seed++;
    return (p->Seed & 3);
};


// shifting the global history:  we manage the history in a big table in order to reduce simulation time
void updateghist (my_predictor *p, int **h, int dir, int *tab, int *PT)
{
        int i = 0;

        if ((*PT) == 0)
        {
                for (i = 0; i < MAXHIST; i++)
                        tab[BUFFERHIST - MAXHIST + i] = tab[i];
                (*PT) = BUFFERHIST - MAXHIST;
                *h = &(tab[(*PT)]);
        }

        (*PT)--;
        (*h)--;
        (*h)[0] = (dir) ? 1 : 0;
}

my_predictor *new_my_predictor()
{
	int i = 0;
	int j = 0;

	my_predictor *p = (my_predictor *)malloc(sizeof(my_predictor));

	p->tag_needed = 0;      
 
	p->clock = 0; 
	p->g_clock = 0; 
	for (i = 0; i <= NHIST; i++)
	{
		p->counters[i] = 0;
	}

	p->c = 0;

	p->USE_ALT_ON_NA = 0;
	p->Seed = 0;
	p->LOGTICK = 19;                //log of the period for useful tag smooth resetting
	p->TICK = (1 << (p->LOGTICK - 1));      //initialize the resetting counter to the half of the period

	p->phist = 0;
	p->GHIST = (unsigned int *) malloc(BUFFERHIST);
	p->ghist = p->GHIST;

	for (i = 0; i < BUFFERHIST; i++)
	{
		// this is probably wrong
		//p->ghist[0] = 0;
		p->ghist[i] = 0;
	}

	p->ptghist = 0;

	// computes the geometric history lengths   
	p->m[1] = MINHIST;
	p->m[NHIST] = MAXHIST;
	for (i = 2; i <= NHIST; i++)
	{
		p->m[i] =
			(int) (((double) MINHIST *
			pow ((double) (MAXHIST) / (double) MINHIST,
			(double) (i - 1) / (double) ((NHIST - 1)))) + 0.5);
	}
	//widths of the partial tags
	p->TB[1] = TBITS;
	p->TB[2] = TBITS;
	p->TB[3] = TBITS + 1;
	p->TB[4] = TBITS + 1;
	p->TB[5] = TBITS + 2;
	p->TB[6] = TBITS + 3;
	p->TB[7] = TBITS + 4;
	p->TB[8] = TBITS + 5;
	p->TB[9] = TBITS + 5;
	p->TB[10] = TBITS + 6;
	p->TB[11] = TBITS + 7;
	p->TB[12] = TBITS + 8;

	// log2 of number entries in the tagged components
	for (i = 1; i <= 2; i++)
			p->logg[i] = LOGG - 1;
	for (i = 3; i <= 6; i++)
			p->logg[i] = LOGG;
	for (i = 7; i <= 10; i++)
			p->logg[i] = LOGG - 1;
	for (i = 11; i <= 12; i++)
			p->logg[i] = LOGG - 2;

	//initialisation of index and tag computation functions
	for (i = 1; i <= NHIST; i++)
	{
		init_tage_folded_history (&(p->ch_i[i]), p->m[i], (p->logg[i]));
		init_tage_folded_history (&(p->ch_t[0][i]), p->ch_i[i].OLENGTH, p->TB[i]);
		init_tage_folded_history (&(p->ch_t[1][i]), p->ch_i[i].OLENGTH, p->TB[i] - 1);
	}

	//allocation of the predictor tables
	p->btable = (bentry *)malloc((1 << LOGB) * sizeof(bentry));
	for (i = 0; i < (1 << LOGB); i++)
	{
		//p->btable[i] = new_bentry();
		p->btable[i].pred = 0;
		p->btable[i].hyst = 1;
	}
	for (i = 1; i <= NHIST; i++)
	{
		p->gtable[i] = (gentry *)malloc((1 << (p->logg[i])) * sizeof(gentry));
		for (j = 0; j < (1 << p->logg[i]); j++)
		{
			//p->gtable[(i * p->logg[i])+ j] = new_gentry();
			p->gtable[i][j].ctr = 0;
			p->gtable[i][j].tag = 0;
			p->gtable[i][j].u = 0;
		}
	}

	return p;
}


// PREDICTION
char *my_predictor_predict (my_predictor *p, int pc)
{
	int i = 0;

	// TAGE prediction
	
	if (p->clock == EPOCH)
	{
		p->clock = 0;
		p->g_clock += 1;
	
		printf("%d, ", p->g_clock); 
		for (i = 0; i <= NHIST; i++)
		{
			if (i == WHICHTABLE)
				printf("%04d,", p->counters[i]);
			p->counters[i] = 0;
		}
		printf("\n");

		//printf("%d\t", p->tag_needed);	
		//printf("%f\n", (((double)(p->tag_needed)/(double)((double)p->g_clock * EPOCH))*100));
	}
	p->clock += 1;
	
	//printf("my_predictor_predict: the pc is: 0x%08lx\n", pc);
	// computes the table addresses and the partial tags
	for (i = 1; i <= NHIST; i++)
	{
		p->GI[i] = gindex (p, pc, i);
		p->GTAG[i] = gtag (p, pc, i);
		//printf("my_predictor_predict: GI(%d) = 0x%08lx GTAG(%d) = 0x%08lx\n", i, p->GI[i], i, p->GTAG[i]);
	}

	p->BI = pc & ((1 << LOGB) - 1);
	//printf("my_predictor_predict: BI 0x%08lx\n", p->BI);

	p->HitBank = 0;
	p->AltBank = 0;

	//Look for the bank with longest matching history
	for (i = NHIST; i > 0; i--)
	{
		if (p->gtable[i][p->GI[i]].tag == p->GTAG[i])
		{
			p->HitBank = i;
			break;
		}
	}
	//printf("my_predictor_predict: HitBank %d %d(NHIST)\n", p->HitBank, NHIST);

	//Look for the alternate bank
	for (i = p->HitBank - 1; i > 0; i--)
	{
		if (p->gtable[i][p->GI[i]].tag == p->GTAG[i])
		{
			if ((p->USE_ALT_ON_NA < 0)
				|| (abs (2 * p->gtable[i][p->GI[i]].ctr + 1) > 1))
			{
				p->AltBank = i;
				break;
			}
		}
	}


	for(i = NHIST; i > p->HitBank; i--)
	{
		if (p->gtable[i][p->GI[i]].u > 0)
		{
			p->tag_needed += 1;
			break;
		}
	}

	//computes the prediction and the alternate prediction
	if (p->HitBank > 0)
	{
		if (p->AltBank > 0)
				p->alttaken = (p->gtable[p->AltBank][p->GI[p->AltBank]].ctr >= 0);
		else
				p->alttaken = getbim (p, pc);

		//if the entry is recognized as a newly allocated entry and 
		//USE_ALT_ON_NA is positive  use the alternate prediction
		if ((p->USE_ALT_ON_NA < 0)
				|| (abs (2 * p->gtable[p->HitBank][p->GI[p->HitBank]].ctr + 1) > 1))
		{
			p->tage_pred = (p->gtable[p->HitBank][p->GI[p->HitBank]].ctr >= 0);
			p->counters[p->HitBank] += 1;
			//printf("%02d\t", p->HitBank);
		}
		else
		{
			p->tage_pred = p->alttaken;
			if (p->AltBank > 0)
			{
				p->counters[p->AltBank] += 1;
				//printf("%02d\t", p->AltBank);
			}
			else
			{
				p->counters[0] += 1;
				//printf("%02d\t", 0);
			}
		}
	}
	else
	{
		p->alttaken = getbim (p, pc);
		p->tage_pred = p->alttaken;
		//printf("%02d\t", 0);
		p->counters[0] += 1;
	}
	//end TAGE prediction

	p->pred_taken = p->tage_pred;

	#if PRINT2 
	//printf("predict: %d, %d, %d, %d, \n", p->HitBank, p->AltBank, p->pred_taken, p->alttaken);
	#endif

	//}

	//(p->u).direction_prediction = p->pred_taken;
	//(p->u).target_prediction = 0;
	//return &(p->u);

	//check this
	//printf("p->pred_taken is: %d\n", p->pred_taken);
	if (p->pred_taken > 0)
			p->c = 3;
	else
			p->c = 0;

	//      if (p->pred_taken > 0) printf("1");
	//      else                    printf("0");

	return &(p->c);
}

// PREDICTOR UPDATE
//void my_predictor_update (my_predictor *p, branch_update_struct * u, int taken, unsigned int target)
void my_predictor_update (my_predictor *p, int pc, int taken)
{
        int i = 0;
        int j = 0;
        int ALLOC;

        //int TAKEN = 0;
        //int PATHBIT = 0;

        int LongestMatchPred = 0;
        int PseudoNewAlloc = 0;

        //address_t pc = p->bi.address;
        int NRAND = MYRANDOM (p);

        //if (p->bi.br_flags & BR_CONDITIONAL)
        //{

        // TAGE UPDATE  
        // try to allocate a  new entries only if prediction was wrong
        //ALLOC = ((p->tage_pred != taken) & (p->HitBank < NHIST));
        ALLOC = ((p->tage_pred != taken) && (p->HitBank < NHIST));

        if (p->HitBank > 0)
        {
                // Manage the selection between longest matching and alternate matching
                // for "pseudo"-newly allocated longest matching entry
                LongestMatchPred = (p->gtable[p->HitBank][p->GI[p->HitBank]].ctr >= 0);
                PseudoNewAlloc =
                (abs (2 * p->gtable[p->HitBank][p->GI[p->HitBank]].ctr + 1) <= 1);

                // an entry is considered as newly allocated if its prediction counter is weak
                if (PseudoNewAlloc)
                {
                        if (LongestMatchPred == taken)
                                ALLOC = 0;

                        // if it was delivering the correct prediction, no need to allocate a new entry
                        //even if the overall prediction was false
                        if (LongestMatchPred != p->alttaken)
                        {
                                if (p->alttaken == taken)
                                {

                                        if (p->USE_ALT_ON_NA < 7)
                                                p->USE_ALT_ON_NA++;
                                }

                                else if (p->USE_ALT_ON_NA > -8)
                                        p->USE_ALT_ON_NA--;

                        }
                        if (p->USE_ALT_ON_NA >= 0)
                                p->tage_pred = LongestMatchPred;
                }

        }
        if (ALLOC)
        {
                // is there some "unuseful" entry to allocate
                int min = 1;
                int Y = 0;
                int X = 0;

                for (i = NHIST; i > p->HitBank; i--)
                {
                        if (p->gtable[i][p->GI[i]].u < min)
                                min = p->gtable[i][p->GI[i]].u;
                }

                // we allocate an entry with a longer history
                //to  avoid ping-pong, we do not choose systematically the next entry, but among the 3 next entries
                Y = NRAND & ((1 << (NHIST - p->HitBank - 1)) - 1);
                X = p->HitBank + 1;

                if (Y & 1)
                {
                        X++;
                        if (Y & 2)
                                X++;
                }

                //NO ENTRY AVAILABLE:  ENFORCES ONE TO BE AVAILABLE 
                if (min > 0)
                        p->gtable[X][p->GI[X]].u = 0;


                //Allocate only  one entry
                for (i = X; i <= NHIST; i += 1)
                {
                        if ((p->gtable[i][p->GI[i]].u == 0))
                        {
                                p->gtable[i][p->GI[i]].tag = p->GTAG[i];
                                p->gtable[i][p->GI[i]].ctr = (taken) ? 0 : -1;
                                p->gtable[i][p->GI[i]].u = 0;
#if PRINT2
                                //printf("update on: %d, %d, %d, %d\n", i, p->gtable[i][p->GI[i]].tag, p->gtable[i][p->GI[i]].ctr , p->gtable[i][p->GI[i]].u);
#endif
                                break;
                        }
                }
        }

        //periodic reset of u: reset is not complete but bit by bit
        p->TICK++;
        if ((p->TICK & ((1 << p->LOGTICK) - 1)) == 0)
        {
                // reset least significant bit
                // most significant bit becomes least significant bit
                for (i = 1; i <= NHIST; i++)
                        for (j = 0; j < (1 << p->logg[i]); j++)
                                p->gtable[i][j].u = p->gtable[i][j].u >> 1;
        }

        if (p->HitBank > 0)
        {
                ctrupdate (&(p->gtable[p->HitBank][p->GI[p->HitBank]].ctr), taken, CWIDTH);
                //if the provider entry is not certified to be useful also update the alternate prediction
                if (p->gtable[p->HitBank][p->GI[p->HitBank]].u == 0)
                {
                        if (p->AltBank > 0)
                                ctrupdate (&(p->gtable[p->AltBank][p->GI[p->AltBank]].ctr), taken, CWIDTH);
                        if (p->AltBank == 0)
                                baseupdate (p, pc, taken);
                }
        }
        else
                baseupdate (p, pc, taken);

        // update the u counter
        if (p->tage_pred != p->alttaken)
        {
                if (p->tage_pred == taken)
                {
                        if (p->gtable[p->HitBank][p->GI[p->HitBank]].u < 3)
                                p->gtable[p->HitBank][p->GI[p->HitBank]].u++;
                }
                else
                {
                        if (p->USE_ALT_ON_NA < 0)
                                if (p->gtable[p->HitBank][p->GI[p->HitBank]].u > 0)
                                        p->gtable[p->HitBank][p->GI[p->HitBank]].u--;
                }

        }

}
void my_predictor_update_histories( my_predictor *p, int pc, int taken)
{
        int TAKEN = 0, PATHBIT = 0;
        int i = 0;
        //pc >>= MD_BR_SHIFT;
		pc >>= 3;

        //check user and kernel mode to detect transition:
        //TAKEN = ((!(p->bi.br_flags & BR_CONDITIONAL)) | (taken));
        TAKEN = taken;

        //PATHBIT = (p->bi.address & 1);
        PATHBIT = pc & 1;

        //update user history
        updateghist (p, &(p->ghist), TAKEN, p->GHIST, &(p->ptghist));
        p->phist = (p->phist << 1) + PATHBIT;

        p->phist = (p->phist & ((1 << 16) - 1));

        //prepare next index and tag computations for user branchs 
        for (i = 1; i <= NHIST; i++)
        {
            update_tage_folded_history(&(p->ch_i[i]), p->ghist);
            update_tage_folded_history(&(p->ch_t[0][i]), p->ghist);
            update_tage_folded_history(&(p->ch_t[1][i]), p->ghist);
        }
}

/* *********************** TAGE ************************* */
                           
