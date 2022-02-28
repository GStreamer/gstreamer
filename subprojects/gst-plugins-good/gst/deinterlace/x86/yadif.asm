;*****************************************************************************
;* x86-optimized functions for yadif filter
;* Copyright (C) 2020 Vivia Nikolaidou <vivia.nikolaidou@ltnglobal.com>
;*
;* Based on libav's vf_yadif.asm file
;* Copyright (C) 2006 Michael Niedermayer <michaelni@gmx.at>
;* Copyright (c) 2013 Daniel Kang <daniel.d.kang@gmail.com>
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with FFmpeg; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"

SECTION_RODATA

; 16 bytes of value 1
pb_1: times 16 db 1
; 8 words of value 1
pw_1: times  8 dw 1

SECTION .text

%macro ABS1 2
%if cpuflag(ssse3)
    pabsw   %1, %1
%elif cpuflag(mmxext) ; a, tmp
    pxor    %2, %2
    psubw   %2, %1
    pmaxsw  %1, %2
%else ; a, tmp
    pxor       %2, %2
    pcmpgtw    %2, %1
    pxor       %1, %2
    psubw      %1, %2
%endif
%endmacro

%macro CHECK 2
; %1 = 1+j, %2 = 1-j
    ; m2 = t0[x+1+j]
    movu      m2, [tzeroq+%1]
    ; m3 = b0[x+1-j]
    movu      m3, [bzeroq+%2]
    ; m4 = t0[x+1+j]
    mova      m4, m2
    ; m5 = t0[x+1+j]
    mova      m5, m2
    ; m4 = xor(t0[x+1+j], b0[x+1-j])
    pxor      m4, m3
    pavgb     m5, m3
    ; round down to 0
    pand      m4, [pb_1]
    ; m5 = rounded down average of the whole thing
    psubusb   m5, m4
    ; shift by 1 quadword to prepare for spatial_pred
    psrldq    m5, 1
    ; m7 = 0
    ; Interleave low-order bytes with 0
    ; so one pixel doesn't spill into the next one
    punpcklbw m5, m7
    ; m4 = t0[x+1+j] (reset)
    mova      m4, m2
    ; m2 = t0[x+1+j] - b0[x+1-j]
    psubusb   m2, m3
    ; m3 = -m2
    psubusb   m3, m4
    ; m2 = FFABS(t0[x+1+j] - b0[x+1-j]);
    pmaxub    m2, m3
    ; m3 = FFABS(t0[x+1+j] - b0[x+1-j]);
    mova      m3, m2
    ; m4 = FFABS(t0[x+1+j] - b0[x+1-j]);
    mova      m4, m2
    ; m3 = FFABS(t0[x+j] - b0[x-j])
    psrldq    m3, 1
    ; m4 = FFABS(t0[x-1+j] - b0[x-1-j])
    psrldq    m4, 2
    ; prevent pixel spilling for all of them
    punpcklbw m2, m7
    punpcklbw m3, m7
    punpcklbw m4, m7
    paddw     m2, m3
    ; m2 = score
    paddw     m2, m4
%endmacro

%macro CHECK1 0
; m0 was spatial_score
; m1 was spatial_pred
    mova    m3, m0
    ; compare for greater than
    ; each word will be 1111 or 0000
    pcmpgtw m3, m2
    ; if (score < spatial_score) spatial_score = score;
    pminsw  m0, m2
    ; m6 = the mask
    mova    m6, m3
    ; m5 = becomes 0 if it should change
    pand    m5, m3
    ; nand: m3 = becomes 0 if it should not change
    pandn   m3, m1
    ; m3 = put them together in an OR
    por     m3, m5
    ; and put it in spatial_pred
    mova    m1, m3
%endmacro

