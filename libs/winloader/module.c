/*
 * Modules
 *
 * Copyright 1995 Alexandre Julliard
 */

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <wine/windef.h>
#include <wine/winerror.h>
#include <wine/heap.h>
#include <wine/module.h>
#include <wine/pe_image.h>
#include <wine/debugtools.h>

struct modref_list_t;

typedef struct modref_list_t
{
    WINE_MODREF* wm;
    struct modref_list_t *next;
    struct modref_list_t *prev;    
}
modref_list;


//WINE_MODREF *local_wm=NULL;
modref_list* local_wm=NULL;

WINE_MODREF *MODULE_FindModule(LPCSTR m)
{
    modref_list* list=local_wm;
    TRACE("Module %s request\n", m);
    if(list==NULL)
	return NULL;
    while(strcmp(m, list->wm->filename))
    {
	list=list->prev;
	if(list==NULL)
	    return NULL;
    }	
    TRACE("Resolved to %s\n", list->wm->filename);
    return list->wm;
}    

void MODULE_RemoveFromList(WINE_MODREF *mod)
{
    modref_list* list=local_wm;
    if(list==0)
	return;
    if(mod==0)
	return;
    if((list->prev==NULL)&&(list->next==NULL))
    {
	free(list);
	local_wm=NULL;
	return;
    }
    for(;list;list=list->prev)
    {
	if(list->wm==mod)
	{
	    if(list->prev)
		list->prev->next=list->next;
	    if(list->next)
		list->next->prev=list->prev;
	    if(list==local_wm)
		local_wm=list->prev;
	    free(list);
	    return;
	}
    }
}    		    	
		
WINE_MODREF *MODULE32_LookupHMODULE(HMODULE m)
{
    modref_list* list=local_wm;
    TRACE("Module %X request\n", m);
    if(list==NULL)
	return NULL;
    while(m!=list->wm->module)
    {
//      printf("Checking list %X wm %X module %X\n",
//	list, list->wm, list->wm->module);
	list=list->prev;
	if(list==NULL)
	    return NULL;
    }	
    TRACE("LookupHMODULE hit %X\n", list->wm);
    return list->wm;
}    

/*************************************************************************
 *		MODULE_InitDll
 */
static WIN_BOOL MODULE_InitDll( WINE_MODREF *wm, DWORD type, LPVOID lpReserved )
{
    WIN_BOOL retv = TRUE;

    static LPCSTR typeName[] = { "PROCESS_DETACH", "PROCESS_ATTACH", 
                                 "THREAD_ATTACH", "THREAD_DETACH" };
    assert( wm );


    /* Skip calls for modules loaded with special load flags */

    if (    ( wm->flags & WINE_MODREF_DONT_RESOLVE_REFS )
         || ( wm->flags & WINE_MODREF_LOAD_AS_DATAFILE ) )
        return TRUE;


    TRACE("(%s,%s,%p) - CALL\n", wm->modname, typeName[type], lpReserved );

    /* Call the initialization routine */
    switch ( wm->type )
    {
    case MODULE32_PE:
        retv = PE_InitDLL( wm, type, lpReserved );
        break;

    case MODULE32_ELF:
        /* no need to do that, dlopen() already does */
        break;

    default:
        ERR("wine_modref type %d not handled.\n", wm->type );
        retv = FALSE;
        break;
    }

    /* The state of the module list may have changed due to the call
       to PE_InitDLL. We cannot assume that this module has not been
       deleted.  */
    TRACE("(%p,%s,%p) - RETURN %d\n", wm, typeName[type], lpReserved, retv );

    return retv;
}

