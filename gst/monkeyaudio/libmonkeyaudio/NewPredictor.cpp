#include "All.h"
#include "APECompress.h"
#include "NewPredictor.h"

typedef struct {
    int  nFilter0Length;
    int  nFilter0Shift;
    int  nFilter1Length;
    int  nFilter1Shift;
    int  nFilter2Length;
    int  nFilter2Shift;
} OrderType;

// pfk:
//
// Where does these numbers come from?
// What are restrictions so the compressor works for every signal possible?
// Instead of a magic CompressionLevel these 6 (or 4 numbers) should be stored in the header
// some MMX code may also increase speed for fast or normal compression, but reduces readability.
// Also note that shifting is extremly slow on Pentium 4 (why? no idea!)
// Rotation + Order of the follwoing buffers mirroring: m_rbPredictionA, m_rbPredictionB, m_rbAdaptA, m_rbAdaptB -> allows MMX/Altivec

const OrderType  OrderTypeArray [] = {
    {    0,  0,    0,  0,   0,  0 },
    {    0,  0,    0,  0,   0,  0 },
    {   16, 11,    0,  0,   0,  0 },
    {   64, 11,    0,  0,   0,  0 },
    {  256, 13,   32, 10,   0,  0 },
    { 1024, 15,  256, 13,   0,  0 },
    { 1024, 15,  256, 13,  16, 11 },
};

/*****************************************************************************************
CPredictorCompressNormal
*****************************************************************************************/
CPredictorCompressNormal::CPredictorCompressNormal(int nCompressionLevel)
    : IPredictorCompress(nCompressionLevel)
{
    const OrderType*  p = OrderTypeArray + nCompressionLevel / 1000;

    if ( nCompressionLevel < 1000  ||  nCompressionLevel > 6000  ||  nCompressionLevel % 1000 != 0 )
        throw (1);

    m_pNNFilter0 = p->nFilter0Length  ?  new CNNFilter (p->nFilter0Length, p->nFilter0Shift)  :  NULL;
    m_pNNFilter1 = p->nFilter1Length  ?  new CNNFilter (p->nFilter1Length, p->nFilter1Shift)  :  NULL;
    m_pNNFilter2 = p->nFilter2Length  ?  new CNNFilter (p->nFilter2Length, p->nFilter2Shift)  :  NULL;
}

CPredictorCompressNormal::~CPredictorCompressNormal()
{
    SAFE_DELETE (m_pNNFilter0)
    SAFE_DELETE (m_pNNFilter1)
    SAFE_DELETE (m_pNNFilter2)
}

int CPredictorCompressNormal::Flush()
{
    if ( m_pNNFilter0 != NULL ) m_pNNFilter0->Flush();
    if ( m_pNNFilter1 != NULL ) m_pNNFilter1->Flush();
    if ( m_pNNFilter2 != NULL ) m_pNNFilter2->Flush();

    m_rbPredictionA.Flush(); m_rbPredictionB.Flush();
    m_rbAdaptA     .Flush(); m_rbAdaptB     .Flush();
    m_Stage1FilterA.Flush(); m_Stage1FilterB.Flush();

    ZeroMemory(m_aryMA, sizeof(m_aryMA));
    ZeroMemory(m_aryMB, sizeof(m_aryMB));

    m_aryMA[0] = 360;
    m_aryMA[1] = 317;
    m_aryMA[2] = -109;
    m_aryMA[3] = 98;

    m_nLastValueA = 0;

    m_nCurrentIndex = 0;

    return 0;
}


