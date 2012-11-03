/*
 * Interplay MVE movie definitions
 *
 * Copyright (C) 2006 Jens Granseuer <jensgr@gmx.net>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __MVE_H__
#define __MVE_H__

#define MVE_PREAMBLE      "Interplay MVE File\032\000\032\000\000\001\063\021"
#define MVE_PREAMBLE_SIZE 26

#define MVE_PALETTE_COUNT 256

/* MVE chunk types */
#define MVE_CHUNK_INIT_AUDIO          0x0000
#define MVE_CHUNK_AUDIO_ONLY          0x0001
#define MVE_CHUNK_INIT_VIDEO          0x0002
#define MVE_CHUNK_VIDEO               0x0003
#define MVE_CHUNK_SHUTDOWN            0x0004
#define MVE_CHUNK_END                 0x0005

/* MVE segment opcodes */
#define MVE_OC_END_OF_STREAM          0x00
#define MVE_OC_END_OF_CHUNK           0x01
#define MVE_OC_CREATE_TIMER           0x02
#define MVE_OC_AUDIO_BUFFERS          0x03
#define MVE_OC_PLAY_AUDIO             0x04
#define MVE_OC_VIDEO_BUFFERS          0x05
#define MVE_OC_PLAY_VIDEO             0x07
#define MVE_OC_AUDIO_DATA             0x08
#define MVE_OC_AUDIO_SILENCE          0x09
#define MVE_OC_VIDEO_MODE             0x0A
#define MVE_OC_PALETTE                0x0C
#define MVE_OC_PALETTE_COMPRESSED     0x0D
#define MVE_OC_CODE_MAP               0x0F
#define MVE_OC_VIDEO_DATA             0x11

/* audio flags */
#define MVE_AUDIO_STEREO              0x0001
#define MVE_AUDIO_16BIT               0x0002
#define MVE_AUDIO_COMPRESSED          0x0004

/* video flags */
#define MVE_VIDEO_DELTA_FRAME         0x0001

#endif /* __MVE_H__ */
