/************************************************************************************
Includes
************************************************************************************/
#include "All.h"
#include "BitArray.h"

/************************************************************************************
Declares
************************************************************************************/
#define BIT_ARRAY_ELEMENTS          (4096)                      // the number of elements in the bit array (4 MB)
#define BIT_ARRAY_BYTES             (BIT_ARRAY_ELEMENTS * 4)    // the number of bytes in the bit array
#define BIT_ARRAY_BITS              (BIT_ARRAY_BYTES    * 8)    // the number of bits in the bit array

#define MAX_ELEMENT_BITS            128
#define REFILL_BIT_THRESHOLD        (BIT_ARRAY_BITS - MAX_ELEMENT_BITS)

#define CODE_BITS 32
#define TOP_VALUE ((unsigned int) 1 << (CODE_BITS - 1))
#define SHIFT_BITS (CODE_BITS - 9)
#define EXTRA_BITS ((CODE_BITS - 2) % 8 + 1)
#define BOTTOM_VALUE (TOP_VALUE >> 8)

/************************************************************************************
Lookup tables
************************************************************************************/
const unsigned __int32 K_SUM_MIN_BOUNDARY[32] = {0,32,64,128,256,512,1024,2048,4096,8192,16384,32768,65536,131072,262144,524288,1048576,2097152,4194304,8388608,16777216,33554432,67108864,134217728,268435456,536870912,1073741824LU,2147483648LU,0,0,0,0};

#define MODEL_ELEMENTS                  64
#define RANGE_OVERFLOW_TOTAL_WIDTH      65536
#define RANGE_OVERFLOW_SHIFT            16

const unsigned __int32 RANGE_TOTAL[64] = {0,14824,28224,39348,47855,53994,58171,60926,62682,63786,64463,64878,65126,65276,65365,65419,65450,65469,65480,65487,65491,65493,65494,65495,65496,65497,65498,65499,65500,65501,65502,65503,65504,65505,65506,65507,65508,65509,65510,65511,65512,65513,65514,65515,65516,65517,65518,65519,65520,65521,65522,65523,65524,65525,65526,65527,65528,65529,65530,65531,65532,65533,65534,65535};
const unsigned __int32 RANGE_WIDTH[64] = {14824,13400,11124,8507,6139,4177,2755,1756,1104,677,415,248,150,89,54,31,19,11,7,4,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};

#ifdef BUILD_RANGE_TABLE
    int g_aryOverflows[256] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    int g_nTotalOverflow = 0;
#endif

/************************************************************************************
Constructor
************************************************************************************/
CBitArray::CBitArray(CIO *pIO)
{
    // allocate memory for the bit array
    m_pBitArray = new unsigned __int32 [BIT_ARRAY_ELEMENTS];
    memset(m_pBitArray, 0, BIT_ARRAY_BYTES);

    // initialize other variables
    m_nCurrentBitIndex = 0;
    m_pIO = pIO;
}

/************************************************************************************
Destructor
************************************************************************************/
CBitArray::~CBitArray()
{
    // free the bit array
    SAFE_ARRAY_DELETE(m_pBitArray)
#ifdef BUILD_RANGE_TABLE
    OutputRangeTable();
#endif
}

/************************************************************************************
Output the bit array via the CIO (typically saves to disk)
************************************************************************************/
int CBitArray::OutputBitArray(BOOL bFinalize)
{
    // write the entire file to disk
    unsigned int nBytesWritten = 0;
    unsigned int nBytesToWrite = 0;
    unsigned int nRetVal = 0;

    if (bFinalize)
    {
        nBytesToWrite = ((m_nCurrentBitIndex >> 5) * 4) + 4;
        RETURN_ON_ERROR(m_pIO->Write(m_pBitArray, nBytesToWrite, &nBytesWritten))

        // reset the bit pointer
        m_nCurrentBitIndex = 0;
    }
    else
    {
        nBytesToWrite = (m_nCurrentBitIndex >> 5) * 4;
        RETURN_ON_ERROR(m_pIO->Write(m_pBitArray, nBytesToWrite, &nBytesWritten))
        //return -1; //(for testing)

        // move the last value to the front of the bit array
        m_pBitArray[0] = m_pBitArray[m_nCurrentBitIndex >> 5];
        m_nCurrentBitIndex = (m_nCurrentBitIndex & 31);

        // zero the rest of the memory (may not need the +1 because of frame byte alignment)
        memset(&m_pBitArray[1], 0, min(nBytesToWrite + 1, BIT_ARRAY_BYTES - 1));
    }

    //return a successfule value
    return 0;

}

