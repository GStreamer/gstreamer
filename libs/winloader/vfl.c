/*
 * Copyright 1998 Marcus Meissner
 */
#include <stdio.h>
#include <string.h>

#include <wine/winbase.h>
#include <wine/windef.h>
#include <wine/winuser.h>
#include <wine/vfw.h>
#include <wine/winestring.h>
#include <wine/driver.h>
#include <avifmt.h>

#define FIXME_(X) printf
#define FIXME printf

long VFWAPI VideoForWindowsVersion(void);


long VFWAPIV ICDecompress(HIC hic,long dwFlags,LPBITMAPINFOHEADER lpbiFormat,void* lpData,LPBITMAPINFOHEADER lpbi,void* lpBits);

WIN_BOOL VFWAPI	ICInfo(long fccType, long fccHandler, ICINFO * lpicinfo);
LRESULT	VFWAPI	ICGetInfo(HIC hic,ICINFO *picinfo, long cb);
HIC	VFWAPI	ICOpen(long fccType, long fccHandler, UINT wMode);
HIC	VFWAPI	ICOpenFunction(long fccType, long fccHandler, unsigned int wMode, void* lpfnHandler);

LRESULT VFWAPI ICClose(HIC hic);
LRESULT	VFWAPI ICSendMessage(HIC hic, unsigned int msg, long dw1, long dw2);
HIC	VFWAPI ICLocate(long fccType, long fccHandler, LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut, short wFlags);

#define OpenDriverA DrvOpen
extern HDRVR VFWAPI DrvOpen(long);


typedef struct {
    unsigned int               uDriverSignature;
    void*        hDriverModule;
    void*        DriverProc;
    long                dwDriverID;
} DRVR;

/***********************************************************************
 *		VideoForWindowsVersion		[MSVFW.2][MSVIDEO.2]
 * Returns the version in major.minor form.
 * In Windows95 this returns 0x040003b6 (4.950)
 */
long VideoForWindowsVersion(void) {
	return 0x040003B6; /* 4.950 */
}

/* system.ini: [drivers] */

/***********************************************************************
 *		ICInfo				[MSVFW.33]
 * Get information about an installable compressor. Return TRUE if there
 * is one.
 */
int VFWAPI
ICInfo(
	long fccType,		/* [in] type of compressor ('vidc') */
	long fccHandler,	/* [in] <n>th compressor */
	ICINFO *lpicinfo	/* [out] information about compressor */
) {
	char	type[5],buf[2000];

	memcpy(type,&fccType,4);type[4]=0;
	TRACE("(%s,%ld,%p).\n",type,fccHandler,lpicinfo);
	/* does OpenDriver/CloseDriver */
	lpicinfo->dwSize = sizeof(ICINFO);
	lpicinfo->fccType = fccType;
	lpicinfo->dwFlags = 0;
/*
	if (GetPrivateProfileStringA("drivers32",NULL,NULL,buf,2000,"system.ini")) {
		char *s = buf;
		while (*s) {
			if (!lstrncmpiA(type,s,4)) {
				if(!fccHandler--) {
					lpicinfo->fccHandler = mmioStringToFOURCCA(s+5,0);
					return TRUE;
				}
			}
			s=s+lstrlenA(s)+1; 
    		}
	}
*/
	return TRUE;
}

/***********************************************************************
 *		ICOpen				[MSVFW.37]
 * Opens an installable compressor. Return special handle.
 */
HIC VFWAPI
ICOpen(long fccType,long fccHandler,unsigned int wMode) {
	char		type[5],handler[5],codecname[20];
	ICOPEN		icopen;
	HDRVR		hdrv;
	WINE_HIC	*whic;

	memcpy(type,&fccType,4);type[4]=0;
	memcpy(handler,&fccHandler,4);handler[4]=0;
	TRACE("(%s,%s,0x%08lx)\n",type,handler,(long)wMode);
	sprintf(codecname,"%s.%s",type,handler);

	/* Well, lParam2 is in fact a LPVIDEO_OPEN_PARMS, but it has the 
	 * same layout as ICOPEN
	 */
	icopen.fccType		= fccType;
	icopen.fccHandler	= fccHandler;
	icopen.dwSize		= sizeof(ICOPEN);
	icopen.dwFlags		= wMode;
	/* FIXME: do we need to fill out the rest too? */
//	hdrv=OpenDriverA(codecname,"drivers32",(long)&icopen);
	hdrv=OpenDriverA((long)&icopen);
/*
	if (!hdrv) {
	    if (!strcasecmp(type,"vids")) {
		sprintf(codecname,"vidc.%s",handler);
		fccType = mmioFOURCC('v','i','d','c');
	    }
//	    hdrv=OpenDriverA(codecname,"drivers32",(long)&icopen);
	    hdrv=OpenDriverA((long)&icopen);
*/
	if (!hdrv)
	    return 0;
//	}
	whic = (WINE_HIC*)my_mreq(sizeof(WINE_HIC), 0);
	whic->hdrv	= hdrv;
	whic->driverproc= ((DRVR*)hdrv)->DriverProc;
//	whic->private	= ICSendMessage((HIC)whic,DRV_OPEN,0,(long)&icopen);
	whic->private	= ((DRVR*)hdrv)->dwDriverID;
	return (HIC)whic;
}