/*************************************************************************
 *		MODULE_DllProcessAttach
 * 
 * Send the process attach notification to all DLLs the given module
 * depends on (recursively). This is somewhat complicated due to the fact that
 *
 * - we have to respect the module dependencies, i.e. modules implicitly
 *   referenced by another module have to be initialized before the module
 *   itself can be initialized
 * 
 * - the initialization routine of a DLL can itself call LoadLibrary,
 *   thereby introducing a whole new set of dependencies (even involving
 *   the 'old' modules) at any time during the whole process
 *
 * (Note that this routine can be recursively entered not only directly
 *  from itself, but also via LoadLibrary from one of the called initialization
 *  routines.)
 *
 * Furthermore, we need to rearrange the main WINE_MODREF list to allow
 * the process *detach* notifications to be sent in the correct order.
 * This must not only take into account module dependencies, but also 
 * 'hidden' dependencies created by modules calling LoadLibrary in their
 * attach notification routine.
 *
 * The strategy is rather simple: we move a WINE_MODREF to the head of the
 * list after the attach notification has returned.  This implies that the
 * detach notifications are called in the reverse of the sequence the attach
 * notifications *returned*.
 *
 * NOTE: Assumes that the process critical section is held!
 *
 */
WIN_BOOL MODULE_DllProcessAttach( WINE_MODREF *wm, LPVOID lpReserved )
{
    WIN_BOOL retv = TRUE;
    int i;
    assert( wm );

    /* prevent infinite recursion in case of cyclical dependencies */
    if (    ( wm->flags & WINE_MODREF_MARKER )
         || ( wm->flags & WINE_MODREF_PROCESS_ATTACHED ) )
        return retv;

    TRACE("(%s,%p) - START\n", wm->modname, lpReserved );

    /* Tag current MODREF to prevent recursive loop */
    wm->flags |= WINE_MODREF_MARKER;

    /* Recursively attach all DLLs this one depends on */
/*    for ( i = 0; retv && i < wm->nDeps; i++ )
        if ( wm->deps[i] )
            retv = MODULE_DllProcessAttach( wm->deps[i], lpReserved );
*/
    /* Call DLL entry point */

    //local_wm=wm;
    if(local_wm)
    {
        local_wm->next=malloc(sizeof(modref_list));
        local_wm->next->prev=local_wm;
        local_wm->next->next=NULL;
        local_wm->next->wm=wm;
        local_wm=local_wm->next;
    }
    else
    {
	local_wm=malloc(sizeof(modref_list));
	local_wm->next=local_wm->prev=NULL;
	local_wm->wm=wm;
    }		
    /* Remove recursion flag */
    wm->flags &= ~WINE_MODREF_MARKER;
    
    if ( retv )
    {
        retv = MODULE_InitDll( wm, DLL_PROCESS_ATTACH, lpReserved );
        if ( retv )
            wm->flags |= WINE_MODREF_PROCESS_ATTACHED;
    }


    TRACE("(%s,%p) - END\n", wm->modname, lpReserved );

    return retv;
}

/*************************************************************************
 *		MODULE_DllProcessDetach
 * 
 * Send DLL process detach notifications.  See the comment about calling 
 * sequence at MODULE_DllProcessAttach.  Unless the bForceDetach flag
 * is set, only DLLs with zero refcount are notified.
 */
void MODULE_DllProcessDetach( WINE_MODREF* wm, WIN_BOOL bForceDetach, LPVOID lpReserved )
{
//    WINE_MODREF *wm=local_wm;
    wm->flags &= ~WINE_MODREF_PROCESS_ATTACHED;
    MODULE_InitDll( wm, DLL_PROCESS_DETACH, lpReserved );
}

/*************************************************************************
 *		MODULE_DllThreadAttach
 * 
 * Send DLL thread attach notifications. These are sent in the
 * reverse sequence of process detach notification.
 *
 */
 /*
void MODULE_DllThreadAttach( LPVOID lpReserved )
{
    WINE_MODREF *wm;

    MODULE_InitDll( wm, DLL_THREAD_ATTACH, lpReserved );
}*/

/*************************************************************************
 *		MODULE_DllThreadDetach
 * 
 * Send DLL thread detach notifications. These are sent in the
 * same sequence as process detach notification.
 *
 */
 /*
void MODULE_DllThreadDetach( LPVOID lpReserved )
{
    WINE_MODREF *wm;
    MODULE_InitDll( wm, DLL_THREAD_DETACH, lpReserved );
}
*/

/***********************************************************************
 *           MODULE_CreateDummyModule
 *
 * Create a dummy NE module for Win32 or Winelib.
 */