/************************************************************************************
Encodes an unsigned int to the bit array (no rice coding)
************************************************************************************/
int CBitArray::EncodeUnsignedLong(unsigned int n)
{
    // make sure there are at least 8 bytes in the buffer
    if (m_nCurrentBitIndex > (BIT_ARRAY_BYTES - 8))
    {
        RETURN_ON_ERROR(OutputBitArray())
    }

    // encode the value
    unsigned __int32 nBitArrayIndex = m_nCurrentBitIndex >> 5;
    int nBitIndex = m_nCurrentBitIndex & 31;

    if (nBitIndex == 0)
    {
        m_pBitArray[nBitArrayIndex] = n;
    }
    else
    {
        m_pBitArray[nBitArrayIndex] |= n >> nBitIndex;
        m_pBitArray[nBitArrayIndex + 1] = n << (32 - nBitIndex);
    }

    m_nCurrentBitIndex += 32;

    return 0;
}

/************************************************************************************
Directly encode bits to the bitstream
************************************************************************************/
int CBitArray::EncodeBits(unsigned int nValue, int nBits)
{
    // make sure there is room for the data
    // this is a little slower than ensuring a huge block to start with, but it's safer
    if (m_nCurrentBitIndex > REFILL_BIT_THRESHOLD)
    {
        RETURN_ON_ERROR(OutputBitArray())
    }

    EncodeDirect(nValue, nBits);
    return 0;
}

/************************************************************************************
Advance to a byte boundary (for frame alignment)
************************************************************************************/
void CBitArray::AdvanceToByteBoundary()
{
#if 0
    m_nCurrentBitIndex--;
    m_nCurrentBitIndex |= 7;
    m_nCurrentBitIndex++;
#else
    while (m_nCurrentBitIndex % 8)
        m_nCurrentBitIndex++;
#endif
}

/************************************************************************************
Range encoding
************************************************************************************/
__inline void CBitArray::PutC(unsigned char ucValue)
{
    m_pBitArray[m_nCurrentBitIndex >> 5] |= ucValue << (24 - (m_nCurrentBitIndex & 31));
    m_nCurrentBitIndex += 8;
}

#define PUTC(VALUE) m_pBitArray[m_nCurrentBitIndex >> 5] |= ((VALUE) & 0xFF) << (24 - (m_nCurrentBitIndex & 31)); m_nCurrentBitIndex += 8;
#define PUTC_NOCAP(VALUE) m_pBitArray[m_nCurrentBitIndex >> 5] |= (VALUE) << (24 - (m_nCurrentBitIndex & 31)); m_nCurrentBitIndex += 8;

__inline void CBitArray::NormalizeRangeCoder()
{
    while (m_RangeCoderInfo.range <= BOTTOM_VALUE)
    {
        if (m_RangeCoderInfo.low < (0xFF << SHIFT_BITS))  // no carry possible --> output
        {
            PUTC(m_RangeCoderInfo.buffer);
            for ( ; m_RangeCoderInfo.help; m_RangeCoderInfo.help--) { PUTC_NOCAP(0xFF); }
            m_RangeCoderInfo.buffer = (m_RangeCoderInfo.low >> SHIFT_BITS) & 0xFF;
        }
        else if (m_RangeCoderInfo.low & TOP_VALUE) // carry now, no future carry
        {
            PUTC(m_RangeCoderInfo.buffer + 1);
            m_nCurrentBitIndex += (m_RangeCoderInfo.help * 8);
            m_RangeCoderInfo.help = 0;
            m_RangeCoderInfo.buffer = (m_RangeCoderInfo.low >> SHIFT_BITS) & 0xFF;
        }
        else
        {
            m_RangeCoderInfo.help++;
        }

        m_RangeCoderInfo.range <<= 8;
        m_RangeCoderInfo.low = (m_RangeCoderInfo.low << 8) & (TOP_VALUE - 1);
    }
}

