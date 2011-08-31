/**********************************************************************
 *
 * Copyright (C) Imagination Technologies Ltd. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#ifndef __SERVICES_H__
#define __SERVICES_H__

#if defined (__cplusplus)
extern "C" {
#endif

#include "img_defs.h"
#include "servicesext.h"

#define PVRSRV_4K_PAGE_SIZE		4096UL

#define PVRSRV_MAX_CMD_SIZE		1024

#define PVRSRV_MAX_DEVICES		16	

#define EVENTOBJNAME_MAXLENGTH (50)

#define PVRSRV_MEM_READ						(1U<<0)
#define PVRSRV_MEM_WRITE					(1U<<1)
#define PVRSRV_MEM_CACHE_CONSISTENT			(1U<<2)
#define PVRSRV_MEM_NO_SYNCOBJ				(1U<<3)
#define PVRSRV_MEM_INTERLEAVED				(1U<<4)
#define PVRSRV_MEM_DUMMY					(1U<<5)
#define PVRSRV_MEM_EDM_PROTECT				(1U<<6)
#define PVRSRV_MEM_ZERO						(1U<<7)
#define PVRSRV_MEM_USER_SUPPLIED_DEVVADDR	(1U<<8)
#define PVRSRV_MEM_RAM_BACKED_ALLOCATION	(1U<<9)
#define PVRSRV_MEM_NO_RESMAN				(1U<<10)
#define PVRSRV_MEM_EXPORTED					(1U<<11)


#define PVRSRV_HAP_CACHED					(1U<<12)
#define PVRSRV_HAP_UNCACHED					(1U<<13)
#define PVRSRV_HAP_SMART					(1U<<20)  /* XXX could we use CACHED|UNCACHED? */
#define PVRSRV_HAP_WRITECOMBINE				(1U<<14)
#define PVRSRV_HAP_CACHETYPE_MASK			(PVRSRV_HAP_CACHED|PVRSRV_HAP_UNCACHED|PVRSRV_HAP_SMART|PVRSRV_HAP_WRITECOMBINE)
#define PVRSRV_HAP_KERNEL_ONLY				(1U<<15)
#define PVRSRV_HAP_SINGLE_PROCESS			(1U<<16)
#define PVRSRV_HAP_MULTI_PROCESS			(1U<<17)
#define PVRSRV_HAP_FROM_EXISTING_PROCESS	(1U<<18)
#define PVRSRV_HAP_NO_CPU_VIRTUAL			(1U<<19)
#define PVRSRV_HAP_GPU_PAGEABLE				(1U<<21)
#define PVRSRV_HAP_MAPTYPE_MASK				(PVRSRV_HAP_KERNEL_ONLY \
                                            |PVRSRV_HAP_SINGLE_PROCESS \
                                            |PVRSRV_HAP_MULTI_PROCESS \
                                            |PVRSRV_HAP_FROM_EXISTING_PROCESS \
                                            |PVRSRV_HAP_NO_CPU_VIRTUAL\
                                            |PVRSRV_HAP_GPU_PAGEABLE)

#define PVRSRV_MEM_CACHED					PVRSRV_HAP_CACHED
#define PVRSRV_MEM_UNCACHED					PVRSRV_HAP_UNCACHED
#define PVRSRV_MEM_SMART					PVRSRV_HAP_SMART
#define PVRSRV_MEM_WRITECOMBINE				PVRSRV_HAP_WRITECOMBINE

#define PVRSRV_MEM_BACKINGSTORE_FIELD_SHIFT	(24)

#define PVRSRV_MAP_NOUSERVIRTUAL            (1UL<<27)
#define PVRSRV_MEM_XPROC  					(1U<<28)

#define PVRSRV_NO_CONTEXT_LOSS					0		
#define PVRSRV_SEVERE_LOSS_OF_CONTEXT			1		
#define PVRSRV_PRE_STATE_CHANGE_MASK			0x80	


#define PVRSRV_DEFAULT_DEV_COOKIE			(1)	 


#define PVRSRV_MISC_INFO_TIMER_PRESENT				(1U<<0)
#define PVRSRV_MISC_INFO_CLOCKGATE_PRESENT			(1U<<1)
#define PVRSRV_MISC_INFO_MEMSTATS_PRESENT			(1U<<2)
#define PVRSRV_MISC_INFO_GLOBALEVENTOBJECT_PRESENT	(1U<<3)
#define PVRSRV_MISC_INFO_DDKVERSION_PRESENT			(1U<<4)
#define PVRSRV_MISC_INFO_CPUCACHEOP_PRESENT			(1U<<5)
#define PVRSRV_MISC_INFO_FREEMEM_PRESENT			(1U<<6)

#define PVRSRV_MISC_INFO_RESET_PRESENT				(1U<<31)

#define PVRSRV_PDUMP_MAX_FILENAME_SIZE			20
#define PVRSRV_PDUMP_MAX_COMMENT_SIZE			200


#define PVRSRV_CHANGEDEVMEM_ATTRIBS_CACHECOHERENT		0x00000001

#define PVRSRV_MAPEXTMEMORY_FLAGS_ALTERNATEVA			0x00000001
#define PVRSRV_MAPEXTMEMORY_FLAGS_PHYSCONTIG			0x00000002

#define PVRSRV_MODIFYSYNCOPS_FLAGS_WO_INC			0x00000001
#define PVRSRV_MODIFYSYNCOPS_FLAGS_RO_INC			0x00000002

#define SRV_FLAGS_PERSIST		0x1
#define SRV_FLAGS_PDUMP_ACTIVE	0x2

#define PVRSRV_PDUMP_FLAGS_CONTINUOUS		0x1

#define PVR_FULL_CACHE_OP_THRESHOLD	(0x7D000)