HMODULE MODULE_CreateDummyModule( LPCSTR filename, HMODULE module32 )
{
    printf("MODULE_CreateDummyModule:: Not implemented\n");
    return 0;
}
/*    
HMODULE MODULE_CreateDummyModule( LPCSTR filename, HMODULE module32 )
{
    HMODULE hModule;
    NE_MODULE *pModule;
    SEGTABLEENTRY *pSegment;
    char *pStr,*s;
    unsigned int len;
    const char* basename;
    OFSTRUCT *ofs;
    int of_size, size;

    // Extract base filename 
    basename = strrchr(filename, '\\');
    if (!basename) basename = filename;
    else basename++;
    len = strlen(basename);
    if ((s = strchr(basename, '.'))) len = s - basename;

    // Allocate module 
    of_size = sizeof(OFSTRUCT) - sizeof(ofs->szPathName)
                    + strlen(filename) + 1;
    size = sizeof(NE_MODULE) +
                // loaded file info 
                 of_size +
                // segment table: DS,CS 
                 2 * sizeof(SEGTABLEENTRY) +
                 // name table 
                 len + 2 +
                 // several empty tables 
                 8;

    hModule = GlobalAlloc16( GMEM_MOVEABLE | GMEM_ZEROINIT, size );
    if (!hModule) return (HMODULE)11;  // invalid exe 

    FarSetOwner16( hModule, hModule );
    pModule = (NE_MODULE *)GlobalLock16( hModule );

    // Set all used entries 
    pModule->magic            = IMAGE_OS2_SIGNATURE;
    pModule->count            = 1;
    pModule->next             = 0;
    pModule->flags            = 0;
    pModule->dgroup           = 0;
    pModule->ss               = 1;
    pModule->cs               = 2;
    pModule->heap_size        = 0;
    pModule->stack_size       = 0;
    pModule->seg_count        = 2;
    pModule->modref_count     = 0;
    pModule->nrname_size      = 0;
    pModule->fileinfo         = sizeof(NE_MODULE);
    pModule->os_flags         = NE_OSFLAGS_WINDOWS;
    pModule->self             = hModule;
    pModule->module32         = module32;

    // Set version and flags 
    if (module32)
    {
        pModule->expected_version =
            ((PE_HEADER(module32)->OptionalHeader.MajorSubsystemVersion & 0xff) << 8 ) |
             (PE_HEADER(module32)->OptionalHeader.MinorSubsystemVersion & 0xff);
        pModule->flags |= NE_FFLAGS_WIN32;
        if (PE_HEADER(module32)->FileHeader.Characteristics & IMAGE_FILE_DLL)
            pModule->flags |= NE_FFLAGS_LIBMODULE | NE_FFLAGS_SINGLEDATA;
    }

    // Set loaded file information 
    ofs = (OFSTRUCT *)(pModule + 1);
    memset( ofs, 0, of_size );
    ofs->cBytes = of_size < 256 ? of_size : 255;   // FIXME 
    strcpy( ofs->szPathName, filename );

    pSegment = (SEGTABLEENTRY*)((char*)(pModule + 1) + of_size);
    pModule->seg_table = (int)pSegment - (int)pModule;
    // Data segment
    pSegment->size    = 0;
    pSegment->flags   = NE_SEGFLAGS_DATA;
    pSegment->minsize = 0x1000;
    pSegment++;
    // Code segment
    pSegment->flags   = 0;
    pSegment++;

    // Module name 
    pStr = (char *)pSegment;
    pModule->name_table = (int)pStr - (int)pModule;
    assert(len<256);
    *pStr = len;
    lstrcpynA( pStr+1, basename, len+1 );
    pStr += len+2;

    // All tables zero terminated 
    pModule->res_table = pModule->import_table = pModule->entry_table =
		(int)pStr - (int)pModule;

    NE_RegisterModule( pModule );
    return hModule;
}

*/

