#include "All.h"
#include "IO.h"
#include "APECompressCreate.h"

#include "APECompressCore.h"

CAPECompressCreate::CAPECompressCreate()
{
    m_nMaxFrames = 0;
}

CAPECompressCreate::~CAPECompressCreate()
{
}

int CAPECompressCreate::Start(CIO * pioOutput, const WAVEFORMATEX * pwfeInput, int nMaxAudioBytes, int nCompressionLevel, const unsigned char * pHeaderData, int nHeaderBytes)
{
    // verify the parameters
    if (pioOutput == NULL || pwfeInput == NULL)
        return ERROR_BAD_PARAMETER;

    // verify the wave format
    if ((pwfeInput->nChannels != 1) && (pwfeInput->nChannels != 2))
    {
        return ERROR_INPUT_FILE_UNSUPPORTED_CHANNEL_COUNT;
    }
    if ((pwfeInput->wBitsPerSample != 8) && (pwfeInput->wBitsPerSample != 16) && (pwfeInput->wBitsPerSample != 24))
    {
        return ERROR_INPUT_FILE_UNSUPPORTED_BIT_DEPTH;
    }

    // initialize (creates the base classes)
    m_nSamplesPerFrame = 73728 * 4;

    m_spIO.Assign(pioOutput, FALSE, FALSE);
    m_spAPECompressCore.Assign(new CAPECompressCore(m_spIO, pwfeInput, m_nSamplesPerFrame, nCompressionLevel));

    // copy the format
    memcpy(&m_wfeInput, pwfeInput, sizeof(WAVEFORMATEX));

    // the compression level
    m_nCompressionLevel = nCompressionLevel;
    m_nFrameIndex = 0;
    m_nLastFrameBlocks = m_nSamplesPerFrame;

    // initialize the file
    if (nMaxAudioBytes < 0)
        nMaxAudioBytes = 2147483647;

    unsigned __int32 nMaxAudioBlocks = nMaxAudioBytes / pwfeInput->nBlockAlign;
    int nMaxFrames = nMaxAudioBlocks / m_nSamplesPerFrame;
    if ((nMaxAudioBlocks % m_nSamplesPerFrame) != 0) nMaxFrames++;

    InitializeFile(m_spIO, &m_wfeInput, nMaxFrames,
        m_nCompressionLevel, pHeaderData, nHeaderBytes);

    return 0;
}

int CAPECompressCreate::GetFullFrameBytes()
{
    return m_nSamplesPerFrame * m_wfeInput.nBlockAlign;
}

int CAPECompressCreate::EncodeFrame(unsigned char * pInputData, int nInputBytes)
{
    int nInputBlocks = nInputBytes / m_wfeInput.nBlockAlign;

    if ((nInputBlocks < m_nSamplesPerFrame) && (m_nLastFrameBlocks < m_nSamplesPerFrame))
    {
        return -1; //can only pass a smaller frame for the very last time
    }

    // update the seek table
    m_spAPECompressCore->GetBitArray()->AdvanceToByteBoundary();
    int nRetVal = SetSeekByte(m_nFrameIndex, m_spIO->GetPosition() + (m_spAPECompressCore->GetBitArray()->GetCurrentBitIndex() / 8));
    if (nRetVal != ERROR_SUCCESS)
        return nRetVal;

    // compress
    nRetVal = m_spAPECompressCore->EncodeFrame(pInputData, nInputBytes);

    // update stats
    m_nLastFrameBlocks = nInputBlocks;
    m_nFrameIndex++;

    return nRetVal;
}

int CAPECompressCreate::Finish(unsigned char * pTerminatingData, int nTerminatingBytes, int nWAVTerminatingBytes)
{
    // clear the bit array
    RETURN_ON_ERROR(m_spAPECompressCore->GetBitArray()->OutputBitArray(TRUE));

    // finalize the file
    RETURN_ON_ERROR(FinalizeFile(m_spIO, m_nFrameIndex, m_nLastFrameBlocks,
        pTerminatingData, nTerminatingBytes, nWAVTerminatingBytes, m_spAPECompressCore->GetPeakLevel()));

    return 0;
}

int CAPECompressCreate::SetSeekByte(int nFrame, int nByteOffset)
{
    if (nFrame >= m_nMaxFrames) return ERROR_APE_COMPRESS_TOO_MUCH_DATA;
    m_spSeekTable[nFrame] = nByteOffset;
    return 0;
}

