#include "All.h"
#include "ID3Genres.h"
#include "APETag.h"
#include IO_HEADER_FILE

CAPETagField::CAPETagField(const char * pFieldName, const char * pFieldValue, int nFieldBytes, int nFlags)
{
    m_nFieldNameBytes = strlen(pFieldName) + 1;
    m_spFieldName.Assign(new char [m_nFieldNameBytes], TRUE);
    strcpy(m_spFieldName, pFieldName);

    if (nFieldBytes == -1)
    {
        m_nFieldValueBytes = strlen(pFieldValue) + 1;
        m_spFieldValue.Assign(new char [m_nFieldValueBytes], TRUE);

        strcpy(m_spFieldValue, pFieldValue);
    }
    else
    {
        m_nFieldValueBytes = nFieldBytes;
        m_spFieldValue.Assign(new char [m_nFieldValueBytes], TRUE);

        memcpy(m_spFieldValue, pFieldValue, nFieldBytes);
    }

    m_nFieldFlags = nFlags;
}

CAPETagField::~CAPETagField()
{
}

int CAPETagField::GetFieldSize()
{
    return (strlen(m_spFieldName) + 1) + m_nFieldValueBytes + 4 + 4;
}

const char * CAPETagField::GetFieldName()
{
    return m_spFieldName;
}

const char * CAPETagField::GetFieldValue()
{
    return m_spFieldValue;
}

int CAPETagField::GetFieldValueSize()
{
    return m_nFieldValueBytes;
}

int CAPETagField::GetFieldFlags()
{
    return m_nFieldFlags;
}

int CAPETagField::SaveField(char * pBuffer)
{
    *((int *) pBuffer) = m_nFieldValueBytes;
    pBuffer += 4;
    *((int *) pBuffer) = m_nFieldFlags;
    pBuffer += 4;
    strcpy(pBuffer, m_spFieldName);
    pBuffer += strlen(m_spFieldName) + 1;
    memcpy(pBuffer, m_spFieldValue, m_nFieldValueBytes);

    return GetFieldSize();
}

CAPETag::CAPETag(const char * pFilename, BOOL bAnalyze)
{
    m_spIO.Assign(new IO_CLASS_NAME);
    m_spIO->Open(pFilename);

    m_bAnalyzed = FALSE;
    m_nFields = 0;
    m_nTagBytes = 0;
    m_nRetrieveFieldIndex = 0;

    if (bAnalyze)
    {
        Analyze();
    }
}

CAPETag::CAPETag(CIO * pIO, BOOL bAnalyze)
{
    m_spIO.Assign(pIO, FALSE, FALSE); // we don't own the IO source
    m_bAnalyzed = FALSE;
    m_nFields = 0;
    m_nTagBytes = 0;
    m_nRetrieveFieldIndex = 0;

    if (bAnalyze)
    {
        Analyze();
    }
}

CAPETag::~CAPETag()
{
    ClearFields();
}

int CAPETag::GetTagBytes()
{
    if (m_bAnalyzed == FALSE) { Analyze(); }

    return m_nTagBytes;
}

BOOL CAPETag::GetNextTagField(BOOL bFirst, CAPETagField ** ppAPETagField)
{
    if (bFirst)
        m_nRetrieveFieldIndex = 0;

    if (m_nRetrieveFieldIndex >= m_nFields)
    {
        return FALSE;
    }
    else
    {
        *ppAPETagField = m_aryFields[m_nRetrieveFieldIndex];
        m_nRetrieveFieldIndex++;
        return TRUE;
    }
}

int CAPETag::Save(BOOL bUseOldID3)
{
    if (Remove(FALSE) != 0)
        return -1;

    if (m_nFields == 0) { return 0; }

    int nRetVal = -1;

    if (bUseOldID3 == FALSE)
    {
        int z; // loop variable
        int nTotalTagBytes = sizeof(APE_TAG_FOOTER);
        for (z = 0; z < m_nFields; z++)
            nTotalTagBytes += m_aryFields[z]->GetFieldSize();

        CSmartPtr<char> spRawTag(new char [nTotalTagBytes], TRUE);

        int nLocation = 0;
        for (z = 0; z < m_nFields; z++)
            nLocation += m_aryFields[z]->SaveField(&spRawTag[nLocation]);

        APE_TAG_FOOTER APETagFooter;
        strncpy(APETagFooter.cID, "APETAGEX", 8);
        ZeroMemory(APETagFooter.cReserved, 8);
        APETagFooter.nFields = m_nFields;
        APETagFooter.nFlags = 0;
        APETagFooter.nSize = nTotalTagBytes;
        APETagFooter.nVersion = CURRENT_APE_TAG_VERSION;

        memcpy(&spRawTag[nLocation], &APETagFooter, sizeof(APE_TAG_FOOTER));

        nRetVal = WriteBufferToEndOfIO(spRawTag, nTotalTagBytes);
    }
    else
    {
        // build the ID3 tag
        ID3_TAG ID3Tag;
        CreateID3Tag(&ID3Tag);
        nRetVal = WriteBufferToEndOfIO(&ID3Tag, sizeof(ID3_TAG));
    }

    return nRetVal;
}

