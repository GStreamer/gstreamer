#ifndef UNBITARRAYBASE_H
#define UNBITARRAYBASE_H

class IAPEDecompress;
#include "BitArray.h"

enum DECODE_VALUE_METHOD
{
    DECODE_VALUE_METHOD_UNSIGNED_INT,
    DECODE_VALUE_METHOD_UNSIGNED_RICE,
    DECODE_VALUE_METHOD_X_BITS
};

class CUnBitArrayBase
{
public:

    //construction/destruction
    CUnBitArrayBase(IAPEDecompress *pAPEDecompress, int nVersion) {}
    virtual ~CUnBitArrayBase() {}

    //functions
    virtual int FillBitArray();
    virtual int FillAndResetBitArray(int nFileLocation = -1, int nNewBitIndex = 0);

    virtual void GenerateArray(int *pOutputArray, int nElements, int nBytesRequired = -1) {}
    virtual unsigned int DecodeValue(DECODE_VALUE_METHOD DecodeMethod, int nParam1 = 0, int nParam2 = 0) { return 0; }

    virtual void AdvanceToByteBoundary();

    virtual int DecodeValueRange(BIT_ARRAY_STATE & BitArrayState) { return 0; }
    virtual void FlushState(BIT_ARRAY_STATE & BitArrayState) {}
    virtual void FlushBitArray() {}
    virtual void Finalize() {}

protected:

    virtual int CreateHelper(IAPEDecompress *pAPEDecompress, int nBytes, int nVersion);
    virtual unsigned __int32 DecodeValueXBits(unsigned __int32 nBits);

    unsigned __int32 m_nElements;
    unsigned __int32 m_nBytes;
    unsigned __int32 m_nBits;

    int         m_nVersion;
    IAPEDecompress * m_pAPEDecompress;

    unsigned __int32 m_nCurrentBitIndex;
    unsigned __int32 *m_pBitArray;
};

CUnBitArrayBase * CreateUnBitArray(IAPEDecompress * pAPEDecompress, int nVersion);

#endif /* UNBITARRAYBASE_H */
