#ifndef APE_APETAG_H
#define APE_APETAG_H

#include "IO.h"

/*****************************************************************************************
Notes:

    -When saving images, store the filename (no directory) (i.e. Cover.jpg) followed by
    a null terminator, followed by the image data.
*****************************************************************************************/

/*****************************************************************************************
The version of the APE tag
*****************************************************************************************/
#define CURRENT_APE_TAG_VERSION     1000

/*****************************************************************************************
"Standard" APE tag fields
*****************************************************************************************/
#define APE_TAG_FIELD_TITLE             "Title"
#define APE_TAG_FIELD_ARTIST            "Artist"
#define APE_TAG_FIELD_ALBUM             "Album"
#define APE_TAG_FIELD_COMMENT           "Comment"
#define APE_TAG_FIELD_YEAR              "Year"
#define APE_TAG_FIELD_TRACK             "Track"
#define APE_TAG_FIELD_GENRE             "Genre"
#define APE_TAG_FIELD_COVER_ART_FRONT   "Cover Art (front)"
#define APE_TAG_FIELD_NOTES             "Notes"
#define APE_TAG_FIELD_LYRICS            "Lyrics"
#define APE_TAG_FIELD_COPYRIGHT         "Copyright"
#define APE_TAG_FIELD_BUY_URL           "Buy URL"
#define APE_TAG_FIELD_ARTIST_URL        "Artist URL"
#define APE_TAG_FIELD_PUBLISHER_URL     "Publisher URL"
#define APE_TAG_FIELD_FILE_URL          "File URL"
#define APE_TAG_FIELD_COPYRIGHT_URL     "Copyright URL"
#define APE_TAG_FIELD_MJ_METADATA       "Media Jukebox Metadata"

/*****************************************************************************************
Standard APE tag field values
*****************************************************************************************/
#define APE_TAG_GENRE_UNDEFINED         "Undefined"

/*****************************************************************************************
ID3 v1.1 tag
*****************************************************************************************/
#define ID3_TAG_BYTES   128
struct ID3_TAG
{
    char TagHeader[3];      // should equal 'TAG'
    char Title[30];         // title
    char Artist[30];        // artist
    char Album[30];         // album
    char Year[4];           // year
    char Comment[29];       // comment
    unsigned char Track;    // track
    unsigned char Genre;    // genre
};

/*****************************************************************************************
The footer at the end of APE tagged files
*****************************************************************************************/
struct APE_TAG_FOOTER
{
    char cID[8];            // should equal 'APETAGEX'
    int nVersion;           // equals CURRENT_APE_TAG_VERSION
    int nSize;              // the complete size of the tag, including this footer
    int nFields;            // the number of fields in the tag
    int nFlags;             // the tag flags (none currently defined)
    char cReserved[8];      // reserved for later use
};

/*****************************************************************************************
CAPETagField class (an APE tag is an array of these)
*****************************************************************************************/
class CAPETagField
{
public:

    // create a tag field (use nFieldBytes = -1 for null-terminated strings)
    CAPETagField(const char * pFieldName, const char * pFieldValue, int nFieldBytes = -1, int nFlags = 0);

    // destructor
    ~CAPETagField();

    // gets the size of the entire field in bytes (name, value, and metadata)
    int GetFieldSize();

    // get the name of the field
    const char * GetFieldName();

    // get the value of the field
    const char * GetFieldValue();

    // get the size of the value (in bytes)
    int GetFieldValueSize();

    // get any special flags (none defined yet)
    int GetFieldFlags();

    // output the entire field to a buffer (GetFieldSize() bytes)
    int SaveField(char * pBuffer);

private:

    CSmartPtr<char> m_spFieldName;
    CSmartPtr<char> m_spFieldValue;
    int m_nFieldFlags;

    int m_nFieldNameBytes;
    int m_nFieldValueBytes;
};

/*****************************************************************************************
CAPETag class
*****************************************************************************************/
class CAPETag
{
public:

    // create an APE tag
    // bAnalyze determines whether it will analyze immediately or on the first request
    // be careful with multiple threads / file pointer movement if you don't analyze immediately
    CAPETag(CIO * pIO, BOOL bAnalyze = TRUE);
    CAPETag(const char * pFilename, BOOL bAnalyze = TRUE);

    // destructor
    ~CAPETag();

    // save the tag to the I/O source (bUseOldID3 forces it to save as an ID3v1.1 tag instead of an APE tag)
    int Save(BOOL bUseOldID3 = FALSE);

    // removes any tags from the file (bUpdate determines whether is should re-analyze after removing the tag)
    int Remove(BOOL bUpdate = TRUE);

    // sets the value of a field (use nFieldBytes = -1 for null terminated strings)
    // note: to remove a field, call (ex. SetField(APE_TAG_FIELD_TITLE, NULL);)
    int SetField(const char * pFieldName, const char * pFieldValue, int nFieldBytes = -1, int nFlags = 0);

    // gets the value of a field (returns -1 and an empty buffer if the field doesn't exist)
    int GetField(const char * pFieldName, char * pBuffer, int * pBufferBytes);

    // clear all the fields
    int ClearFields();

    // get the total tag bytes in the file from the last analyze
    // need to call Save() then Analyze() to update any changes
    int GetTagBytes();

    // fills in an ID3_TAG using the current fields (useful for quickly converting the tag)
    int CreateID3Tag(ID3_TAG * pID3Tag);

    // see whether the file has an ID3 or APE tag
    BOOL GetHasID3Tag() { if (m_bAnalyzed == FALSE) { Analyze(); } return m_bHasID3Tag; }
    BOOL GetHasAPETag() { if (m_bAnalyzed == FALSE) { Analyze(); } return m_bHasAPETag; }

    // iterate through the tag fields
    // set bFirst to TRUE on the first call, and to FALSE on subsequent calls (TRUE resets the internal counter)
    // also, be careful since you're getting a pointer to the field in the tag (pointer expiration, etc.)
    BOOL GetNextTagField(BOOL bFirst, CAPETagField ** ppAPETagField);

    // gets a desired tag field (returns NULL if not found)
    // again, be careful, because this a pointer to the actual field in this class
    CAPETagField * GetTagField(const char * pFieldName);

private:

    // private functions
    int Analyze();
    int WriteBufferToEndOfIO(void * pBuffer, int nBytes);

    // helper set / get field functions
    int SetFieldID3String(const char * pFieldName, const char * pFieldValue, int nBytes);
    int GetFieldID3String(const char * pFieldName, char * pBuffer, int nBytes);

    // private data
    CSmartPtr<CIO>  m_spIO;
    BOOL            m_bAnalyzed;
    int             m_nTagBytes;
    int             m_nFields;
    CAPETagField *  m_aryFields[256];
    BOOL            m_bHasAPETag;
    BOOL            m_bHasID3Tag;
    int             m_nRetrieveFieldIndex;
};

#endif /* APE_APETAG_H */
