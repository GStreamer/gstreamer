#include "All.h"
#include "Prepare.h"

const unsigned __int32 CRC32_Table[256] = {0LU,1996959894LU,3993919788LU,2567524794LU,124634137LU,1886057615LU,3915621685LU,2657392035LU,249268274LU,2044508324LU,3772115230LU,2547177864LU,162941995LU,2125561021LU,3887607047LU,2428444049LU,498536548LU,1789927666LU,4089016648LU,2227061214LU,450548861LU,1843258603LU,4107580753LU,2211677639LU,325883990LU,1684777152LU,4251122042LU,2321926636LU,335633487LU,1661365465LU,4195302755LU,2366115317LU,997073096LU,1281953886LU,3579855332LU,2724688242LU,1006888145LU,1258607687LU,3524101629LU,2768942443LU,901097722LU,1119000684LU,3686517206LU,2898065728LU,853044451LU,1172266101LU,3705015759LU,2882616665LU,651767980LU,1373503546LU,3369554304LU,3218104598LU,565507253LU,1454621731LU,3485111705LU,3099436303LU,671266974LU,1594198024LU,3322730930LU,2970347812LU,795835527LU,1483230225LU,3244367275LU,3060149565LU,1994146192LU,31158534LU,2563907772LU,4023717930LU,1907459465LU,112637215LU,2680153253LU,3904427059LU,2013776290LU,251722036LU,2517215374LU,3775830040LU,2137656763LU,141376813LU,2439277719LU,3865271297LU,1802195444LU,476864866LU,2238001368LU,
    4066508878LU,1812370925LU,453092731LU,2181625025LU,4111451223LU,1706088902LU,314042704LU,2344532202LU,4240017532LU,1658658271LU,366619977LU,2362670323LU,4224994405LU,1303535960LU,984961486LU,2747007092LU,3569037538LU,1256170817LU,1037604311LU,2765210733LU,3554079995LU,1131014506LU,879679996LU,2909243462LU,3663771856LU,1141124467LU,855842277LU,2852801631LU,3708648649LU,1342533948LU,654459306LU,3188396048LU,3373015174LU,1466479909LU,544179635LU,3110523913LU,3462522015LU,1591671054LU,702138776LU,2966460450LU,3352799412LU,1504918807LU,783551873LU,3082640443LU,3233442989LU,3988292384LU,2596254646LU,62317068LU,1957810842LU,3939845945LU,2647816111LU,81470997LU,1943803523LU,3814918930LU,2489596804LU,225274430LU,2053790376LU,3826175755LU,2466906013LU,167816743LU,2097651377LU,4027552580LU,2265490386LU,503444072LU,1762050814LU,4150417245LU,2154129355LU,426522225LU,1852507879LU,4275313526LU,2312317920LU,282753626LU,1742555852LU,4189708143LU,2394877945LU,397917763LU,1622183637LU,3604390888LU,2714866558LU,953729732LU,1340076626LU,3518719985LU,2797360999LU,1068828381LU,1219638859LU,3624741850LU,
    2936675148LU,906185462LU,1090812512LU,3747672003LU,2825379669LU,829329135LU,1181335161LU,3412177804LU,3160834842LU,628085408LU,1382605366LU,3423369109LU,3138078467LU,570562233LU,1426400815LU,3317316542LU,2998733608LU,733239954LU,1555261956LU,3268935591LU,3050360625LU,752459403LU,1541320221LU,2607071920LU,3965973030LU,1969922972LU,40735498LU,2617837225LU,3943577151LU,1913087877LU,83908371LU,2512341634LU,3803740692LU,2075208622LU,213261112LU,2463272603LU,3855990285LU,2094854071LU,198958881LU,2262029012LU,4057260610LU,1759359992LU,534414190LU,2176718541LU,4139329115LU,1873836001LU,414664567LU,2282248934LU,4279200368LU,1711684554LU,285281116LU,2405801727LU,4167216745LU,1634467795LU,376229701LU,2685067896LU,3608007406LU,1308918612LU,956543938LU,2808555105LU,3495958263LU,1231636301LU,1047427035LU,2932959818LU,3654703836LU,1088359270LU,936918000LU,2847714899LU,3736837829LU,1202900863LU,817233897LU,3183342108LU,3401237130LU,1404277552LU,615818150LU,3134207493LU,3453421203LU,1423857449LU,601450431LU,3009837614LU,3294710456LU,1567103746LU,711928724LU,3020668471LU,3272380065LU,1510334235LU,755167117LU};

