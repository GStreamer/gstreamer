/*
 * ttadec.h
 *
 * Description:  TTAv1 decoder definitions and prototypes
 * Developed by: Alexander Djourik <sasha@iszf.irk.ru>
 *               Pavel Zhilin <pzh@iszf.irk.ru>
 *
 * Copyright (c) 1999-2004 Alexander Djourik. All rights reserved.
 *
 */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Please see the file COPYING in this directory for full copyright
 * information.
 */

#ifndef TTADEC_H_
#define TTADEC_H_

#ifdef _WIN32
#pragma pack(1)
#define __ATTRIBUTE_PACKED__
#else
#define __ATTRIBUTE_PACKED__    __attribute__((packed))
#endif

#define TTA1_SIGN       0x31415454
#define FRAME_TIME      1.04489795918367346939
#define MAX_ORDER       8

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM 1
#endif

#ifdef _WIN32
        typedef unsigned __int64 uint64;
#else
        typedef unsigned long long uint64;
#endif

/**
 * moved to gstttadec.c to silence gcc warnings
 */
/*
static const unsigned long bit_mask[] = {
    0x00000000, 0x00000001, 0x00000003, 0x00000007,
    0x0000000f, 0x0000001f, 0x0000003f, 0x0000007f,
    0x000000ff, 0x000001ff, 0x000003ff, 0x000007ff,
    0x00000fff, 0x00001fff, 0x00003fff, 0x00007fff,
    0x0000ffff, 0x0001ffff, 0x0003ffff, 0x0007ffff,
    0x000fffff, 0x001fffff, 0x003fffff, 0x007fffff,
    0x00ffffff, 0x01ffffff, 0x03ffffff, 0x07ffffff,
    0x0fffffff, 0x1fffffff, 0x3fffffff, 0x7fffffff,
    0xffffffff
};

static const unsigned long bit_shift[] = {
    0x00000001, 0x00000002, 0x00000004, 0x00000008,
    0x00000010, 0x00000020, 0x00000040, 0x00000080,
    0x00000100, 0x00000200, 0x00000400, 0x00000800,
    0x00001000, 0x00002000, 0x00004000, 0x00008000,
    0x00010000, 0x00020000, 0x00040000, 0x00080000,
    0x00100000, 0x00200000, 0x00400000, 0x00800000,
    0x01000000, 0x02000000, 0x04000000, 0x08000000,
    0x10000000, 0x20000000, 0x40000000, 0x80000000,
    0x80000000, 0x80000000, 0x80000000, 0x80000000,
    0x80000000, 0x80000000, 0x80000000, 0x80000000
};

static const unsigned long *shift_16 = bit_shift + 4;
*/

typedef unsigned char byte;

#ifdef _BIG_ENDIAN
#define ENDSWAP_INT16(x)        (((((x)>>8)&0xFF)|(((x)&0xFF)<<8)))
#define ENDSWAP_INT32(x)        (((((x)>>24)&0xFF)|(((x)>>8)&0xFF00)|(((x)&0xFF00)<<8)|(((x)&0xFF)<<24)))
#define WRITE_BUFFER(x, bsize, out) { \
        if (bsize > 2) *out++ = (byte)(*x >> 16); \
        if (bsize > 1) *out++ = (byte)(*x >> 8); \
        *out++ = (byte) *x; }
#else
#define ENDSWAP_INT16(x)        (x)
#define ENDSWAP_INT32(x)        (x)
#define WRITE_BUFFER(x, bsize, out) { \
        *out++ = (byte) *x; \
        if (bsize > 1) *out++ = (byte)(*x >> 8); \
        if (bsize > 2) *out++ = (byte)(*x >> 16); }
#endif

#define PREDICTOR1(x, k)        ((long)((((uint64)x << k) - x) >> k))
#define DEC(x)                  (((x)&1)?(++(x)>>1):(-(x)>>1))

#if 0
/* This doesn't compile on non-gcc compilers */
typedef struct {
    unsigned long TTAid;
    unsigned short AudioFormat;
    unsigned short NumChannels;
    unsigned short BitsPerSample;
    unsigned long SampleRate;
    unsigned long DataLength;
    unsigned long CRC32;
} __ATTRIBUTE_PACKED__ tta_hdr;
#endif

typedef struct {
        unsigned long k0;
        unsigned long k1;
        unsigned long sum0;
        unsigned long sum1;
} adapt;

typedef struct {
        long shift;
        long round;
        long error;
        long mutex;
        long qm[MAX_ORDER+1];
        long dx[MAX_ORDER+1];
        long dl[MAX_ORDER+1];
} fltst;

typedef struct {
        fltst fst;
        adapt rice;
        long last;
} decoder;

#endif /* TTADEC_H_ */
