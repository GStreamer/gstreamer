#ifndef APE_GLOBALFUNCTIONS_H
#define APE_GLOBALFUNCTIONS_H

/*************************************************************************************
Definitions
*************************************************************************************/
class CIO;

/*************************************************************************************
Checks for MMX
*************************************************************************************/
extern "C" BOOL GetMMXAvailable ( void );

/*************************************************************************************
Read / Write from an IO source and return failure if the number of bytes specified
isn't read or written
*************************************************************************************/
int ReadSafe(CIO * pIO, void * pBuffer, int nBytes);
int WriteSafe(CIO * pIO, void * pBuffer, int nBytes);

/*************************************************************************************
Checks for the existence of a file
*************************************************************************************/
BOOL FileExists(const char * pFilename);

#endif /* APE_GLOBALFUNCTIONS_H */