/***********************************************************************
 *           MODULE_GetBinaryType
 *
 * The GetBinaryType function determines whether a file is executable
 * or not and if it is it returns what type of executable it is.
 * The type of executable is a property that determines in which
 * subsystem an executable file runs under.
 *
 * Binary types returned:
 * SCS_32BIT_BINARY: A Win32 based application
 * SCS_DOS_BINARY: An MS-Dos based application
 * SCS_WOW_BINARY: A Win16 based application
 * SCS_PIF_BINARY: A PIF file that executes an MS-Dos based app
 * SCS_POSIX_BINARY: A POSIX based application ( Not implemented )
 * SCS_OS216_BINARY: A 16bit OS/2 based application
 *
 * Returns TRUE if the file is an executable in which case
 * the value pointed by lpBinaryType is set.
 * Returns FALSE if the file is not an executable or if the function fails.
 *
 * To do so it opens the file and reads in the header information
 * if the extended header information is not present it will
 * assume that the file is a DOS executable.
 * If the extended header information is present it will
 * determine if the file is a 16 or 32 bit Windows executable
 * by check the flags in the header.
 *
 * Note that .COM and .PIF files are only recognized by their
 * file name extension; but Windows does it the same way ...
 */
 /*
static WIN_BOOL MODULE_GetBinaryType( HANDLE hfile, LPCSTR filename,
                                  LPDWORD lpBinaryType )
{
    IMAGE_DOS_HEADER mz_header;
    char magic[4], *ptr;
    DWORD len;

    // Seek to the start of the file and read the DOS header information.
    if (    SetFilePointer( hfile, 0, NULL, SEEK_SET ) != -1  
         && ReadFile( hfile, &mz_header, sizeof(mz_header), &len, NULL )
         && len == sizeof(mz_header) )
    {
        // Now that we have the header check the e_magic field
         // to see if this is a dos image.
         //
        if ( mz_header.e_magic == IMAGE_DOS_SIGNATURE )
        {
            WIN_BOOL lfanewValid = FALSE;
            // We do have a DOS image so we will now try to seek into
            // the file by the amount indicated by the field
            // "Offset to extended header" and read in the
            // "magic" field information at that location.
            // This will tell us if there is more header information
            // to read or not.
            //
            // But before we do we will make sure that header
             // structure encompasses the "Offset to extended header"
             // field.
             //
            if ( (mz_header.e_cparhdr<<4) >= sizeof(IMAGE_DOS_HEADER) )
                if ( ( mz_header.e_crlc == 0 ) ||
                     ( mz_header.e_lfarlc >= sizeof(IMAGE_DOS_HEADER) ) )
                    if (    mz_header.e_lfanew >= sizeof(IMAGE_DOS_HEADER)
                         && SetFilePointer( hfile, mz_header.e_lfanew, NULL, SEEK_SET ) != -1  
                         && ReadFile( hfile, magic, sizeof(magic), &len, NULL )
                         && len == sizeof(magic) )
                        lfanewValid = TRUE;

            if ( !lfanewValid )
            {
                // If we cannot read this "extended header" we will
                 // assume that we have a simple DOS executable.
                 //
                *lpBinaryType = SCS_DOS_BINARY;
                return TRUE;
            }
            else
            {
                // Reading the magic field succeeded so
                // we will try to determine what type it is.
                 //
                if ( *(DWORD*)magic      == IMAGE_NT_SIGNATURE )
                {
                    // This is an NT signature.
                     //
                    *lpBinaryType = SCS_32BIT_BINARY;
                    return TRUE;
                }
                else if ( *(WORD*)magic == IMAGE_OS2_SIGNATURE )
                {
                    // The IMAGE_OS2_SIGNATURE indicates that the
                    // "extended header is a Windows executable (NE)
                    // header."  This can mean either a 16-bit OS/2
                    // or a 16-bit Windows or even a DOS program 
                    // (running under a DOS extender).  To decide
                    // which, we'll have to read the NE header.
                    ///

                     IMAGE_OS2_HEADER ne;
                     if (    SetFilePointer( hfile, mz_header.e_lfanew, NULL, SEEK_SET ) != -1  
                          && ReadFile( hfile, &ne, sizeof(ne), &len, NULL )
                          && len == sizeof(ne) )
                     {
                         switch ( ne.ne_exetyp )
                         {
                         case 2:  *lpBinaryType = SCS_WOW_BINARY;   return TRUE;
                         case 5:  *lpBinaryType = SCS_DOS_BINARY;   return TRUE;
                         default: *lpBinaryType = SCS_OS216_BINARY; return TRUE;
                         }
                     }
                     // Couldn't read header, so abort. 
                     return FALSE;
                }
                else
                {
                    // Unknown extended header, but this file is nonetheless
		     //  DOS-executable.
                     //
                    *lpBinaryType = SCS_DOS_BINARY;
	            return TRUE;
                }
            }
        }
    }

    // If we get here, we don't even have a correct MZ header.
    // Try to check the file extension for known types ...
     //
    ptr = strrchr( filename, '.' );
    if ( ptr && !strchr( ptr, '\\' ) && !strchr( ptr, '/' ) )
    {
        if ( !lstrcmpiA( ptr, ".COM" ) )
        {
            *lpBinaryType = SCS_DOS_BINARY;
            return TRUE;
        }

        if ( !lstrcmpiA( ptr, ".PIF" ) )
        {
            *lpBinaryType = SCS_PIF_BINARY;
            return TRUE;
        }
    }

    return FALSE;
}
*/
/***********************************************************************
 *             GetBinaryTypeA                     [KERNEL32.280]
 */
