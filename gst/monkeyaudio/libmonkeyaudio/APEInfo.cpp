/*****************************************************************************************
CAPEInfo:
    -a class to make working with APE files and getting information about them simple
*****************************************************************************************/
#include "All.h"
#include "APEInfo.h"
#include IO_HEADER_FILE
#include "APECompress.h"

/*****************************************************************************************
Construction
*****************************************************************************************/
CAPEInfo::CAPEInfo(int * pErrorCode, const char * pFilename, CAPETag * pTag)
{
    *pErrorCode = ERROR_SUCCESS;
    CloseFile();

    // open the file
    m_spIO.Assign(new IO_CLASS_NAME);

    if (m_spIO->Open(pFilename) != 0)
    {
        CloseFile();
        *pErrorCode = ERROR_INVALID_INPUT_FILE;
        return;
    }

    // get the file information
    if (GetFileInformation(TRUE) != 0)
    {
        CloseFile();
        *pErrorCode = ERROR_INVALID_INPUT_FILE;
        return;
    }

    // get the tag (do this second so that we don't do it on failure)
    if (pTag == NULL)
        m_spAPETag.Assign(new CAPETag(m_spIO, TRUE));
    else
        m_spAPETag.Assign(pTag);

}

CAPEInfo::CAPEInfo(int * pErrorCode, CIO *pIO, CAPETag * pTag)
{
    *pErrorCode = ERROR_SUCCESS;
    CloseFile();

    m_spIO.Assign(pIO, FALSE, FALSE);

    // get the file information
    if (GetFileInformation(TRUE) != 0)
    {
        CloseFile();
        *pErrorCode = ERROR_INVALID_INPUT_FILE;
        return;
    }

    // get the tag (do this second so that we don't do it on failure)
    if (pTag == NULL)
        m_spAPETag.Assign(new CAPETag(m_spIO, TRUE));
    else
        m_spAPETag.Assign(pTag);
}


/*****************************************************************************************
Destruction
*****************************************************************************************/
CAPEInfo::~CAPEInfo()
{
    CloseFile();
}

/*****************************************************************************************
Close the file
*****************************************************************************************/
int CAPEInfo::CloseFile()
{
    m_spIO.Delete();
    m_spWaveHeaderData.Delete();
    m_spSeekBitTable.Delete();
    m_spSeekByteTable.Delete();
    m_spAPETag.Delete();

    // re-initialize variables
    m_nSeekTableElements = 0;
    m_bHasFileInformationLoaded = FALSE;

    return 0;
}