/*
if (nR > 32767) nR = 32767;
else if (nR < -32768) nR = -32768;
if (nL > 32767) nL = 32767;
else if (nL < -32768) nL = -32768;
*/

int CPrepare::Prepare(unsigned char * pRawData, int nBytes, const WAVEFORMATEX * pWaveFormatEx, int * pOutputX, int *pOutputY, unsigned int *pCRC, int *pSpecialCodes, int *pPeakLevel)
{
    // error check the parameters
    if (pRawData == NULL || pWaveFormatEx == NULL)
        return ERROR_BAD_PARAMETER;

    // initialize the pointers that got passed in
    *pCRC = 0xFFFFFFFF;
    *pSpecialCodes = 0;

    // variables
    unsigned __int32 CRC = 0xFFFFFFFF;
    const int nTotalBlocks = nBytes / pWaveFormatEx->nBlockAlign;
    int R,L;

    // the prepare code

    if (pWaveFormatEx->wBitsPerSample == 8)
    {
        if (pWaveFormatEx->nChannels == 2)
        {
            for (int nBlockIndex = 0; nBlockIndex < nTotalBlocks; nBlockIndex++)
            {
                R = (int) (*((unsigned char *) pRawData) - 128);
                L = (int) (*((unsigned char *) (pRawData + 1)) - 128);

                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                // check the peak
                if (labs(L) > *pPeakLevel)
                    *pPeakLevel = labs(L);
                if (labs(R) > *pPeakLevel)
                    *pPeakLevel = labs(R);

                // convert to x,y
                pOutputY[nBlockIndex] = L - R;
                pOutputX[nBlockIndex] = R + (pOutputY[nBlockIndex] / 2);
            }
        }
        else if (pWaveFormatEx->nChannels == 1)
        {
            for (int nBlockIndex = 0; nBlockIndex < nTotalBlocks; nBlockIndex++)
            {
                R = (int) (*((unsigned char *) pRawData) - 128);

                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                // check the peak
                if (labs(R) > *pPeakLevel)
                    *pPeakLevel = labs(R);

                // convert to x,y
                pOutputX[nBlockIndex] = R;
            }
        }
    }
    else if (pWaveFormatEx->wBitsPerSample == 24)
    {
        if (pWaveFormatEx->nChannels == 2)
        {
            for (int nBlockIndex = 0; nBlockIndex < nTotalBlocks; nBlockIndex++)
            {
                unsigned __int32 nTemp = 0;

                nTemp |= (*pRawData << 0);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                nTemp |= (*pRawData << 8);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                nTemp |= (*pRawData << 16);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                if (nTemp & 0x800000)
                    R = (int) (nTemp & 0x7fffff) - 0x800000;
                else
                    R = (int) (nTemp & 0x7fffff);

                nTemp = 0;

                nTemp |= (*pRawData << 0);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                nTemp |= (*pRawData << 8);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                nTemp |= (*pRawData << 16);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                if (nTemp & 0x800000)
                    L = (int) (nTemp & 0x7fffff) - 0x800000;
                else
                    L = (int) (nTemp & 0x7fffff);

                // check the peak
                if (labs(L) > *pPeakLevel)
                    *pPeakLevel = labs(L);
                if (labs(R) > *pPeakLevel)
                    *pPeakLevel = labs(R);

                // convert to x,y
                pOutputY[nBlockIndex] = L - R;
                pOutputX[nBlockIndex] = R + (pOutputY[nBlockIndex] / 2);

            }
        }
        else if (pWaveFormatEx->nChannels == 1)
        {
            for (int nBlockIndex = 0; nBlockIndex < nTotalBlocks; nBlockIndex++)
            {
                unsigned __int32 nTemp = 0;

                nTemp |= (*pRawData << 0);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                nTemp |= (*pRawData << 8);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                nTemp |= (*pRawData << 16);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                if (nTemp & 0x800000)
                    R = (int) (nTemp & 0x7fffff) - 0x800000;
                else
                    R = (int) (nTemp & 0x7fffff);

                // check the peak
                if (labs(R) > *pPeakLevel)
                    *pPeakLevel = labs(R);

                // convert to x,y
                pOutputX[nBlockIndex] = R;
            }
        }
    }
    else
    {
        if (pWaveFormatEx->nChannels == 2)
        {
            int LPeak = 0;
            int RPeak = 0;
            int nBlockIndex = 0;
            for (nBlockIndex = 0; nBlockIndex < nTotalBlocks; nBlockIndex++)
            {

                R = (int) *((__int16 *) pRawData);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                L = (int) *((__int16 *) pRawData);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                // check the peak
                if (labs(L) > LPeak)
                    LPeak = labs(L);
                if (labs(R) > RPeak)
                    RPeak = labs(R);

                // convert to x,y
                pOutputY[nBlockIndex] = L - R;
                pOutputX[nBlockIndex] = R + (pOutputY[nBlockIndex] / 2);
            }

            if (LPeak == 0) { *pSpecialCodes |= SPECIAL_FRAME_LEFT_SILENCE; }
            if (RPeak == 0) { *pSpecialCodes |= SPECIAL_FRAME_RIGHT_SILENCE; }
            if (max(LPeak, RPeak) > *pPeakLevel)
            {
                *pPeakLevel = max(LPeak, RPeak);
            }

            // check for pseudo-stereo files
            nBlockIndex = 0;
            while (pOutputY[nBlockIndex++] == 0)
            {
                if (nBlockIndex == (nBytes / 4))
                {
                    *pSpecialCodes |= SPECIAL_FRAME_PSEUDO_STEREO;
                    break;
                }
            }

        }
        else if (pWaveFormatEx->nChannels == 1)
        {
            int nPeak = 0;
            for (int nBlockIndex = 0; nBlockIndex < nTotalBlocks; nBlockIndex++)
            {
                R = (int) *((__int16 *) pRawData);

                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *pRawData++];

                // check the peak
                if (labs(R) > nPeak)
                    nPeak = labs(R);

                //convert to x,y
                pOutputX[nBlockIndex] = R;
            }

            if (nPeak > *pPeakLevel)
                *pPeakLevel = nPeak;
            if (nPeak == 0) { *pSpecialCodes |= SPECIAL_FRAME_MONO_SILENCE; }
        }
    }

    CRC = CRC ^ 0xFFFFFFFF;

    // add the special code
    CRC >>= 1;

    if (*pSpecialCodes != 0)
    {
        CRC |= (1 << 31);
    }

    *pCRC = CRC;

    return 0;
}

