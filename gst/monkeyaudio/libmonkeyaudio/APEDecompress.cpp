#include "All.h"
#include "APEDecompress.h"

#include "APEInfo.h"
#include "Prepare.h"
#include "UnBitArray.h"
#include "NewPredictor.h"

CAPEDecompress::CAPEDecompress(int * pErrorCode, CAPEInfo * pAPEInfo, int nStartBlock, int nFinishBlock)
{
    *pErrorCode = ERROR_SUCCESS;

    // open / analyze the file
    m_spAPEInfo.Assign(pAPEInfo);

    // version check (this implementation only works with 3.93 and later files)
    if (GetInfo(APE_INFO_FILE_VERSION) < 3930)
    {
        *pErrorCode = ERROR_UNDEFINED;
        return;
    }

    // get format information
    GetInfo(APE_INFO_WAVEFORMATEX, (int) &m_wfeInput);
    m_nBlockAlign = GetInfo(APE_INFO_BLOCK_ALIGN);

    // initialize other stuff
    m_bDecompressorInitialized = FALSE;
    m_nCurrentFrame = 0;
    m_nCurrentBlock = 0;

    // set the "real" start and finish blocks
    m_nStartBlock = (nStartBlock < 0) ? 0 : min(nStartBlock, GetInfo(APE_INFO_TOTAL_BLOCKS));
    m_nFinishBlock = (nFinishBlock < 0) ? GetInfo(APE_INFO_TOTAL_BLOCKS) : min(nFinishBlock, GetInfo(APE_INFO_TOTAL_BLOCKS));
    m_bIsRanged = (m_nStartBlock != 0) || (m_nFinishBlock != GetInfo(APE_INFO_TOTAL_BLOCKS));
}

CAPEDecompress::~CAPEDecompress()
{

}

int CAPEDecompress::InitializeDecompressor()
{
    // check if we have anything to do
    if (m_bDecompressorInitialized)
        return ERROR_SUCCESS;

    // update the initialized flag
    m_bDecompressorInitialized = TRUE;

    // create decoding components
    m_spUnBitArray.Assign((CUnBitArrayBase *) CreateUnBitArray(this, GetInfo(APE_INFO_FILE_VERSION)));
    if (GetInfo(APE_INFO_FILE_VERSION) >= 3950)
    {
        m_spNewPredictorX.Assign(new CPredictorDecompress3950toCurrent(GetInfo(APE_INFO_COMPRESSION_LEVEL)));
        m_spNewPredictorY.Assign(new CPredictorDecompress3950toCurrent(GetInfo(APE_INFO_COMPRESSION_LEVEL)));
    }
    else
    {
        m_spNewPredictorX.Assign(new CPredictorDecompressNormal3930to3950(GetInfo(APE_INFO_COMPRESSION_LEVEL)));
        m_spNewPredictorY.Assign(new CPredictorDecompressNormal3930to3950(GetInfo(APE_INFO_COMPRESSION_LEVEL)));
    }

    // seek to the beginning
    return Seek(0);
}

int CAPEDecompress::GetData(char * pBuffer, int nBlocks, int * pBlocksRetrieved)
{

    if (pBlocksRetrieved) *pBlocksRetrieved = 0;

    RETURN_ON_ERROR(InitializeDecompressor())

    // cap
    int nBlocksUntilFinish = m_nFinishBlock - m_nCurrentBlock;
    const int nBlocksToRetrieve = min(nBlocks, nBlocksUntilFinish);

    int nRetVal = GetBlocks((unsigned char *) pBuffer, nBlocksToRetrieve);

    m_nCurrentBlock += nBlocksToRetrieve;

    if (pBlocksRetrieved) *pBlocksRetrieved = nBlocksToRetrieve;

    return nRetVal;
}

int CAPEDecompress::Seek(int nBlockOffset)
{
    RETURN_ON_ERROR(InitializeDecompressor())

    // use the offset
    nBlockOffset += m_nStartBlock;

    // cap (to prevent seeking too far)
    if (nBlockOffset >= m_nFinishBlock)
        nBlockOffset = m_nFinishBlock - 1;
    if (nBlockOffset < m_nStartBlock)
        nBlockOffset = m_nStartBlock;

    // seek to the perfect location
    int nBaseFrame = nBlockOffset / GetInfo(APE_INFO_BLOCKS_PER_FRAME);
    int nBlocksToSkip = nBlockOffset % GetInfo(APE_INFO_BLOCKS_PER_FRAME);
    int nBytesToSkip = nBlocksToSkip * m_nBlockAlign;

    m_nCurrentFrame = nBaseFrame;
    RETURN_ON_ERROR(SeekToFrame(m_nCurrentFrame));

    m_nBlocksProcessed = 0;

    // skip necessary blocks
    CSmartPtr<char> spTempBuffer(new char [nBytesToSkip], TRUE);
    if (spTempBuffer == NULL) return ERROR_INSUFFICIENT_MEMORY;

    int nBlocksRetrieved = 0;
    GetData(spTempBuffer, nBlocksToSkip, &nBlocksRetrieved);
    if (nBlocksRetrieved != nBlocksToSkip)
        return ERROR_UNDEFINED;

    m_nCurrentBlock = nBlockOffset;
    return ERROR_SUCCESS;
}

