/**********************************************************************
*
* Copyright(c) Imagination Technologies Ltd.
*
* The contents of this file are subject to the MIT license as set out below.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
* 
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
* DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
* OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
* OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
* 
* This License is also included in this distribution in the file called 
* "COPYING".
* 
******************************************************************************/



#ifndef __IMG_TYPES_H__
#define __IMG_TYPES_H__

#if !defined(IMG_ADDRSPACE_CPUVADDR_BITS)
#define IMG_ADDRSPACE_CPUVADDR_BITS		32
#endif

#if !defined(IMG_ADDRSPACE_PHYSADDR_BITS)
#define IMG_ADDRSPACE_PHYSADDR_BITS		32
#endif

typedef unsigned int	IMG_UINT,	*IMG_PUINT;
typedef signed int		IMG_INT,	*IMG_PINT;

typedef unsigned char	IMG_UINT8,	*IMG_PUINT8;
typedef unsigned char	IMG_BYTE,	*IMG_PBYTE;
typedef signed char		IMG_INT8,	*IMG_PINT8;
typedef char			IMG_CHAR,	*IMG_PCHAR;

typedef unsigned short	IMG_UINT16,	*IMG_PUINT16;
typedef signed short	IMG_INT16,	*IMG_PINT16;
#if !defined(IMG_UINT32_IS_ULONG)
typedef unsigned int	IMG_UINT32,	*IMG_PUINT32;
typedef signed int		IMG_INT32,	*IMG_PINT32;
#else
typedef unsigned long	IMG_UINT32,	*IMG_PUINT32;
typedef signed long		IMG_INT32,	*IMG_PINT32;
#endif
#if !defined(IMG_UINT32_MAX)
	#define IMG_UINT32_MAX 0xFFFFFFFFUL
#endif

	#if (defined(LINUX) || defined(__METAG))
#if !defined(USE_CODE)
		typedef unsigned long long		IMG_UINT64,	*IMG_PUINT64;
		typedef long long 				IMG_INT64,	*IMG_PINT64;
#endif
	#else

		#error("define an OS")

	#endif

#if !(defined(LINUX) && defined (__KERNEL__))
typedef float			IMG_FLOAT,	*IMG_PFLOAT;
typedef double			IMG_DOUBLE, *IMG_PDOUBLE;
#endif

typedef	enum tag_img_bool
{
	IMG_FALSE		= 0,
	IMG_TRUE		= 1,
	IMG_FORCE_ALIGN = 0x7FFFFFFF
} IMG_BOOL, *IMG_PBOOL;

typedef void            IMG_VOID, *IMG_PVOID;

typedef IMG_INT32       IMG_RESULT;

#if defined(_WIN64)
typedef unsigned __int64 IMG_UINTPTR_T;
#else
typedef unsigned int     IMG_UINTPTR_T;
#endif

typedef IMG_PVOID       IMG_HANDLE;

typedef void**          IMG_HVOID,	* IMG_PHVOID;

typedef IMG_UINT32		IMG_SIZE_T;

#define IMG_NULL        0 

typedef IMG_UINT32      IMG_SID;


typedef IMG_PVOID IMG_CPU_VIRTADDR;

typedef struct _IMG_DEV_VIRTADDR
{
	
	IMG_UINT32  uiAddr;
#define IMG_CAST_TO_DEVVADDR_UINT(var)		(IMG_UINT32)(var)
	
} IMG_DEV_VIRTADDR;

typedef struct _IMG_CPU_PHYADDR
{
	
	IMG_UINTPTR_T uiAddr;
} IMG_CPU_PHYADDR;

typedef struct _IMG_DEV_PHYADDR
{
#if IMG_ADDRSPACE_PHYSADDR_BITS == 32
	
	IMG_UINTPTR_T uiAddr;
#else
	IMG_UINT32 uiAddr;
	IMG_UINT32 uiHighAddr;
#endif
} IMG_DEV_PHYADDR;

typedef struct _IMG_SYS_PHYADDR
{
	
	IMG_UINTPTR_T uiAddr;
} IMG_SYS_PHYADDR;

#include "img_defs.h"

#endif	
