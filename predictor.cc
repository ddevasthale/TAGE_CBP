#include "predictor.h"
#include <cstdlib>
#include <bitset>
#include <math.h>
#include <random>
#include <iostream>

// defining some hard coded values which are tbd for implementation
// initializations
#define INIT_BASE_BIMODAL_CTR 2 // weakly taken
#define BIMODAL_CTR_MAX 3

#define INIT_TAGPRED_CTR 0 //strongly not taken
#define TAGPRED_CTR_MAX 7

#define SIZE_BASE_PREDICTOR 14
#define SIZE_TAGGED_PREDICTOR 12

#define SIZE_OF_TAG 8

using namespace std;

PREDICTOR::PREDICTOR(void)
{
	numBimodalEntries = 1ULL << SIZE_BASE_PREDICTOR;
	
	baseBimodal = (uint32_t*) malloc(sizeof(uint32_t)*numBimodalEntries);
	
	for (uint32_t i = 0; i < numBimodalEntries; i++)
	{
		baseBimodal[i] = INIT_BASE_BIMODAL_CTR;
	}

	
	numTagPredEntries = 1 << SIZE_TAGGED_PREDICTOR;
	// number of Tagged Predictors with numTagPredEntries each
	for (uint32_t i = 0; i < NUMTAGTABLES; i++)
	{
		//tagPred[i] = new tagEntry[numTagPredEntries];
		tagPred[i] = (tagEntry*) malloc(sizeof(tagEntry)*numTagPredEntries);
	}

	for (uint32_t i = 0; i < NUMTAGTABLES; i++)
	{
		for (uint32_t j = 0; j < numTagPredEntries; j++)
		{
			tagPred[i][j].predCtr = INIT_TAGPRED_CTR;
			tagPred[i][j].tag = 0;
			tagPred[i][j].usefulCtr = 0;
		}
	}

	// Initializing Predictions banks and prediction values to out of range
	primePred = 0;
	altPred = 0;
	pseudoNewAlloc = 0;
	primeBank = -1;
	altBank = -1;
	actualProvider = -1;

	primeBankIndex = -1;
	altBankIndex = -1;

	// According to definition, using geometric history length for 4 tagged predictors
	geometric[3] = 130;
	geometric[2] = 44;
	geometric[1] = 15;
	geometric[0] = 5;

	// Initializing Compressed Buffers.
	// first for index of the the tagged tables
	for (int i = 0; i < NUMTAGTABLES; i++)
	{
		indexComp[i].compHist = 0;
		indexComp[i].geomLength = geometric[i];
		indexComp[i].targetLength = SIZE_TAGGED_PREDICTOR;
	}

	// Initializing compressed buffers for tags themselves
	for (int j = 0; j < 2; j++)
	{
		for (int i = 0; i < NUMTAGTABLES; i++)
		{
			tagComp[j][i].compHist = 0;
			tagComp[j][i].geomLength = geometric[i];
			if (j == 0)
			{
				tagComp[j][i].targetLength = SIZE_OF_TAG;
			}
			else
			{
				tagComp[j][i].targetLength = SIZE_OF_TAG -1;
			}
		}
	}

	// calculated index to find an entry
	for (int i = 0; i < NUMTAGTABLES; i++)
	{
		indexOfTagPredictor[i] = 0;
	}
	for (int i = 0; i < NUMTAGTABLES; i++)
	{
		tag[i] = 0;
	}	

	
	globalHistoryRegister.reset();
	patternHistoryRegister = 0;

	altBetterCount = 7;
	numberOfCycles = 0;
	msbReset = 1;
	prediction = true;

}


