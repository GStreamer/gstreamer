#include "All.h"
#include "GlobalFunctions.h"
#include "IO.h"

#ifndef __GNUC_IA32__

#pragma warning( disable : 4035 )

extern "C" BOOL
GetMMXAvailable ( void )	// No check for 80286 and below, code won't compile on 16 bit due to "int = 32 bit" assumption
{
#if defined ENABLE_ASSEMBLY
    _asm {
            pushfd
            pop     eax
            mov     ecx, eax
            xor     eax, 0x200000
            push    eax
            popfd
            pushfd
            pop     eax
            cmp     eax, ecx
            jz      retval          // no CPUID instruction available => 80386, 80486
            mov     eax, 1
            CPUID
            test    edx, 0x800000   // MMX available ?
retval:     setnz   al
            and     eax, 1
    }
#else
    return FALSE;
#endif
}

#pragma warning( default : 4035 )

#endif /* __GNUC_IA32__ */


int ReadSafe(CIO * pIO, void * pBuffer, int nBytes)
{
    unsigned int nBytesRead = 0;
    int nRetVal = pIO->Read(pBuffer, nBytes, &nBytesRead);
    if (nRetVal == ERROR_SUCCESS)
    {
        if (nBytes != int(nBytesRead))
            nRetVal = ERROR_IO_READ;
    }

    return nRetVal;
}

int WriteSafe(CIO * pIO, void * pBuffer, int nBytes)
{
    unsigned int nBytesWritten = 0;
    int nRetVal = pIO->Write(pBuffer, nBytes, &nBytesWritten);
    if (nRetVal == ERROR_SUCCESS)
    {
        if (nBytes != int(nBytesWritten))
            nRetVal = ERROR_IO_WRITE;
    }

    return nRetVal;
}

BOOL FileExists ( const char* pFilename )
{
    if ( 0 == strcmp ( pFilename, "-" )  ||  0 == strcmp ( pFilename, "/dev/stdin" ) )
        return TRUE;
#ifdef _WIN32
    BOOL bFound = FALSE;

    WIN32_FIND_DATA WFD;
    HANDLE hFind = FindFirstFile(pFilename, &WFD);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        bFound = TRUE;
        CloseHandle(hFind);
    }

    return bFound;
#else
    struct stat  b;

    if ( stat (  pFilename, &b ) != 0 )
        return FALSE;

    if ( ! S_ISREG (b.st_mode) )
        return FALSE;

    return TRUE;

#endif
}