__inline void CBitArray::EncodeFast(int nRangeWidth, int nRangeTotal, int nShift)
{
    // normalize
    NormalizeRangeCoder();

    // code the value
    const int nTemp = m_RangeCoderInfo.range >> nShift;
    m_RangeCoderInfo.low += nTemp * nRangeTotal;
    m_RangeCoderInfo.range = nTemp * nRangeWidth;
}

__inline void CBitArray::EncodeDirect(int nValue, int nShift)
{
    // normalize
    NormalizeRangeCoder();

    // code the value
    m_RangeCoderInfo.range = m_RangeCoderInfo.range >> nShift;
    m_RangeCoderInfo.low += m_RangeCoderInfo.range * nValue;
}

/************************************************************************************
Encode a value
************************************************************************************/
int CBitArray::EncodeValue(int nEncode, BIT_ARRAY_STATE & BitArrayState)
{
    // make sure there is room for the data
    // this is a little slower than ensuring a huge block to start with, but it's safer
    if (m_nCurrentBitIndex > REFILL_BIT_THRESHOLD)
    {
        RETURN_ON_ERROR(OutputBitArray())
    }

    // convert to unsigned
    nEncode = (nEncode > 0) ? nEncode * 2 - 1 : -nEncode * 2;

    // get the working k
    int nTempK = (BitArrayState.k) ? BitArrayState.k - 1 : 0;

    // update nKSum
    BitArrayState.nKSum += ((nEncode + 1) / 2) - ((BitArrayState.nKSum + 16) >> 5);

    // update k
    if (BitArrayState.nKSum < K_SUM_MIN_BOUNDARY[BitArrayState.k])
        BitArrayState.k--;
    else if (BitArrayState.nKSum >= K_SUM_MIN_BOUNDARY[BitArrayState.k + 1])
        BitArrayState.k++;

    // break the value into value (k bits) and overflow
    const int nOverflow = nEncode >> nTempK;
    int nValue = nEncode & ((1 << nTempK) - 1);

    // store the overflow
    if (nOverflow < (MODEL_ELEMENTS - 1))
    {
        EncodeFast(RANGE_WIDTH[nOverflow], RANGE_TOTAL[nOverflow], RANGE_OVERFLOW_SHIFT);

        #ifdef BUILD_RANGE_TABLE
            g_aryOverflows[nOverflow]++;
            g_nTotalOverflow++;
        #endif
    }
    else
    {
        // store the "special" overflow (tells that perfect k is encoded next)
        EncodeFast(RANGE_WIDTH[MODEL_ELEMENTS - 1], RANGE_TOTAL[MODEL_ELEMENTS - 1], RANGE_OVERFLOW_SHIFT);

        #ifdef BUILD_RANGE_TABLE
            g_aryOverflows[MODEL_ELEMENTS - 1]++;
            g_nTotalOverflow++;
        #endif

        // store the "perfect" k
        int nPerfectK = 0;
        while ((nEncode >> nPerfectK) > 0) { nPerfectK++; }
        EncodeDirect(nPerfectK, 5);
        nTempK = nPerfectK;
        nValue = nEncode;
    }

    if (nTempK <= 16)
    {
        EncodeDirect(nValue, nTempK);
    }
    else
    {
        const int nX1 = nValue & 0xFFFF;
        const int nX2 = nValue >> 16;
        EncodeDirect(nX1, 16);
        EncodeDirect(nX2, nTempK - 16);
    }

    return 0;
}