%macro CHECK2 0
; m6 was the mask from CHECK1 (we don't change it)
    paddw   m6, [pw_1]
    ; shift words left while shifting in 14 0s (16 - j)
    ; essentially to not recalculate the mask!
    psllw   m6, 14
    ; add it to score
    paddsw  m2, m6
    ; same as CHECK1
    mova    m3, m0
    pcmpgtw m3, m2
    pminsw  m0, m2
    pand    m5, m3
    pandn   m3, m1
    por     m3, m5
    mova    m1, m3
%endmacro

%macro LOAD 2
    movh      %1, %2
    punpcklbw %1, m7
%endmacro

%macro FILTER_HEAD 0
    ; m7 = 0
    pxor         m7, m7
    ; m0 = c
    LOAD         m0, [tzeroq]
    ; m1 = e
    LOAD         m1, [bzeroq]
    ; m3 = mp
    LOAD         m3, [mpq]
    ; m2 = m1
    LOAD         m2, [moneq]
    ; m4 = mp
    mova         m4, m3
    ; m3 = m1 + mp
    paddw        m3, m2
    ; m3 = d
    psraw        m3, 1
    ; rsp + 0 = d
    mova   [rsp+ 0], m3
    ; rsp + 16 = bzeroq
    mova   [rsp+16], m1
    ; m2 = m1 - mp
    psubw        m2, m4
    ; m2 = temporal_diff0 (m4 is temporary)
    ABS1         m2, m4
    ; m3 = t2
    LOAD         m3, [ttwoq]
    ; m4 = b2
    LOAD         m4, [btwoq]
    ; m3 = t2 - c
    psubw        m3, m0
    ; m4 = b2 - e
    psubw        m4, m1
    ; m3 = ABS(t2 - c)
    ABS1         m3, m5
    ; m4 = ABS(b2 - e)
    ABS1         m4, m5
    paddw        m3, m4
    psrlw        m2, 1
    ; m3 = temporal_diff1
    psrlw        m3, 1
    ; m2 = left part of diff
    pmaxsw       m2, m3
    ; m3 = tp2
    LOAD         m3, [tptwoq]
    ; m4 = bp2
    LOAD         m4, [bptwoq]
    psubw        m3, m0
    psubw        m4, m1
    ABS1         m3, m5
    ABS1         m4, m5
    paddw        m3, m4
    ; m3 = temporal_diff2
    psrlw        m3, 1
    ; m2 = diff (for real)
    pmaxsw       m2, m3
    ; rsp + 32 = diff
    mova   [rsp+32], m2

    ; m1 = e + c
    paddw        m1, m0
    ; m0 = 2c
    paddw        m0, m0
    ; m0 = c - e
    psubw        m0, m1
    ; m1 = spatial_pred
    psrlw        m1, 1
    ; m0 = FFABS(c-e)
    ABS1         m0, m2

    ; m2 = t0[x-1]
    ; if it's unpacked it should contain 4 bytes
    movu         m2, [tzeroq-1]
    ; m3 = b0[x-1]
    movu         m3, [bzeroq-1]
    ; m4 = t0[x-1]
    mova         m4, m2
    ; m2 = t0[x-1]-b0[x-1] unsigned packed
    psubusb      m2, m3
    ; m3 = m3 - m4 = b0[x-1]-t0[x-1] = -m2 unsigned packed
    psubusb      m3, m4
    ; m2 = max(m2, -m2) = abs(t0[x-1]-b0[x-1])
    pmaxub       m2, m3
%if mmsize == 16
    ; m3 = m2 >> 2quadwords
    ; pixel jump: go from x-1 to x+1
    mova         m3, m2
    psrldq       m3, 2
%else
    pshufw       m3, m2, q0021
%endif
    ; m7 = 0
    ; unpack and interleave low-order bytes
    ; to prevent pixel spilling when adding
    punpcklbw    m2, m7
    punpcklbw    m3, m7
    paddw        m0, m2
    paddw        m0, m3
    ; m0 = spatial_score
    psubw        m0, [pw_1]

    CHECK -2, 0
    CHECK1
    CHECK -3, 1
    CHECK2
    CHECK 0, -2
    CHECK1
    CHECK 1, -3
    CHECK2
    ; now m0 = spatial_score, m1 = spatial_pred

    ; m6 = diff
    mova         m6, [rsp+32]
%endmacro

%macro FILTER_TAIL 0
    ; m2 = d
    mova         m2, [rsp]
    ; m3 = d
    mova         m3, m2
    ; m2 = d - diff
    psubw        m2, m6
    ; m3 = d + diff
    paddw        m3, m6
    ; m1 = max(spatial_pred, d-diff)
    pmaxsw       m1, m2
    ; m1 = min(d + diff, max(spatial_pred, d-diff))
    ; m1 = spatial_pred
    pminsw       m1, m3
    ; Converts 8 signed word integers into 16 unsigned byte integers with saturation
    packuswb     m1, m1

    ; dst = spatial_pred
    movh     [dstq], m1
    ; half the register size
    add        dstq, mmsize/2
    add        tzeroq, mmsize/2
    add        bzeroq, mmsize/2
    add        moneq, mmsize/2
    add        mpq, mmsize/2
    add        ttwoq, mmsize/2
    add        btwoq, mmsize/2
    add        tptwoq, mmsize/2
    add        bptwoq, mmsize/2
    add        ttoneq, mmsize/2
    add        ttpq, mmsize/2
    add        bboneq, mmsize/2
    add        bbpq, mmsize/2
%endmacro

%macro FILTER_MODE0 0
.loop0:
    FILTER_HEAD
    ; m2 = tt1
    LOAD         m2, [ttoneq]
    ; m4 = ttp
    LOAD         m4, [ttpq]
    ; m3 = bb1
    LOAD         m3, [bboneq]
    ; m5 = bbp
    LOAD         m5, [bbpq]
    paddw        m2, m4
    paddw        m3, m5
    ; m2 = b
    psrlw        m2, 1
    ; m3 = f
    psrlw        m3, 1
    ; m4 = c
    LOAD         m4, [tzeroq]
    ; m5 = d
    mova         m5, [rsp]
    ; m7 = e
    mova         m7, [rsp+16]
    ; m2 = b - c
    psubw        m2, m4
    ; m3 = f - e
    psubw        m3, m7
    ; m0 = d
    mova         m0, m5
    ; m5 = d - c
    psubw        m5, m4
    ; m0 = d - e
    psubw        m0, m7
    ; m4 = b - c
    mova         m4, m2
    ; m2 = FFMIN(b-c, f-e)
    pminsw       m2, m3
    ; m3 = FFMAX(f-e, b-c)
    pmaxsw       m3, m4
    ; m2 = FFMAX(d-c, FFMIN(b-c, f-e))
    pmaxsw       m2, m5
    ; m3 = FFMIN(d-c, FFMAX(f-e, b-c))
    pminsw       m3, m5
    ; m2 = max
    pmaxsw       m2, m0
    ; m3 = min
    pminsw       m3, m0
    ; m4 = 0
    pxor         m4, m4
    ; m6 = MAX(diff, min)
    pmaxsw       m6, m3
    ; m4 = -max
    psubw        m4, m2
    ; m6 = diff
    pmaxsw       m6, m4

    FILTER_TAIL
    ; r13m = w
    sub   DWORD r13m, mmsize/2
    jg .loop0
%endmacro

%macro FILTER_MODE2 0
.loop2:
    FILTER_HEAD
    FILTER_TAIL
    ; r13m = w
    sub   DWORD r13m, mmsize/2
    jg .loop2
%endmacro

%macro YADIF_ADD3 0
    ; start 3 pixels later
    add        dstq, 3
    add        tzeroq, 3
    add        bzeroq, 3
    add        moneq, 3
    add        mpq, 3
    add        ttwoq, 3
    add        btwoq, 3
    add        tptwoq, 3
    add        bptwoq, 3
    add        ttoneq, 3
    add        ttpq, 3
    add        bboneq, 3
    add        bbpq, 3
%endmacro

; cglobal foo, 2,3,7,0x40, dst, src, tmp
; declares a function (foo) that automatically loads two arguments (dst and
; src) into registers, uses one additional register (tmp) plus 7 vector
; registers (m0-m6) and allocates 0x40 bytes of stack space.
%macro YADIF_MODE0 0
cglobal yadif_filter_line_mode0, 13, 14, 8, 80, dst, tzero, bzero, mone, mp, \
                                        ttwo, btwo, tptwo, bptwo, ttone, \
                                        ttp, bbone, bbp, w

    YADIF_ADD3
    FILTER_MODE0
    RET
%endmacro

%macro YADIF_MODE2 0
cglobal yadif_filter_line_mode2, 13, 14, 8, 80, dst, tzero, bzero, mone, mp, \
                                        ttwo, btwo, tptwo, bptwo, ttone, \
                                        ttp, bbone, bbp, w

    YADIF_ADD3
    FILTER_MODE2
    RET
%endmacro

; declares two functions for ssse3, and two for sse2
INIT_XMM ssse3
YADIF_MODE0
YADIF_MODE2
INIT_XMM sse2
YADIF_MODE0
YADIF_MODE2
