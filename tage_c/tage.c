#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tage.h"


void main()
{
  // make prediction at fetch stage
  for (;;)
  {
      if (uop->type & IS_BR_CONDITIONAL)

	{
	  // get prediction

//We use only 18 bits of the PC            
             
	  bool gpred = mypred->predict_brcond (uop->pc & 0x3ffff, uop->type );

	  // report prediction:
	  // you need to provide direction predictions for conditional branches,
	  // targets of conditional branches are available at fetch stage.
	  // for indirect branches, you need to provide target predictions.
	  // you can report multiple predictions for the same branch
	  // the framework will use the last reported prediction to calculate 
	  // misprediction penalty
	  assert (report_pred (fe_ptr, false, gpred));
	}
      // update fetch branch history
      if (uop->type & 31)

      {
// We use only 7 bits of the target PC

mypred->FetchHistoryUpdate (uop->pc & 0x3ffff, uop->type , uop->br_taken,
				    uop->br_target & 0x7f );
     
           
           
      }
//#define IMMEDIATEUPDATE
//If you need to test with immediate update
#ifdef IMMEDIATEUPDATE
// for the contest just ignore this

      if (uop->type & 31)
	{
              mypred->update_brcond (uop->pc & 0x3ffff, uop->type , uop->br_taken, 				 uop->br_target & 0x7f);
              }

#endif 

}

  for (int i = 0; i < cycle_info->num_retire; i++)
    {
uint32_t rob_ptr = cycle_info->retire_q[i];
const cbp3_uop_dynamic_t *uop = &rob_entry (rob_ptr)->uop;
#ifndef  IMMEDIATEUPDATE
   if (uop->type & 31)
	{
              mypred->update_brcond (uop->pc & 0x3ffff, uop->type , uop->br_taken, 				 uop->br_target & 0x7f);

     
              
              }
#endif

    }
}

void
PredictorRunEnd ()
{

}

void
PredictorExit ()
{
     
     
     
     

}
