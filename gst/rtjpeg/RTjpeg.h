/* 
   bttvgrab 0.15.4 [1999-03-23]
   (c) 1998, 1999 by Joerg Walter <trouble@moes.pmnet.uni-oldenburg.de>
   Maintained by: Joerg Walter
   Current version at http://moes.pmnet.uni-oldenburg.de/bttvgrab/

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   This file is a modified version of RTjpeg 0.1.2, (C) Justin Schoeman 1998
*/

#include <inttypes.h>

typedef uint8_t __u8;
typedef uint32_t __u32;
typedef int8_t __s8;
typedef uint16_t __u16;

extern void RTjpeg_init_Q (__u8 Q);
extern void RTjpeg_init_compress (long unsigned int *buf, int width, int height,
    __u8 Q);
extern void RTjpeg_init_decompress (long unsigned int *buf, int width,
    int height);
extern int RTjpeg_compressYUV420 (__s8 * sp, unsigned char *bp);
extern int RTjpeg_compressYUV422 (__s8 * sp, unsigned char *bp);
extern void RTjpeg_decompressYUV420 (__s8 * sp, __u8 * bp);
extern void RTjpeg_decompressYUV422 (__s8 * sp, __u8 * bp);
extern int RTjpeg_compress8 (__s8 * sp, unsigned char *bp);
extern void RTjpeg_decompress8 (__s8 * sp, __u8 * bp);

extern void RTjpeg_init_mcompress (void);
extern int RTjpeg_mcompress (__s8 * sp, unsigned char *bp, __u16 lmask,
    __u16 cmask);
extern int RTjpeg_mcompress8 (__s8 * sp, unsigned char *bp, __u16 lmask);
extern void RTjpeg_set_test (int i);

extern void RTjpeg_yuv420rgb (__u8 * buf, __u8 * rgb);
extern void RTjpeg_yuv422rgb (__u8 * buf, __u8 * rgb);
extern void RTjpeg_yuvrgb8 (__u8 * buf, __u8 * rgb);
extern void RTjpeg_yuvrgb16 (__u8 * buf, __u8 * rgb);
extern void RTjpeg_yuvrgb24 (__u8 * buf, __u8 * rgb);
extern void RTjpeg_yuvrgb32 (__u8 * buf, __u8 * rgb);