typedef enum _PVRSRV_DEVICE_TYPE_
{
	PVRSRV_DEVICE_TYPE_UNKNOWN			= 0 ,
	PVRSRV_DEVICE_TYPE_MBX1				= 1 ,
	PVRSRV_DEVICE_TYPE_MBX1_LITE		= 2 ,

	PVRSRV_DEVICE_TYPE_M24VA			= 3,
	PVRSRV_DEVICE_TYPE_MVDA2			= 4,
	PVRSRV_DEVICE_TYPE_MVED1			= 5,
	PVRSRV_DEVICE_TYPE_MSVDX			= 6,

	PVRSRV_DEVICE_TYPE_SGX				= 7,

	PVRSRV_DEVICE_TYPE_VGX				= 8,

	
	PVRSRV_DEVICE_TYPE_EXT				= 9,

    PVRSRV_DEVICE_TYPE_LAST             = 9,

	PVRSRV_DEVICE_TYPE_FORCE_I32		= 0x7fffffff

} PVRSRV_DEVICE_TYPE;

#define HEAP_ID( _dev_ , _dev_heap_idx_ )	(  ((_dev_)<<24) | ((_dev_heap_idx_)&((1<<24)-1))  )
#define HEAP_IDX( _heap_id_ )				( (_heap_id_)&((1<<24) - 1 ) )
#define HEAP_DEV( _heap_id_ )				( (_heap_id_)>>24 )

#define PVRSRV_UNDEFINED_HEAP_ID			(~0LU)

typedef enum
{
	IMG_EGL				= 0x00000001,
	IMG_OPENGLES1		= 0x00000002,
	IMG_OPENGLES2		= 0x00000003,
	IMG_D3DM			= 0x00000004,
	IMG_SRV_UM			= 0x00000005,
	IMG_OPENVG			= 0x00000006,
	IMG_SRVCLIENT		= 0x00000007,
	IMG_VISTAKMD		= 0x00000008,
	IMG_VISTA3DNODE		= 0x00000009,
	IMG_VISTAMVIDEONODE	= 0x0000000A,
	IMG_VISTAVPBNODE	= 0x0000000B,
	IMG_OPENGL			= 0x0000000C,
	IMG_D3D				= 0x0000000D,
#if defined(SUPPORT_GRAPHICS_HAL) || defined(SUPPORT_COMPOSER_HAL)
	IMG_ANDROID_HAL		= 0x0000000E,
#endif
#if defined(SUPPORT_OPENCL)
	IMG_OPENCL			= 0x0000000F,
#endif

} IMG_MODULE_ID;


#define APPHINT_MAX_STRING_SIZE	256

typedef enum
{
	IMG_STRING_TYPE		= 1,
	IMG_FLOAT_TYPE		,
	IMG_UINT_TYPE		,
	IMG_INT_TYPE		,
	IMG_FLAG_TYPE
}IMG_DATA_TYPE;


typedef struct _PVRSRV_DEV_DATA_ *PPVRSRV_DEV_DATA;

typedef struct _PVRSRV_DEVICE_IDENTIFIER_
{
	PVRSRV_DEVICE_TYPE		eDeviceType;		
	PVRSRV_DEVICE_CLASS		eDeviceClass;		
	IMG_UINT32				ui32DeviceIndex;	
	IMG_CHAR				*pszPDumpDevName;	
	IMG_CHAR				*pszPDumpRegName;	

} PVRSRV_DEVICE_IDENTIFIER;


typedef struct _PVRSRV_CLIENT_DEV_DATA_
{
	IMG_UINT32		ui32NumDevices;				
	PVRSRV_DEVICE_IDENTIFIER asDevID[PVRSRV_MAX_DEVICES];		
	PVRSRV_ERROR	(*apfnDevConnect[PVRSRV_MAX_DEVICES])(PPVRSRV_DEV_DATA);	
	PVRSRV_ERROR	(*apfnDumpTrace[PVRSRV_MAX_DEVICES])(PPVRSRV_DEV_DATA);		

} PVRSRV_CLIENT_DEV_DATA;


typedef struct _PVRSRV_CONNECTION_
{
	IMG_HANDLE hServices;					
	IMG_UINT32 ui32ProcessID;				
	PVRSRV_CLIENT_DEV_DATA	sClientDevData;	
	IMG_UINT32 ui32SrvFlags;				
}PVRSRV_CONNECTION;


typedef struct _PVRSRV_DEV_DATA_
{
	IMG_CONST PVRSRV_CONNECTION	 *psConnection;	
#if defined (SUPPORT_SID_INTERFACE)
	IMG_SID			hDevCookie;				
#else
	IMG_HANDLE			hDevCookie;				
#endif

} PVRSRV_DEV_DATA;

typedef struct _PVRSRV_MEMUPDATE_
{
	IMG_UINT32			ui32UpdateAddr;		
	IMG_UINT32			ui32UpdateVal;		
} PVRSRV_MEMUPDATE;

typedef struct _PVRSRV_HWREG_
{
	IMG_UINT32			ui32RegAddr;	
	IMG_UINT32			ui32RegVal;		
} PVRSRV_HWREG;

typedef struct _PVRSRV_MEMBLK_
{
	IMG_DEV_VIRTADDR	sDevVirtAddr;			
	IMG_HANDLE			hOSMemHandle;			
	IMG_HANDLE			hOSWrapMem;				
	IMG_HANDLE			hBuffer;				
	IMG_HANDLE			hResItem;				
	IMG_SYS_PHYADDR	 	*psIntSysPAddr;

} PVRSRV_MEMBLK;

typedef struct _PVRSRV_KERNEL_MEM_INFO_ *PPVRSRV_KERNEL_MEM_INFO;

