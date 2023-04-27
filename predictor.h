#ifndef PREDICTOR_H_SEEN
#define PREDICTOR_H_SEEN

#include <cstddef>
#include <inttypes.h>
#include <vector>
#include "op_state.h"   // defines op_state_c (architectural state) class 
#include "tread.h"      // defines branch_record_c class

#include <stdint.h>
#include <bitset>
#include <iostream>
#include <time.h>
#include <stdlib.h>

using namespace std;

#define NUMTAGTABLES 4

//defining the structure for storing one entry in the (partially) tagged tables
struct tagEntry
{
	uint32_t predCtr; // 3 bit counter 
	uint32_t tag; //based on sensitivity params
	uint32_t usefulCtr; // 2 bit counter
};



// Calculation of Folded History to go from Geometric History length -> compressed (target)
struct compressedHistory
{
	uint32_t geomLength; //will be based on number of global history bits we want to use for tagged table
	uint32_t targetLength; // will be based on the number of entries in the tagged table
	uint32_t compHist; // used to hash with pc for final index/ tag calculation



	void updateCompHist(bitset <131> ghr)
	{
		// using method as in PPM paper and https://pharm.ece.wisc.edu/papers/badgr_iccd16.pdf
		// if no folding needed
		if (geomLength <= targetLength)
		{
			compHist = (compHist << 1ULL) + ghr[0];
		}
		else 
		{
			//creating important masks for incoming history bit H and previous MSB 
			int mask = (1ULL << targetLength) - 1ULL;
			int mask1 = ghr[geomLength] << (geomLength % targetLength);
			int mask2 = (1ULL << targetLength);

			//calculating the updated compressed History using the concept of Circular Shift Register and relation with Global History Bits
			compHist = (compHist << 1ULL) | ghr[0];
			compHist ^= ((compHist & mask2) >> targetLength);
			compHist ^= mask1;
			compHist &= mask;

		}
		
	}

};

	
class PREDICTOR {

private:
	// Global History Register: discuss about length
	bitset <131> globalHistoryRegister;


	//Pattern History Register // to check for improvement
	// adding as per paper
	int patternHistoryRegister;
	//Base predictor: bimodal
	uint32_t *baseBimodal;
	uint32_t numBimodalEntries; //entries in base pht

	//Tagged Predictors
	tagEntry *tagPred[NUMTAGTABLES];
	uint32_t numTagPredEntries; //entries in tagged pht
	//Tagged Predictors

	// size of Global History for Tagged Tables
	uint32_t geometric[NUMTAGTABLES];

	//Compressed buffers
	compressedHistory indexComp[NUMTAGTABLES]; // for index
	// as mentioned in the paper we will be using two compressed histories 
	// for calculating tags
	// one will be the same size of tag
	// one will be of the size <tag - 1> and left shifted by 1
	compressedHistory tagComp[2][NUMTAGTABLES]; // for tag

	//store values for updation after prediction
	uint32_t indexOfTagPredictor[NUMTAGTABLES];
	uint32_t tag[NUMTAGTABLES];
	

	// keeping track of predictor and alternate prediction
	bool primePred;
	bool altPred;
	int primeBank;
	int altBank;
	int actualProvider;

	//added as per gem
	int primeBankIndex;
	int altBankIndex;

	// if entry is newly allocated last time
	bool pseudoNewAlloc;

	// variable to track number of branch prediction cycles done
	uint32_t numberOfCycles;

	// variable to track MSB or LSB resetting
	bool msbReset;

	// number of time alternate prediction has been correct when prime prediction was incorrect
	int altBetterCount;

	
	// edit made for trace based
	bool prediction;


public:
	PREDICTOR(void);

	bool get_prediction(const branch_record_c* br, const op_state_c* os);

	void update_predictor(const branch_record_c* br, const op_state_c* os, bool taken);

};

static inline uint32_t sat_increment(uint32_t x, uint32_t max)
{
	if (x < max)
	{
		return x + 1;
	}
	else
	{
		return x;
	}
}

static inline uint32_t sat_decrement(uint32_t x)
{
	if (x > 0)
	{
		return x - 1;
	}
	else
	{
		return x;
	}
}

#endif

