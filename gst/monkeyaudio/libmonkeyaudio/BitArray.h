#ifndef APE_BITARRAY_H
#define APE_BITARRAY_H

#include "IO.h"

//#define BUILD_RANGE_TABLE

struct RANGE_CODER_STRUCT_COMPRESS
{
    unsigned int low;       // low end of interval
    unsigned int range;     // length of interval
    unsigned int help;      // bytes_to_follow resp. intermediate value
    unsigned char buffer;   // buffer for input / output
};

struct BIT_ARRAY_STATE
{
    unsigned __int32    k;
    unsigned __int32    nKSum;
};

class CBitArray
{
public:

    // construction / destruction
    CBitArray(CIO *pIO);
    ~CBitArray();

    // encoding
    int EncodeUnsignedLong(unsigned int n);
    int EncodeValue(int nEncode, BIT_ARRAY_STATE & BitArrayState);
    int EncodeBits(unsigned int nValue, int nBits);

    // output (saving)
    int OutputBitArray(BOOL bFinalize = FALSE);

    // other functions
    void Finalize();
    void AdvanceToByteBoundary();
    __inline unsigned __int32 GetCurrentBitIndex() { return m_nCurrentBitIndex; }
    void FlushState(BIT_ARRAY_STATE & BitArrayState);
    void FlushBitArray();

private:

    // data members
    unsigned __int32 *          m_pBitArray;
    CIO *                       m_pIO;
    unsigned __int32            m_nCurrentBitIndex;
    RANGE_CODER_STRUCT_COMPRESS m_RangeCoderInfo;

    // functions
    __inline void NormalizeRangeCoder();
    __inline void EncodeFast(int nRangeWidth, int nRangeTotal, int nShift);
    __inline void PutC(unsigned char ucValue);
    __inline void EncodeDirect(int nValue, int nShift);

#ifdef BUILD_RANGE_TABLE
    void OutputRangeTable();
#endif

};

#endif /* APE_BITARRAY_H */
