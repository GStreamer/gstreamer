#ifndef APE_COMPRESSCREATE_H
#define APE_COMPRESSCREATE_H

#include "APECompress.h"

class CAPECompressCore;

class CAPECompressCreate
{
public:
    CAPECompressCreate();
    ~CAPECompressCreate();

    int InitializeFile(CIO * pIO, const WAVEFORMATEX * pwfeInput, int nMaxFrames, int nCompressionLevel, const unsigned char * pHeaderData, int nHeaderBytes);
    int FinalizeFile(CIO * pIO, int nNumberOfFrames, int nFinalFrameBlocks, const unsigned char * pTerminatingData, int nTerminatingBytes, int nWAVTerminatingBytes, int nPeakLevel);

    int SetSeekByte(int nFrame, int nByteOffset);

    int Start(CIO * pioOutput, const WAVEFORMATEX * pwfeInput, int nMaxAudioBytes, int nCompressionLevel = COMPRESSION_LEVEL_NORMAL, const unsigned char * pHeaderData = NULL, int nHeaderBytes = CREATE_WAV_HEADER_ON_DECOMPRESSION);

    int GetFullFrameBytes();
    int EncodeFrame(unsigned char * pInputData, int nInputBytes);

    int Finish(unsigned char * pTerminatingData, int nTerminatingBytes, int nWAVTerminatingBytes);


private:

    CSmartPtr<unsigned __int32> m_spSeekTable;
    int m_nMaxFrames;

    CSmartPtr<CIO> m_spIO;
    CSmartPtr<CAPECompressCore> m_spAPECompressCore;

    WAVEFORMATEX    m_wfeInput;
    int             m_nCompressionLevel;
    int             m_nSamplesPerFrame;
    int             m_nFrameIndex;
    int             m_nLastFrameBlocks;

};

#endif /* APE_COMPRESSCREATE_H */
