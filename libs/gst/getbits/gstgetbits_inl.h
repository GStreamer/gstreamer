/*
 * Copyright (c) 1995 The Regents of the University of California.
 * All rights reserved.
 * 
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 * 
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
 * CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

/*
 * Portions of this software Copyright (c) 1995 Brown University.
 * All rights reserved.
 * 
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement
 * is hereby granted, provided that the above copyright notice and the
 * following two paragraphs appear in all copies of this software.
 * 
 * IN NO EVENT SHALL BROWN UNIVERSITY BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF BROWN
 * UNIVERSITY HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * BROWN UNIVERSITY SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
 * BASIS, AND BROWN UNIVERSITY HAS NO OBLIGATION TO PROVIDE MAINTENANCE,
 * SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

/*
   Changes to make the code reentrant:
     deglobalized: curBits, curVidStream
     deglobalized: bitOffset, bitLength, bitBuffer in vid_stream, not used
       here
   Additional changes:
   -lsh@cs.brown.edu (Loring Holden)
 */

#ifndef __GST_GETBITS_INL_H__
#define __GST_GETBITS_INL_H__

#include <glib.h>

/*#define GETBITS_DEBUG_ENABLED */
/*#define GETBITS_OVERRUN_ENABLED */

#ifdef GETBITS_DEBUG_ENABLED
#define debug2(format,args...) g_print(format,##args)
#define debug(format,args...) g_print(format,##args),
#else
#define debug(format,args...)
#define debug2(format,args...)
#endif

#ifdef GETBITS_OVERRUN_ENABLED
#define checklength2(src, dst) (((unsigned char*)src)<(dst)?0:printf("overrun !! %p>=%p %ld %s %d\n", (src), (dst), (gb)->bits, __PRETTY_FUNCTION__, __LINE__))
#define checklength(src, dst) (((unsigned char*)src)<(dst)?0:printf("overrun !! %p>=%p %ld %s %d\n", (src), (dst), (gb)->bits, __PRETTY_FUNCTION__, __LINE__)),
#else
#define checklength(src, dst)
#define checklength2(src, dst)
#endif

#define swab32(x) GUINT32_FROM_BE(x)

/* External declarations for bitstream i/o operations. */
extern unsigned long gst_getbits_nBitMask[];

#define gst_getbits_init(gb, callback, data)

#define gst_getbits_newbuf(gb, buffer, len)				\
{									\
  (gb)->longptr = (unsigned long *)(buffer);				\
  (gb)->endptr = (unsigned char *)buffer+len;				\
  (gb)->length = len;							\
  (gb)->bits = 0;							\
  (gb)->dword = swab32(*(gb)->longptr);					\
}

#define gst_getbits_bitoffset(gb)  					\
(									\
  debug("bitoffset: %ld %p\n", (gb)->bits, (gb)->longptr)		\
  (gb)->bits								\
)

#define gst_getbits_bufferpos(gb)  ((gb)->longptr)

#define gst_getbits_bytesleft(gb) ((gb)->endptr - (unsigned char*)(gb)->longptr)	

#define gst_getbits_bitsleft(gb) (((gb)->endptr - (unsigned char*)(gb)->longptr)*8  - (gb)->bits)

#define gst_getbits1(gb)                                                \
(                                                                       \
  ((gb)->temp = (((gb)->dword & 0x80000000) != 0)),                   	\
  (gb)->dword <<= 1,                                              	\
  (gb)->bits++,                                               	 	\
                                                                        \
  ((gb)->bits & 0x20 ? (                                   		\
    (gb)->bits = 0,                                             	\
    (gb)->longptr++,                                                 	\
    checklength((gb)->longptr, (gb)->endptr)				\
    ((gb)->dword = swab32(*(gb)->longptr))                    	    	\
  )									\
  :0),                                                                  \
  debug("getbits1 : %04lx %08lx %p\n", (gb)->temp, (gb)->dword, (gb)->longptr)	\
  (gb)->temp								\
)

#define gst_getbits2(gb)                                                \
(                                                                       \
  (gb)->bits += 2,                                            		\
                                                                        \
  ((gb)->bits & 0x20 ? (						\
    (gb)->bits -= 32,                                         		\
    (gb)->longptr++,                                                 	\
    checklength((gb)->longptr, (gb)->endptr)				\
    ((gb)->bits ? (                                        		\
      ((gb)->dword |=                                              	\
	 (swab32(*(gb)->longptr) >> (2 - (gb)->bits)))           		\
    )									\
    : 0									\
    ),(                                                                \
    ((gb)->temp = (((gb)->dword & 0xc0000000) >> 30)),                 	\
    ((gb)->dword = swab32(*(gb)->longptr) << (gb)->bits))    		\
  )									\
  : (                                                                   \
      ((gb)->temp = (((gb)->dword & 0xc0000000) >> 30)),                \
      ((gb)->dword <<= 2)            					\
    )									\
  ),                                                               	\
  debug("getbits2 : %04lx %08lx %p\n", (gb)->temp, (gb)->dword, (gb)->longptr)		\
  (gb)->temp								\
)