int CPredictorCompressNormal::CompressValue(int nA, int nB)
{
    if (m_nCurrentIndex == WINDOW_BLOCKS)
    {
        m_rbPredictionA.Roll(); m_rbPredictionB.Roll();
        m_rbAdaptA     .Roll(); m_rbAdaptB     .Roll();

        m_nCurrentIndex = 0;
    }

    // stage 1: simple, non-adaptive order 1 prediction
    int nCurrentA = m_Stage1FilterA.Compress(nA);
    int nCurrentB = m_Stage1FilterB.Compress(nB);

    // stage 2: adaptive offset filter(s)
    m_rbPredictionA[0] = m_nLastValueA;
    m_rbPredictionA[-1] = m_rbPredictionA[0] - m_rbPredictionA[-1];

    m_rbPredictionB[0] = nCurrentB;
    m_rbPredictionB[-1] = m_rbPredictionB[0] - m_rbPredictionB[-1];

    int nPredictionA = (m_rbPredictionA[0] * m_aryMA[0]) + (m_rbPredictionA[-1] * m_aryMA[1]) + (m_rbPredictionA[-2] * m_aryMA[2]) + (m_rbPredictionA[-3] * m_aryMA[3]);
    int nPredictionB = (m_rbPredictionB[0] * m_aryMB[0]) + (m_rbPredictionB[-1] * m_aryMB[1]) + (m_rbPredictionB[-2] * m_aryMB[2]) + (m_rbPredictionB[-3] * m_aryMB[3]) + (m_rbPredictionB[-4] * m_aryMB[4]);

    int nOutput = nCurrentA - ((nPredictionA + (nPredictionB >> 1)) >> 10);

    m_nLastValueA = nCurrentA;

    m_rbAdaptA[0] = (m_rbPredictionA[0]) ? ((m_rbPredictionA[0] >> 30) & 2) - 1 : 0;
    m_rbAdaptA[-1] = (m_rbPredictionA[-1]) ? ((m_rbPredictionA[-1] >> 30) & 2) - 1 : 0;

    m_rbAdaptB[0] = (m_rbPredictionB[0]) ? ((m_rbPredictionB[0] >> 30) & 2) - 1 : 0;
    m_rbAdaptB[-1] = (m_rbPredictionB[-1]) ? ((m_rbPredictionB[-1] >> 30) & 2) - 1 : 0;

    if (nOutput > 0)
    {
        m_aryMA[0] -= m_rbAdaptA[0];
        m_aryMA[1] -= m_rbAdaptA[-1];
        m_aryMA[2] -= m_rbAdaptA[-2];
        m_aryMA[3] -= m_rbAdaptA[-3];

        m_aryMB[0] -= m_rbAdaptB[0];
        m_aryMB[1] -= m_rbAdaptB[-1];
        m_aryMB[2] -= m_rbAdaptB[-2];
        m_aryMB[3] -= m_rbAdaptB[-3];
        m_aryMB[4] -= m_rbAdaptB[-4];
    }
    else if (nOutput < 0)
    {
        m_aryMA[0] += m_rbAdaptA[0];
        m_aryMA[1] += m_rbAdaptA[-1];
        m_aryMA[2] += m_rbAdaptA[-2];
        m_aryMA[3] += m_rbAdaptA[-3];

        m_aryMB[0] += m_rbAdaptB[0];
        m_aryMB[1] += m_rbAdaptB[-1];
        m_aryMB[2] += m_rbAdaptB[-2];
        m_aryMB[3] += m_rbAdaptB[-3];
        m_aryMB[4] += m_rbAdaptB[-4];
    }

    // stage 3: NNFilters

    if ( m_pNNFilter0 != NULL ) nOutput = m_pNNFilter0->Compress(nOutput);
    if ( m_pNNFilter1 != NULL ) nOutput = m_pNNFilter1->Compress(nOutput);
    if ( m_pNNFilter2 != NULL ) nOutput = m_pNNFilter2->Compress(nOutput);

    m_rbPredictionA.IncrementFast(); m_rbPredictionB.IncrementFast();
    m_rbAdaptA.IncrementFast(); m_rbAdaptB.IncrementFast();

    m_nCurrentIndex++;

    return nOutput;
}

/*****************************************************************************************
CPredictorDecompressNormal3930to3950
*****************************************************************************************/
CPredictorDecompressNormal3930to3950::CPredictorDecompressNormal3930to3950(int nCompressionLevel)
    : IPredictorDecompress(nCompressionLevel)
{
    const OrderType*  p = OrderTypeArray + nCompressionLevel / 1000;
    m_pBuffer[0] = new int [HISTORY_ELEMENTS + WINDOW_BLOCKS];

    if ( nCompressionLevel < 1000  ||  nCompressionLevel > 6000  ||  nCompressionLevel % 1000 != 0 )
        throw (1);

    m_pNNFilter0 = p->nFilter0Length  ?  new CNNFilter (p->nFilter0Length, p->nFilter0Shift)  :  NULL;
    m_pNNFilter1 = p->nFilter1Length  ?  new CNNFilter (p->nFilter1Length, p->nFilter1Shift)  :  NULL;
    m_pNNFilter2 = p->nFilter2Length  ?  new CNNFilter (p->nFilter2Length, p->nFilter2Shift)  :  NULL;
}

CPredictorDecompressNormal3930to3950::~CPredictorDecompressNormal3930to3950()
{
    SAFE_DELETE (m_pNNFilter0)
    SAFE_DELETE (m_pNNFilter1)
    SAFE_DELETE (m_pNNFilter2)
    SAFE_ARRAY_DELETE(m_pBuffer[0])
}

