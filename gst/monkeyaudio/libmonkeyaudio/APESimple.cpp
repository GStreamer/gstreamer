#include "All.h"

#include "APEInfo.h"
#include "APECompress.h"
#include "APEDecompress.h"

#include "WAVInputSource.h"
#include IO_HEADER_FILE
#include "MACProgressHelper.h"
#include "GlobalFunctions.h"

#define UNMAC_DECODER_OUTPUT_NONE       0
#define UNMAC_DECODER_OUTPUT_WAV        1
#define UNMAC_DECODER_OUTPUT_APE        2

#define BLOCKS_PER_DECODE 9216

int DecompressCore(const char * pInputFilename, const char * pOutputFilename, int nOutputMode, int nCompressionLevel, int * pPercentageDone, APE_PROGRESS_CALLBACK ProgressCallback, int * pKillFlag);

/*****************************************************************************************
Compress file
*****************************************************************************************/
int __stdcall CompressFile(const char *pInputFile, const char *pOutputFile, int nCompressionLevel, int *pPercentageDone, APE_PROGRESS_CALLBACK ProgressCallback, int *pKillFlag)
{
    // declare the variables
    int nFunctionRetVal = ERROR_SUCCESS;
    WAVEFORMATEX WaveFormatEx;
    CSmartPtr<CMACProgressHelper> spMACProgressHelper;
    CSmartPtr<unsigned char> spBuffer;
    CSmartPtr<IAPECompress> spAPECompress;

    try
    {
        // create the input source
        int nRetVal = ERROR_UNDEFINED;
        int nAudioBlocks = 0; int nHeaderBytes = 0; int nTerminatingBytes = 0;
        CSmartPtr<CInputSource> spInputSource(CreateInputSource(pInputFile, &WaveFormatEx, &nAudioBlocks,
            &nHeaderBytes, &nTerminatingBytes, &nRetVal));

        if ((spInputSource == NULL) || (nRetVal != ERROR_SUCCESS))
            throw nRetVal;

        // create the compressor
        spAPECompress.Assign(CreateIAPECompress());
        if (spAPECompress == NULL) throw ERROR_UNDEFINED;

        // figure the audio bytes
        int nAudioBytes = nAudioBlocks * WaveFormatEx.nBlockAlign;

        // start the encoder
        if (nHeaderBytes > 0) spBuffer.Assign(new unsigned char [nHeaderBytes], TRUE);
        THROW_ON_ERROR(spInputSource->GetHeaderData(spBuffer.GetPtr()))
        THROW_ON_ERROR(spAPECompress->Start(pOutputFile, &WaveFormatEx, nAudioBytes,
            nCompressionLevel, spBuffer.GetPtr(), nHeaderBytes));

        spBuffer.Delete();

        // set-up the progress
        spMACProgressHelper.Assign(new CMACProgressHelper(nAudioBytes, pPercentageDone, ProgressCallback, pKillFlag));

        // master loop
        int nBytesLeft = nAudioBytes;

        while (nBytesLeft > 0)
        {
            int nBytesAdded = 0;
            THROW_ON_ERROR(spAPECompress->AddDataFromInputSource(spInputSource.GetPtr(), nBytesLeft, &nBytesAdded))

            nBytesLeft -= nBytesAdded;

            // update the progress
            spMACProgressHelper->UpdateProgress(nAudioBytes - nBytesLeft);

            // process the kill flag
            if (spMACProgressHelper->ProcessKillFlag(TRUE) != ERROR_SUCCESS)
                throw(ERROR_USER_STOPPED_PROCESSING);
        }

        // finalize the file
        if (nTerminatingBytes > 0) spBuffer.Assign(new unsigned char [nTerminatingBytes], TRUE);
        THROW_ON_ERROR(spInputSource->GetTerminatingData(spBuffer.GetPtr()));
        THROW_ON_ERROR(spAPECompress->Finish(spBuffer.GetPtr(), nTerminatingBytes, nTerminatingBytes))

        // update the progress to 100%
        spMACProgressHelper->UpdateProgressComplete();
    }
    catch(int nErrorCode)
    {
        nFunctionRetVal = (nErrorCode == 0) ? ERROR_UNDEFINED : nErrorCode;
    }
    catch(...)
    {
        nFunctionRetVal = ERROR_UNDEFINED;
    }

    // kill the compressor if we failed
    if ((nFunctionRetVal != 0) && (spAPECompress != NULL))
        spAPECompress->Kill();

    // return
    return nFunctionRetVal;
}


/*****************************************************************************************
Verify file
*****************************************************************************************/
int __stdcall VerifyFile(const char * pInputFilename, int * pPercentageDone, APE_PROGRESS_CALLBACK ProgressCallback, int * pKillFlag)
{
    return DecompressCore(pInputFilename, NULL, UNMAC_DECODER_OUTPUT_NONE, -1, pPercentageDone, ProgressCallback, pKillFlag);
}

