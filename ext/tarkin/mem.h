#ifndef __MEM_H
#define __MEM_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


#if defined(DBG_MEMLEAKS)

extern void* dbg_malloc (char *file, int line, char *func, size_t bytes);
extern void* dbg_calloc (char *file, int line, char *func, size_t count, size_t bytes);
extern void* dbg_realloc (char *file, int line, char *func, char *what, void *mem, size_t bytes);
extern void dbg_free (char *file, int line, char *func, char *what, void *mem);

#define MALLOC(bytes)        dbg_malloc(__FILE__,__LINE__,__FUNCTION__,bytes)
#define CALLOC(count,bytes)  dbg_calloc(__FILE__,__LINE__,__FUNCTION__,count,bytes)
#define FREE(mem)            dbg_free(__FILE__,__LINE__,__FUNCTION__,#mem,mem)
#define REALLOC(mem,bytes)   dbg_realloc(__FILE__,__LINE__,__FUNCTION__,#mem,mem,bytes)

#else

#define MALLOC malloc
#define CALLOC calloc
#define REALLOC realloc
#define FREE free

#endif

#endif