/*****************************************************************************************
Get the file information about the file
*****************************************************************************************/
int CAPEInfo::GetFileInformation(BOOL bGetTagInformation)
{
    // quit if there is no simple file
    if (m_spIO == NULL) { return -1; }

    // quit if the file information has already been loaded
    if (m_bHasFileInformationLoaded) { return 0; }

    // variable declares
    unsigned int BytesRead = 0;

    // read the MAC header from the file
    int nOriginalFileLocation = m_spIO->GetPosition();

    // get to the beginning of the APE file (skip ID3v2, etc.)
    m_spIO->Seek(0, FILE_BEGIN);

    int nRetVal = SkipToAPEHeader();
    if (nRetVal != ERROR_SUCCESS) return nRetVal;

    APE_HEADER APEHeader;
    m_spIO->Read((unsigned char *) &APEHeader, APE_HEADER_BYTES, &BytesRead);

    // fail on 0 length APE files (catches non-finalized APE files)
    if (APEHeader.nTotalFrames == 0)
        return -1;

    int nPeakLevel = -1;
    if (APEHeader.nFormatFlags & MAC_FORMAT_FLAG_HAS_PEAK_LEVEL)
        m_spIO->Read((unsigned char *) &nPeakLevel, 4, &BytesRead);

    if (APEHeader.nFormatFlags & MAC_FORMAT_FLAG_HAS_SEEK_ELEMENTS)
        m_spIO->Read((unsigned char *) &m_nSeekTableElements, 4, &BytesRead);
    else
        m_nSeekTableElements = APEHeader.nTotalFrames;

    // fill the APE info structure
    m_APEFileInfo.nVersion              = int(APEHeader.nVersion);
    m_APEFileInfo.nCompressionLevel     = int(APEHeader.nCompressionLevel);
    m_APEFileInfo.nFormatFlags          = int(APEHeader.nFormatFlags);
    m_APEFileInfo.nTotalFrames          = int(APEHeader.nTotalFrames);
    m_APEFileInfo.nFinalFrameBlocks     = int(APEHeader.nFinalFrameBlocks);
    m_APEFileInfo.nBlocksPerFrame       = ((APEHeader.nVersion >= 3900) || ((APEHeader.nVersion >= 3800) && (APEHeader.nCompressionLevel == COMPRESSION_LEVEL_EXTRA_HIGH))) ? 0x12000 : 0x02400;
    if ((APEHeader.nVersion >= 3950)) m_APEFileInfo.nBlocksPerFrame = 0x48000;
    m_APEFileInfo.nChannels             = int(APEHeader.nChannels);
    m_APEFileInfo.nSampleRate           = int(APEHeader.nSampleRate);
    m_APEFileInfo.nBitsPerSample        = (m_APEFileInfo.nFormatFlags & MAC_FORMAT_FLAG_8_BIT) ? 8 : ((m_APEFileInfo.nFormatFlags & MAC_FORMAT_FLAG_24_BIT) ? 24 : 16);
    m_APEFileInfo.nBytesPerSample       = m_APEFileInfo.nBitsPerSample / 8;
    m_APEFileInfo.nBlockAlign           = m_APEFileInfo.nBytesPerSample * m_APEFileInfo.nChannels;
    m_APEFileInfo.nTotalBlocks          = (APEHeader.nTotalFrames == 0) ? 0 : ((APEHeader.nTotalFrames -  1) * m_APEFileInfo.nBlocksPerFrame) + APEHeader.nFinalFrameBlocks;
    m_APEFileInfo.nWAVHeaderBytes       = (APEHeader.nFormatFlags & MAC_FORMAT_FLAG_CREATE_WAV_HEADER) ? sizeof(WAVE_HEADER) : APEHeader.nHeaderBytes;
    m_APEFileInfo.nWAVTerminatingBytes  = int(APEHeader.nTerminatingBytes);
    m_APEFileInfo.nWAVDataBytes         = m_APEFileInfo.nTotalBlocks * m_APEFileInfo.nBlockAlign;
    m_APEFileInfo.nWAVTotalBytes        = m_APEFileInfo.nWAVDataBytes + m_APEFileInfo.nWAVHeaderBytes + m_APEFileInfo.nWAVTerminatingBytes;
    m_APEFileInfo.nAPETotalBytes        = m_spIO->GetSize();
    m_APEFileInfo.nLengthMS             = int((double(m_APEFileInfo.nTotalBlocks) * double(1000)) / double(m_APEFileInfo.nSampleRate));
    m_APEFileInfo.nAverageBitrate       = (m_APEFileInfo.nLengthMS <= 0) ? 0 : int((double(m_APEFileInfo.nAPETotalBytes) * double(8)) / double(m_APEFileInfo.nLengthMS));
    m_APEFileInfo.nDecompressedBitrate  = (m_APEFileInfo.nBlockAlign * m_APEFileInfo.nSampleRate * 8) / 1000;
    m_APEFileInfo.nPeakLevel            = nPeakLevel;

    // get the wave header
    if (!(APEHeader.nFormatFlags & MAC_FORMAT_FLAG_CREATE_WAV_HEADER))
    {
        m_spWaveHeaderData.Assign(new unsigned char [APEHeader.nHeaderBytes], TRUE);
        if (m_spWaveHeaderData == NULL) { return -1; }
        m_spIO->Read((unsigned char *) m_spWaveHeaderData, APEHeader.nHeaderBytes, &BytesRead);
    }

    // get the seek tables (really no reason to get the whole thing if there's extra)
    m_spSeekByteTable.Assign(new unsigned __int32 [m_nSeekTableElements], TRUE);
    if (m_spSeekByteTable == NULL) { return -1; }

    m_spIO->Read((unsigned char *) m_spSeekByteTable.GetPtr(), 4 * m_nSeekTableElements, &BytesRead);

    if (APEHeader.nVersion <= 3800)
    {
        m_spSeekBitTable.Assign(new unsigned char [m_nSeekTableElements], TRUE);
        if (m_spSeekBitTable == NULL) { return -1; }

        m_spIO->Read((unsigned char *) m_spSeekBitTable, m_nSeekTableElements, &BytesRead);
    }

    m_spIO->Seek(nOriginalFileLocation, FILE_BEGIN);

    m_bHasFileInformationLoaded = TRUE;

    return 0;
}