int CPredictorDecompressNormal3930to3950::Flush()
{
    if (m_pNNFilter0) m_pNNFilter0->Flush();
    if (m_pNNFilter1) m_pNNFilter1->Flush();
    if (m_pNNFilter2) m_pNNFilter2->Flush();

    ZeroMemory(m_pBuffer[0], (HISTORY_ELEMENTS + 1) * sizeof(int));
    ZeroMemory(&m_aryM[0], M_COUNT * sizeof(int));

    m_aryM[0] = 360;
    m_aryM[1] = 317;
    m_aryM[2] = -109;
    m_aryM[3] = 98;

    m_pInputBuffer = &m_pBuffer[0][HISTORY_ELEMENTS];

    m_nLastValue = 0;
    m_nCurrentIndex = 0;

    return 0;
}

int CPredictorDecompressNormal3930to3950::DecompressValue(int nInput, int)
{
    if (m_nCurrentIndex == WINDOW_BLOCKS)
    {
        // copy forward and adjust pointers
        memcpy(&m_pBuffer[0][0], &m_pBuffer[0][WINDOW_BLOCKS], HISTORY_ELEMENTS * sizeof(int));
        m_pInputBuffer = &m_pBuffer[0][HISTORY_ELEMENTS];

        m_nCurrentIndex = 0;
    }

    // stage 2: NNFilter
    if (m_pNNFilter2 != NULL) nInput = m_pNNFilter2->Decompress(nInput);
    if (m_pNNFilter1 != NULL) nInput = m_pNNFilter1->Decompress(nInput);
    if (m_pNNFilter0 != NULL) nInput = m_pNNFilter0->Decompress(nInput);

    // stage 1: multiple predictors (order 2 and offset 1)

    int p1 = m_pInputBuffer[-1];
    int p2 = m_pInputBuffer[-1] - m_pInputBuffer[-2];
    int p3 = m_pInputBuffer[-2] - m_pInputBuffer[-3];
    int p4 = m_pInputBuffer[-3] - m_pInputBuffer[-4];

    m_pInputBuffer[0] = nInput + ((p1 * m_aryM[0] + p2 * m_aryM[1] + p3 * m_aryM[2] + p4 * m_aryM[3]) >> 9);

    if (nInput > 0)
    {
        m_aryM[0] -= ((p1 >> 30) & 2) - 1;
        m_aryM[1] -= ((p2 >> 30) & 2) - 1;
        m_aryM[2] -= ((p3 >> 30) & 2) - 1;
        m_aryM[3] -= ((p4 >> 30) & 2) - 1;
    }
    else if (nInput < 0)
    {
        m_aryM[0] += ((p1 >> 30) & 2) - 1;
        m_aryM[1] += ((p2 >> 30) & 2) - 1;
        m_aryM[2] += ((p3 >> 30) & 2) - 1;
        m_aryM[3] += ((p4 >> 30) & 2) - 1;
    }

    int nRetVal = m_pInputBuffer[0] + ((m_nLastValue * 31) >> 5);
    m_nLastValue = nRetVal;

    m_nCurrentIndex++;
    m_pInputBuffer++;

    return nRetVal;
}

/*****************************************************************************************
CPredictorDecompress3950toCurrent
*****************************************************************************************/
CPredictorDecompress3950toCurrent::CPredictorDecompress3950toCurrent(int nCompressionLevel)
    : IPredictorDecompress(nCompressionLevel)
{
    const OrderType*  p = OrderTypeArray + nCompressionLevel / 1000;

    if ( nCompressionLevel < 1000  ||  nCompressionLevel > 6000  ||  nCompressionLevel % 1000 != 0 )
        throw (1);

    m_pNNFilter0 = p->nFilter0Length  ?  new CNNFilter (p->nFilter0Length, p->nFilter0Shift)  :  NULL;
    m_pNNFilter1 = p->nFilter1Length  ?  new CNNFilter (p->nFilter1Length, p->nFilter1Shift)  :  NULL;
    m_pNNFilter2 = p->nFilter2Length  ?  new CNNFilter (p->nFilter2Length, p->nFilter2Shift)  :  NULL;
}

CPredictorDecompress3950toCurrent::~CPredictorDecompress3950toCurrent()
{
    SAFE_DELETE(m_pNNFilter0)
    SAFE_DELETE(m_pNNFilter1)
    SAFE_DELETE(m_pNNFilter2)
}