void CPrepare::Unprepare(int X, int Y, const WAVEFORMATEX * pWaveFormatEx, unsigned char * pOutput, unsigned int * pCRC)
{
    #define CALCULATE_CRC_BYTE  *pCRC = (*pCRC >> 8) ^ CRC32_Table[(*pCRC & 0xFF) ^ *pOutput++];
    // decompress and convert from (x,y) -> (l,r)
    // sort of long and ugly.... sorry

    if (pWaveFormatEx->nChannels == 2)
    {
        if (pWaveFormatEx->wBitsPerSample == 16)
        {
            // get the right and left values
            int nR = X - (Y / 2);
            int nL = nR + Y;

            // error check (for overflows)
            if ((nR < -32768) || (nR > 32767) || (nL < -32768) || (nL > 32767))
            {
                throw(-1);
            }

            *(__int16 *) pOutput = (__int16) nR;
            CALCULATE_CRC_BYTE
            CALCULATE_CRC_BYTE

            *(__int16 *) pOutput = (__int16) nL;
            CALCULATE_CRC_BYTE
            CALCULATE_CRC_BYTE
        }
        else if (pWaveFormatEx->wBitsPerSample == 8)
        {
            unsigned char R = (X - (Y / 2) + 128);
            *pOutput = R;
            CALCULATE_CRC_BYTE
            *pOutput = (unsigned char) (R + Y);
            CALCULATE_CRC_BYTE
        }
        else if (pWaveFormatEx->wBitsPerSample == 24)
        {
            __int32 RV, LV;

            RV = X - (Y / 2);
            LV = RV + Y;

            unsigned __int32 nTemp = 0;
            if (RV < 0)
                nTemp = ((unsigned __int32) (RV + 0x800000)) | 0x800000;
            else
                nTemp = (unsigned __int32) RV;

            *pOutput = (unsigned char) ((nTemp >> 0) & 0xFF);
            CALCULATE_CRC_BYTE
            *pOutput = (unsigned char) ((nTemp >> 8) & 0xFF);
            CALCULATE_CRC_BYTE
            *pOutput = (unsigned char) ((nTemp >> 16) & 0xFF);
            CALCULATE_CRC_BYTE

            nTemp = 0;
            if (LV < 0)
                nTemp = ((unsigned __int32) (LV + 0x800000)) | 0x800000;
            else
                nTemp = (unsigned __int32) LV;

            *pOutput = (unsigned char) ((nTemp >> 0) & 0xFF);
            CALCULATE_CRC_BYTE

            *pOutput = (unsigned char) ((nTemp >> 8) & 0xFF);
            CALCULATE_CRC_BYTE

            *pOutput = (unsigned char) ((nTemp >> 16) & 0xFF);
            CALCULATE_CRC_BYTE
        }
    }
    else if (pWaveFormatEx->nChannels == 1)
    {
        if (pWaveFormatEx->wBitsPerSample == 16)
        {
            __int16 R = X;

            *(__int16 *) pOutput = (__int16) R;
            CALCULATE_CRC_BYTE
            CALCULATE_CRC_BYTE
        }
        else if (pWaveFormatEx->wBitsPerSample == 8)
        {
            unsigned char R = X + 128;
            *pOutput = R;
            CALCULATE_CRC_BYTE
        }
        else if (pWaveFormatEx->wBitsPerSample == 24)
        {
            __int32 RV = X;

            unsigned __int32 nTemp = 0;
            if (RV < 0)
                nTemp = ((unsigned __int32) (RV + 0x800000)) | 0x800000;
            else
                nTemp = (unsigned __int32) RV;

            *pOutput = (unsigned char) ((nTemp >> 0) & 0xFF);
            CALCULATE_CRC_BYTE
            *pOutput = (unsigned char) ((nTemp >> 8) & 0xFF);
            CALCULATE_CRC_BYTE
            *pOutput = (unsigned char) ((nTemp >> 16) & 0xFF);
            CALCULATE_CRC_BYTE
        }
    }
}