/************************************************************************************
Flush
************************************************************************************/
void CBitArray::FlushBitArray()
{
    // advance to a byte boundary (for alignment)
    AdvanceToByteBoundary();

    // the range coder
    m_RangeCoderInfo.low = 0;  // full code range
    m_RangeCoderInfo.range = TOP_VALUE;
    m_RangeCoderInfo.buffer = 0;
    m_RangeCoderInfo.help = 0;  // no bytes to follow
}

void CBitArray::FlushState(BIT_ARRAY_STATE & BitArrayState)
{
    // k and ksum
    BitArrayState.k = 10;
    BitArrayState.nKSum = (1 << BitArrayState.k) * 16;
}

/************************************************************************************
Finalize
************************************************************************************/
void CBitArray::Finalize()
{
    NormalizeRangeCoder();

    unsigned int nTemp = (m_RangeCoderInfo.low >> SHIFT_BITS) + 1;

    if (nTemp > 0xFF) // we have a carry
    {
        PutC(m_RangeCoderInfo.buffer + 1);
        for(; m_RangeCoderInfo.help; m_RangeCoderInfo.help--)
            PutC(0);
    }
    else  // no carry
    {
        PutC(m_RangeCoderInfo.buffer);
        for(; m_RangeCoderInfo.help; m_RangeCoderInfo.help--)
            PutC(0xFF);
    }

    // we must output these bytes so the decoder can properly work at the end of the stream
    PutC(nTemp & 0xFF);
    PutC(0);
    PutC(0);
    PutC(0);
}

/************************************************************************************
Build a range table (for development / debugging)
************************************************************************************/
#ifdef BUILD_RANGE_TABLE
void CBitArray::OutputRangeTable()
{
    int z;

    if (g_nTotalOverflow == 0) return;

    int nTotal = 0;
    int aryWidth[256]; ZeroMemory(aryWidth, 256 * 4);
    for (z = 0; z < MODEL_ELEMENTS; z++)
    {
        aryWidth[z] = int(((float(g_aryOverflows[z]) * float(65536)) + (g_nTotalOverflow / 2)) / float(g_nTotalOverflow));
        if (aryWidth[z] == 0) aryWidth[z] = 1;
        nTotal += aryWidth[z];
    }

    z = 0;
    while (nTotal > 65536)
    {
        if (aryWidth[z] != 1)
        {
            aryWidth[z]--;
            nTotal--;
        }
        z++;
        if (z == MODEL_ELEMENTS) z = 0;
    }

    z = 0;
    while (nTotal < 65536)
    {
        aryWidth[z++]++;
        nTotal++;
        if (z == MODEL_ELEMENTS) z = 0;
    }

    int aryTotal[256]; ZeroMemory(aryTotal, 256 * 4);
    for (z = 0; z < MODEL_ELEMENTS; z++)
    {
        for (int q = 0; q < z; q++)
        {
            aryTotal[z] += aryWidth[q];
        }
    }

    char buf[1024];
    sprintf(buf, "const unsigned __int32 RANGE_TOTAL[%d] = {", MODEL_ELEMENTS);
    ODS(buf);
    for (z = 0; z < MODEL_ELEMENTS; z++)
    {
        sprintf(buf, "%d,", aryTotal[z]);
        OutputDebugString(buf);
    }
    ODS("};\r\n");

    sprintf(buf, "const unsigned __int32 RANGE_WIDTH[%d] = {", MODEL_ELEMENTS);
    ODS(buf);
    for (z = 0; z < MODEL_ELEMENTS; z++)
    {
        sprintf(buf, "%d,", aryWidth[z]);
        OutputDebugString(buf);
    }
    ODS("};\r\n\r\n");
}
#endif // #ifdef BUILD_RANGE_TABLE