int CPredictorDecompress3950toCurrent::Flush()
{
    if (m_pNNFilter0 != NULL) m_pNNFilter0->Flush();
    if (m_pNNFilter1 != NULL) m_pNNFilter1->Flush();
    if (m_pNNFilter2 != NULL) m_pNNFilter2->Flush();

    ZeroMemory(m_aryMA, sizeof(m_aryMA));
    ZeroMemory(m_aryMB, sizeof(m_aryMB));

    m_rbPredictionA.Flush();
    m_rbPredictionB.Flush();
    m_rbAdaptA.Flush();
    m_rbAdaptB.Flush();

    m_aryMA[0] = 360;
    m_aryMA[1] = 317;
    m_aryMA[2] = -109;
    m_aryMA[3] = 98;

    m_Stage1FilterA.Flush();
    m_Stage1FilterB.Flush();

    m_nLastValueA = 0;

    m_nCurrentIndex = 0;

    return 0;
}

int CPredictorDecompress3950toCurrent::DecompressValue(int nA, int nB)
{
    if (m_nCurrentIndex == WINDOW_BLOCKS)
    {
        // copy forward and adjust pointers
        m_rbPredictionA.Roll(); m_rbPredictionB.Roll();
        m_rbAdaptA.Roll(); m_rbAdaptB.Roll();

        m_nCurrentIndex = 0;
    }

    // stage 2: NNFilter
    if (m_pNNFilter2 != NULL) nA = m_pNNFilter2->Decompress(nA);
    if (m_pNNFilter1 != NULL) nA = m_pNNFilter1->Decompress(nA);
    if (m_pNNFilter0 != NULL) nA = m_pNNFilter0->Decompress(nA);

    // stage 1: multiple predictors (order 2 and offset 1)
    m_rbPredictionA[0] = m_nLastValueA;
    m_rbPredictionA[-1] = m_rbPredictionA[0] - m_rbPredictionA[-1];

    m_rbPredictionB[0] = m_Stage1FilterB.Compress(nB);
    m_rbPredictionB[-1] = m_rbPredictionB[0] - m_rbPredictionB[-1];

    int nPredictionA = (m_rbPredictionA[0] * m_aryMA[0]) + (m_rbPredictionA[-1] * m_aryMA[1]) + (m_rbPredictionA[-2] * m_aryMA[2]) + (m_rbPredictionA[-3] * m_aryMA[3]);
    int nPredictionB = (m_rbPredictionB[0] * m_aryMB[0]) + (m_rbPredictionB[-1] * m_aryMB[1]) + (m_rbPredictionB[-2] * m_aryMB[2]) + (m_rbPredictionB[-3] * m_aryMB[3]) + (m_rbPredictionB[-4] * m_aryMB[4]);

    int nCurrentA = nA + ((nPredictionA + (nPredictionB >> 1)) >> 10);

    m_rbAdaptA[0] = (m_rbPredictionA[0]) ? ((m_rbPredictionA[0] >> 30) & 2) - 1 : 0;
    m_rbAdaptA[-1] = (m_rbPredictionA[-1]) ? ((m_rbPredictionA[-1] >> 30) & 2) - 1 : 0;

    m_rbAdaptB[0] = (m_rbPredictionB[0]) ? ((m_rbPredictionB[0] >> 30) & 2) - 1 : 0;
    m_rbAdaptB[-1] = (m_rbPredictionB[-1]) ? ((m_rbPredictionB[-1] >> 30) & 2) - 1 : 0;

    if (nA > 0)
    {
        m_aryMA[0] -= m_rbAdaptA[0];
        m_aryMA[1] -= m_rbAdaptA[-1];
        m_aryMA[2] -= m_rbAdaptA[-2];
        m_aryMA[3] -= m_rbAdaptA[-3];

        m_aryMB[0] -= m_rbAdaptB[0];
        m_aryMB[1] -= m_rbAdaptB[-1];
        m_aryMB[2] -= m_rbAdaptB[-2];
        m_aryMB[3] -= m_rbAdaptB[-3];
        m_aryMB[4] -= m_rbAdaptB[-4];
    }
    else if (nA < 0)
    {
        m_aryMA[0] += m_rbAdaptA[0];
        m_aryMA[1] += m_rbAdaptA[-1];
        m_aryMA[2] += m_rbAdaptA[-2];
        m_aryMA[3] += m_rbAdaptA[-3];

        m_aryMB[0] += m_rbAdaptB[0];
        m_aryMB[1] += m_rbAdaptB[-1];
        m_aryMB[2] += m_rbAdaptB[-2];
        m_aryMB[3] += m_rbAdaptB[-3];
        m_aryMB[4] += m_rbAdaptB[-4];
    }

    int nRetVal = m_Stage1FilterA.Decompress(nCurrentA);
    m_nLastValueA = nCurrentA;

    m_rbPredictionA.IncrementFast(); m_rbPredictionB.IncrementFast();
    m_rbAdaptA.IncrementFast(); m_rbAdaptB.IncrementFast();

    m_nCurrentIndex++;

    return nRetVal;
}