int CAPETag::WriteBufferToEndOfIO(void * pBuffer, int nBytes)
{
    int nOriginalPosition = m_spIO->GetPosition();

    unsigned int nBytesWritten = 0;
    m_spIO->Seek(0, FILE_END);

    int nRetVal = m_spIO->Write(pBuffer, nBytes, &nBytesWritten);

    m_spIO->Seek(nOriginalPosition, FILE_BEGIN);

    return nRetVal;
}

int CAPETag::Analyze()
{
    // clean-up
    ID3_TAG ID3Tag;
    ClearFields();
    m_nTagBytes = 0;

    m_bAnalyzed = TRUE;

    // store the original location
    int nOriginalPosition = m_spIO->GetPosition();

    // check for a tag
    unsigned int nBytesRead;
    int nRetVal;
    m_bHasID3Tag = FALSE;
    m_bHasAPETag = FALSE;
    m_spIO->Seek(-ID3_TAG_BYTES, FILE_END);
    nRetVal = m_spIO->Read((unsigned char *) &ID3Tag, sizeof(ID3_TAG), &nBytesRead);

    if ((nBytesRead == sizeof(ID3_TAG)) && (nRetVal == 0))
    {
        if (ID3Tag.TagHeader[0] == 'T' && ID3Tag.TagHeader[1] == 'A' && ID3Tag.TagHeader[2] == 'G')
        {
            m_bHasID3Tag = TRUE;
            m_nTagBytes += ID3_TAG_BYTES;
        }
    }

    // set the fields
    if (m_bHasID3Tag)
    {
        SetFieldID3String(APE_TAG_FIELD_ARTIST, ID3Tag.Artist, 30);
        SetFieldID3String(APE_TAG_FIELD_ALBUM, ID3Tag.Album, 30);
        SetFieldID3String(APE_TAG_FIELD_TITLE, ID3Tag.Title, 30);
        SetFieldID3String(APE_TAG_FIELD_COMMENT, ID3Tag.Comment, 28);
        SetFieldID3String(APE_TAG_FIELD_YEAR, ID3Tag.Year, 4);

        char cTemp[16]; sprintf(cTemp, "%d", ID3Tag.Track);
        SetField(APE_TAG_FIELD_TRACK, cTemp);

        if ((ID3Tag.Genre == GENRE_UNDEFINED) || (ID3Tag.Genre >= GENRE_COUNT))
            SetField(APE_TAG_FIELD_GENRE, APE_TAG_GENRE_UNDEFINED);
        else
            SetField(APE_TAG_FIELD_GENRE, (char *) g_ID3Genre[ID3Tag.Genre]);
    }

    // try loading the APE tag
    if (m_bHasID3Tag == FALSE)
    {
        APE_TAG_FOOTER APETagFooter;
        m_spIO->Seek(-int(sizeof(APE_TAG_FOOTER)), FILE_END);
        nRetVal = m_spIO->Read((unsigned char *) &APETagFooter, sizeof(APE_TAG_FOOTER), &nBytesRead);
        if ((nBytesRead == sizeof(APE_TAG_FOOTER)) && (nRetVal == 0))
        {
            if ((strncmp(APETagFooter.cID, "APETAGEX", 8) == 0) &&
                (APETagFooter.nVersion <= CURRENT_APE_TAG_VERSION) &&
                (APETagFooter.nFields <= 65536) &&
                (APETagFooter.nSize <= (1024 * 1024 * 16)))
            {
                m_bHasAPETag = TRUE;

                int nRawFieldBytes = APETagFooter.nSize - sizeof(APE_TAG_FOOTER);
                m_nTagBytes += APETagFooter.nSize;

                char * pRawTag = new char [nRawFieldBytes];

                m_spIO->Seek(-APETagFooter.nSize, FILE_END);
                nRetVal = m_spIO->Read((unsigned char *) pRawTag, nRawFieldBytes, &nBytesRead);
                if ((nRetVal == 0) && (nRawFieldBytes == int(nBytesRead)))
                {
                    // parse out the raw fields
                    int nLocation = 0;

                    for (int z = 0; z < APETagFooter.nFields; z++)
                    {
                        int nFieldValueSize = *((int *) &pRawTag[nLocation]);
                        nLocation += 4;
                        int nFieldFlags = *((int *) &pRawTag[nLocation]);
                        nLocation += 4;

                        char cNameBuffer[256];
                        strcpy(cNameBuffer, &pRawTag[nLocation]);
                        nLocation += strlen(cNameBuffer) + 1;

                        char * pFieldBuffer = new char [nFieldValueSize];
                        memcpy(pFieldBuffer, &pRawTag[nLocation], nFieldValueSize);
                        nLocation += nFieldValueSize;

                        SetField(cNameBuffer, pFieldBuffer, nFieldValueSize, nFieldFlags);

                        delete [] pFieldBuffer;
                    }
                }

                delete [] pRawTag;
            }
        }
    }

    // restore the file pointer
    m_spIO->Seek(nOriginalPosition, FILE_BEGIN);

    return 0;
}