typedef struct _PVRSRV_CLIENT_MEM_INFO_
{
	
	IMG_PVOID				pvLinAddr;

	
	IMG_PVOID				pvLinAddrKM;

	
	IMG_DEV_VIRTADDR		sDevVAddr;

	




	IMG_CPU_PHYADDR			sCpuPAddr;

	
	IMG_UINT32				ui32Flags;

	


	IMG_UINT32				ui32ClientFlags;

	
	IMG_SIZE_T				uAllocSize;


	
	struct _PVRSRV_CLIENT_SYNC_INFO_	*psClientSyncInfo;

#if defined (SUPPORT_SID_INTERFACE)
	
	IMG_SID								hMappingInfo;

	
	IMG_SID								hKernelMemInfo;

	
	IMG_SID								hResItem;
#else
	
	IMG_HANDLE							hMappingInfo;

	
	IMG_HANDLE							hKernelMemInfo;

	
	IMG_HANDLE							hResItem;
#endif

#if defined(SUPPORT_MEMINFO_IDS)
	#if !defined(USE_CODE)
	
	IMG_UINT64							ui64Stamp;
	#else 
	IMG_UINT32							dummy1;
	IMG_UINT32							dummy2;
	#endif 
#endif 

	


	struct _PVRSRV_CLIENT_MEM_INFO_		*psNext;

} PVRSRV_CLIENT_MEM_INFO, *PPVRSRV_CLIENT_MEM_INFO;


#define PVRSRV_MAX_CLIENT_HEAPS (32)
typedef struct _PVRSRV_HEAP_INFO_
{
	IMG_UINT32			ui32HeapID;
#if defined (SUPPORT_SID_INTERFACE)
	IMG_SID 			hDevMemHeap;
#else
	IMG_HANDLE 			hDevMemHeap;
#endif
	IMG_DEV_VIRTADDR	sDevVAddrBase;
	IMG_UINT32			ui32HeapByteSize;
	IMG_UINT32			ui32Attribs;
	IMG_UINT32			ui32XTileStride;
}PVRSRV_HEAP_INFO;




typedef struct _PVRSRV_EVENTOBJECT_
{
	
	IMG_CHAR	szName[EVENTOBJNAME_MAXLENGTH];
	
#if defined (SUPPORT_SID_INTERFACE)
	IMG_SID     hOSEventKM;
#else
	IMG_HANDLE	hOSEventKM;
#endif

} PVRSRV_EVENTOBJECT;

typedef enum
{
	PVRSRV_MISC_INFO_CPUCACHEOP_NONE = 0,
	PVRSRV_MISC_INFO_CPUCACHEOP_CLEAN,
	PVRSRV_MISC_INFO_CPUCACHEOP_FLUSH,
	PVRSRV_MISC_INFO_CPUCACHEOP_CUSTOM_FLUSH,
	PVRSRV_MISC_INFO_CPUCACHEOP_CUSTOM_INV
} PVRSRV_MISC_INFO_CPUCACHEOP_TYPE;

typedef struct _PVRSRV_MISC_INFO_
{
	IMG_UINT32	ui32StateRequest;		
	IMG_UINT32	ui32StatePresent;		

	
	IMG_VOID	*pvSOCTimerRegisterKM;
	IMG_VOID	*pvSOCTimerRegisterUM;
#if defined (SUPPORT_SID_INTERFACE)
	IMG_SID		hSOCTimerRegisterOSMemHandle;
	IMG_SID		hSOCTimerRegisterMappingInfo;
#else
	IMG_HANDLE	hSOCTimerRegisterOSMemHandle;
	IMG_HANDLE	hSOCTimerRegisterMappingInfo;
#endif

	
	IMG_VOID	*pvSOCClockGateRegs;
	IMG_UINT32	ui32SOCClockGateRegsSize;

	
	IMG_CHAR	*pszMemoryStr;
	IMG_UINT32	ui32MemoryStrLen;

	
	PVRSRV_EVENTOBJECT	sGlobalEventObject;
#if defined (SUPPORT_SID_INTERFACE)
	IMG_EVENTSID		hOSGlobalEvent;
#else
	IMG_HANDLE			hOSGlobalEvent;
#endif

	
	IMG_UINT32	aui32DDKVersion[4];

	
	struct
	{
		
		IMG_BOOL bDeferOp;

		
		PVRSRV_MISC_INFO_CPUCACHEOP_TYPE eCacheOpType;

		
#if !defined (SUPPORT_SID_INTERFACE)
		union
		{
			
			PVRSRV_CLIENT_MEM_INFO *psClientMemInfo;

			
			struct _PVRSRV_KERNEL_MEM_INFO_ *psKernelMemInfo;
		} u;
#endif

		
		IMG_VOID *pvBaseVAddr;

		
		IMG_UINT32	ui32Length;
	} sCacheOpCtl;
} PVRSRV_MISC_INFO;

typedef struct _PVRSRV_SYNC_TOKEN_
{
	

	struct
	{
#if defined (SUPPORT_SID_INTERFACE)
		IMG_SID    hKernelSyncInfo;
#else
		IMG_HANDLE hKernelSyncInfo;
#endif
		IMG_UINT32 ui32ReadOpsPendingSnapshot;
		IMG_UINT32 ui32WriteOpsPendingSnapshot;
	} sPrivate;
} PVRSRV_SYNC_TOKEN;


typedef enum _PVRSRV_CLIENT_EVENT_
{
	PVRSRV_CLIENT_EVENT_HWTIMEOUT = 0,
} PVRSRV_CLIENT_EVENT;

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVClientEvent(IMG_CONST PVRSRV_CLIENT_EVENT eEvent,
											PVRSRV_DEV_DATA *psDevData,
											IMG_PVOID pvData);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVConnect(PVRSRV_CONNECTION **ppsConnection, IMG_UINT32 ui32SrvFlags);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDisconnect(IMG_CONST PVRSRV_CONNECTION *psConnection);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVEnumerateDevices(IMG_CONST PVRSRV_CONNECTION 			*psConnection,
													IMG_UINT32 					*puiNumDevices,
													PVRSRV_DEVICE_IDENTIFIER 	*puiDevIDs);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAcquireDeviceData(IMG_CONST PVRSRV_CONNECTION 	*psConnection,
													IMG_UINT32			uiDevIndex,
													PVRSRV_DEV_DATA		*psDevData,
													PVRSRV_DEVICE_TYPE	eDeviceType);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetMiscInfo (IMG_CONST PVRSRV_CONNECTION *psConnection, PVRSRV_MISC_INFO *psMiscInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVReleaseMiscInfo (IMG_CONST PVRSRV_CONNECTION *psConnection, PVRSRV_MISC_INFO *psMiscInfo);

#if 1
IMG_IMPORT
IMG_UINT32 ReadHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset);

