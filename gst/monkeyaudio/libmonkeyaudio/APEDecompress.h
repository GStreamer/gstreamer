#ifndef APE_DECOMPRESS_H
#define APE_DECOMPRESS_H

#include "APEDecompress.h"

class CUnBitArray;
class CPrepare;
class CAPEInfo;
class IPredictorDecompress;
#include "UnBitArrayBase.h"
#include "MACLib.h"
#include "Prepare.h"

class CAPEDecompress : public IAPEDecompress
{
public:
    CAPEDecompress(int * pErrorCode, CAPEInfo * pAPEInfo, int nStartBlock = -1, int nFinishBlock = -1);
    ~CAPEDecompress();

    int GetData(char * pBuffer, int nBlocks, int * pBlocksRetrieved);
    int Seek(int nBlockOffset);

    int GetInfo(APE_DECOMPRESS_FIELDS Field, int nParam1 = 0, int nParam2 = 0);

protected:

    // file info
    int m_nBlockAlign;
    int m_nCurrentFrame;

    // start / finish information
    int m_nStartBlock;
    int m_nFinishBlock;
    int m_nCurrentBlock;
    BOOL m_bIsRanged;
    BOOL m_bDecompressorInitialized;

    // decoding tools
    CPrepare m_Prepare;
    WAVEFORMATEX m_wfeInput;
    int m_nBlocksProcessed;
    unsigned int m_nCRC;
    unsigned int m_nStoredCRC;
    int m_nSpecialCodes;
    BOOL m_bCurrentFrameCorrupt;

    int SeekToFrame(int nFrameIndex);
    int GetBlocks(unsigned char * pOutputBuffer, int nBlocks);
    int StartFrame();
    int EndFrame();
    int InitializeDecompressor();

    // more decoding components
    CSmartPtr<CAPEInfo> m_spAPEInfo;
    CSmartPtr<CUnBitArrayBase> m_spUnBitArray;
    BIT_ARRAY_STATE m_BitArrayStateX;
    BIT_ARRAY_STATE m_BitArrayStateY;

    CSmartPtr<IPredictorDecompress> m_spNewPredictorX;
    CSmartPtr<IPredictorDecompress> m_spNewPredictorY;

    int m_nLastX;
};

#endif /* APE_DECOMPRESS_H */
