#include <stdio.h>
#include <malloc.h>
#include <wine/driver.h>
#include <wine/pe_image.h>
#include <wine/winreg.h>
#include <wine/vfw.h>
#include <registry.h>

#include "config.h"

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


#define WIN32_PATH GST_WIN32_LIBDIR

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

LRESULT WINAPI SendDriverMessage( HDRVR hDriver, UINT message,
                                    LPARAM lParam1, LPARAM lParam2 )
{
    DRVR* module=(DRVR*)hDriver;
    int result;
#ifdef DETAILED_OUT    
    printf("SendDriverMessage: driver %X, message %X, arg1 %X, arg2 %X\n", hDriver, message, lParam1, lParam2);
#endif
    if(module==0)return -1;
    if(module->hDriverModule==0)return -1;
    if(module->DriverProc==0)return -1;
    STORE_ALL;
    result=module->DriverProc(module->dwDriverID,1,message,lParam1,lParam2);
    REST_ALL;
#ifdef DETAILED_OUT    
    printf("\t\tResult: %X\n", result);
#endif    
    return result;
}				    

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

static codec_t avi_codecs[]={
 {0, WIN32_PATH"/divxc32.dll", 0},	//0
 {0, WIN32_PATH"/ir50_32.dll", 0},
 {0, WIN32_PATH"/ir41_32.dll", 0},
 {0, WIN32_PATH"/ir32_32.dll", 0},    
 {0, WIN32_PATH"/mpg4c32.dll", 0},
 {0, WIN32_PATH"/iccvid.dll", 0},	//5
 {0, WIN32_PATH"/libvideodll.so", 0},
 {0, WIN32_PATH"/divxa32.acm", 0},	
 {0, WIN32_PATH"/msadp32.acm", 0},
 {0, WIN32_PATH"/ativcr1.dll", 0},
 {0, WIN32_PATH"/ativcr2.dll", 0},	//10
 {0, WIN32_PATH"/i263_32.drv", 0},
 {0, WIN32_PATH"/l3codeca.acm", 0},
// {0, WIN32_PATH"/atiyvu9.dll", 0},
};

                                                                                                                    
static void DrvFree(HDRVR hDriver)
{
    int i;
    if(hDriver)
    	if(((DRVR*)hDriver)->hDriverModule)
    	if(((DRVR*)hDriver)->DriverProc)
	(((DRVR*)hDriver)->DriverProc)(((DRVR*)hDriver)->dwDriverID, hDriver, DRV_CLOSE, 0, 0);
    if(hDriver)
    for(i=0; i<sizeof(avi_codecs)/sizeof(avi_codecs[0]); i++)
	if(avi_codecs[i].handle==((DRVR*)hDriver)->hDriverModule)
	{
	    avi_codecs[i].usage--;	
	    if(avi_codecs[i].usage==0)
	    { 
    		avi_codecs[i].handle=0;	    
                 if(((DRVR*)hDriver)->hDriverModule)
    		if(((DRVR*)hDriver)->DriverProc)
			(((DRVR*)hDriver)->DriverProc)(0, hDriver, DRV_FREE, 0, 0);
		FreeLibrary(((DRVR*)hDriver)->hDriverModule);
    		if (hDriver)
        	    		free((NPDRVR)hDriver);
		return;	
	    }	
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
    int fccType=*((int*)lParam2+1);
    if(fccType==0x63646976)//vidc
	switch(fccHandler)
	{
	case mmioFOURCC('D', 'I', 'V', '3'):
	case mmioFOURCC('D', 'I', 'V', '4'):
	case mmioFOURCC('d', 'i', 'v', '3'):
        case mmioFOURCC('d', 'i', 'v', '4'):
	case mmioFOURCC('M', 'P', '4', '1'):
	case mmioFOURCC('M', 'P', '4', '2'):
	case mmioFOURCC('M', 'P', '4', '3'):
	    printf("Video in DivX ;-) format\n");
	    drv_id=0;
	    break;
	case mmioFOURCC('I', 'V', '5', '0'):	    
	case mmioFOURCC('i', 'v', '5', '0'):	 
	    printf("Video in Indeo Video 5 format\n");   
	    drv_id=1;
	    break;
	case mmioFOURCC('I', 'V', '4', '1'):	    
	case mmioFOURCC('i', 'v', '4', '1'):	    
	    printf("Video in Indeo Video 4.1 format\n");   
	    drv_id=2;
	    break;
	case mmioFOURCC('I', 'V', '3', '2'):	    
	case mmioFOURCC('i', 'v', '3', '2'):
	    printf("Video in Indeo Video 3.2 format\n");   
	    drv_id=3;
	    break;
	
	case mmioFOURCC('m', 'p', '4', '1'):
	case mmioFOURCC('m', 'p', '4', '2'):
        case mmioFOURCC('m', 'p', '4', '3'):
	case mmioFOURCC('M', 'P', 'G', '4'):
	    printf("Video in Microsoft MPEG-4 format\n");   
	    drv_id=4;
	    break;
	case mmioFOURCC('c', 'v', 'i', 'd'):
	    printf("Video in Cinepak format\n");   
	    drv_id=5;
	    break;	
	case mmioFOURCC('V', 'C', 'R', '1'):
	    drv_id=9;
	    break;
	case mmioFOURCC('V', 'C', 'R', '2'):
	    drv_id=10;
	    break;
	case mmioFOURCC('i', '2', '6', '3'):
	case mmioFOURCC('I', '2', '6', '3'):
	    drv_id=11;
	    break;	    	  
//	case mmioFOURCC('Y', 'V', 'U', '9'):
//	    drv_id=12;
//	    break;  
	default:
	    printf("Unknown codec %X='%c%c%c%c'\n", fccHandler, 
	    fccHandler&0xFF, (fccHandler&0xFF00)>>8,
    	    (fccHandler&0xFF0000)>>16, (fccHandler&0xFF000000)>>24);
    	    return (HDRVR)0;	
        }
    else
	switch(fccHandler)
	{
    	case 0x160://DivX audio
    	case 0x161://DivX audio
	    drv_id=7; 
	    break;
	case 0x2://MS ADPCM
	    drv_id=8;
	    break;
	case 0x55://MPEG Layer 3
	printf("MPEG Layer 3 ( 0x55 )\n");
	    drv_id=12;
	    break;
	default:
	    printf("Unknown ACM codec 0x%X\n", fccHandler);
	    return (HDRVR)0;
	}
	
    if (!(npDriver = DrvAlloc(&hDriver, &uDrvResult)))
	return ((HDRVR) 0);

    if(avi_codecs[drv_id].handle==0)
    {
     if (!(avi_codecs[drv_id].handle=npDriver->hDriverModule = LoadLibraryA(avi_codecs[drv_id].name)))
     {
     	printf("Can't open library %s\n", avi_codecs[drv_id].name);
        DrvFree(hDriver);
        return ((HDRVR) 0);
     }
     else avi_codecs[drv_id].usage=1;
    }
    else
    {
	npDriver->hDriverModule=avi_codecs[drv_id].handle;
	avi_codecs[drv_id].usage++;
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
         printf("Library %s is not a valid codec\n", avi_codecs[drv_id].name);
         FreeLibrary(npDriver->hDriverModule);
         DrvFree(hDriver);
         return ((HDRVR) 0);
     }
     
    TRACE("DriverProc == %X\n", npDriver->DriverProc);
     npDriver->dwDriverID = ++dwDrvID;

     if (avi_codecs[drv_id].usage==1)
     {
	STORE_ALL;
        (npDriver->DriverProc)(0, hDriver, DRV_LOAD, 0, 0);
	REST_ALL;
	TRACE("DRV_LOAD Ok!\n");
	STORE_ALL;
	(npDriver->DriverProc)(0, hDriver, DRV_ENABLE, 0, 0);
	REST_ALL;
	TRACE("DRV_ENABLE Ok!\n");
     }

     // open driver 
    STORE_ALL;
     npDriver->dwDriverID=(npDriver->DriverProc)(npDriver->dwDriverID, hDriver, DRV_OPEN,
         (LPARAM) (LPSTR) unknown, lParam2);
    REST_ALL;

    TRACE("DRV_OPEN Ok!(%X)\n", npDriver->dwDriverID);

    if (uDrvResult)
    {
         DrvFree(hDriver);
         hDriver = (HDRVR) 0;
     }
     return (hDriver);
}
  