/*
WIN_BOOL WINAPI GetBinaryTypeA( LPCSTR lpApplicationName, LPDWORD lpBinaryType )
{
    WIN_BOOL ret = FALSE;
    HANDLE hfile;

    TRACE_(win32)("%s\n", lpApplicationName );

    // Sanity check.
    
    if ( lpApplicationName == NULL || lpBinaryType == NULL )
        return FALSE;

    // Open the file indicated by lpApplicationName for reading.
     
    hfile = CreateFileA( lpApplicationName, GENERIC_READ, 0,
                         NULL, OPEN_EXISTING, 0, -1 );
    if ( hfile == INVALID_HANDLE_VALUE )
        return FALSE;

    // Check binary type
     
    ret = MODULE_GetBinaryType( hfile, lpApplicationName, lpBinaryType );

    // Close the file.
     
    CloseHandle( hfile );

    return ret;
}
*/

/***********************************************************************
 *           LoadLibraryExA   (KERNEL32)
 */
HMODULE WINAPI LoadLibraryExA(LPCSTR libname, HANDLE hfile, DWORD flags)
{
	WINE_MODREF *wm;

	if(!libname)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}


	wm = MODULE_LoadLibraryExA( libname, hfile, flags );
	if ( wm )
	{
		if ( !MODULE_DllProcessAttach( wm, NULL ) )
		{
			WARN_(module)("Attach failed for module '%s', \n", libname);
			MODULE_FreeLibrary(wm);
			SetLastError(ERROR_DLL_INIT_FAILED);
			MODULE_RemoveFromList(wm);
			wm = NULL;
		}
	}

	return wm ? wm->module : 0;
}


/***********************************************************************
 *	MODULE_LoadLibraryExA	(internal)
 *
 * Load a PE style module according to the load order.
 *
 * The HFILE parameter is not used and marked reserved in the SDK. I can
 * only guess that it should force a file to be mapped, but I rather
 * ignore the parameter because it would be extremely difficult to
 * integrate this with different types of module represenations.
 *
 */
WINE_MODREF *MODULE_LoadLibraryExA( LPCSTR libname, HFILE hfile, DWORD flags )
{
	DWORD err = GetLastError();
	WINE_MODREF *pwm;
	int i;
//	module_loadorder_t *plo;


        SetLastError( ERROR_FILE_NOT_FOUND );
	TRACE("Trying native dll '%s'\n", libname);
	pwm = PE_LoadLibraryExA(libname, flags);
	if(!pwm)
	{
    	    TRACE("Trying ELF dll '%s'\n", libname);
	    pwm=ELFDLL_LoadLibraryExA(libname, flags);
	}	
//		printf("0x%08x\n", pwm);
//		break;
	if(pwm)
	{
		/* Initialize DLL just loaded */
		TRACE("Loaded module '%s' at 0x%08x, \n", libname, pwm->module);
		/* Set the refCount here so that an attach failure will */
		/* decrement the dependencies through the MODULE_FreeLibrary call. */
		pwm->refCount++;

                SetLastError( err );  /* restore last error */
		return pwm;
	}

	
	WARN("Failed to load module '%s'; error=0x%08lx, \n", libname, GetLastError());
	return NULL;
}

