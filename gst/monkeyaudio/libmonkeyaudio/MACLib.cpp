#include "All.h"
#include "MACLib.h"

#include "APECompress.h"
#include "APECompressCreate.h"
#include "APECompressCore.h"
#include "APECompress.h"
#include "APEDecompress.h"
#include "APEInfo.h"
#include "APELink.h"

#ifdef BACKWARDS_COMPATIBILITY
    #include "Old/APEDecompressOld.h"
#endif

IAPEDecompress * CreateIAPEDecompressCore(CAPEInfo * pAPEInfo, int nStartBlock, int nFinishBlock, int * pErrorCode)
{
    IAPEDecompress * pAPEDecompress = NULL;
    if (pAPEInfo != NULL && *pErrorCode == ERROR_SUCCESS)
    {
        try
        {
            if (pAPEInfo->GetInfo(APE_INFO_FILE_VERSION) >= 3930)
                pAPEDecompress = new CAPEDecompress(pErrorCode, pAPEInfo, nStartBlock, nFinishBlock);
#ifdef BACKWARDS_COMPATIBILITY
            else
                pAPEDecompress = new CAPEDecompressOld(pErrorCode, pAPEInfo, nStartBlock, nFinishBlock);
#endif

            if (pAPEDecompress == NULL || *pErrorCode != ERROR_SUCCESS)
            {
                SAFE_DELETE(pAPEDecompress)
            }
        }
        catch(...)
        {
            SAFE_DELETE(pAPEDecompress)
            *pErrorCode = ERROR_UNDEFINED;
        }
    }

    return pAPEDecompress;
}

IAPEDecompress * __stdcall CreateIAPEDecompress(const char * pFilename, int * pErrorCode)
{
    // error check the parameters
    if ((pFilename == NULL) || (strlen(pFilename) == 0))
    {
        if (pErrorCode) *pErrorCode = ERROR_BAD_PARAMETER;
        return NULL;
    }

    // variables
    int nErrorCode = ERROR_UNDEFINED;
    CAPEInfo * pAPEInfo = NULL;
    int nStartBlock = -1; int nFinishBlock = -1;

    // get the extension
    const char * pExtension = &pFilename[strlen(pFilename)];
    while ((pExtension > pFilename) && (*pExtension != '.'))
        pExtension--;

    // take the appropriate action (based on the extension)
    if (_stricmp(pExtension, ".apl") == 0)
    {
        // "link" file (.apl linked large APE file)
        CAPELink APELink(pFilename);
        CAPETag * pAPETag = new CAPETag(pFilename, TRUE);

        pAPEInfo = new CAPEInfo(&nErrorCode, APELink.m_cImageFile, pAPETag);
        nStartBlock = APELink.m_nStartBlock; nFinishBlock = APELink.m_nFinishBlock;
    }
    else
#if 0
    if ((_stricmp(pExtension, ".mac") == 0) || (_stricmp(pExtension, ".ape") == 0))
#endif
    {
        // plain .ape file
        pAPEInfo = new CAPEInfo(&nErrorCode, pFilename);
    }
#if 0
    else
    {
        if (pErrorCode) *pErrorCode = ERROR_INVALID_INPUT_FILE;
        return NULL;
    }
#endif

    // create and return
    IAPEDecompress * pAPEDecompress = CreateIAPEDecompressCore(pAPEInfo, nStartBlock, nFinishBlock, &nErrorCode);
    if (pErrorCode) *pErrorCode = nErrorCode;
    return pAPEDecompress;
}

IAPEDecompress * __stdcall CreateIAPEDecompressEx(CIO * pIO, int * pErrorCode)
{
    int nErrorCode = ERROR_UNDEFINED;
    CAPEInfo * pAPEInfo = new CAPEInfo(&nErrorCode, pIO);

    IAPEDecompress * pAPEDecompress = CreateIAPEDecompressCore(pAPEInfo, -1, -1, &nErrorCode);
    if (pErrorCode) *pErrorCode = nErrorCode;
    return pAPEDecompress;
}

IAPECompress * __stdcall CreateIAPECompress(int * pErrorCode)
{
    if (pErrorCode)
        *pErrorCode = ERROR_SUCCESS;

    return new CAPECompress();
}

int __stdcall FillWaveFormatEx(WAVEFORMATEX * pWaveFormatEx, int nSampleRate, int nBitsPerSample, int nChannels)
{
    pWaveFormatEx->cbSize = 0;
    pWaveFormatEx->nSamplesPerSec = nSampleRate;
    pWaveFormatEx->wBitsPerSample = nBitsPerSample;
    pWaveFormatEx->nChannels = nChannels;
    pWaveFormatEx->wFormatTag = 1;

    pWaveFormatEx->nBlockAlign = (pWaveFormatEx->wBitsPerSample / 8) * pWaveFormatEx->nChannels;
    pWaveFormatEx->nAvgBytesPerSec = pWaveFormatEx->nBlockAlign * pWaveFormatEx->nSamplesPerSec;

    return 0;
}

int __stdcall FillWaveHeader(WAVE_HEADER * pWAVHeader, int nAudioBytes, WAVEFORMATEX * pWaveFormatEx, int nTerminatingBytes)
{
    try
    {
        // RIFF header
        memcpy(pWAVHeader->cRIFFHeader, "RIFF", 4);
        pWAVHeader->nRIFFBytes = (nAudioBytes + 44) - 8 + nTerminatingBytes;

        // format header
        memcpy(pWAVHeader->cDataTypeID, "WAVE", 4);
        memcpy(pWAVHeader->cFormatHeader, "fmt ", 4);

        // the format chunk is the first 16 bytes of a waveformatex
        pWAVHeader->nFormatBytes = 16;
        memcpy(&pWAVHeader->nFormatTag, pWaveFormatEx, 16);

        // the data header
        memcpy(pWAVHeader->cDataHeader, "data", 4);
        pWAVHeader->nDataBytes = nAudioBytes;

        return 0;
    }
    catch(...) { return -1; }
}