IMG_IMPORT
IMG_VOID WriteHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset, IMG_UINT32 ui32Value);

IMG_IMPORT IMG_VOID WriteHWRegs(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Count, PVRSRV_HWREG *psHWRegs);
#endif

IMG_IMPORT
PVRSRV_ERROR PVRSRVPollForValue ( const PVRSRV_CONNECTION *psConnection,
#if defined (SUPPORT_SID_INTERFACE)
							IMG_SID    hOSEvent,
#else
							IMG_HANDLE hOSEvent,
#endif
							volatile IMG_UINT32 *pui32LinMemAddr,
							IMG_UINT32 ui32Value,
							IMG_UINT32 ui32Mask,
							IMG_UINT32 ui32Waitus,
							IMG_UINT32 ui32Tries);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateDeviceMemContext(IMG_CONST PVRSRV_DEV_DATA *psDevData,
#if defined (SUPPORT_SID_INTERFACE)
											IMG_SID    *phDevMemContext,
#else
											IMG_HANDLE *phDevMemContext,
#endif
											IMG_UINT32 *pui32SharedHeapCount,
											PVRSRV_HEAP_INFO *psHeapInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyDeviceMemContext(IMG_CONST PVRSRV_DEV_DATA *psDevData,
#if defined (SUPPORT_SID_INTERFACE)
											IMG_SID 			hDevMemContext);
#else
											IMG_HANDLE 			hDevMemContext);
#endif

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDeviceMemHeapInfo(IMG_CONST PVRSRV_DEV_DATA *psDevData,
#if defined (SUPPORT_SID_INTERFACE)
											IMG_SID     hDevMemContext,
#else
											IMG_HANDLE hDevMemContext,
#endif
											IMG_UINT32 *pui32SharedHeapCount,
											PVRSRV_HEAP_INFO *psHeapInfo);

#if defined(PVRSRV_LOG_MEMORY_ALLOCS)
	#define PVRSRVAllocDeviceMem_log(psDevData, hDevMemHeap, ui32Attribs, ui32Size, ui32Alignment, ppsMemInfo, logStr) \
		(PVR_TRACE(("PVRSRVAllocDeviceMem(" #psDevData "," #hDevMemHeap "," #ui32Attribs "," #ui32Size "," #ui32Alignment "," #ppsMemInfo ")" \
			": " logStr " (size = 0x%lx)", ui32Size)), \
		PVRSRVAllocDeviceMem(psDevData, hDevMemHeap, ui32Attribs, ui32Size, ui32Alignment, ppsMemInfo))
#else
	#define PVRSRVAllocDeviceMem_log(psDevData, hDevMemHeap, ui32Attribs, ui32Size, ui32Alignment, ppsMemInfo, logStr) \
		PVRSRVAllocDeviceMem(psDevData, hDevMemHeap, ui32Attribs, ui32Size, ui32Alignment, ppsMemInfo)
#endif


IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAllocDeviceMem(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
#if defined (SUPPORT_SID_INTERFACE)
									IMG_SID			hDevMemHeap,
#else
									IMG_HANDLE		hDevMemHeap,
#endif
									IMG_UINT32		ui32Attribs,
									IMG_SIZE_T		ui32Size,
									IMG_SIZE_T		ui32Alignment,
									PVRSRV_CLIENT_MEM_INFO	**ppsMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVFreeDeviceMem(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
								PVRSRV_CLIENT_MEM_INFO		*psMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVRemapToDev(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
								PVRSRV_CLIENT_MEM_INFO		*psMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapFromDev(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
								PVRSRV_CLIENT_MEM_INFO		*psMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVExportDeviceMem(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
												PVRSRV_CLIENT_MEM_INFO		*psMemInfo,
#if defined (SUPPORT_SID_INTERFACE)
												IMG_SID						*phMemInfo);
#else
												IMG_HANDLE					*phMemInfo);
#endif

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVReserveDeviceVirtualMem(IMG_CONST PVRSRV_DEV_DATA *psDevData,
#if defined (SUPPORT_SID_INTERFACE)
											IMG_SID				hDevMemHeap,
#else
											IMG_HANDLE			hDevMemHeap,
#endif
											IMG_DEV_VIRTADDR	*psDevVAddr,
											IMG_SIZE_T			ui32Size,
											IMG_SIZE_T			ui32Alignment,
											PVRSRV_CLIENT_MEM_INFO		**ppsMemInfo);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVFreeDeviceVirtualMem(IMG_CONST PVRSRV_DEV_DATA *psDevData,
													PVRSRV_CLIENT_MEM_INFO *psMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapDeviceMemory (IMG_CONST PVRSRV_DEV_DATA *psDevData,
#if defined (SUPPORT_SID_INTERFACE)
									IMG_SID hKernelMemInfo,
									IMG_SID hDstDevMemHeap,
#else
									IMG_HANDLE hKernelMemInfo,
									IMG_HANDLE hDstDevMemHeap,
#endif
									PVRSRV_CLIENT_MEM_INFO **ppsDstMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapDeviceMemory (IMG_CONST PVRSRV_DEV_DATA *psDevData,
										PVRSRV_CLIENT_MEM_INFO *psMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapExtMemory (IMG_CONST PVRSRV_DEV_DATA	*psDevData,
									PVRSRV_CLIENT_MEM_INFO		*psMemInfo,
									IMG_SYS_PHYADDR				*psSysPAddr,
									IMG_UINT32					ui32Flags);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapExtMemory (IMG_CONST PVRSRV_DEV_DATA *psDevData,
									PVRSRV_CLIENT_MEM_INFO		*psMemInfo,
									IMG_UINT32					ui32Flags);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVWrapExtMemory(IMG_CONST PVRSRV_DEV_DATA *psDevData,
#if defined (SUPPORT_SID_INTERFACE)
												IMG_SID					hDevMemContext,
#else
												IMG_HANDLE				hDevMemContext,
#endif
												IMG_SIZE_T 				ui32ByteSize,
												IMG_SIZE_T				ui32PageOffset,
												IMG_BOOL				bPhysContig,
												IMG_SYS_PHYADDR	 		*psSysPAddr,
												IMG_VOID 				*pvLinAddr,
												IMG_UINT32				ui32Flags,
												PVRSRV_CLIENT_MEM_INFO **ppsMemInfo);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnwrapExtMemory (IMG_CONST PVRSRV_DEV_DATA *psDevData,
												PVRSRV_CLIENT_MEM_INFO *psMemInfo);

PVRSRV_ERROR PVRSRVChangeDeviceMemoryAttributes(IMG_CONST PVRSRV_DEV_DATA			*psDevData,
												PVRSRV_CLIENT_MEM_INFO	*psClientMemInfo,
												IMG_UINT32				ui32Attribs);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapDeviceClassMemory (IMG_CONST PVRSRV_DEV_DATA *psDevData,
#if defined (SUPPORT_SID_INTERFACE)
										IMG_SID hDevMemContext,
										IMG_SID hDeviceClassBuffer,
#else
										IMG_HANDLE hDevMemContext,
										IMG_HANDLE hDeviceClassBuffer,
#endif
										PVRSRV_CLIENT_MEM_INFO **ppsMemInfo);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapDeviceClassMemory (IMG_CONST PVRSRV_DEV_DATA *psDevData,
										PVRSRV_CLIENT_MEM_INFO *psMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapPhysToUserSpace(IMG_CONST PVRSRV_DEV_DATA *psDevData,
									  IMG_SYS_PHYADDR sSysPhysAddr,
									  IMG_UINT32 uiSizeInBytes,
									  IMG_PVOID *ppvUserAddr,
									  IMG_UINT32 *puiActualSize,
									  IMG_PVOID *ppvProcess);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapPhysToUserSpace(IMG_CONST PVRSRV_DEV_DATA *psDevData,
										IMG_PVOID pvUserAddr,
										IMG_PVOID pvProcess);

#if defined(LINUX)
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVExportDeviceMem2(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
												 PVRSRV_CLIENT_MEM_INFO		*psMemInfo,
												 IMG_INT					*iFd);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapDeviceMemory2(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
												 IMG_INT					iFd,
#if defined (SUPPORT_SID_INTERFACE)
												 IMG_SID					hDstDevMemHeap,
#else
												 IMG_HANDLE					hDstDevMemHeap,
#endif
												 PVRSRV_CLIENT_MEM_INFO		**ppsDstMemInfo);
#endif 

typedef enum _PVRSRV_SYNCVAL_MODE_
{
	PVRSRV_SYNCVAL_READ				= IMG_TRUE,
	PVRSRV_SYNCVAL_WRITE			= IMG_FALSE,

} PVRSRV_SYNCVAL_MODE, *PPVRSRV_SYNCVAL_MODE;

typedef IMG_UINT32 PVRSRV_SYNCVAL;

IMG_IMPORT PVRSRV_ERROR PVRSRVWaitForOpsComplete(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode, PVRSRV_SYNCVAL OpRequired);

IMG_IMPORT PVRSRV_ERROR PVRSRVWaitForAllOpsComplete(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode);

IMG_IMPORT IMG_BOOL PVRSRVTestOpsComplete(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode, PVRSRV_SYNCVAL OpRequired);

IMG_IMPORT IMG_BOOL PVRSRVTestAllOpsComplete(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode);

IMG_IMPORT IMG_BOOL PVRSRVTestOpsNotComplete(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode, PVRSRV_SYNCVAL OpRequired);

IMG_IMPORT IMG_BOOL PVRSRVTestAllOpsNotComplete(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode);

IMG_IMPORT PVRSRV_SYNCVAL PVRSRVGetPendingOpSyncVal(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode);


IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVEnumerateDeviceClass(IMG_CONST PVRSRV_CONNECTION *psConnection,
													PVRSRV_DEVICE_CLASS DeviceClass,
													IMG_UINT32 *pui32DevCount,
													IMG_UINT32 *pui32DevID);

IMG_IMPORT
IMG_HANDLE IMG_CALLCONV PVRSRVOpenDCDevice(IMG_CONST PVRSRV_DEV_DATA *psDevData,
											IMG_UINT32 ui32DeviceID);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCloseDCDevice(IMG_CONST PVRSRV_CONNECTION	*psConnection, IMG_HANDLE hDevice);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVEnumDCFormats (IMG_HANDLE hDevice,
											IMG_UINT32		*pui32Count,
											DISPLAY_FORMAT	*psFormat);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVEnumDCDims (IMG_HANDLE hDevice,
										IMG_UINT32 		*pui32Count,
										DISPLAY_FORMAT	*psFormat,
										DISPLAY_DIMS	*psDims);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDCSystemBuffer(IMG_HANDLE hDevice,
#if defined (SUPPORT_SID_INTERFACE)
										IMG_SID *phBuffer);
#else
										IMG_HANDLE *phBuffer);
#endif

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDCInfo(IMG_HANDLE hDevice,
										DISPLAY_INFO* psDisplayInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateDCSwapChain (IMG_HANDLE				hDevice,
													IMG_UINT32				ui32Flags,
													DISPLAY_SURF_ATTRIBUTES	*psDstSurfAttrib,
													DISPLAY_SURF_ATTRIBUTES	*psSrcSurfAttrib,
													IMG_UINT32				ui32BufferCount,
													IMG_UINT32				ui32OEMFlags,
													IMG_UINT32				*pui32SwapChainID,
#if defined (SUPPORT_SID_INTERFACE)
													IMG_SID					*phSwapChain);
#else
													IMG_HANDLE				*phSwapChain);
#endif

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyDCSwapChain (IMG_HANDLE hDevice,
#if defined (SUPPORT_SID_INTERFACE)
											IMG_SID hSwapChain);
#else
											IMG_HANDLE		hSwapChain);
#endif

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSetDCDstRect (IMG_HANDLE hDevice,
#if defined (SUPPORT_SID_INTERFACE)
										IMG_SID		hSwapChain,
#else
										IMG_HANDLE	hSwapChain,
#endif
										IMG_RECT	*psDstRect);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSetDCSrcRect (IMG_HANDLE hDevice,
#if defined (SUPPORT_SID_INTERFACE)
										IMG_SID		hSwapChain,
#else
										IMG_HANDLE	hSwapChain,
#endif
										IMG_RECT	*psSrcRect);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSetDCDstColourKey (IMG_HANDLE hDevice,
#if defined (SUPPORT_SID_INTERFACE)
											IMG_SID		hSwapChain,
#else
											IMG_HANDLE	hSwapChain,
#endif
											IMG_UINT32	ui32CKColour);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSetDCSrcColourKey (IMG_HANDLE hDevice,
#if defined (SUPPORT_SID_INTERFACE)
											IMG_SID		hSwapChain,
#else
											IMG_HANDLE	hSwapChain,
#endif
											IMG_UINT32	ui32CKColour);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDCBuffers(IMG_HANDLE hDevice,
#if defined (SUPPORT_SID_INTERFACE)
									IMG_SID     hSwapChain,
									IMG_SID    *phBuffer);
#else
									IMG_HANDLE hSwapChain,
									IMG_HANDLE *phBuffer);
#endif

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSwapToDCBuffer (IMG_HANDLE hDevice,
#if defined (SUPPORT_SID_INTERFACE)
										IMG_SID    hBuffer,
#else
										IMG_HANDLE hBuffer,
#endif
										IMG_UINT32 ui32ClipRectCount,
										IMG_RECT  *psClipRect,
										IMG_UINT32 ui32SwapInterval,
#if defined (SUPPORT_SID_INTERFACE)
										IMG_SID hPrivateTag);
#else
										IMG_HANDLE hPrivateTag);
#endif

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSwapToDCSystem (IMG_HANDLE hDevice,
#if defined (SUPPORT_SID_INTERFACE)
										IMG_SID hSwapChain);
#else
										IMG_HANDLE hSwapChain);
#endif


IMG_IMPORT
IMG_HANDLE IMG_CALLCONV PVRSRVOpenBCDevice(IMG_CONST PVRSRV_DEV_DATA *psDevData,
											IMG_UINT32 ui32DeviceID);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCloseBCDevice(IMG_CONST PVRSRV_CONNECTION *psConnection,
												IMG_HANDLE hDevice);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetBCBufferInfo(IMG_HANDLE hDevice,
												BUFFER_INFO	*psBuffer);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetBCBuffer(IMG_HANDLE hDevice,
												IMG_UINT32 ui32BufferIndex,
#if defined (SUPPORT_SID_INTERFACE)
												IMG_SID   *phBuffer);
#else
												IMG_HANDLE *phBuffer);
#endif


IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpInit(IMG_CONST PVRSRV_CONNECTION *psConnection);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpStartInitPhase(IMG_CONST PVRSRV_CONNECTION *psConnection);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpStopInitPhase(IMG_CONST PVRSRV_CONNECTION *psConnection);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpSyncPol(IMG_CONST PVRSRV_CONNECTION *psConnection,
#if defined (SUPPORT_SID_INTERFACE)
											 IMG_SID    hKernelSyncInfo,
#else
											 PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo,
#endif
											 IMG_BOOL   bIsRead,
											 IMG_UINT32 ui32Value,
											 IMG_UINT32 ui32Mask);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpSyncPol2(IMG_CONST PVRSRV_CONNECTION *psConnection,
#if defined (SUPPORT_SID_INTERFACE)
											 IMG_SID  hKernelSyncInfo,
#else
											 PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo,
#endif
											 IMG_BOOL bIsRead);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpMem(IMG_CONST PVRSRV_CONNECTION *psConnection,
									IMG_PVOID pvAltLinAddr,
									PVRSRV_CLIENT_MEM_INFO *psMemInfo,
									IMG_UINT32 ui32Offset,
									IMG_UINT32 ui32Bytes,
									IMG_UINT32 ui32Flags);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpSync(IMG_CONST PVRSRV_CONNECTION *psConnection,
										IMG_PVOID pvAltLinAddr,
										PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo,
										IMG_UINT32 ui32Offset,
										IMG_UINT32 ui32Bytes);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpReg(IMG_CONST PVRSRV_DEV_DATA *psDevData,
										 IMG_CHAR *pszRegRegion,
											IMG_UINT32 ui32RegAddr,
											IMG_UINT32 ui32RegValue,
											IMG_UINT32 ui32Flags);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpRegPolWithFlags(const PVRSRV_DEV_DATA *psDevData,
													 IMG_CHAR *pszRegRegion,
													 IMG_UINT32 ui32RegAddr,
													 IMG_UINT32 ui32RegValue,
													 IMG_UINT32 ui32Mask,
													 IMG_UINT32 ui32Flags);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpRegPol(const PVRSRV_DEV_DATA *psDevData,
											IMG_CHAR *pszRegRegion,
											IMG_UINT32 ui32RegAddr,
											IMG_UINT32 ui32RegValue,
											IMG_UINT32 ui32Mask);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpPDReg(IMG_CONST PVRSRV_CONNECTION *psConnection,
											IMG_UINT32 ui32RegAddr,
											IMG_UINT32 ui32RegValue);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpPDDevPAddr(IMG_CONST PVRSRV_CONNECTION *psConnection,
												PVRSRV_CLIENT_MEM_INFO *psMemInfo,
												IMG_UINT32 ui32Offset,
												IMG_DEV_PHYADDR sPDDevPAddr);

#if !defined(USE_CODE)
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpMemPages(IMG_CONST PVRSRV_DEV_DATA *psDevData,
#if defined (SUPPORT_SID_INTERFACE)
														   IMG_SID				hKernelMemInfo,
#else
														   IMG_HANDLE			hKernelMemInfo,
#endif
														   IMG_DEV_PHYADDR		*pPages,
														   IMG_UINT32			ui32NumPages,
												   		   IMG_DEV_VIRTADDR		sDevVAddr,
														   IMG_UINT32			ui32Start,
														   IMG_UINT32			ui32Length,
														   IMG_UINT32			ui32Flags);
#endif

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpSetFrame(IMG_CONST PVRSRV_CONNECTION *psConnection,
											  IMG_UINT32 ui32Frame);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpComment(IMG_CONST PVRSRV_CONNECTION *psConnection,
											 IMG_CONST IMG_CHAR *pszComment,
											 IMG_BOOL bContinuous);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpCommentf(IMG_CONST PVRSRV_CONNECTION *psConnection,
											  IMG_BOOL bContinuous,
											  IMG_CONST IMG_CHAR *pszFormat, ...)
#if !defined(USE_CODE)
											  IMG_FORMAT_PRINTF(3, 4)
#endif
;

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpCommentWithFlagsf(IMG_CONST PVRSRV_CONNECTION *psConnection,
													   IMG_UINT32 ui32Flags,
													   IMG_CONST IMG_CHAR *pszFormat, ...)
#if !defined(USE_CODE)
													   IMG_FORMAT_PRINTF(3, 4)
#endif
;

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpDriverInfo(IMG_CONST PVRSRV_CONNECTION *psConnection,
								 				IMG_CHAR *pszString,
												IMG_BOOL bContinuous);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpIsCapturing(IMG_CONST PVRSRV_CONNECTION *psConnection,
								 				IMG_BOOL *pbIsCapturing);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpRegRead(IMG_CONST PVRSRV_DEV_DATA *psDevData,
											IMG_CONST IMG_CHAR *pszRegRegion,
								 			IMG_CONST IMG_CHAR *pszFileName,
											IMG_UINT32 ui32FileOffset,
											IMG_UINT32 ui32Address,
											IMG_UINT32 ui32Size,
											IMG_UINT32 ui32PDumpFlags);


IMG_IMPORT
IMG_BOOL IMG_CALLCONV PVRSRVPDumpIsCapturingTest(IMG_CONST PVRSRV_CONNECTION *psConnection);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpCycleCountRegRead(IMG_CONST PVRSRV_DEV_DATA *psDevData,
														IMG_UINT32 ui32RegOffset,
														IMG_BOOL bLastFrame);

IMG_IMPORT IMG_HANDLE	PVRSRVLoadLibrary(const IMG_CHAR *pszLibraryName);
IMG_IMPORT PVRSRV_ERROR	PVRSRVUnloadLibrary(IMG_HANDLE hExtDrv);
IMG_IMPORT PVRSRV_ERROR	PVRSRVGetLibFuncAddr(IMG_HANDLE hExtDrv, const IMG_CHAR *pszFunctionName, IMG_VOID **ppvFuncAddr);

IMG_IMPORT IMG_UINT32 PVRSRVClockus (void);
IMG_IMPORT IMG_VOID PVRSRVWaitus (IMG_UINT32 ui32Timeus);
IMG_IMPORT IMG_VOID PVRSRVReleaseThreadQuanta (void);
IMG_IMPORT IMG_UINT32 IMG_CALLCONV PVRSRVGetCurrentProcessID(void);
IMG_IMPORT IMG_CHAR * IMG_CALLCONV PVRSRVSetLocale(const IMG_CHAR *pszLocale);





IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVCreateAppHintState(IMG_MODULE_ID eModuleID,
														const IMG_CHAR *pszAppName,
														IMG_VOID **ppvState);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVFreeAppHintState(IMG_MODULE_ID eModuleID,
										 IMG_VOID *pvHintState);

IMG_IMPORT IMG_BOOL IMG_CALLCONV PVRSRVGetAppHint(IMG_VOID			*pvHintState,
												  const IMG_CHAR	*pszHintName,
												  IMG_DATA_TYPE		eDataType,
												  const IMG_VOID	*pvDefault,
												  IMG_VOID			*pvReturn);

IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVAllocUserModeMem (IMG_SIZE_T ui32Size);
IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVCallocUserModeMem (IMG_SIZE_T ui32Size);
IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVReallocUserModeMem (IMG_PVOID pvBase, IMG_SIZE_T uNewSize);
IMG_IMPORT IMG_VOID  IMG_CALLCONV PVRSRVFreeUserModeMem (IMG_PVOID pvMem);
IMG_IMPORT IMG_VOID PVRSRVMemCopy(IMG_VOID *pvDst, const IMG_VOID *pvSrc, IMG_SIZE_T ui32Size);
IMG_IMPORT IMG_VOID PVRSRVMemSet(IMG_VOID *pvDest, IMG_UINT8 ui8Value, IMG_SIZE_T ui32Size);

struct _PVRSRV_MUTEX_OPAQUE_STRUCT_;
typedef	struct  _PVRSRV_MUTEX_OPAQUE_STRUCT_ *PVRSRV_MUTEX_HANDLE;

IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateMutex(PVRSRV_MUTEX_HANDLE *phMutex);
IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyMutex(PVRSRV_MUTEX_HANDLE hMutex);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVLockMutex(PVRSRV_MUTEX_HANDLE hMutex);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVUnlockMutex(PVRSRV_MUTEX_HANDLE hMutex);

IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVLockProcessGlobalMutex(void);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVUnlockProcessGlobalMutex(void);


struct _PVRSRV_SEMAPHORE_OPAQUE_STRUCT_;
typedef	struct  _PVRSRV_SEMAPHORE_OPAQUE_STRUCT_ *PVRSRV_SEMAPHORE_HANDLE;


  	#define IMG_SEMAPHORE_WAIT_INFINITE       ((IMG_UINT64)0xFFFFFFFFFFFFFFFFull)


#if !defined(USE_CODE)

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVCreateSemaphore)
#endif
static INLINE PVRSRV_ERROR PVRSRVCreateSemaphore(PVRSRV_SEMAPHORE_HANDLE *phSemaphore, IMG_INT iInitialCount)
{
	PVR_UNREFERENCED_PARAMETER(iInitialCount);
	*phSemaphore = 0;
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVDestroySemaphore)
#endif
static INLINE PVRSRV_ERROR PVRSRVDestroySemaphore(PVRSRV_SEMAPHORE_HANDLE hSemaphore)
{
	PVR_UNREFERENCED_PARAMETER(hSemaphore);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVWaitSemaphore)