/*****************************************************************************************
Decodes blocks of data
*****************************************************************************************/
int CAPEDecompress::GetBlocks(unsigned char * pOutputBuffer, int nBlocks)
{
    if (nBlocks <= 0) return 0;

    int nRetVal = ERROR_SUCCESS;
    int nBlocksLeft = nBlocks;

    while (nBlocksLeft > 0)
    {
        // start of frame code
        if (m_nBlocksProcessed == 0)
        {
            if (StartFrame() != 0)
            {
                m_bCurrentFrameCorrupt = TRUE;
                nRetVal = -1;
            }
        }

        // get the blocks (up to the end of the frame)

        const int nBlocksUntilEndOfFrame = GetInfo(APE_INFO_BLOCKS_PER_FRAME) - m_nBlocksProcessed;
        const int nBlocksThisPass = min(nBlocksLeft, nBlocksUntilEndOfFrame);

        int nBlocksProcessed = 0;

        if (m_bCurrentFrameCorrupt)
        {
            for (nBlocksProcessed = 0; nBlocksProcessed < nBlocksThisPass; nBlocksProcessed++)
            {
                m_Prepare.Unprepare(0, 0, &m_wfeInput, pOutputBuffer, &m_nCRC);
                m_nBlocksProcessed++;
            }
            nBlocksLeft -= nBlocksProcessed;
        }
        else
        {
            try
            {
                if (m_wfeInput.nChannels == 2)
                {
                    if ((m_nSpecialCodes & SPECIAL_FRAME_LEFT_SILENCE) &&
                        (m_nSpecialCodes & SPECIAL_FRAME_RIGHT_SILENCE))
                    {
                        for (nBlocksProcessed = 0; nBlocksProcessed < nBlocksThisPass; nBlocksProcessed++)
                        {
                            m_Prepare.Unprepare(0, 0, &m_wfeInput, pOutputBuffer, &m_nCRC);
                            pOutputBuffer += m_nBlockAlign;
                            m_nBlocksProcessed++;
                        }
                    }
                    else if (m_nSpecialCodes & SPECIAL_FRAME_PSEUDO_STEREO)
                    {
                        for (nBlocksProcessed = 0; nBlocksProcessed < nBlocksThisPass; nBlocksProcessed++)
                        {
                            int X = m_spNewPredictorX->DecompressValue(m_spUnBitArray->DecodeValueRange(m_BitArrayStateX));
                            m_Prepare.Unprepare(X, 0, &m_wfeInput, pOutputBuffer, &m_nCRC);
                            pOutputBuffer += m_nBlockAlign;
                            m_nBlocksProcessed++;
                        }
                    }
                    else
                    {
                        if (m_spAPEInfo->GetInfo(APE_INFO_FILE_VERSION) >= 3950)
                        {
                            for (nBlocksProcessed = 0; nBlocksProcessed < nBlocksThisPass; nBlocksProcessed++)
                            {
                                int nY = m_spUnBitArray->DecodeValueRange(m_BitArrayStateY);
                                int nX = m_spUnBitArray->DecodeValueRange(m_BitArrayStateX);
                                int Y = m_spNewPredictorY->DecompressValue(nY, m_nLastX);
                                int X = m_spNewPredictorX->DecompressValue(nX, Y);
                                m_nLastX = X;

                                m_Prepare.Unprepare(X, Y, &m_wfeInput, pOutputBuffer, &m_nCRC);
                                pOutputBuffer += m_nBlockAlign;
                                m_nBlocksProcessed++;
                            }
                        }
                        else
                        {
                            for (nBlocksProcessed = 0; nBlocksProcessed < nBlocksThisPass; nBlocksProcessed++)
                            {
                                int X = m_spNewPredictorX->DecompressValue(m_spUnBitArray->DecodeValueRange(m_BitArrayStateX));
                                int Y = m_spNewPredictorY->DecompressValue(m_spUnBitArray->DecodeValueRange(m_BitArrayStateY));
                                m_Prepare.Unprepare(X, Y, &m_wfeInput, pOutputBuffer, &m_nCRC);
                                pOutputBuffer += m_nBlockAlign;
                                m_nBlocksProcessed++;
                            }
                        }
                    }
                }
                else
                {
                    if (m_nSpecialCodes & SPECIAL_FRAME_MONO_SILENCE)
                    {
                        for (nBlocksProcessed = 0; nBlocksProcessed < nBlocksThisPass; nBlocksProcessed++)
                        {
                            m_Prepare.Unprepare(0, 0, &m_wfeInput, pOutputBuffer, &m_nCRC);
                            pOutputBuffer += m_nBlockAlign;
                            m_nBlocksProcessed++;
                        }
                    }
                    else
                    {
                        for (nBlocksProcessed = 0; nBlocksProcessed < nBlocksThisPass; nBlocksProcessed++)
                        {
                            int X = m_spNewPredictorX->DecompressValue(m_spUnBitArray->DecodeValueRange(m_BitArrayStateX));
                            m_Prepare.Unprepare(X, 0, &m_wfeInput, pOutputBuffer, &m_nCRC);
                            pOutputBuffer += m_nBlockAlign;
                            m_nBlocksProcessed++;
                        }
                    }
                }

                nBlocksLeft -= nBlocksProcessed;
            }
            catch(...)
            {
                m_bCurrentFrameCorrupt = TRUE;
                nRetVal = -1;
                nBlocksLeft -= nBlocksProcessed;
            }
        }

        // end of frame code
        if (m_nBlocksProcessed == GetInfo(APE_INFO_BLOCKS_PER_FRAME))
        {
            if (EndFrame() != 0)
                nRetVal = -1;
        }
    }

    return nRetVal;
}