int CAPETag::ClearFields()
{
    for (int z = 0; z < m_nFields; z++)
    {
        SAFE_DELETE(m_aryFields[z])
    }

    m_nFields = 0;

    return 0;
}

CAPETagField * CAPETag::GetTagField(const char * pFieldName)
{
    if (m_bAnalyzed == FALSE) { Analyze(); }

    for (int z = 0; z < m_nFields; z++)
    {
        if (strcmp(m_aryFields[z]->GetFieldName(), pFieldName) == 0)
            return m_aryFields[z];
    }

    return NULL;
}

int CAPETag::GetField(const char * pFieldName, char * pBuffer, int * pBufferBytes)
{
    if (m_bAnalyzed == FALSE) { Analyze(); }

    CAPETagField * pAPETagField = GetTagField(pFieldName);
    if (pAPETagField == NULL)
    {
        strcpy(pBuffer, "");
        *pBufferBytes = 0;
        return -1;
    }
    else
    {
        int nFieldValueLength = strlen(pAPETagField->GetFieldValue());
        if (nFieldValueLength > *pBufferBytes)
        {
            memcpy(pBuffer, pAPETagField->GetFieldValue(), *pBufferBytes);
        }
        else
        {
            *pBufferBytes = nFieldValueLength;
            strcpy(pBuffer, pAPETagField->GetFieldValue());
        }

        return 0;
    }
}

int CAPETag::CreateID3Tag(ID3_TAG * pID3Tag)
{
    if (pID3Tag == NULL) { return -1; }
    if (m_bAnalyzed == FALSE) { Analyze(); }

    if (m_nFields == 0) { return -1; }

    ZeroMemory(pID3Tag, ID3_TAG_BYTES);

    pID3Tag->TagHeader[0] = 'T';
    pID3Tag->TagHeader[1] = 'A';
    pID3Tag->TagHeader[2] = 'G';

    GetFieldID3String(APE_TAG_FIELD_ARTIST, pID3Tag->Artist, 30);
    GetFieldID3String(APE_TAG_FIELD_ALBUM, pID3Tag->Album, 30);
    GetFieldID3String(APE_TAG_FIELD_TITLE, pID3Tag->Title, 30);
    GetFieldID3String(APE_TAG_FIELD_COMMENT, pID3Tag->Comment, 28);
    GetFieldID3String(APE_TAG_FIELD_YEAR, pID3Tag->Year, 4);

    char cBuffer[256];
    int nBufferBytes;

    nBufferBytes = 256;
    GetField("Track", cBuffer, &nBufferBytes);
    pID3Tag->Track = (unsigned char) atoi(cBuffer);

    nBufferBytes = 256;
    GetField("Genre", cBuffer, &nBufferBytes);

    pID3Tag->Genre = 255;

    int nGenreIndex = 0;
    BOOL bFound = FALSE;
    while ((nGenreIndex < GENRE_COUNT) && (bFound == FALSE))
    {
        if (_stricmp(cBuffer, g_ID3Genre[nGenreIndex]) == 0)
        {
            pID3Tag->Genre = nGenreIndex;
            bFound = TRUE;
        }

        nGenreIndex++;
    }

    return 0;
}