#endif
static INLINE PVRSRV_ERROR PVRSRVWaitSemaphore(PVRSRV_SEMAPHORE_HANDLE hSemaphore, IMG_UINT64 ui64TimeoutMicroSeconds)
{
	PVR_UNREFERENCED_PARAMETER(hSemaphore);
	PVR_UNREFERENCED_PARAMETER(ui64TimeoutMicroSeconds);
	return PVRSRV_ERROR_INVALID_PARAMS;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPostSemaphore)
#endif
static INLINE IMG_VOID PVRSRVPostSemaphore(PVRSRV_SEMAPHORE_HANDLE hSemaphore, IMG_INT iPostCount)
{
	PVR_UNREFERENCED_PARAMETER(hSemaphore);
	PVR_UNREFERENCED_PARAMETER(iPostCount);
}

#endif 


#if (defined(DEBUG) && defined(__linux__))
IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVAllocUserModeMemTracking(IMG_SIZE_T ui32Size, IMG_CHAR *pszFileName, IMG_UINT32 ui32LineNumber);

IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVCallocUserModeMemTracking(IMG_SIZE_T ui32Size, IMG_CHAR *pszFileName, IMG_UINT32 ui32LineNumber);

IMG_IMPORT IMG_VOID  IMG_CALLCONV PVRSRVFreeUserModeMemTracking(IMG_VOID *pvMem);

IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVReallocUserModeMemTracking(IMG_VOID *pvMem, IMG_SIZE_T ui32NewSize, 
													  IMG_CHAR *pszFileName, IMG_UINT32 ui32LineNumber);
#endif 

IMG_IMPORT PVRSRV_ERROR PVRSRVEventObjectWait(const PVRSRV_CONNECTION *psConnection,
#if defined (SUPPORT_SID_INTERFACE)
									IMG_EVENTSID hOSEvent);
#else
									IMG_HANDLE hOSEvent);
#endif

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateSyncInfoModObj(const PVRSRV_CONNECTION *psConnection,
#if defined (SUPPORT_SID_INTERFACE)
													 IMG_SID *phKernelSyncInfoModObj);
#else
													 IMG_HANDLE *phKernelSyncInfoModObj);
#endif

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroySyncInfoModObj(const PVRSRV_CONNECTION *psConnection,
#if defined (SUPPORT_SID_INTERFACE)
													  IMG_SID hKernelSyncInfoModObj);
#else
													  IMG_HANDLE hKernelSyncInfoModObj);
#endif



IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVModifyPendingSyncOps(const PVRSRV_CONNECTION *psConnection,
#if defined (SUPPORT_SID_INTERFACE)
													  IMG_SID    hKernelSyncInfoModObj,
#else
													  IMG_HANDLE hKernelSyncInfoModObj,
#endif
													  PVRSRV_CLIENT_SYNC_INFO *psSyncInfo,
													  IMG_UINT32 ui32ModifyFlags,
													  IMG_UINT32 *pui32ReadOpsPending,
													  IMG_UINT32 *pui32WriteOpsPending);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVModifyCompleteSyncOps(const PVRSRV_CONNECTION *psConnection,
#if defined (SUPPORT_SID_INTERFACE)
													  IMG_SID hKernelSyncInfoModObj);