int CAPEDecompress::StartFrame()
{
    m_nCRC = 0xFFFFFFFF;

    // get the frame header
    m_nStoredCRC = m_spUnBitArray->DecodeValue(DECODE_VALUE_METHOD_UNSIGNED_INT);

    //get any 'special' codes if the file uses them (for silence, FALSE stereo, etc.)
    m_nSpecialCodes = 0;
    if (GET_USES_SPECIAL_FRAMES(m_spAPEInfo))
    {
        if (m_nStoredCRC & 0x80000000)
        {
            m_nSpecialCodes = m_spUnBitArray->DecodeValue(DECODE_VALUE_METHOD_UNSIGNED_INT);
        }
        m_nStoredCRC &= 0x7fffffff;
    }

    m_spNewPredictorX->Flush();
    m_spNewPredictorY->Flush();

    m_spUnBitArray->FlushState(m_BitArrayStateX);
    m_spUnBitArray->FlushState(m_BitArrayStateY);

    m_spUnBitArray->FlushBitArray();

    m_bCurrentFrameCorrupt = FALSE;

    m_nLastX = 0;

    return ERROR_SUCCESS;
}

int CAPEDecompress::EndFrame()
{
    int nRetVal = ERROR_SUCCESS;

    m_nCurrentFrame++;
    m_nBlocksProcessed = 0;

    if (m_bCurrentFrameCorrupt == FALSE)
    {
        // finalize
        m_spUnBitArray->Finalize();

        // check the CRC
        m_nCRC = m_nCRC ^ 0xFFFFFFFF;
        m_nCRC >>= 1;
        if (m_nCRC != m_nStoredCRC)
        {
            nRetVal = -1;
            m_bCurrentFrameCorrupt = TRUE;
        }
    }

    if (m_bCurrentFrameCorrupt)
    {
        SeekToFrame(m_nCurrentFrame);
    }

    return nRetVal;
}

/*****************************************************************************************
Seek to the proper frame (if necessary) and do any alignment of the bit array
*****************************************************************************************/
int CAPEDecompress::SeekToFrame(int nFrameIndex)
{
    int nSeekRemainder = (GetInfo(APE_INFO_SEEK_BYTE, nFrameIndex) - GetInfo(APE_INFO_SEEK_BYTE, 0)) % 4;
    return m_spUnBitArray->FillAndResetBitArray(GetInfo(APE_INFO_SEEK_BYTE, nFrameIndex) - nSeekRemainder, nSeekRemainder * 8);
}