int CAPEInfo::SkipToAPEHeader()
{
    // reset
    m_nExtraHeaderBytes = 0;

    // figure the extra header bytes

    // skip an ID3v2 tag (which we really don't support anyway...)
    unsigned int nBytesRead = 0;
    unsigned char cID3v2Header[10];
    m_spIO->Read((unsigned char *) cID3v2Header, 10, &nBytesRead);
    if (cID3v2Header[0] == 'I' && cID3v2Header[1] == 'D' && cID3v2Header[2] == '3')
    {
        // why is it so hard to figure the lenght of an ID3v2 tag ?!?
        unsigned int nLength = *((unsigned int *) &cID3v2Header[6]);

        unsigned int nSyncSafeLength = 0;
        nSyncSafeLength = (cID3v2Header[6] & 127) << 21;
        nSyncSafeLength += (cID3v2Header[7] & 127) << 14;
        nSyncSafeLength += (cID3v2Header[8] & 127) << 7;
        nSyncSafeLength += (cID3v2Header[9] & 127);

        BOOL bHasTagFooter = FALSE;

        if (cID3v2Header[5] & 16)
        {
            bHasTagFooter = TRUE;
            m_nExtraHeaderBytes = nSyncSafeLength + 20;
        }
        else
        {
            m_nExtraHeaderBytes = nSyncSafeLength + 10;
        }

        // error check
        if (cID3v2Header[5] & 64)
        {
            // this ID3v2 length calculator algorithm can't cope with extended headers
            // we should be ok though, because the scan for the MAC header below should
            // really do the trick
        }

        m_spIO->Seek(m_nExtraHeaderBytes, FILE_BEGIN);

        // scan for padding (slow and stupid, but who cares here...)
        if (!bHasTagFooter)
        {
            char cTemp = 0;
            m_spIO->Read((unsigned char *) &cTemp, 1, &nBytesRead);
            while (cTemp == 0 && nBytesRead == 1)
            {
                m_nExtraHeaderBytes++;
                m_spIO->Read((unsigned char *) &cTemp, 1, &nBytesRead);
            }
        }
    }

    m_spIO->Seek(m_nExtraHeaderBytes, FILE_BEGIN);

    // scan until we hit the APE header, the end of the file, or 1 MB later
    unsigned int nGoalID = (' ' << 24) | ('C' << 16) | ('A' << 8) | ('M');
    unsigned int nReadID = 0;
    int nRetVal = m_spIO->Read(&nReadID, 4, &nBytesRead);
    if (nRetVal != 0 || nBytesRead != 4) return -1;

    nBytesRead = 1;
    int nScanBytes = 0;
    while (nGoalID != nReadID && nBytesRead == 1 && nScanBytes < (1024 * 1024))
    {
        unsigned char cTemp;
        m_spIO->Read(&cTemp, 1, &nBytesRead);
        nReadID = (((unsigned int) cTemp) << 24) | (nReadID >> 8);
        m_nExtraHeaderBytes++;
        nScanBytes++;
    }

    if (nGoalID != nReadID)
        return -1;

    // successfully found the start of the file (seek to it and return)
    m_spIO->Seek(m_nExtraHeaderBytes, FILE_BEGIN);
    return 0;
}

