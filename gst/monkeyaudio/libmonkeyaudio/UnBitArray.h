#ifndef UNBITARRAY_H
#define UNBITARRAY_H

#include "UnBitArrayBase.h"
#include "BitArray.h"

class IAPEDecompress;

struct RANGE_CODER_STRUCT_DECOMPRESS
{
    unsigned int low;          //low end of interval
    unsigned int range;        //length of interval
    unsigned int buffer;        //buffer for input/output
};

class CUnBitArray : public CUnBitArrayBase
{

public:
    //construction/destruction
    CUnBitArray(IAPEDecompress *pAPEInfo, int nVersion);
    ~CUnBitArray();

    unsigned int DecodeValue(DECODE_VALUE_METHOD DecodeMethod, int nParam1 = 0, int nParam2 = 0);

    void GenerateArray(int *pOutputArray, int nElements, int nBytesRequired = -1);

    int DecodeValueRange(BIT_ARRAY_STATE & BitArrayState);

    void FlushState(BIT_ARRAY_STATE & BitArrayState);
    void FlushBitArray();
    void Finalize();

private:

    void GenerateArrayRange(int *pOutputArray, int nElements);

    //data
    int m_nFlushCounter;
    int m_nFinalizeCounter;

    RANGE_CODER_STRUCT_DECOMPRESS   m_RangeCoderInfo;

    unsigned __int32    m_nRefillBitThreshold;

    //functions
    __inline int RangeDecodeFast(int nShift);
    __inline int RangeDecodeFastWithUpdate(int nShift);
    __inline unsigned char GetC();


};

#endif /* UNBITARRAY_H */
