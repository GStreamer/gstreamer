/*
 * This source code is public domain.
 *
 * Authors: Rani Assaf <rani@magic.metawire.com>,
 *          Olivier Lapicque <olivierl@jps.net>,
 *          Adam Goode       <adam@evdebs.org> (endian and char fixes for PPC)
*/

#ifndef _STDAFX_H_
#define _STDAFX_H_


#ifdef MSC_VER

#pragma warning (disable:4201)
#pragma warning (disable:4514)
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <stdio.h>

inline void ProcessPlugins(int n) {}

#else

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef signed char CHAR;
typedef unsigned char UCHAR;
typedef unsigned char* PUCHAR;
typedef unsigned short USHORT;
typedef unsigned long ULONG;
typedef unsigned long UINT;
typedef unsigned long DWORD;
typedef long LONG;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef unsigned char * LPBYTE;
typedef bool BOOL;
typedef char * LPSTR;
typedef void *  LPVOID;
typedef long * LPLONG;
typedef unsigned long * LPDWORD;
typedef unsigned short * LPWORD;
typedef const char * LPCSTR;
typedef long long LONGLONG;
typedef void * PVOID;
typedef void VOID;


inline LONG MulDiv (long a, long b, long c)
{
  /* if (!c) return 0; */
  return ((unsigned long long) a * (unsigned long long) b ) / c;
}

#define MODPLUG_NO_FILESAVE
#define NO_AGC
#define LPCTSTR LPCSTR
#define lstrcpyn strncpy
#define lstrcpy strcpy
#define lstrcmp strcmp
#define WAVE_FORMAT_PCM 1
/*#define ENABLE_EQ */

#define  GHND   0

inline signed char * GlobalAllocPtr(unsigned int, size_t size)
{
  signed char * p = (signed char *) malloc(size);

  if (p != NULL) memset(p, 0, size);
  return p;
}

inline void ProcessPlugins(int n) {}

#define GlobalFreePtr(p) free((void *)(p))

#define strnicmp(a,b,c)		strncasecmp(a,b,c)
#define wsprintf			sprintf

#ifndef FALSE
#define FALSE	false
#endif

#ifndef TRUE
#define TRUE	true
#endif

#endif /* MSC_VER */

#endif