#ifdef BACKWARDS_COMPATIBILITY

int CPrepare::UnprepareOld(int *pInputX, int *pInputY, int nBlocks, const WAVEFORMATEX *pWaveFormatEx, unsigned char *pRawData, unsigned int *pCRC, int *pSpecialCodes, int nFileVersion)
{
    //the CRC that will be figured during decompression
    unsigned __int32 CRC = 0xffffffff;

    //decompress and convert from (x,y) -> (l,r)
    //sort of int and ugly.... sorry
    if (pWaveFormatEx->nChannels == 2)
    {
        //convert the x,y data to raw data
        if (pWaveFormatEx->wBitsPerSample == 16)
        {
            __int16 R;
            unsigned char *Buffer = &pRawData[0];
            int *pX = pInputX;
            int *pY = pInputY;

            for (; pX < &pInputX[nBlocks]; pX++, pY++)
            {
                R = *pX - (*pY / 2);

                *(__int16 *) Buffer = (__int16) R;
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];

                *(__int16 *) Buffer = (__int16) R + *pY;
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];
            }
        }
        else if (pWaveFormatEx->wBitsPerSample == 8)
        {
            unsigned char *R = (unsigned char *) &pRawData[0];
            unsigned char *L = (unsigned char *) &pRawData[1];

            if (nFileVersion > 3830)
            {
                for (int SampleIndex = 0; SampleIndex < nBlocks; SampleIndex++, L+=2, R+=2)
                {
                    *R = (unsigned char) (pInputX[SampleIndex] - (pInputY[SampleIndex] / 2) + 128);
                    CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *R];
                    *L = (unsigned char) (*R + pInputY[SampleIndex]);
                    CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *L];
                }
            }
            else
            {
                for (int SampleIndex = 0; SampleIndex < nBlocks; SampleIndex++, L+=2, R+=2)
                {
                    *R = (unsigned char) (pInputX[SampleIndex] - (pInputY[SampleIndex] / 2));
                    CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *R];
                    *L = (unsigned char) (*R + pInputY[SampleIndex]);
                    CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *L];

                }
            }
        }
        else if (pWaveFormatEx->wBitsPerSample == 24)
        {
            unsigned char *Buffer = (unsigned char *) &pRawData[0];
            __int32 RV, LV;

            for (int SampleIndex = 0; SampleIndex < nBlocks; SampleIndex++)
            {
                RV = pInputX[SampleIndex] - (pInputY[SampleIndex] / 2);
                LV = RV + pInputY[SampleIndex];

                unsigned __int32 nTemp = 0;
                if (RV < 0)
                    nTemp = ((unsigned __int32) (RV + 0x800000)) | 0x800000;
                else
                    nTemp = (unsigned __int32) RV;

                *Buffer = (unsigned char) ((nTemp >> 0) & 0xFF);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];

                *Buffer = (unsigned char) ((nTemp >> 8) & 0xFF);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];

                *Buffer = (unsigned char) ((nTemp >> 16) & 0xFF);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];

                nTemp = 0;
                if (LV < 0)
                    nTemp = ((unsigned __int32) (LV + 0x800000)) | 0x800000;
                else
                    nTemp = (unsigned __int32) LV;

                *Buffer = (unsigned char) ((nTemp >> 0) & 0xFF);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];

                *Buffer = (unsigned char) ((nTemp >> 8) & 0xFF);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];

                *Buffer = (unsigned char) ((nTemp >> 16) & 0xFF);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];
            }
        }
    }
    else if (pWaveFormatEx->nChannels == 1)
    {
        //convert to raw data
        if (pWaveFormatEx->wBitsPerSample == 8)
        {
            unsigned char *R = (unsigned char *) &pRawData[0];

            if (nFileVersion > 3830)
            {
                for (int SampleIndex = 0; SampleIndex < nBlocks; SampleIndex++, R++)
                {
                    *R = pInputX[SampleIndex] + 128;
                    CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *R];
                }
            }
            else
            {
                for (int SampleIndex = 0; SampleIndex < nBlocks; SampleIndex++, R++)
                {
                    *R = (unsigned char) (pInputX[SampleIndex]);
                    CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *R];
                }
            }

        }
        else if (pWaveFormatEx->wBitsPerSample == 24)
        {

            unsigned char *Buffer = (unsigned char *) &pRawData[0];
            __int32 RV;
            for (int SampleIndex = 0; SampleIndex<nBlocks; SampleIndex++)
            {
                RV = pInputX[SampleIndex];

                unsigned __int32 nTemp = 0;
                if (RV < 0)
                    nTemp = ((unsigned __int32) (RV + 0x800000)) | 0x800000;
                else
                    nTemp = (unsigned __int32) RV;

                *Buffer = (unsigned char) ((nTemp >> 0) & 0xFF);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];

                *Buffer = (unsigned char) ((nTemp >> 8) & 0xFF);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];

                *Buffer = (unsigned char) ((nTemp >> 16) & 0xFF);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];
            }
        }
        else
        {
            unsigned char *Buffer = &pRawData[0];

            for (int SampleIndex = 0; SampleIndex < nBlocks; SampleIndex++)
            {
                *(__int16 *) Buffer = (__int16) (pInputX[SampleIndex]);
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];
                CRC = (CRC >> 8) ^ CRC32_Table[(CRC & 0xFF) ^ *Buffer++];
            }
        }
    }

    CRC = CRC ^ 0xffffffff;

    *pCRC = CRC;

    return 0;
}

#endif // BACKWARDS_COMPATIBILITY