/***********************************************************************
 *           LoadLibraryA         (KERNEL32)
 */
HMODULE WINAPI LoadLibraryA(LPCSTR libname) {
	return LoadLibraryExA(libname,0,0);
}


/***********************************************************************
 *           FreeLibrary
 */
WIN_BOOL WINAPI FreeLibrary(HINSTANCE hLibModule)
{
    WIN_BOOL retv = FALSE;
    WINE_MODREF *wm;

    wm=MODULE32_LookupHMODULE(hLibModule);
//    wm=local_wm;

    if ( !wm || !hLibModule )
    {
        SetLastError( ERROR_INVALID_HANDLE );
	return 0;
    }	
    else
        retv = MODULE_FreeLibrary( wm );
    
    MODULE_RemoveFromList(wm);

    return retv;
}

/***********************************************************************
 *           MODULE_DecRefCount
 *
 * NOTE: Assumes that the process critical section is held!
 */
static void MODULE_DecRefCount( WINE_MODREF *wm )
{
    int i;

    if ( wm->flags & WINE_MODREF_MARKER )
        return;

    if ( wm->refCount <= 0 )
        return;

    --wm->refCount;
    TRACE("(%s) refCount: %d\n", wm->modname, wm->refCount );

    if ( wm->refCount == 0 )
    {
        wm->flags |= WINE_MODREF_MARKER;

        for ( i = 0; i < wm->nDeps; i++ )
            if ( wm->deps[i] )
                MODULE_DecRefCount( wm->deps[i] );

        wm->flags &= ~WINE_MODREF_MARKER;
    }
}

/***********************************************************************
 *           MODULE_FreeLibrary
 *
 * NOTE: Assumes that the process critical section is held!
 */
WIN_BOOL MODULE_FreeLibrary( WINE_MODREF *wm )
{
    TRACE("(%s) - START\n", wm->modname );

    /* Recursively decrement reference counts */
    //MODULE_DecRefCount( wm );

    /* Call process detach notifications */
    MODULE_DllProcessDetach( wm, FALSE, NULL );

    PE_UnloadLibrary(wm);

    TRACE("END\n");

    return TRUE;
}

/***********************************************************************
 *           GetProcAddress   		(KERNEL32.257)
 */
FARPROC WINAPI GetProcAddress( HMODULE hModule, LPCSTR function )
{
    return MODULE_GetProcAddress( hModule, function, TRUE );
}

/***********************************************************************
 *           MODULE_GetProcAddress   		(internal)
 */
FARPROC MODULE_GetProcAddress( 
	HMODULE hModule, 	/* [in] current module handle */
	LPCSTR function,	/* [in] function to be looked up */
	WIN_BOOL snoop )
{
    WINE_MODREF	*wm = MODULE32_LookupHMODULE( hModule );
//    WINE_MODREF *wm=local_wm;    
    FARPROC	retproc;

    if (HIWORD(function))
	TRACE_(win32)("(%08lx,%s)\n",(DWORD)hModule,function);
    else
	TRACE_(win32)("(%08lx,%p)\n",(DWORD)hModule,function);
    if (!wm) {
    	SetLastError(ERROR_INVALID_HANDLE);
        return (FARPROC)0;
    }
    switch (wm->type)
    {
    case MODULE32_PE:
     	retproc = PE_FindExportedFunction( wm, function, snoop );
	if (!retproc) SetLastError(ERROR_PROC_NOT_FOUND);
	return retproc;
    case MODULE32_ELF:
	retproc = dlsym( wm->module, function);
	if (!retproc) SetLastError(ERROR_PROC_NOT_FOUND);
	return retproc;
    default:
    	ERR("wine_modref type %d not handled.\n",wm->type);
    	SetLastError(ERROR_INVALID_HANDLE);
    	return (FARPROC)0;
    }
}