#else
													  IMG_HANDLE hKernelSyncInfoModObj);
#endif

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSyncOpsTakeToken(const PVRSRV_CONNECTION *psConnection,
#if defined (SUPPORT_SID_INTERFACE)
												 const IMG_SID      hKernelSyncInfo,
#else
												 const PVRSRV_CLIENT_SYNC_INFO *psSyncInfo,
#endif
												 PVRSRV_SYNC_TOKEN *psSyncToken);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSyncOpsFlushToToken(const PVRSRV_CONNECTION *psConnection,
#if defined (SUPPORT_SID_INTERFACE)
													const IMG_SID            hKernelSyncInfo,
#else
													const PVRSRV_CLIENT_SYNC_INFO *psSyncInfo,
#endif
													const PVRSRV_SYNC_TOKEN *psSyncToken,
													IMG_BOOL bWait);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSyncOpsFlushToModObj(const PVRSRV_CONNECTION *psConnection,
#if defined (SUPPORT_SID_INTERFACE)
													 IMG_SID  hKernelSyncInfoModObj,
#else
													 IMG_HANDLE hKernelSyncInfoModObj,
#endif
													 IMG_BOOL bWait);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSyncOpsFlushToDelta(const PVRSRV_CONNECTION *psConnection,
													PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo,
													IMG_UINT32 ui32Delta,
													IMG_BOOL bWait);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAllocSyncInfo(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
											  PVRSRV_CLIENT_SYNC_INFO **ppsSyncInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVFreeSyncInfo(IMG_CONST PVRSRV_DEV_DATA *psDevData,
											 PVRSRV_CLIENT_SYNC_INFO *psSyncInfo);

IMG_IMPORT
const IMG_CHAR *PVRSRVGetErrorString(PVRSRV_ERROR eError);


#define TIME_NOT_PASSED_UINT32(a,b,c)		(((a) - (b)) < (c))

#if defined (__cplusplus)
}
#endif
#endif 

