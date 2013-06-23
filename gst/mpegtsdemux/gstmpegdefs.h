/*
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
 *
 * The Original Code is Fluendo MPEG Demuxer plugin.
 *
 * The Initial Developer of the Original Code is Fluendo, S.L.
 * Portions created by Fluendo, S.L. are Copyright (C) 2005
 * Fluendo, S.L. All Rights Reserved.
 *
 * Contributor(s): Wim Taymans <wim@fluendo.com>
 */

#ifndef __GST_MPEG_DEFS_H__
#define __GST_MPEG_DEFS_H__

/* Stream type assignments
 * 
 *   0x00    ITU-T | ISO/IEC Reserved
 *   0x01    ISO/IEC 11172 Video
 *   0x02    ITU-T Rec. H.262 | ISO/IEC 13818-2 Video or
 *           ISO/IEC 11172-2 constrained parameter video
 *           stream
 *   0x03    ISO/IEC 11172 Audio
 *   0x04    ISO/IEC 13818-3 Audio
 *   0x05    ITU-T Rec. H.222.0 | ISO/IEC 13818-1
 *           private_sections
 *   0x06    ITU-T Rec. H.222.0 | ISO/IEC 13818-1 PES
 *           packets containing private data
 *   0x07    ISO/IEC 13522 MHEG
 *   0x08    ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A
 *           DSM CC
 *   0x09    ITU-T Rec. H.222.1
 *   0x0A    ISO/IEC 13818-6 type A
 *   0x0B    ISO/IEC 13818-6 type B
 *   0x0C    ISO/IEC 13818-6 type C
 *   0x0D    ISO/IEC 13818-6 type D
 *   0x0E    ISO/IEC 13818-1 auxiliary
 *   0x0F    ISO/IEC 13818-7 Audio with ADTS transport syntax
 *   0x10    ISO/IEC 14496-2 Visual
 *   0x11    ISO/IEC 14496-3 Audio with the LATM transport syntax as
 *           defined in ISO/IEC 14496-3
 *   0x12    ISO/IEC 14496-1 SL-packetized stream or FlexMux stream
 *           carried in PES packets
 *   0x13    ISO/IEC 14496-1 SL-packetized stream or FlexMux stream
 *           carried in ISO/IEC 14496_sections
 *   0x14    ISO/IEC 13818-6 Synchronized Download Protocol
 *   0x15    Metadata carried in PES packets
 *   0x16    Metadata carried in metadata_sections
 *   0x17    Metadata carried in ISO/IEC 13818-6 Data Carousel
 *   0x18    Metadata carried in ISO/IEC 13818-6 Object Carousel
 *   0x19    Metadata carried in ISO/IEC 13818-6 Synchronized Donwnload Protocol
 *   0x1A    IPMP stream (ISO/IEC 13818-11, MPEG-2 IPMP)
 *   0x1B    AVC video stream (ITU-T H.264 | ISO/IEC 14496-10 Video)
 * 0x1C-0x7E ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Reserved
 *   0x7F    IPMP stream
 * 0x80-0xFF User Private
 */

/* FIXME : Move well-defined Stream type to an enum in the mpegts library */

#define ST_RESERVED                     0x00
#define ST_VIDEO_MPEG1                  0x01
#define ST_VIDEO_MPEG2                  0x02
#define ST_AUDIO_MPEG1                  0x03
#define ST_AUDIO_MPEG2                  0x04
#define ST_PRIVATE_SECTIONS             0x05
#define ST_PRIVATE_DATA                 0x06
#define ST_MHEG                         0x07
#define ST_DSMCC                        0x08
#define ST_H222_1                       0x09
#define ST_DSMCC_A                      0x0a
#define ST_DSMCC_B                      0x0b
#define ST_DSMCC_C                      0x0c
#define ST_DSMCC_D                      0x0d
#define ST_13818_1_AUXILIARY		0x0e
#define ST_AUDIO_AAC_ADTS               0x0f
#define ST_VIDEO_MPEG4                  0x10
#define ST_AUDIO_AAC_LATM               0x11

#define ST_IPMP_MPEG2			0x1a
#define ST_VIDEO_H264                   0x1b

#define ST_IPMP_STREAM			0x7f

/* Un-official Dirac extension */
#define ST_VIDEO_DIRAC                  0xd1

/* private stream types */
#define ST_PS_VIDEO_MPEG2_DCII          0x80
#define ST_PS_AUDIO_AC3                 0x81
#define ST_PS_AUDIO_DTS                 0x8a
#define ST_PS_AUDIO_LPCM                0x8b
#define ST_PS_DVD_SUBPICTURE            0xff

/* Blu-ray related */
#define ST_BD_AUDIO_LPCM                0x80
#define ST_BD_AUDIO_AC3                 0x81
#define ST_BD_AUDIO_DTS                 0x82
#define ST_BD_AUDIO_AC3_TRUE_HD         0x83
#define ST_BD_AUDIO_AC3_PLUS            0x84
#define ST_BD_AUDIO_DTS_HD              0x85
#define ST_BD_AUDIO_DTS_HD_MASTER_AUDIO 0x86
#define ST_BD_AUDIO_EAC3                0x87
#define ST_BD_PGS_SUBPICTURE            0x90
#define ST_BD_IGS                       0x91
#define ST_BD_SUBTITLE                  0x92
#define ST_BD_SECONDARY_AC3_PLUS        0xa1
#define ST_BD_SECONDARY_DTS_HD          0xa2

/* defined for VC1 extension in RP227 */
#define ST_PRIVATE_EA                   0xea

/* HDV AUX stream mapping
 * 0xA0      ISO/IEC 61834-11
 * 0xA1      ISO/IEC 61834-11
 */
#define ST_HDV_AUX_A                    0xa0
#define ST_HDV_AUX_V                    0xa1

#define CLOCK_BASE 9LL
#define CLOCK_FREQ (CLOCK_BASE * 10000)

#define PCRTIME_TO_GSTTIME(time) (gst_util_uint64_scale ((time), \
            GST_MSECOND/10, 300 * CLOCK_BASE))
#define MPEGTIME_TO_GSTTIME(time) (gst_util_uint64_scale ((time), \
            GST_MSECOND/10, CLOCK_BASE))
#define GSTTIME_TO_MPEGTIME(time) (gst_util_uint64_scale ((time), \
            CLOCK_BASE, GST_MSECOND/10))
#define GSTTIME_TO_PCRTIME(time) (gst_util_uint64_scale ((time), \
            300 * CLOCK_BASE, GST_MSECOND/10))

#define MPEG_MUX_RATE_MULT      50

/* sync:4 == 00xx ! pts:3 ! 1 ! pts:15 ! 1 | pts:15 ! 1 */
#define READ_TS(data, target, lost_sync_label)          \
    if ((*data & 0x01) != 0x01) goto lost_sync_label;   \
    target  = ((guint64) (*data++ & 0x0E)) << 29;       \
    target |= ((guint64) (*data++       )) << 22;       \
    if ((*data & 0x01) != 0x01) goto lost_sync_label;   \
    target |= ((guint64) (*data++ & 0xFE)) << 14;       \
    target |= ((guint64) (*data++       )) << 7;        \
    if ((*data & 0x01) != 0x01) goto lost_sync_label;   \
    target |= ((guint64) (*data++ & 0xFE)) >> 1;

#endif /* __GST_MPEG_DEFS_H__ */