/***********************************************************************
 *		ICOpenFunction			[MSVFW.38]
 */
HIC VFWAPI ICOpenFunction(long fccType, long fccHandler, unsigned int wMode,
void* lpfnHandler) {
	char		type[5],handler[5];
	HIC		hic;
	WINE_HIC	*whic;

	memcpy(type,&fccType,4);type[4]=0;
	memcpy(handler,&fccHandler,4);handler[4]=0;
	FIXME("(%s,%s,%d,%p), stub!\n",type,handler,wMode,lpfnHandler);
	hic = ICOpen(fccType,fccHandler,wMode);
	if (!hic)
		return hic;
	whic = (WINE_HIC*)hic;
	whic->driverproc = lpfnHandler;
	return hic;
}


/***********************************************************************
 *		ICGetInfo			[MSVFW.30]
 */
LRESULT VFWAPI
ICGetInfo(HIC hic,ICINFO *picinfo,long cb) {
	LRESULT		ret;

	TRACE("(0x%08lx,%p,%ld)\n",(long)hic,picinfo,cb);
	ret = ICSendMessage(hic,ICM_GETINFO,(long)picinfo,cb);
	TRACE("	-> 0x%08lx\n",ret);
	return ret;
}

/***********************************************************************
 *		ICLocate			[MSVFW.35]
 */
HIC  VFWAPI
ICLocate(
	long fccType, long fccHandler, LPBITMAPINFOHEADER lpbiIn,
	LPBITMAPINFOHEADER lpbiOut, short wMode
) {
	char	type[5],handler[5];
	HIC	hic;
	long	querymsg;

	switch (wMode) {
	case ICMODE_FASTCOMPRESS:
	case ICMODE_COMPRESS: 
		querymsg = ICM_COMPRESS_QUERY;
		break;
	case ICMODE_DECOMPRESS:
	case ICMODE_FASTDECOMPRESS:
		querymsg = ICM_DECOMPRESS_QUERY;
		break;
	case ICMODE_DRAW:
		querymsg = ICM_DRAW_QUERY;
		break;
	default:
		FIXME("Unknown mode (%d)\n",wMode);
		return 0;
	}

	/* Easy case: handler/type match, we just fire a query and return */
	hic = ICOpen(fccType,fccHandler,wMode);
	if (hic) {
		if (!ICSendMessage(hic,querymsg,(long)lpbiIn,(long)lpbiOut))
			return hic;
		ICClose(hic);
	}
	type[4]='\0';memcpy(type,&fccType,4);
	handler[4]='\0';memcpy(handler,&fccHandler,4);
	if (fccType==streamtypeVIDEO) {
		hic = ICLocate(ICTYPE_VIDEO,fccHandler,lpbiIn,lpbiOut,wMode);
		if (hic)
			return hic;
	}
	FIXME("(%s,%s,%p,%p,0x%04x),unhandled!\n",type,handler,lpbiIn,lpbiOut,wMode);
	return 0;
}

/***********************************************************************
 *		ICCompress			[MSVFW.23]
 */
long VFWAPIV
ICCompress(
	HIC hic,long dwFlags,LPBITMAPINFOHEADER lpbiOutput,void* lpData,
	LPBITMAPINFOHEADER lpbiInput,void* lpBits,long* lpckid,
	long* lpdwFlags,long lFrameNum,long dwFrameSize,long dwQuality,
	LPBITMAPINFOHEADER lpbiPrev,void* lpPrev
) {
	ICCOMPRESS	iccmp;

	iccmp.dwFlags		= dwFlags;

	iccmp.lpbiOutput	= lpbiOutput;
	iccmp.lpOutput		= lpData;
	iccmp.lpbiInput		= lpbiInput;
	iccmp.lpInput		= lpBits;

	iccmp.lpckid		= lpckid;
	iccmp.lpdwFlags		= lpdwFlags;
	iccmp.lFrameNum		= lFrameNum;
	iccmp.dwFrameSize	= dwFrameSize;
	iccmp.dwQuality		= dwQuality;
	iccmp.lpbiPrev		= lpbiPrev;
	iccmp.lpPrev		= lpPrev;
	return ICSendMessage(hic,ICM_COMPRESS,(long)&iccmp,sizeof(iccmp));
}