int CAPECompressCreate::InitializeFile(CIO * pIO, const WAVEFORMATEX * pwfeInput, int nMaxFrames, int nCompressionLevel, const unsigned char * pHeaderData, int nHeaderBytes)
{
    // error check the parameters
    if (pIO == NULL || pwfeInput == NULL || nMaxFrames <= 0)
        return ERROR_BAD_PARAMETER;

    // create a MAC header using the audio format info
    APE_HEADER APEHeader;
    APEHeader.cID[0] = 'M';
    APEHeader.cID[1] = 'A';
    APEHeader.cID[2] = 'C';
    APEHeader.cID[3] = ' ';
    APEHeader.nVersion = MAC_VERSION_NUMBER;
    APEHeader.nChannels = pwfeInput->nChannels;
    APEHeader.nCompressionLevel = (unsigned __int16) nCompressionLevel;
    APEHeader.nSampleRate = pwfeInput->nSamplesPerSec;
    APEHeader.nHeaderBytes = (nHeaderBytes == CREATE_WAV_HEADER_ON_DECOMPRESSION) ? 0 : nHeaderBytes;
    APEHeader.nTerminatingBytes = 0;

    // the format flag(s)
    APEHeader.nFormatFlags = MAC_FORMAT_FLAG_CRC | MAC_FORMAT_FLAG_HAS_PEAK_LEVEL | MAC_FORMAT_FLAG_HAS_SEEK_ELEMENTS;

    if (nHeaderBytes == CREATE_WAV_HEADER_ON_DECOMPRESSION)
        APEHeader.nFormatFlags |= MAC_FORMAT_FLAG_CREATE_WAV_HEADER;

    if (pwfeInput->wBitsPerSample == 8)
        APEHeader.nFormatFlags |= MAC_FORMAT_FLAG_8_BIT;
    else if (pwfeInput->wBitsPerSample == 24)
        APEHeader.nFormatFlags |= MAC_FORMAT_FLAG_24_BIT;

    // the number of frames
    APEHeader.nTotalFrames = 0;
    APEHeader.nFinalFrameBlocks = 0;

    // write the data to the file
    unsigned int BytesWritten = 0;
    RETURN_ON_ERROR(pIO->Write(&APEHeader, APE_HEADER_BYTES, &BytesWritten))

    int nPeakLevel = -1;
    RETURN_ON_ERROR(pIO->Write(&nPeakLevel, 4, &BytesWritten))

    RETURN_ON_ERROR(pIO->Write(&nMaxFrames, 4, &BytesWritten))

    if ((pHeaderData != NULL) && (nHeaderBytes > 0) && (nHeaderBytes != CREATE_WAV_HEADER_ON_DECOMPRESSION))
        RETURN_ON_ERROR(pIO->Write((void *) pHeaderData, nHeaderBytes, &BytesWritten))

    // write an empty seek table
    m_spSeekTable.Assign(new unsigned __int32 [nMaxFrames], TRUE);
    if (m_spSeekTable == NULL) { return ERROR_INSUFFICIENT_MEMORY; }
    ZeroMemory(m_spSeekTable, nMaxFrames * 4);
    RETURN_ON_ERROR(pIO->Write(m_spSeekTable, (nMaxFrames * 4), &BytesWritten))
    m_nMaxFrames = nMaxFrames;

    return 0;
}


int CAPECompressCreate::FinalizeFile(CIO * pIO, int nNumberOfFrames, int nFinalFrameBlocks, const unsigned char * pTerminatingData, int nTerminatingBytes, int nWAVTerminatingBytes, int nPeakLevel)
{
    // append the terminating data
    unsigned int BytesWritten = 0;
    unsigned int BytesRead = 0;
    int nRetVal = 0;

    if (nTerminatingBytes > 0)
    {
        if (pIO->Write((void *) pTerminatingData, nTerminatingBytes, &BytesWritten) != 0) { return ERROR_IO_WRITE; }
    }

    // go to the beginning and update the information
    nRetVal = pIO->Seek(0, FILE_BEGIN);

    // get the current header
    APE_HEADER APEHeader;
    nRetVal = pIO->Read(&APEHeader, APE_HEADER_BYTES, &BytesRead);
    if (nRetVal != 0 || BytesRead != APE_HEADER_BYTES) { return ERROR_IO_READ; }

    // update the header
    APEHeader.nTerminatingBytes = nWAVTerminatingBytes;
    APEHeader.nFinalFrameBlocks = nFinalFrameBlocks;
    APEHeader.nTotalFrames = nNumberOfFrames;

    // set the pointer and re-write the updated header and peak level
    nRetVal = pIO->Seek(0, FILE_BEGIN);
    if (pIO->Write(&APEHeader, APE_HEADER_BYTES, &BytesWritten) != 0) { return ERROR_IO_WRITE; }
    if (pIO->Write(&nPeakLevel, 4, &BytesWritten) != 0) { return ERROR_IO_WRITE; }
    if (pIO->Write(&m_nMaxFrames, 4, &BytesWritten) != 0) { return ERROR_IO_WRITE; }

    // write the updated seek table
    nRetVal = pIO->Seek(APEHeader.nHeaderBytes, FILE_CURRENT);

    if (pIO->Write(m_spSeekTable, m_nMaxFrames * 4, &BytesWritten) != 0) { return ERROR_IO_WRITE; }

    return 0;
}