#define gst_getbitsX(gb, num, mask, shift)	                     	\
(                                                                       \
  (gb)->bits += (num),                                         		\
                                                                        \
  ((gb)->bits & 0x20 ? (						\
    (gb)->bits -= 32,                                         		\
    (gb)->longptr++,                                                 	\
    checklength((gb)->longptr, (gb)->endptr)				\
    ((gb)->bits ? (                                        		\
        ((gb)->dword |= (swab32(*(gb)->longptr) >>             		\
        ((num) - (gb)->bits)))                                 		\
      )									\
      :0								\
      ),                                                                \
      ((gb)->temp = (((gb)->dword & (mask)) >> (shift))),              	\
      ((gb)->dword = swab32(*(gb)->longptr) << (gb)->bits)    		\
    )									\
  : (                                                                   \
      ((gb)->temp = (((gb)->dword & mask) >> shift)),                   \
      ((gb)->dword <<= (num))                                          	\
    )									\
  ),                                                                    \
  debug("getbits%-2d: %04lx %08lx %lu %p\n", num, (gb)->temp, (gb)->dword, mask, (gb)->longptr)	\
  (gb)->temp								\
)

#define gst_getbits3(gb) gst_getbitsX(gb, 3,   0xe0000000UL, 29)
#define gst_getbits4(gb) gst_getbitsX(gb, 4,   0xf0000000UL, 28)
#define gst_getbits5(gb) gst_getbitsX(gb, 5,   0xf8000000UL, 27)
#define gst_getbits6(gb) gst_getbitsX(gb, 6,   0xfc000000UL, 26)
#define gst_getbits7(gb) gst_getbitsX(gb, 7,   0xfe000000UL, 25)
#define gst_getbits8(gb) gst_getbitsX(gb, 8,   0xff000000UL, 24)
#define gst_getbits9(gb) gst_getbitsX(gb, 9,   0xff800000UL, 23)
#define gst_getbits10(gb) gst_getbitsX(gb, 10, 0xffc00000UL, 22)
#define gst_getbits11(gb) gst_getbitsX(gb, 11, 0xffe00000UL, 21)
#define gst_getbits12(gb) gst_getbitsX(gb, 12, 0xfff00000UL, 20)
#define gst_getbits13(gb) gst_getbitsX(gb, 13, 0xfff80000UL, 19)
#define gst_getbits14(gb) gst_getbitsX(gb, 14, 0xfffc0000UL, 18)
#define gst_getbits15(gb) gst_getbitsX(gb, 15, 0xfffe0000UL, 17)
#define gst_getbits16(gb) gst_getbitsX(gb, 16, 0xffff0000UL, 16)
#define gst_getbits17(gb) gst_getbitsX(gb, 17, 0xffff8000UL, 15)
#define gst_getbits18(gb) gst_getbitsX(gb, 18, 0xffffc000UL, 14)
#define gst_getbits19(gb) gst_getbitsX(gb, 19, 0xffffe000UL, 13)
#define gst_getbits20(gb) gst_getbitsX(gb, 20, 0xfffff000UL, 12)
#define gst_getbits21(gb) gst_getbitsX(gb, 21, 0xfffff800UL, 11)
#define gst_getbits22(gb) gst_getbitsX(gb, 22, 0xfffffc00UL, 10)
#define gst_getbits32(gb) gst_getbitsX(gb, 32, 0xffffffffUL,  0)

#define gst_getbitsn(gb,num) gst_getbitsX(gb, (num), ((num) ? ((0xffffffffUL) << (32-(num))):0), (32-(num)))

#define gst_showbits32(gb)        	                      		\
(                                                                       \
  ((gb)->bits ? (					        	\
      (gb)->dword | (swab32(*((gb)->longptr+1)) >>  			\
	 (32 - (gb)->bits))						\
  )									\
  : (                                                                   \
      (gb)->dword							\
    )									\
  )                                                                     \
)

#define gst_showbitsX(gb, num, mask, shift)		                \
(                                                                       \
  ((gb)->temp = (gb)->bits + num),                                     	\
  ((gb)->temp > 32 ? (                                                 \
      (gb)->temp -= 32,                                                 \
      (((gb)->dword & mask) >> shift) |                    		\
           (swab32(*((gb)->longptr+1)) >> (shift + (num - (gb)->temp)))	\
  )                                                                     \
  : (                                                                	\
      (((gb)->dword & mask) >> shift)                     		\
    )									\
  )									\
)