// Function to get the prediction
bool PREDICTOR::get_prediction(const branch_record_c* br, const op_state_c* os)
{
	prediction = true;
	// base bimodal predictor
	if(br->is_conditional) 
	{
		uint32_t branchPc = br->instruction_addr;
		bool baseBimodalPrediction;
		uint32_t baseBimodalIndex = branchPc % numBimodalEntries;
		uint32_t baseBimodalCounter = baseBimodal[baseBimodalIndex];

		if (baseBimodalCounter > BIMODAL_CTR_MAX / 2)
		{
			baseBimodalPrediction = 1;
		}
		else
		{
			baseBimodalPrediction = 0;
		}

		//Tagged Predictors

		//prediction paramters
		primePred = 0;
		altPred = 0;
		pseudoNewAlloc = 0;
		primeBank = -1;
		altBank = -1;
		actualProvider = -1;

		//added as per gem
		primeBankIndex = -1;
		altBankIndex = -1;

		// setting the paramters to out of range before prediction

		// Calculating index masks for predictors 1-4
		// We will use the compressed History to hash with pc
		// As metioned in PPM paper, we will use folded history.
		// Eg: in the current case, since the size of tagged predictors is 2^12
		//  Table 1 geometric history length is 5 bits, 
		// it will be indexed by pc[25:12] xor pc[11:0] xor geomHistory[4:0] xor 
		// for Table 2, it will be pc[25:12] xor [11:0] xor compHistory[11:0]
		
		for (int table = NUMTAGTABLES - 1; table >= 0; table--)
		{
			indexOfTagPredictor[table] = branchPc ^ (branchPc >> (SIZE_TAGGED_PREDICTOR - table))
			 ^ (indexComp[table].compHist) ^ patternHistoryRegister;
			// masking for taking just the last n bits to index into the tagged predictor
			indexOfTagPredictor[table] = indexOfTagPredictor[table] & ((1ULL << SIZE_TAGGED_PREDICTOR) - 1);
		}


		// Calculating tags for predictors 1-4
		// Using the method as in PPM paper 
		// Since Tag length is 9 bits for the current implementation: CSR1: 9 bits, CSR2: 8 bits
		// tag = pc[8:0] xor CSR1 xor (CSR2 << 1)

		for (uint32_t table = 0; table < NUMTAGTABLES; table++)
		{
			tag[table] = branchPc ^ (tagComp[0][table].compHist) ^ (tagComp[1][table].compHist << 1);
			tag[table] &= ((1ULL << SIZE_OF_TAG) - 1);
		}


		// Now index and tag are ready
		// Proceeding with the prediction

		// Table 0 - shortest history length
		// Table 3 - longest history length

		// checking all tables
		for (int table = NUMTAGTABLES - 1; table >= 0; table--)
		{
			if (tagPred[table][indexOfTagPredictor[table]].tag == tag[table])
			{
				primeBank = table;
				primeBankIndex = indexOfTagPredictor[table];
				break;
			}
		}

		for (int table = primeBank - 1; table >= 0; table--)
		{
			if (tagPred[table][indexOfTagPredictor[table]].tag == tag[table])
			{
				altBank = table;
				altBankIndex = indexOfTagPredictor[table];
				break;
			}
		}

		//if there was a tag match with one of tagged predictor
		if (primeBank > -1)
		{
			if (altBank == -1)
			{
				altPred = baseBimodalPrediction;
			}
			else
			{
				if (tagPred[altBank][indexOfTagPredictor[altBank]].predCtr >= TAGPRED_CTR_MAX / 2)
					altPred = 1;
				else
					altPred = 0;

				//cout << "Alternate prediction is " << altPred << "\n";
			}

			// calculating prime prediction (even if it is not used)
			if (tagPred[primeBank][indexOfTagPredictor[primeBank]].predCtr >= TAGPRED_CTR_MAX / 2)
					primePred = 1;
				else
					primePred = 0;

			
			pseudoNewAlloc = (((tagPred[primeBank][indexOfTagPredictor[primeBank]].predCtr == 4) || (tagPred[primeBank][indexOfTagPredictor[primeBank]].predCtr == 3)) && tagPred[primeBank][indexOfTagPredictor[primeBank]].usefulCtr == 0) || altBetterCount > 8;

			// check if the entry is a newly allocated entry.
			// if it is not
			
			if(!pseudoNewAlloc)
			{
				if (tagPred[primeBank][indexOfTagPredictor[primeBank]].predCtr >= TAGPRED_CTR_MAX / 2)
					primePred = 1;
				else
					primePred = 0;

				
				prediction = primePred;
				actualProvider = primeBank;
				return primePred;
			}
			// if it is not
			else
			{
				prediction = altPred;
				actualProvider = altBank;
				return altPred;
			}
		}
		else
		{
			altPred = baseBimodalPrediction;
			prediction = altPred;
			primePred = altPred;
			return altPred;
		}
	}
	

}