int CAPEInfo::GetInfo(APE_DECOMPRESS_FIELDS Field, int nParam1, int nParam2)
{
    int nRetVal = -1;

    switch (Field)
    {
    case APE_INFO_FILE_VERSION:
        nRetVal = m_APEFileInfo.nVersion;
        break;
    case APE_INFO_COMPRESSION_LEVEL:
        nRetVal = m_APEFileInfo.nCompressionLevel;
        break;
    case APE_INFO_FORMAT_FLAGS:
        nRetVal = m_APEFileInfo.nFormatFlags;
        break;
    case APE_INFO_SAMPLE_RATE:
        nRetVal = m_APEFileInfo.nSampleRate;
        break;
    case APE_INFO_BITS_PER_SAMPLE:
        nRetVal = m_APEFileInfo.nBitsPerSample;
        break;
    case APE_INFO_BYTES_PER_SAMPLE:
        nRetVal = m_APEFileInfo.nBytesPerSample;
        break;
    case APE_INFO_CHANNELS:
        nRetVal = m_APEFileInfo.nChannels;
        break;
    case APE_INFO_BLOCK_ALIGN:
        nRetVal = m_APEFileInfo.nBlockAlign;
        break;
    case APE_INFO_BLOCKS_PER_FRAME:
        nRetVal = m_APEFileInfo.nBlocksPerFrame;
        break;
    case APE_INFO_FINAL_FRAME_BLOCKS:
        nRetVal = m_APEFileInfo.nFinalFrameBlocks;
        break;
    case APE_INFO_TOTAL_FRAMES:
        nRetVal = m_APEFileInfo.nTotalFrames;
        break;
    case APE_INFO_WAV_HEADER_BYTES:
        nRetVal = m_APEFileInfo.nWAVHeaderBytes;
        break;
    case APE_INFO_WAV_TERMINATING_BYTES:
        nRetVal = m_APEFileInfo.nWAVTerminatingBytes;
        break;
    case APE_INFO_WAV_DATA_BYTES:
        nRetVal = m_APEFileInfo.nWAVDataBytes;
        break;
    case APE_INFO_WAV_TOTAL_BYTES:
        nRetVal = m_APEFileInfo.nWAVTotalBytes;
        break;
    case APE_INFO_APE_TOTAL_BYTES:
        nRetVal = m_APEFileInfo.nAPETotalBytes;
        break;
    case APE_INFO_TOTAL_BLOCKS:
        nRetVal = m_APEFileInfo.nTotalBlocks;
        break;
    case APE_INFO_LENGTH_MS:
        nRetVal = m_APEFileInfo.nLengthMS;
        break;
    case APE_INFO_AVERAGE_BITRATE:
        nRetVal = m_APEFileInfo.nAverageBitrate;
        break;
    case APE_INFO_FRAME_BITRATE:
    {
        int nFrame = nParam1;

        nRetVal = 0;

        int nFrameBytes = GetInfo(APE_INFO_FRAME_BYTES, nFrame);
        int nFrameBlocks = GetInfo(APE_INFO_FRAME_BLOCKS, nFrame);
        if ((nFrameBytes > 0) && (nFrameBlocks > 0) && m_APEFileInfo.nSampleRate > 0)
        {
            int nFrameMS = (nFrameBlocks * 1000) / m_APEFileInfo.nSampleRate;
            if (nFrameMS != 0)
            {
                nRetVal = (nFrameBytes * 8) / nFrameMS;
            }
        }
        break;
    }
    case APE_INFO_DECOMPRESSED_BITRATE:
        nRetVal = m_APEFileInfo.nDecompressedBitrate;
        break;
    case APE_INFO_PEAK_LEVEL:
        nRetVal = m_APEFileInfo.nPeakLevel;
        break;
    case APE_INFO_SEEK_BIT:
    {
        int nFrame = nParam1;
        if (GET_FRAMES_START_ON_BYTES_BOUNDARIES(this))
        {
            nRetVal = 0;
        }
        else
        {
            if (nFrame < 0 || nFrame >= m_APEFileInfo.nTotalFrames)
                nRetVal = 0;
            else
                nRetVal = m_spSeekBitTable[nFrame];
        }
        break;
    }
    case APE_INFO_SEEK_BYTE:
    {
        int nFrame = nParam1;
        if (nFrame < 0 || nFrame >= m_APEFileInfo.nTotalFrames)
            nRetVal = 0;
        else
            nRetVal = m_spSeekByteTable[nFrame] + m_nExtraHeaderBytes;
        break;
    }
    case APE_INFO_WAV_HEADER_DATA:
    {
        char * pBuffer = (char *) nParam1;
        int nMaxBytes = nParam2;

        if (m_APEFileInfo.nFormatFlags & MAC_FORMAT_FLAG_CREATE_WAV_HEADER)
        {
            if (sizeof(WAVE_HEADER) > nMaxBytes)
            {
                nRetVal = -1;
            }
            else
            {
                WAVEFORMATEX wfeFormat; GetInfo(APE_INFO_WAVEFORMATEX, (int) &wfeFormat, 0);
                WAVE_HEADER WAVHeader; FillWaveHeader(&WAVHeader, m_APEFileInfo.nWAVDataBytes, &wfeFormat,
                    m_APEFileInfo.nWAVTerminatingBytes);
                memcpy(pBuffer, &WAVHeader, sizeof(WAVE_HEADER));
                nRetVal = 0;
            }
        }
        else
        {
            if (m_APEFileInfo.nWAVHeaderBytes > nMaxBytes)
            {
                nRetVal = -1;
            }
            else
            {
                memcpy(pBuffer, m_spWaveHeaderData, m_APEFileInfo.nWAVHeaderBytes);
                nRetVal = 0;
            }
        }
        break;
    }
    case APE_INFO_WAV_TERMINATING_DATA:
    {
        char * pBuffer = (char *) nParam1;
        int nMaxBytes = nParam2;

        if (m_APEFileInfo.nWAVTerminatingBytes > nMaxBytes)
        {
            nRetVal = -1;
        }
        else
        {
            if (m_APEFileInfo.nWAVTerminatingBytes > 0)
            {
                // variables
                int nOriginalFileLocation = m_spIO->GetPosition();
                unsigned int nBytesRead = 0;

                // check for a tag
                m_spIO->Seek(-(m_spAPETag->GetTagBytes() + m_APEFileInfo.nWAVTerminatingBytes), FILE_END);
                m_spIO->Read(pBuffer, m_APEFileInfo.nWAVTerminatingBytes, &nBytesRead);

                // restore the file pointer
                m_spIO->Seek(nOriginalFileLocation, FILE_BEGIN);
            }
            nRetVal = 0;
        }
        break;
    }
    case APE_INFO_WAVEFORMATEX:
    {
        WAVEFORMATEX * pWaveFormatEx = (WAVEFORMATEX *) nParam1;
        FillWaveFormatEx(pWaveFormatEx, m_APEFileInfo.nSampleRate, m_APEFileInfo.nBitsPerSample, m_APEFileInfo.nChannels);
        nRetVal = 0;
        break;
    }
    case APE_INFO_IO_SOURCE:
        nRetVal = (int) m_spIO.GetPtr();
        break;
    case APE_INFO_FRAME_BYTES:
    {
        int nFrame = nParam1;

        // bound-check the frame index
        if ((nFrame < 0) || (nFrame >= m_APEFileInfo.nTotalFrames))
        {
            nRetVal = -1;
        }
        else
        {
            if (nFrame != (m_APEFileInfo.nTotalFrames - 1))
                nRetVal = GetInfo(APE_INFO_SEEK_BYTE, nFrame + 1) - GetInfo(APE_INFO_SEEK_BYTE, nFrame);
            else
                nRetVal = m_spIO->GetSize() - m_spAPETag->GetTagBytes() - m_APEFileInfo.nWAVTerminatingBytes - GetInfo(APE_INFO_SEEK_BYTE, nFrame);
        }
        break;
    }
    case APE_INFO_FRAME_BLOCKS:
    {
        int nFrame = nParam1;

        // bound-check the frame index
        if ((nFrame < 0) || (nFrame >= m_APEFileInfo.nTotalFrames))
        {
            nRetVal = -1;
        }
        else
        {
            if (nFrame != (m_APEFileInfo.nTotalFrames - 1))
                nRetVal = m_APEFileInfo.nBlocksPerFrame;
            else
                nRetVal = m_APEFileInfo.nFinalFrameBlocks;
        }
        break;
    }
    case APE_INFO_TAG:
        nRetVal = (int) m_spAPETag.GetPtr();
        break;
    }

    return nRetVal;
}