#define gst_showbits1(gb)  gst_showbitsX(gb, 1,  0x80000000, 31)
#define gst_showbits2(gb)  gst_showbitsX(gb, 2,  0xc0000000, 30)
#define gst_showbits3(gb)  gst_showbitsX(gb, 3,  0xe0000000, 29)
#define gst_showbits4(gb)  gst_showbitsX(gb, 4,  0xf0000000, 28)
#define gst_showbits5(gb)  gst_showbitsX(gb, 5,  0xf8000000, 27)
#define gst_showbits6(gb)  gst_showbitsX(gb, 6,  0xfc000000, 26)
#define gst_showbits7(gb)  gst_showbitsX(gb, 7,  0xfe000000, 25)
#define gst_showbits8(gb)  gst_showbitsX(gb, 8,  0xff000000, 24)
#define gst_showbits9(gb)  gst_showbitsX(gb, 9,  0xff800000, 23)
#define gst_showbits10(gb) gst_showbitsX(gb, 10, 0xffc00000, 22)
#define gst_showbits11(gb) gst_showbitsX(gb, 11, 0xffe00000, 21)
#define gst_showbits12(gb) gst_showbitsX(gb, 12, 0xfff00000, 20)
#define gst_showbits13(gb) gst_showbitsX(gb, 13, 0xfff80000, 19)
#define gst_showbits14(gb) gst_showbitsX(gb, 14, 0xfffc0000, 18)
#define gst_showbits15(gb) gst_showbitsX(gb, 15, 0xfffe0000, 17)
#define gst_showbits16(gb) gst_showbitsX(gb, 16, 0xffff0000, 16)
#define gst_showbits17(gb) gst_showbitsX(gb, 17, 0xffff8000, 15)
#define gst_showbits18(gb) gst_showbitsX(gb, 18, 0xffffc000, 14)
#define gst_showbits19(gb) gst_showbitsX(gb, 19, 0xffffe000, 13)
#define gst_showbits20(gb) gst_showbitsX(gb, 20, 0xfffff000, 12)
#define gst_showbits21(gb) gst_showbitsX(gb, 21, 0xfffff800, 11)
#define gst_showbits22(gb) gst_showbitsX(gb, 22, 0xfffffc00, 10)
#define gst_showbits23(gb) gst_showbitsX(gb, 23, 0xfffffe00,  9)
#define gst_showbits24(gb) gst_showbitsX(gb, 24, 0xffffff00,  8)
#define gst_showbits25(gb) gst_showbitsX(gb, 25, 0xffffff80,  7)
#define gst_showbits26(gb) gst_showbitsX(gb, 26, 0xffffffc0,  6)
#define gst_showbits27(gb) gst_showbitsX(gb, 27, 0xffffffe0,  5)
#define gst_showbits28(gb) gst_showbitsX(gb, 28, 0xfffffff0,  4)
#define gst_showbits29(gb) gst_showbitsX(gb, 29, 0xfffffff8,  3)
#define gst_showbits30(gb) gst_showbitsX(gb, 30, 0xfffffffc,  2)
#define gst_showbits31(gb) gst_showbitsX(gb, 31, 0xfffffffe,  1)

#define gst_showbitsn(gb,num) gst_showbitsX(gb, (num), ((0xffffffff) << (32-num)), (32-(num)))

#define gst_flushbits32(gb)                                            	\
{                                                                       \
  (gb)->longptr++;                                                      \
  checklength2((gb)->longptr, (gb)->endptr);				\
  (gb)->dword = swab32(*(gb)->longptr)  << (gb)->bits;			\
}


#define gst_flushbitsn(gb, num)                                         \
{                                                                       \
  (gb)->bits += num;                                      		\
                                                                      	\
  if ((gb)->bits & 0x20) {                                		\
    (gb)->bits -= 32;                                     		\
    (gb)->longptr++;                                             	\
    checklength2((gb)->longptr, (gb)->endptr);				\
    (gb)->dword = swab32(*(gb)->longptr)  << (gb)->bits;		\
  }                                                                   	\
  else {                                                              	\
    (gb)->dword <<= num;                                      		\
  }                                                                   	\
  debug2("flushbits%-2d: %08lx %p\n", num, (gb)->dword, (gb)->longptr);	\
}

#define gst_backbits24(gb)                                            	\
{									\
  (gb)->bits -= 24;							\
  if ((gb)->bits < 0) {							\
    (gb)->bits += 32;							\
    (gb)->longptr--;							\
  }									\
  (gb)->dword = swab32(*(gb)->longptr)  << (gb)->bits;			\
}

#define gst_backbitsn(gb, num)                                          \
{									\
  (gb)->bits -= num;							\
  while ((gb)->bits < 0) {						\
    (gb)->bits += 32;							\
    (gb)->longptr--;							\
  }									\
  (gb)->dword = swab32(*(gb)->longptr)  << (gb)->bits;			\
  debug2("backbits%-2d: %08lx %p\n", num, (gb)->dword, (gb)->longptr);			\
}

#endif /* __GST_GETBITS_INL_H__ */