int CAPETag::SetField(const char * pFieldName, const char * pFieldValue, int nFieldBytes, int nFlags)
{
    if (m_bAnalyzed == FALSE) { Analyze(); }

    if (pFieldName == NULL)
        return -1;

    // get the right index
    int z = 0;
    while (z < m_nFields)
    {
        if (strcmp(m_aryFields[z]->GetFieldName(), pFieldName) == 0)
            break;

        z++;
    }

    BOOL bNewField = TRUE;
    if (z != m_nFields)
    {
        SAFE_DELETE(m_aryFields[z])
        bNewField = FALSE;
    }

    if (nFieldBytes == -1)
    {
        if (pFieldValue == NULL || strlen(pFieldValue) == 0)
        {
            // shuffle down if necessary
            if (bNewField == FALSE)
            {
                memmove(&m_aryFields[z], &m_aryFields[z + 1], (256 - z - 1) * sizeof(CAPETagField *));
                m_nFields--;
            }

            return -1;
        }
    }

    if (bNewField)
        m_nFields++;

    m_aryFields[z] = new CAPETagField(pFieldName, (char *) pFieldValue, nFieldBytes, nFlags);

    return 0;
}

int CAPETag::Remove(BOOL bUpdate)
{
    // variables
    unsigned int nBytesRead = 0;
    int nRetVal = 0;
    int nOriginalPosition = m_spIO->GetPosition();

    BOOL bID3Removed = TRUE;
    BOOL bAPETagRemoved = TRUE;

    BOOL bFailedToRemove = FALSE;

    while (bID3Removed || bAPETagRemoved)
    {
        bID3Removed = FALSE;
        bAPETagRemoved = FALSE;

        // ID3 tag
        if (m_spIO->GetSize() > ID3_TAG_BYTES)
        {
            char cTagHeader[3];
            m_spIO->Seek(-ID3_TAG_BYTES, FILE_END);
            nRetVal = m_spIO->Read(cTagHeader, 3, &nBytesRead);
            if ((nRetVal == 0) && (nBytesRead == 3))
            {
                if (strncmp(cTagHeader, "TAG", 3) == 0)
                {
                    m_spIO->Seek(-ID3_TAG_BYTES, FILE_END);
                    if (m_spIO->SetEOF() != 0)
                        bFailedToRemove = TRUE;
                    else
                        bID3Removed = TRUE;
                }
            }
        }


        // APE Tag
        if (m_spIO->GetSize() > sizeof(APE_TAG_FOOTER) && bFailedToRemove == FALSE)
        {
            APE_TAG_FOOTER APETagFooter;
            m_spIO->Seek(-int(sizeof(APE_TAG_FOOTER)), FILE_END);
            nRetVal = m_spIO->Read(&APETagFooter, sizeof(APE_TAG_FOOTER), &nBytesRead);
            if ((nRetVal == 0) && (nBytesRead == sizeof(APE_TAG_FOOTER)))
            {
                if ((strncmp(APETagFooter.cID, "APETAGEX", 8) == 0) &&
                    (APETagFooter.nVersion <= CURRENT_APE_TAG_VERSION) &&
                    (APETagFooter.nFields <= 65536) &&
                    (APETagFooter.nSize <= (1024 * 1024 * 16)))
                {
                    m_spIO->Seek(-APETagFooter.nSize, FILE_END);
                    if (m_spIO->SetEOF() != 0)
                        bFailedToRemove = TRUE;
                    else
                        bAPETagRemoved = TRUE;
                }
            }
        }

    }

    m_spIO->Seek(nOriginalPosition, FILE_BEGIN);

    if (bUpdate && bFailedToRemove == FALSE)
    {
        Analyze();
    }

    return bFailedToRemove ? -1 : 0;
}

int CAPETag::SetFieldID3String(const char * pFieldName, const char * pFieldValue, int nBytes)
{
    // allocate a buffer and terminate it
    char * pBuffer = new char [nBytes + 1];
    pBuffer[nBytes] = 0;

    // make a capped copy of the string
    memcpy(pBuffer, pFieldValue, nBytes);

    // remove trailing white-space
    char * pEnd = &pBuffer[nBytes];
    while (((*pEnd == ' ') || (*pEnd == 0)) && pEnd >= &pBuffer[0]) { *pEnd-- = 0; }

    // set the field
    SetField(pFieldName, pBuffer, -1, 0);

    // clean-up
    delete [] pBuffer;

    return 0;
}

int CAPETag::GetFieldID3String(const char * pFieldName, char * pBuffer, int nBytes)
{
    int nBufferBytes = 256;
    char cField[256]; ZeroMemory(cField, 256);
    GetField(pFieldName, cField, &nBufferBytes);
    memcpy(pBuffer, cField, nBytes);

    return 0;
}
