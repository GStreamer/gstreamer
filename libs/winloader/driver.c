#include <stdio.h>
#include <malloc.h>
#include <wine/driver.h>
#include <wine/pe_image.h>
#include <wine/winreg.h>
#include <wine/vfw.h>
#include <registry.h>

#include <config.h>

#define STORE_ALL \
    __asm__ ( \
    "push %%ebx\n\t" \
    "push %%ecx\n\t" \
    "push %%edx\n\t" \
    "push %%esi\n\t" \
    "push %%edi\n\t"::)

#define REST_ALL \
    __asm__ ( \
    "pop %%edi\n\t" \
    "pop %%esi\n\t" \
    "pop %%edx\n\t" \
    "pop %%ecx\n\t" \
    "pop %%ebx\n\t"::)



typedef struct {
    UINT             uDriverSignature;
    HINSTANCE        hDriverModule;
    DRIVERPROC       DriverProc;
    DWORD            dwDriverID;
} DRVR;

typedef DRVR  *PDRVR;
typedef DRVR  *NPDRVR;
typedef DRVR  *LPDRVR;
    
static DWORD dwDrvID = 0;

static NPDRVR DrvAlloc(HDRVR*lpDriver, LPUINT lpDrvResult)
{
    NPDRVR npDriver;
    /* allocate and lock handle */
    if (lpDriver)
    {
        if (*lpDriver = (HDRVR) malloc(sizeof(DRVR)) )
        {
            if (npDriver = (NPDRVR) *lpDriver)
            {
                    *lpDrvResult = MMSYSERR_NOERROR;
                    return (npDriver);
            }
            free((NPDRVR)*lpDriver);
        }
        return (*lpDrvResult = MMSYSERR_NOMEM, (NPDRVR) 0);
    }
    return (*lpDrvResult = MMSYSERR_INVALPARAM, (NPDRVR) 0);
}

typedef struct
{
    HMODULE handle;
    char name[64];
    int usage;
}codec_t;

static codec_t codecs[3]={
 {0, PLUGINS_SRCDIR "/win32/divxc32.dll", 0},
 {0, PLUGINS_SRCDIR "/win32/ir50_32.dll", 0},
// {0, "./mpg4c32.dll", 0},
 {0, PLUGINS_SRCDIR "/win32/libvideodll.so", 0},
};

                                                                                                                    
static void DrvFree(HDRVR hDriver)
{
    int i;
    FreeLibrary(((DRVR*)hDriver)->hDriverModule);
    if(hDriver)
    for(i=0; i<sizeof(codecs)/sizeof(codecs[0]); i++)
	if(codecs[i].handle==((DRVR*)hDriver)->hDriverModule)
	{
	    codecs[i].handle=0;	    
	    codecs[i].usage--;	 
	    if (hDriver)
        	free((NPDRVR)hDriver);
	    return;	
	}       
}       

void DrvClose(HDRVR hdrvr)
{
    DrvFree(hdrvr);
}
    
HDRVR
//DrvOpen(LPCSTR lpszDriverName, LPCSTR lpszSectionName, LPARAM lParam2)
DrvOpen(LPARAM lParam2)
{
    int drv_id;
    char filename[MAX_PATH], *f;
    UINT uDrvResult;
    HDRVR hDriver;
    NPDRVR npDriver;
     char unknown[0x24];
    int seg;
    int qwe;
    int regs[10];

    int fccHandler=*((int*)lParam2+2);
    switch(fccHandler)
    {
    case mmioFOURCC('D', 'I', 'V', '3'):
    case mmioFOURCC('D', 'I', 'V', '4'):
    case mmioFOURCC('d', 'i', 'v', '3'):
    case mmioFOURCC('d', 'i', 'v', '4'):
	drv_id=0;
	break;
    case mmioFOURCC('I', 'V', '5', '0'):	    
    case mmioFOURCC('i', 'v', '5', '0'):	    
	drv_id=1;
	break;
    case mmioFOURCC('m', 'p', '4', '3'):
    case mmioFOURCC('M', 'P', 'G', '4'):
	drv_id=2;
	break;
    default:
	printf("Unknown codec %X='%c%c%c%c'\n", fccHandler, 
	fccHandler&0xFF, (fccHandler&0xFF00)>>8,
	(fccHandler&0xFF0000)>>16, (fccHandler&0xFF000000)>>24);
	return (HDRVR)0;	
    }
    
	
    if (!(npDriver = DrvAlloc(&hDriver, &uDrvResult)))
	return ((HDRVR) 0);

    if(codecs[drv_id].handle==0)
    {
     if (!(codecs[drv_id].handle=npDriver->hDriverModule = LoadLibraryA(codecs[drv_id].name)))
     {
        DrvFree(hDriver);
        return ((HDRVR) 0);
     }
     else codecs[drv_id].usage=1;
    }
    else
    {
	npDriver->hDriverModule=codecs[drv_id].handle;
	codecs[drv_id].usage++;
    }	
    
//    14c0 
    if(drv_id==0)
    {
	int newkey;
	int bitrate;	
	int count=4;
	if(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\LinuxLoader\\Divx", 0, 0, &newkey)!=0)
	    goto no_reg;
	if(RegQueryValueExA(newkey, "BitRate", 0, 0, &bitrate, &count)!=0)
	{
	    RegCloseKey(newkey);
	    goto no_reg;
	}    
//	printf("Setting default bitrate from %f to %d\n",
//		*(double*)((char*)npDriver->hDriverModule+0x14c0), bitrate);
 
	*(double*)((char*)npDriver->hDriverModule+0x14c0)=bitrate;
	RegCloseKey(newkey);
    no_reg:
	;	
    }	
    
     if (!(npDriver->DriverProc = (DRIVERPROC)
             GetProcAddress(npDriver->hDriverModule, "DriverProc")))
         {
         FreeLibrary(npDriver->hDriverModule);
         DrvFree(hDriver);
         return ((HDRVR) 0);
     }
     
     //printf("DriverProc == %X\n", npDriver->DriverProc);
     npDriver->dwDriverID = ++dwDrvID;

     if (codecs[drv_id].usage==1)
     {
	STORE_ALL;
        (npDriver->DriverProc)(0, hDriver, DRV_LOAD, 0, 0);
	REST_ALL;
	//printf("DRV_LOAD Ok!\n");
	STORE_ALL;
	(npDriver->DriverProc)(0, hDriver, DRV_ENABLE, 0, 0);
	REST_ALL;
	//printf("DRV_ENABLE Ok!\n");
     }

     // open driver 
    STORE_ALL;
     npDriver->dwDriverID=(npDriver->DriverProc)(npDriver->dwDriverID, hDriver, DRV_OPEN,
         (LPARAM) (LPSTR) unknown, lParam2);
    REST_ALL;

    //printf("DRV_OPEN Ok!(%X)\n", npDriver->dwDriverID);

    if (uDrvResult)
    {
         DrvFree(hDriver);
         hDriver = (HDRVR) 0;
     }
     return (hDriver);
}
  
