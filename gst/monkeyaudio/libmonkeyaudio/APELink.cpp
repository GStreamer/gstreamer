#include "All.h"
#include "APELink.h"
#include IO_HEADER_FILE

#define APE_LINK_HEADER             "[Monkey's Audio Image Link File]"
#define APE_LINK_IMAGE_FILE_TAG     "Image File="
#define APE_LINK_START_BLOCK_TAG    "Start Block="
#define APE_LINK_FINISH_BLOCK_TAG   "Finish Block="

CAPELink::CAPELink(const char * pFilename)
{
    // empty
    m_nStartBlock = 0;
    m_nFinishBlock = 0;
    m_cImageFile[0] = 0;

    // open the file
    IO_CLASS_NAME ioLinkFile;
    if (ioLinkFile.Open(pFilename) == ERROR_SUCCESS)
    {
        // create a buffer
        CSmartPtr<char> spBuffer(new char [1024], TRUE);

        // fill the buffer from the file and null terminate it
        unsigned int nBytesRead = 0;
        ioLinkFile.Read(spBuffer.GetPtr(), 1023, &nBytesRead);
        spBuffer[nBytesRead] = 0;

        // parse out the information
        char * pHeader = strstr(spBuffer.GetPtr(), APE_LINK_HEADER);
        char * pImageFile = strstr(spBuffer.GetPtr(), APE_LINK_IMAGE_FILE_TAG);
        char * pStartBlock = strstr(spBuffer.GetPtr(), APE_LINK_START_BLOCK_TAG);
        char * pFinishBlock = strstr(spBuffer.GetPtr(), APE_LINK_FINISH_BLOCK_TAG);

        if (pHeader && pImageFile && pStartBlock && pFinishBlock)
        {
            if ((_strnicmp(pHeader, APE_LINK_HEADER, strlen(APE_LINK_HEADER)) == 0) &&
                (_strnicmp(pImageFile, APE_LINK_IMAGE_FILE_TAG, strlen(APE_LINK_IMAGE_FILE_TAG)) == 0) &&
                (_strnicmp(pStartBlock, APE_LINK_START_BLOCK_TAG, strlen(APE_LINK_START_BLOCK_TAG)) == 0) &&
                (_strnicmp(pFinishBlock, APE_LINK_FINISH_BLOCK_TAG, strlen(APE_LINK_FINISH_BLOCK_TAG)) == 0))
            {
                // get the start and finish blocks
                m_nStartBlock = atoi(&pStartBlock[strlen(APE_LINK_START_BLOCK_TAG)]);
                m_nFinishBlock = atoi(&pFinishBlock[strlen(APE_LINK_FINISH_BLOCK_TAG)]);

                // get the path
                char cImageFile[MAX_PATH + 1]; int nIndex = 0;
                char * pImageCharacter = &pImageFile[strlen(APE_LINK_IMAGE_FILE_TAG)];
                while ((*pImageCharacter != 0) && (*pImageCharacter != '\r') && (*pImageCharacter != '\n'))
                    cImageFile[nIndex++] = *pImageCharacter++;
                cImageFile[nIndex] = 0;

                // process the path
                if (strrchr(cImageFile, '\\') == NULL)
                {
                    char cImagePath[MAX_PATH + 1];
                    strcpy(cImagePath, pFilename);
                    strcpy(strrchr(cImagePath, '\\') + 1, cImageFile);
                    strcpy(m_cImageFile, cImagePath);
                }
                else
                {
                    strcpy(m_cImageFile, cImageFile);
                }
            }
        }
    }
}

CAPELink::~CAPELink()
{
}