// Now writing the function to update the predictor bsaed upon the prediction and actual resolution of thr branch

void PREDICTOR::update_predictor(const branch_record_c* br, const op_state_c* os, bool resolution)
{
	if(br->is_conditional)
	{
		
		uint32_t branchPc = br->instruction_addr;
		bool needToAllocate = false;

		needToAllocate = (prediction != resolution) && primeBank > -1 && (primeBank < NUMTAGTABLES -1);


		// Now to check if the current prediction entry is a newly allocated entry or not
		if (primeBank > -1)
		{

			if(pseudoNewAlloc)
			{
				if(primePred == resolution)
				{
					needToAllocate = false;
				}
				if (primePred != altPred)
				{
					if (primePred != resolution)
					{
						altBetterCount = sat_increment(altBetterCount, 15);
					}
					else
					{
						altBetterCount = sat_decrement(altBetterCount);
					}
				}
				
			}
		}	
		

		// Allocating new entry as per the protocol mentioned in the paper
		
		if(needToAllocate)
		{
			uint32_t min = 1;

			for (int i = NUMTAGTABLES - 1; i > primeBank; i--)
			{
				if(tagPred[i][indexOfTagPredictor[i]].usefulCtr < min)
				{
					min = tagPred[i][indexOfTagPredictor[i]].usefulCtr;
				}
			}


			if (min > 0)
			{
				for (int i = primeBank + 1; i < NUMTAGTABLES; i++)
				{
					tagPred[i][indexOfTagPredictor[i]].usefulCtr--;
				}
			}
			
			else
			{
				// variables to assist in allocating upto one entry
				uint allocatedTable = 0;

				int countNotUseful = 0;
				int tablesNotUseful[NUMTAGTABLES - 1];

				
				for (int i = 0; i < NUMTAGTABLES - 1; i++)
				{
					tablesNotUseful[i] = -1;
				}
				
				int currentIndex = 0;
				int probability = 0;

				// Getting a random number distribution based on random seed for each simulation
				random_device myRandomDevice;
				unsigned seed = myRandomDevice();

				default_random_engine generator(seed);
				uniform_int_distribution<int> distribution(1, 100);
				// if there is only one table not useful, that is the entry to be allocated
				for (int i = primeBank + 1; i < NUMTAGTABLES; i++)
				{
					if (tagPred[i][indexOfTagPredictor[i]].usefulCtr == 0)
					{
						countNotUseful++;
						tablesNotUseful[currentIndex++] = i;
					}
				}
				
				switch (countNotUseful)
				{
				case 1:
				{
					allocatedTable = tablesNotUseful[0];
					break;
				}
				case 2:
				{
					probability = distribution(generator);
					if (probability > 33)
					{
						allocatedTable = tablesNotUseful[0];
					}
					
					else
					{
						allocatedTable = tablesNotUseful[1];
					}
					break;
				}
				case 3:
				{
					probability = distribution(generator);
					if (probability > 50)
					{
						allocatedTable = tablesNotUseful[0];
					}
					
					else if (probability > 12 && probability <= 50)
					{
						allocatedTable = tablesNotUseful[1];
					}
					
					else
					{
						allocatedTable = tablesNotUseful[2];
					}
					break;
				}
				
				default:
					break;
				}
				
				// now allocating to the selected tagged predictor table
				// predictor counter to weakly taken/ not taken and useful counter to 0
				if (resolution)
				{
					tagPred[allocatedTable][indexOfTagPredictor[allocatedTable]].predCtr = 4;
				}
				else
				{
					tagPred[allocatedTable][indexOfTagPredictor[allocatedTable]].predCtr = 3;
				}
				tagPred[allocatedTable][indexOfTagPredictor[allocatedTable]].usefulCtr = 0;
				tagPred[allocatedTable][indexOfTagPredictor[allocatedTable]].tag = tag[allocatedTable];
				
			}
				
				
		}
		
		
		
		// Periodic resetting of the useful bits

		// increment the access counter to predictor
		numberOfCycles++;

		// periodically after 256K instructions, following reset mades alternately:
		// MSB reset to 0
		// LSB reset to 0
		
		if (numberOfCycles == 256 * 1024)
		{
			// resetting the numberOfCycles to restart counting again
			numberOfCycles = 0;

			if (msbReset)
			{
				for (int i = 0; i < NUMTAGTABLES; i++)
				{
					for (uint32_t j = 0; j < numTagPredEntries; j++)
					{
						tagPred[i][j].usefulCtr = tagPred[i][j].usefulCtr & 1;
					}
				}
			}
			else
			{
				for (int i = 0; i < NUMTAGTABLES; i++)
				{
					for (uint32_t j = 0; j < numTagPredEntries; j++)
					{
						tagPred[i][j].usefulCtr = tagPred[i][j].usefulCtr & 2;
					}
				}
			}
	
			msbReset = !msbReset;
			
		}
		

		// Updating if the prediction is not from the base bimodal predictor
		if (primeBank > -1)
		{

			// as per policy 2
			// updating based on if the resolution and prediction are same
			if (resolution)
			{
				tagPred[primeBank][indexOfTagPredictor[primeBank]].predCtr = sat_increment(tagPred[primeBank][indexOfTagPredictor[primeBank]].predCtr, TAGPRED_CTR_MAX);
			}
			// if prediction is incorrect
			else
			{
				// decrementing the prediction counter
				tagPred[primeBank][indexOfTagPredictor[primeBank]].predCtr = sat_decrement(tagPred[primeBank][indexOfTagPredictor[primeBank]].predCtr);
			}
			
			//if prime bank is not useful also update the alternate prediction
			if(tagPred[primeBank][indexOfTagPredictor[primeBank]].usefulCtr == 0)
			{
				if(altBank > -1)
				{
					if(resolution)
					{
						tagPred[altBank][indexOfTagPredictor[altBank]].predCtr = sat_increment(tagPred[altBank][indexOfTagPredictor[altBank]].predCtr, TAGPRED_CTR_MAX);
					}
					else
					{
						tagPred[altBank][indexOfTagPredictor[altBank]].predCtr = sat_decrement(tagPred[altBank][indexOfTagPredictor[altBank]].predCtr);
					}
				}
				else
				{
					uint32_t baseBimodalIndex = branchPc % numBimodalEntries;

					if (resolution)
					{
						baseBimodal[baseBimodalIndex] = sat_increment(baseBimodal[baseBimodalIndex], BIMODAL_CTR_MAX);
					}
					else
					{
						baseBimodal[baseBimodalIndex] = sat_decrement(baseBimodal[baseBimodalIndex]);
					}
				}
			}
			// if the final prediction is just from the base predictor
		
			
			
			// as per policy 1
			// updating the useful counter
			if (prediction != altPred)
			{
				// if prediction is correct
				if (prediction == resolution)
				{
					tagPred[actualProvider][indexOfTagPredictor[actualProvider]].usefulCtr = sat_increment(tagPred[actualProvider][indexOfTagPredictor[actualProvider]].usefulCtr, BIMODAL_CTR_MAX);
				}
				else
				{
					tagPred[actualProvider][indexOfTagPredictor[actualProvider]].usefulCtr = sat_decrement(tagPred[actualProvider][indexOfTagPredictor[actualProvider]].usefulCtr);
				}
			}
		}
		else
		{
			uint32_t baseBimodalIndex = branchPc % numBimodalEntries;

			if (resolution)
			{
				baseBimodal[baseBimodalIndex] = sat_increment(baseBimodal[baseBimodalIndex], BIMODAL_CTR_MAX);
			}
			else
			{
				baseBimodal[baseBimodalIndex] = sat_decrement(baseBimodal[baseBimodalIndex]);
			}
		}
		
		
		// update the global history register
		globalHistoryRegister = globalHistoryRegister << 1;

		if (resolution)
		{
			globalHistoryRegister.set(0, 1);
		}

		// Shifting this to the start for debug
		// calculating the new folded history for index and tag base on new global history register
		
		for (int i = 0; i < NUMTAGTABLES; i++)
		{
			indexComp[i].updateCompHist(globalHistoryRegister);
			tagComp[0][i].updateCompHist(globalHistoryRegister);
			tagComp[1][i].updateCompHist(globalHistoryRegister);
		}

	patternHistoryRegister = (patternHistoryRegister << 1); 
    if(branchPc & 1)
    {
        patternHistoryRegister = patternHistoryRegister + 1;
    }
    patternHistoryRegister = (patternHistoryRegister & ((1 << 16) - 1));

	}
		
}