/*****************************************************************************************
Decompress file
*****************************************************************************************/
int __stdcall DecompressFile(const char * pInputFilename, const char * pOutputFilename, int * pPercentageDone, APE_PROGRESS_CALLBACK ProgressCallback, int * pKillFlag)
{
    if (pOutputFilename == NULL)
        return VerifyFile(pInputFilename, pPercentageDone, ProgressCallback, pKillFlag);
    else
        return DecompressCore(pInputFilename, pOutputFilename, UNMAC_DECODER_OUTPUT_WAV, -1, pPercentageDone, ProgressCallback, pKillFlag);
}

/*****************************************************************************************
Convert file
*****************************************************************************************/
int __stdcall ConvertFile(const char * pInputFilename, const char * pOutputFilename, int nCompressionLevel, int * pPercentageDone, APE_PROGRESS_CALLBACK ProgressCallback, int * pKillFlag)
{
    return DecompressCore(pInputFilename, pOutputFilename, UNMAC_DECODER_OUTPUT_APE, nCompressionLevel, pPercentageDone, ProgressCallback, pKillFlag);
}

/*****************************************************************************************
Decompress a file using the specified output method
*****************************************************************************************/
int DecompressCore(const char * pInputFilename, const char * pOutputFilename, int nOutputMode, int nCompressionLevel, int * pPercentageDone, APE_PROGRESS_CALLBACK ProgressCallback, int * pKillFlag)
{
    // error check the function parameters
    if ((pInputFilename == NULL) || (pPercentageDone == NULL) || (pKillFlag == NULL))
    {
        return ERROR_INVALID_FUNCTION_PARAMETER;
    }

    // variable declares
    int nFunctionRetVal = ERROR_SUCCESS;
    CSmartPtr<IO_CLASS_NAME> spioOutput;
    CSmartPtr<IAPECompress> spAPECompress;
    CSmartPtr<IAPEDecompress> spAPEDecompress;
    CSmartPtr<unsigned char> spTempBuffer;
    CSmartPtr<CMACProgressHelper> spMACProgressHelper;
    WAVEFORMATEX wfeInput;

    try
    {
        // create the decoder
        spAPEDecompress.Assign(CreateIAPEDecompress(pInputFilename, &nFunctionRetVal));
        if (spAPEDecompress == NULL || nFunctionRetVal != ERROR_SUCCESS) throw(nFunctionRetVal);

        // get the input format
        THROW_ON_ERROR(spAPEDecompress->GetInfo(APE_INFO_WAVEFORMATEX, (int) &wfeInput))

        // allocate space for the header
        spTempBuffer.Assign(new unsigned char [spAPEDecompress->GetInfo(APE_INFO_WAV_HEADER_BYTES)], TRUE);
        if (spTempBuffer == NULL) throw(ERROR_INSUFFICIENT_MEMORY);

        // get the header
        THROW_ON_ERROR(spAPEDecompress->GetInfo(APE_INFO_WAV_HEADER_DATA, (int) spTempBuffer.GetPtr(), spAPEDecompress->GetInfo(APE_INFO_WAV_HEADER_BYTES)));

        // initialize the output
        if (nOutputMode == UNMAC_DECODER_OUTPUT_WAV)
        {
            // create the file
            spioOutput.Assign(new IO_CLASS_NAME); THROW_ON_ERROR(spioOutput->Create(pOutputFilename))

            // output the header
            THROW_ON_ERROR(WriteSafe(spioOutput, spTempBuffer, spAPEDecompress->GetInfo(APE_INFO_WAV_HEADER_BYTES)));
        }
        else if (nOutputMode == UNMAC_DECODER_OUTPUT_APE)
        {
            // quit if there is nothing to do
            if (spAPEDecompress->GetInfo(APE_INFO_FILE_VERSION) == MAC_VERSION_NUMBER && spAPEDecompress->GetInfo(APE_INFO_COMPRESSION_LEVEL) == nCompressionLevel)
                throw(ERROR_SKIPPED);

            // create and start the compressor
            spAPECompress.Assign(CreateIAPECompress());
            THROW_ON_ERROR(spAPECompress->Start(pOutputFilename, &wfeInput, spAPEDecompress->GetInfo(APE_DECOMPRESS_TOTAL_BLOCKS) * spAPEDecompress->GetInfo(APE_INFO_BLOCK_ALIGN),
                nCompressionLevel, spTempBuffer, spAPEDecompress->GetInfo(APE_INFO_WAV_HEADER_BYTES)))
        }

        // allocate space for decompression
        spTempBuffer.Assign(new unsigned char [spAPEDecompress->GetInfo(APE_INFO_BLOCK_ALIGN) * BLOCKS_PER_DECODE], TRUE);
        if (spTempBuffer == NULL) throw(ERROR_INSUFFICIENT_MEMORY);

        int nBlocksLeft = spAPEDecompress->GetInfo(APE_DECOMPRESS_TOTAL_BLOCKS);

        // create the progress helper
        spMACProgressHelper.Assign(new CMACProgressHelper(nBlocksLeft / BLOCKS_PER_DECODE, pPercentageDone, ProgressCallback, pKillFlag));

        // main decoding loop
        while (nBlocksLeft > 0)
        {
            // decode data
            int nBlocksDecoded = -1;
            int nRetVal = spAPEDecompress->GetData((char *) spTempBuffer.GetPtr(), BLOCKS_PER_DECODE, &nBlocksDecoded);
            if (nRetVal != ERROR_SUCCESS)
                throw(ERROR_INVALID_CHECKSUM);

            // handle the output
            if (nOutputMode == UNMAC_DECODER_OUTPUT_WAV)
            {
                unsigned int nBytesToWrite = (nBlocksDecoded * spAPEDecompress->GetInfo(APE_INFO_BLOCK_ALIGN));
                unsigned int nBytesWritten = 0;
                int nRetVal = spioOutput->Write(spTempBuffer, nBytesToWrite, &nBytesWritten);
                if ((nRetVal != 0) || (nBytesToWrite != nBytesWritten))
                    throw(ERROR_IO_WRITE);
            }
            else if (nOutputMode == UNMAC_DECODER_OUTPUT_APE)
            {
                THROW_ON_ERROR(spAPECompress->AddData(spTempBuffer, nBlocksDecoded * spAPEDecompress->GetInfo(APE_INFO_BLOCK_ALIGN)))
            }

            // update amount remaining
            nBlocksLeft -= nBlocksDecoded;

            // update progress and kill flag
            spMACProgressHelper->UpdateProgress();
            if (spMACProgressHelper->ProcessKillFlag(TRUE) != 0)
                throw(ERROR_USER_STOPPED_PROCESSING);
        }

        // terminate the output
        if (nOutputMode == UNMAC_DECODER_OUTPUT_WAV)
        {
            // write any terminating WAV data
            if (spAPEDecompress->GetInfo(APE_INFO_WAV_TERMINATING_BYTES) > 0)
            {
                spTempBuffer.Assign(new unsigned char[spAPEDecompress->GetInfo(APE_INFO_WAV_TERMINATING_BYTES)], TRUE);
                if (spTempBuffer == NULL) throw(ERROR_INSUFFICIENT_MEMORY);
                THROW_ON_ERROR(spAPEDecompress->GetInfo(APE_INFO_WAV_TERMINATING_DATA, (int) spTempBuffer.GetPtr(), spAPEDecompress->GetInfo(APE_INFO_WAV_TERMINATING_BYTES)))

                unsigned int nBytesToWrite = spAPEDecompress->GetInfo(APE_INFO_WAV_TERMINATING_BYTES);
                unsigned int nBytesWritten = 0;
                int nRetVal = spioOutput->Write(spTempBuffer, nBytesToWrite, &nBytesWritten);
                if ((nRetVal != 0) || (nBytesToWrite != nBytesWritten))
                    throw(ERROR_IO_WRITE);
            }
        }
        else if (nOutputMode == UNMAC_DECODER_OUTPUT_APE)
        {
            // write the WAV data and any tag
            int nTagBytes = GET_TAG(spAPEDecompress)->GetTagBytes();
            BOOL bHasTag = (nTagBytes > 0);
            int nTerminatingBytes = nTagBytes;
            nTerminatingBytes += spAPEDecompress->GetInfo(APE_INFO_WAV_TERMINATING_BYTES);

            if (nTerminatingBytes > 0)
            {
                spTempBuffer.Assign(new unsigned char[nTerminatingBytes], TRUE);
                if (spTempBuffer == NULL) throw(ERROR_INSUFFICIENT_MEMORY);

                THROW_ON_ERROR(spAPEDecompress->GetInfo(APE_INFO_WAV_TERMINATING_DATA, (int) spTempBuffer.GetPtr(), nTerminatingBytes))

                if (bHasTag)
                {
                    unsigned int nBytesRead = 0;
                    THROW_ON_ERROR(GET_IO(spAPEDecompress)->Seek(-(nTagBytes), FILE_END))
                    THROW_ON_ERROR(GET_IO(spAPEDecompress)->Read(&spTempBuffer[spAPEDecompress->GetInfo(APE_INFO_WAV_TERMINATING_BYTES)], nTagBytes, &nBytesRead))
                }

                THROW_ON_ERROR(spAPECompress->Finish(spTempBuffer, nTerminatingBytes, spAPEDecompress->GetInfo(APE_INFO_WAV_TERMINATING_BYTES)));
            }
            else
            {
                THROW_ON_ERROR(spAPECompress->Finish(NULL, 0, 0));
            }
        }

        // fire the "complete" progress notification
        spMACProgressHelper->UpdateProgressComplete();
    }
    catch(int nErrorCode)
    {
        nFunctionRetVal = (nErrorCode == 0) ? ERROR_UNDEFINED : nErrorCode;
    }
    catch(...)
    {
        nFunctionRetVal = ERROR_UNDEFINED;
    }

    // return
    return nFunctionRetVal;
}