/***********************************************************************
 *		ICDecompress			[MSVFW.26]
 */
long VFWAPIV 
ICDecompress(HIC hic,long dwFlags,LPBITMAPINFOHEADER lpbiFormat,void* lpData,LPBITMAPINFOHEADER  lpbi,void* lpBits) {
	ICDECOMPRESS	icd;

	icd.dwFlags	= dwFlags;
	icd.lpbiInput	= lpbiFormat;
	icd.lpInput	= lpData;

	icd.lpbiOutput	= lpbi;
	icd.lpOutput	= lpBits;
	icd.ckid	= 0;
	return ICSendMessage(hic,ICM_DECOMPRESS,(long)&icd,sizeof(icd));
}

/***********************************************************************
 *		ICSendMessage			[MSVFW.40]
 */
LRESULT VFWAPI
ICSendMessage(HIC hic,unsigned int msg,long lParam1,long lParam2) {
	LRESULT		ret;
	WINE_HIC	*whic = (WINE_HIC*)hic;

#define XX(x) case x: TRACE("(0x%08lx,"#x",0x%08lx,0x%08lx)\n",(long)hic,lParam1,lParam2);break;
/*
	switch (msg) {
	XX(ICM_ABOUT)
	XX(ICM_GETINFO)
	XX(ICM_COMPRESS_FRAMES_INFO)
	XX(ICM_COMPRESS_GET_FORMAT)
	XX(ICM_COMPRESS_GET_SIZE)
	XX(ICM_COMPRESS_QUERY)
	XX(ICM_COMPRESS_BEGIN)
	XX(ICM_COMPRESS)
	XX(ICM_COMPRESS_END)
	XX(ICM_DECOMPRESS_GET_FORMAT)
	XX(ICM_DECOMPRESS_QUERY)
	XX(ICM_DECOMPRESS_BEGIN)
	XX(ICM_DECOMPRESS)
	XX(ICM_DECOMPRESS_END)
	XX(ICM_DECOMPRESS_SET_PALETTE)
	XX(ICM_DECOMPRESS_GET_PALETTE)
	XX(ICM_DRAW_QUERY)
	XX(ICM_DRAW_BEGIN)
	XX(ICM_DRAW_GET_PALETTE)
	XX(ICM_DRAW_START)
	XX(ICM_DRAW_STOP)
	XX(ICM_DRAW_END)
	XX(ICM_DRAW_GETTIME)
	XX(ICM_DRAW)
	XX(ICM_DRAW_WINDOW)
	XX(ICM_DRAW_SETTIME)
	XX(ICM_DRAW_REALIZE)
	XX(ICM_DRAW_FLUSH)
	XX(ICM_DRAW_RENDERBUFFER)
	XX(ICM_DRAW_START_PLAY)
	XX(ICM_DRAW_STOP_PLAY)
	XX(ICM_DRAW_SUGGESTFORMAT)
	XX(ICM_DRAW_CHANGEPALETTE)
	XX(ICM_GETBUFFERSWANTED)
	XX(ICM_GETDEFAULTKEYFRAMERATE)
	XX(ICM_DECOMPRESSEX_BEGIN)
	XX(ICM_DECOMPRESSEX_QUERY)
	XX(ICM_DECOMPRESSEX)
	XX(ICM_DECOMPRESSEX_END)
	XX(ICM_SET_STATUS_PROC)
	default:
		FIXME("(0x%08lx,0x%08lx,0x%08lx,0x%08lx)\n",(long)hic,(long)msg,lParam1,lParam2);
	}
*/
//	if (whic->driverproc) {
//		FIXME("(0x%08lx,0x%08lx,0x%08lx,0x%08lx), calling %p\n",(long)hic,(long)msg,lParam1,lParam2,whic->driverproc);
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
//	printf("private=%x\n", whic->private);
    STORE_ALL;	
	ret = whic->driverproc(whic->private,1,msg,lParam1,lParam2);
    REST_ALL;	
//	} else

//		ret = SendDriverMessage(whic->hdrv,msg,lParam1,lParam2);
//	TRACE("	-> 0x%08lx\n",ret);
	return ret;
}


/***********************************************************************
 *		ICClose			[MSVFW.22]
 */
LRESULT VFWAPI ICClose(HIC hic) {
	WINE_HIC	*whic = (WINE_HIC*)hic;
	TRACE("(%d).\n",hic);
	/* FIXME: correct? */
//	CloseDriver(whic->hdrv,0,0);
        DrvClose(whic->hdrv);
//#warning FIXME: DrvClose
	my_release(whic);
	return 0;
}
int VFWAPI ICDoSomething()
{
}

