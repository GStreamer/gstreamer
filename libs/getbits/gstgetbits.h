#ifndef __GST_GETBITS_H__
#define __GST_GETBITS_H__

#include <stdio.h>

#include <config.h>
#undef HAVE_LIBMMX

#include <byteswap.h>

#ifdef HAVE_LIBMMX
#include <mmx.h>
#endif /* HAVE_LIBMMX */
#ifdef HAVE_LIBSSE
#include <sse.h>
#endif /* HAVE_LIBSSE */

typedef struct _gst_getbits_t gst_getbits_t;

/* breaks in structure show alignment on quadword boundaries */
/* FIXME: need to find out how to force GCC to align this to octwords */
struct _gst_getbits_t {
  unsigned char *ptr;		
  unsigned long *longptr;
  unsigned char *endptr;
  unsigned long length;
  long bits;	
  unsigned long dword;
  unsigned long temp;

  unsigned long (*getbits)(gst_getbits_t *gb,unsigned long bits);	/* dword */
  void (*backbits)(gst_getbits_t *gb,unsigned long bits);		/* dword */
  void (*backbytes)(gst_getbits_t *gb,unsigned long bytes);		/* dword */

#ifdef HAVE_LIBMMX
  mmx_t qword;			/* qword */
#endif /* HAVE_LIBMMX */

#ifdef HAVE_LIBSSE
  sse_t oword;			/* oword */
#endif /* HAVE_LIBSSE */
};


#define GST_GETBITS_INLINE

#ifdef GST_GETBITS_INLINE
#include "gstgetbits_inl.h"
#else

void gst_getbits_init(gst_getbits_t *gb);
void gst_getbits_newbuf(gst_getbits_t *gb,unsigned char *buffer);

#define getbits(gb,bits) (((gb)->getbits)(gb,bits))
#define getbits_back(gb,bits) (((gb)->backbits)(gb,bits))
#define getbits_back_bytes(gb,bytes) (((gb)->backbytes)(gb,bytes))
#endif

#endif /* __GST_GETBITS_H__ */