/*****************************************************************************************
Get information from the decompressor
*****************************************************************************************/
int CAPEDecompress::GetInfo(APE_DECOMPRESS_FIELDS Field, int nParam1, int nParam2)
{
    int nRetVal = 0;
    BOOL bHandled = TRUE;

    switch (Field)
    {
    case APE_DECOMPRESS_CURRENT_BLOCK:
        nRetVal = m_nCurrentBlock - m_nStartBlock;
        break;
    case APE_DECOMPRESS_CURRENT_MS:
    {
        int nSampleRate = m_spAPEInfo->GetInfo(APE_INFO_SAMPLE_RATE, 0, 0);
        if (nSampleRate > 0)
            nRetVal = int((double(m_nCurrentBlock) * double(1000)) / double(nSampleRate));
        break;
    }
    case APE_DECOMPRESS_TOTAL_BLOCKS:
        nRetVal = m_nFinishBlock - m_nStartBlock;
        break;
    case APE_DECOMPRESS_LENGTH_MS:
    {
        int nSampleRate = m_spAPEInfo->GetInfo(APE_INFO_SAMPLE_RATE, 0, 0);
        if (nSampleRate > 0)
            nRetVal = int((double(m_nFinishBlock - m_nStartBlock) * double(1000)) / double(nSampleRate));
        break;
    }
    case APE_DECOMPRESS_CURRENT_BITRATE:
        nRetVal = GetInfo(APE_INFO_FRAME_BITRATE, m_nCurrentFrame);
        break;
    case APE_DECOMPRESS_AVERAGE_BITRATE:
    {
        if (m_bIsRanged)
        {
            // figure the frame range
            const int nBlocksPerFrame = GetInfo(APE_INFO_BLOCKS_PER_FRAME);
            int nStartFrame = m_nStartBlock / nBlocksPerFrame;
            int nFinishFrame = (m_nFinishBlock + nBlocksPerFrame - 1) / nBlocksPerFrame;

            // get the number of bytes in the first and last frame
            int nTotalBytes = (GetInfo(APE_INFO_FRAME_BYTES, nStartFrame) * (m_nStartBlock % nBlocksPerFrame)) / nBlocksPerFrame;
            if (nFinishFrame != nStartFrame)
                nTotalBytes += (GetInfo(APE_INFO_FRAME_BYTES, nFinishFrame) * (m_nFinishBlock % nBlocksPerFrame)) / nBlocksPerFrame;

            // get the number of bytes in between
            const int nTotalFrames = GetInfo(APE_INFO_TOTAL_FRAMES);
            for (int nFrame = nStartFrame + 1; (nFrame < nFinishFrame) && (nFrame < nTotalFrames); nFrame++)
                nTotalBytes += GetInfo(APE_INFO_FRAME_BYTES, nFrame);

            // figure the bitrate
            int nTotalMS = int((double(m_nFinishBlock - m_nStartBlock) * double(1000)) / double(GetInfo(APE_INFO_SAMPLE_RATE)));
            if (nTotalMS != 0)
                nRetVal = (nTotalBytes * 8) / nTotalMS;
        }
        else
        {
            nRetVal = GetInfo(APE_INFO_AVERAGE_BITRATE);
        }

        break;
    }
    default:
        bHandled = FALSE;
    }

    if (!bHandled && m_bIsRanged)
    {
        bHandled = TRUE;

        switch (Field)
        {
        case APE_INFO_WAV_HEADER_BYTES:
            nRetVal = sizeof(WAVE_HEADER);
            break;
        case APE_INFO_WAV_HEADER_DATA:
        {
            char * pBuffer = (char *) nParam1;
            int nMaxBytes = nParam2;

            if (sizeof(WAVE_HEADER) > nMaxBytes)
            {
                nRetVal = -1;
            }
            else
            {
                WAVEFORMATEX wfeFormat; GetInfo(APE_INFO_WAVEFORMATEX, (int) &wfeFormat, 0);
                WAVE_HEADER WAVHeader; FillWaveHeader(&WAVHeader,
                    (m_nFinishBlock - m_nStartBlock) * GetInfo(APE_INFO_BLOCK_ALIGN),
                    &wfeFormat, 0);
                memcpy(pBuffer, &WAVHeader, sizeof(WAVE_HEADER));
                nRetVal = 0;
            }
            break;
        }
        case APE_INFO_WAV_TERMINATING_BYTES:
            nRetVal = 0;
            break;
        case APE_INFO_WAV_TERMINATING_DATA:
            nRetVal = 0;
            break;
        default:
            bHandled = FALSE;
        }
    }

    if (bHandled == FALSE)
        nRetVal = m_spAPEInfo->GetInfo(Field, nParam1, nParam2);

    return nRetVal;
}
