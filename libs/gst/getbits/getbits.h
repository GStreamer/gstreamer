#ifndef __GST_GETBITS_H__
#define __GST_GETBITS_H__

#include <stdio.h>

/* getbits is critical, we need to forcibly disable DEBUG    */
#define GST_DEBUG_FORCE_DISABLE
#include <gst/gst.h>

#define swab32(x) GUINT32_FROM_BE(x)

typedef struct _gst_getbits_t gst_getbits_t;
typedef void (*GstGetbitsCallback) (gst_getbits_t *gb, void *data);

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

  GstGetbitsCallback callback;
  void *data;

  unsigned long (*get1bit)(gst_getbits_t *gb, unsigned long bits);
  unsigned long (*getbits)(gst_getbits_t *gb, unsigned long bits);
  unsigned long (*getbits_fast)(gst_getbits_t *gb, unsigned long bits);
  unsigned long (*getbyte)(gst_getbits_t *gb, unsigned long bits);
  unsigned long (*show1bit)(gst_getbits_t *gb, unsigned long bits);
  unsigned long (*showbits)(gst_getbits_t *gb, unsigned long bits);
  void (*flushbits)(gst_getbits_t *gb, unsigned long bits);	
  void (*backbits)(gst_getbits_t *gb, unsigned long bits);
};


#ifdef GST_GETBITS_INLINE
#include "gstgetbits_inl.h"
#else

void gst_getbits_init(gst_getbits_t *gb, GstGetbitsCallback callback, void *data);
void gst_getbits_newbuf(gst_getbits_t *gb, unsigned char *buffer, unsigned long len);

#define gst_getbits_bitoffset(gb)                                       \
(                                                                       \
  (-(gb)->bits)&0x7                                                     \
)

#define gst_getbits_align_byte(gb)                                 

#define gst_getbits_bufferpos(gb)  ((gb)->ptr)

#define gst_getbits_bytesleft(gb) ((gb)->endptr - (gb)->ptr)        

#define gst_getbits_bitsleft(gb) (((gb)->endptr - (gb)->ptr)*8  - ((-(gb)->bits)&0x7))

#define gst_get1bit(gb) (((gb)->get1bit)(gb, 1))
#define gst_getbitsX(gb,bits) (((gb)->getbits)(gb,bits))
#define gst_getbits_fastX(gb,bits) (((gb)->getbits_fast)(gb,bits))
#define gst_show1bit(gb,bits) (((gb)->show1bit)(gb,bits))
#define gst_showbitsX(gb,bits) (((gb)->showbits)(gb,bits))
#define gst_flushbitsX(gb,bits) (((gb)->flushbits)(gb,bits))
#define gst_backbitsX(gb,bits) (((gb)->backbits)(gb,bits))

#define gst_getbyte(gb) (((gb)->getbyte)(gb,8))

#define gst_getbits_fastn(gb,n) gst_getbits_fastX(gb, n)

#define gst_getbitsn(gb,n) gst_getbitsX(gb, n)
#define gst_getbits1(gb) gst_get1bit(gb)
#define gst_getbits2(gb) gst_getbits_fastX(gb, 2)
#define gst_getbits3(gb) gst_getbits_fastX(gb, 3)
#define gst_getbits4(gb) gst_getbits_fastX(gb, 4)
#define gst_getbits5(gb) gst_getbits_fastX(gb, 5)
#define gst_getbits6(gb) gst_getbits_fastX(gb, 6)
#define gst_getbits7(gb) gst_getbits_fastX(gb, 7)
#define gst_getbits8(gb) gst_getbits_fastX(gb, 8)
#define gst_getbits9(gb) gst_getbits_fastX(gb, 9)
#define gst_getbits10(gb) gst_getbitsX(gb, 10)
#define gst_getbits11(gb) gst_getbitsX(gb, 11)
#define gst_getbits12(gb) gst_getbitsX(gb, 12)
#define gst_getbits13(gb) gst_getbitsX(gb, 13)
#define gst_getbits14(gb) gst_getbitsX(gb, 14)
#define gst_getbits15(gb) gst_getbitsX(gb, 15)
#define gst_getbits16(gb) gst_getbitsX(gb, 16)
#define gst_getbits17(gb) gst_getbitsX(gb, 17)
#define gst_getbits18(gb) gst_getbitsX(gb, 18)
#define gst_getbits19(gb) gst_getbitsX(gb, 19)
#define gst_getbits20(gb) gst_getbitsX(gb, 20)
#define gst_getbits21(gb) gst_getbitsX(gb, 21)
#define gst_getbits22(gb) gst_getbitsX(gb, 22)
#define gst_getbits23(gb) gst_getbitsX(gb, 23)

#define gst_showbitsn(gb,n) gst_showbitsX(gb, n)
#define gst_showbits1(gb) gst_show1bit(gb, 1)
#define gst_showbits2(gb) gst_showbitsX(gb, 2)
#define gst_showbits3(gb) gst_showbitsX(gb, 3)
#define gst_showbits4(gb) gst_showbitsX(gb, 4)
#define gst_showbits5(gb) gst_showbitsX(gb, 5)
#define gst_showbits6(gb) gst_showbitsX(gb, 6)
#define gst_showbits7(gb) gst_showbitsX(gb, 7)
#define gst_showbits8(gb) gst_showbitsX(gb, 8)
#define gst_showbits9(gb) gst_showbitsX(gb, 9)
#define gst_showbits10(gb) gst_showbitsX(gb, 10)
#define gst_showbits11(gb) gst_showbitsX(gb, 11)
#define gst_showbits12(gb) gst_showbitsX(gb, 12)
#define gst_showbits13(gb) gst_showbitsX(gb, 13)
#define gst_showbits14(gb) gst_showbitsX(gb, 14)
#define gst_showbits15(gb) gst_showbitsX(gb, 15)
#define gst_showbits16(gb) gst_showbitsX(gb, 16)
#define gst_showbits17(gb) gst_showbitsX(gb, 17)
#define gst_showbits18(gb) gst_showbitsX(gb, 18)
#define gst_showbits19(gb) gst_showbitsX(gb, 19)
#define gst_showbits20(gb) gst_showbitsX(gb, 20)
#define gst_showbits21(gb) gst_showbitsX(gb, 21)
#define gst_showbits22(gb) gst_showbitsX(gb, 22)
#define gst_showbits23(gb) gst_showbitsX(gb, 23)
#define gst_showbits24(gb) gst_showbitsX(gb, 24)
#define gst_showbits32(gb) gst_showbitsX(gb, 32)

#define gst_flushbitsn(gb,n) gst_flushbitsX(gb, n)
#define gst_flushbits32(gb) gst_flushbitsX(gb, 32)

#define gst_backbitsn(gb,n) gst_backbitsX(gb, n)
#define gst_backbits24(gb) gst_backbitsX(gb, 24)
#endif

#endif /* __GST_GETBITS_H__ */
