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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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

/*
 * 1011 1100                program_stream_map
 * 1011 1101                private_stream_1
 * 1011 1110                padding_stream
 * 1011 1111                private_stream_2
 * 110x xxxx                ISO/IEC 13818-3 or ISO/IEC 11172-3 audio stream number x xxxx
 * 1110 xxxx                ITU-T Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 11172-2 video stream number xxxx
 * 1111 0000                ECM_stream
 * 1111 0001                EMM_stream
 * 1111 0010                ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A or ISO/IEC 13818-6_DSMCC_stream
 * 1111 0011                ISO/IEC_13522_stream
 * 1111 0100                ITU-T Rec. H.222.1 type A
 * 1111 0101                ITU-T Rec. H.222.1 type B
 * 1111 0110                ITU-T Rec. H.222.1 type C
 * 1111 0111                ITU-T Rec. H.222.1 type D
 * 1111 1000                ITU-T Rec. H.222.1 type E
 * 1111 1001                ancillary_stream
 * 1111 1010                ISO/IEC 14496-1_SL-packetized_stream
 * 1111 1011                ISO/IEC 14496-1_FlexMux_stream
 * 1111 1100                metadata stream
 * 1111 1101                extended_stream_id
 * 1111 1110                reserved data stream
 * 1111 1111                program_stream_directory
 */

#define ID_PS_END_CODE                          0x000001B9
#define ID_PS_PACK_START_CODE                   0x000001BA
#define ID_PS_SYSTEM_HEADER_START_CODE          0x000001BB
#define ID_PS_PROGRAM_STREAM_MAP                0x000001BC
#define ID_PRIVATE_STREAM_1                     0x000001BD
#define ID_PADDING_STREAM                       0x000001BE
#define ID_PRIVATE_STREAM_2                     0x000001BF
#define ID_ISO_IEC_MPEG12_AUDIO_STREAM_0        0x000001C0
#define ID_ISO_IEC_MPEG12_AUDIO_STREAM_32       0x000001DF
#define ID_ISO_IEC_MPEG12_VIDEO_STREAM_0        0x000001E0
#define ID_ISO_IEC_MPEG12_VIDEO_STREAM_16       0x000001EF
#define ID_ECM_STREAM                           0x000001F0
#define ID_EMM_STREAM                           0x000001F1
#define ID_DSMCC_STREAM                         0x000001F2
#define ID_ISO_IEC_13522_STREAM                 0x000001F3
#define ID_ITU_TREC_H222_TYPE_A_STREAM          0x000001F4
#define ID_ITU_TREC_H222_TYPE_B_STREAM          0x000001F5
#define ID_ITU_TREC_H222_TYPE_C_STREAM          0x000001F6
#define ID_ITU_TREC_H222_TYPE_D_STREAM          0x000001F7
#define ID_ITU_TREC_H222_TYPE_E_STREAM          0x000001F8
#define ID_ANCILLARY_STREAM                     0x000001F9
#define ID_14496_1_SL_PACKETIZED_STREAM         0x000001FA
#define ID_14496_1_SL_FLEXMUX_STREAM            0x000001FB
#define ID_METADATA_STREAM                      0x000001FC
#define ID_EXTENDED_STREAM_ID                   0x000001FD
#define ID_RESERVED_STREAM_3                    0x000001FE
#define ID_PROGRAM_STREAM_DIRECTORY             0x000001FF

#define PACKET_VIDEO_START_CODE                 0x000001E0
#define PACKET_AUDIO_START_CODE                 0x000001C0
#define PICTURE_START_CODE                      0x00000100
#define USER_DATA_START_CODE                    0x000001B2
#define SEQUENCE_HEADER_CODE                    0x000001B3
#define SEQUENCE_ERROR_CODE                     0x000001B4
#define EXTENSION_START_CODE                    0x000001B5
#define SEQUENCE_END_CODE                       0x000001B7
#define GROUP_START_CODE                        0x000001B8

#define AC3_SYNC_WORD                           0x0b770000

#define MPEG_TS_SYNC_BYTE                       0x00000047

#define PID_PROGRAM_ASSOCIATION_TABLE          0x0000
#define PID_CONDITIONAL_ACCESS_TABLE           0x0001
#define PID_RESERVED_FIRST                     0x0002
#define PID_RESERVED_LAST                      0x0010
#define PID_NULL_PACKET                        0x1FFF

#define PID_TYPE_UNKNOWN                        0
#define PID_TYPE_RESERVED                       1
#define PID_TYPE_PROGRAM_ASSOCIATION            2
#define PID_TYPE_CONDITIONAL_ACCESS             3
#define PID_TYPE_PROGRAM_MAP                    4
#define PID_TYPE_ELEMENTARY                     5
#define PID_TYPE_NULL_PACKET                    6
#define PID_TYPE_PRIVATE_SECTION                7

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
 * 0x0F-0x7F ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Reserved
 * 0x80-0xFF User Private
 */
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

/* later extensions */
#define ST_AUDIO_AAC_ADTS               0x0f
#define ST_VIDEO_MPEG4                  0x10
#define ST_AUDIO_AAC_LATM               0x11
#define ST_VIDEO_H264                   0x1b

/* Un-official Dirac extension */
#define ST_VIDEO_DIRAC                  0xd1

/* private stream types */
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

/* Un-official time-code stream */
#define ST_PS_TIMECODE                  0xd2

/* Internal stream types >= 0x100 */
#define ST_GST_AUDIO_RAWA52             0x181
/* Used when we don't yet know which stream type it will be in a PS stream */
#define ST_GST_VIDEO_MPEG1_OR_2         0x102


/* Table IDs */
/* ITU H.222.0 / IEC 13818-1 */
#define TABLE_ID_PROGRAM_ASSOCIATION		0x00
#define TABLE_ID_CONDITIONAL_ACCESS		0x01
#define TABLE_ID_TS_PROGRAM_MAP			0x02
#define TABLE_ID_TS_DESCRIPTION			0x03
#define TABLE_ID_14496_SCENE_DESCRIPTION	0x04
#define TABLE_ID_14496_OBJET_DESCRIPTOR		0x05
#define TABLE_ID_METADATA			0x06
#define TABLE_ID_IPMP_CONTROL_INFORMATION	0x07
/* IEC 13818-6 (DSM-CC) */
#define TABLE_ID_DSM_CC_MULTIPROTO_ENCAPSULATED_DATA	0x3A
#define TABLE_ID_DSM_CC_U_N_MESSAGES			0x3B
#define TABLE_ID_DSM_CC_DOWNLOAD_DATA_MESSAGES		0x3C
#define TABLE_ID_DSM_CC_STREAM_DESCRIPTORS		0x3D
#define TABLE_ID_DSM_CC_PRIVATE_DATA			0x3E
#define TABLE_ID_DSM_CC_ADDRESSABLE_SECTIONS		0x3F
/* EN 300 468 (DVB) v 1.12.1 */
#define TABLE_ID_NETWORK_INFORMATION_ACTUAL_NETWORK	0x40
#define TABLE_ID_NETWORK_INFORMATION_OTHER_NETWORK	0x41
#define TABLE_ID_SERVICE_DESCRIPTION_ACTUAL_TS		0x42
#define TABLE_ID_SERVICE_DESCRIPTION_OTHER_TS		0x46
#define TABLE_ID_BOUQUET_ASSOCIATION			0x4A
#define TABLE_ID_EVENT_INFORMATION_ACTUAL_TS_PRESENT	0x4E
#define TABLE_ID_EVENT_INFORMATION_OTHER_TS_PRESENT	0x4F
#define TABLE_ID_EVENT_INFORMATION_ACTUAL_TS_SCHEDULE_1	0x50 /* First */
#define TABLE_ID_EVENT_INFORMATION_ACTUAL_TS_SCHEDULE_N	0x5F /* Last */
#define TABLE_ID_EVENT_INFORMATION_OTHER_TS_SCHEDULE_1	0x60 /* First */
#define TABLE_ID_EVENT_INFORMATION_OTHER_TS_SCHEDULE_N	0x6F /* Last */
#define TABLE_ID_TIME_DATE				0x70
#define TABLE_ID_RUNNING_STATUS				0x71
#define TABLE_ID_STUFFING				0x72
#define TABLE_ID_TIME_OFFSET				0x73
/* TS 102 812 (MHP v1.1.3) */
#define TABLE_ID_APPLICATION_INFORMATION_TABLE		0x74
/* TS 102 323 (DVB TV Anytime v1.5.1) */
#define TABLE_ID_CONTAINER				0x75
#define TABLE_ID_RELATED_CONTENT			0x76
#define TABLE_ID_CONTENT_IDENTIFIER			0x77
/* EN 301 192 (DVB specification for data broadcasting) */
#define TABLE_ID_MPE_FEC				0x78
/* TS 102 323 (DVB TV Anytime v1.5.1) */
#define TABLE_ID_RESOLUTION_NOTIFICATION		0x79
/* TS 102 772 (DVB-SH Multi-Protocol Encapsulation) */
#define TABLE_ID_MPE_IFEC				0x7A
/* EN 300 468 (DVB) v 1.12.1 */
#define TABLE_ID_DISCONTINUITY_INFORMATION		0x7E
#define TABLE_ID_SELECTION_INFORMATION			0x7F
/* ETR 289 (DVB Support for use of scrambling and CA) */
#define TABLE_ID_CA_MESSAGE_ECM_0			0x80
#define TABLE_ID_CA_MESSAGE_ECM_1			0x81
#define TABLE_ID_CA_MESSAGE_SYSTEM_PRIVATE_1		0x82 /* First */
#define TABLE_ID_CA_MESSAGE_SYSTEM_PRIVATE_N		0x8F /* Last */
/* ... */
/* EN 301 790 (DVB interaction channel for satellite distribution channels) */
#define TABLE_ID_SCT					0xA0
#define TABLE_ID_FCT					0xA1
#define TABLE_ID_TCT					0xA2
#define TABLE_ID_SPT					0xA3
#define TABLE_ID_CMT					0xA4
#define TABLE_ID_TBTP					0xA5
#define TABLE_ID_PCR_PACKET_PAYLOAD			0xA6
#define TABLE_ID_TRANSMISSION_MODE_SUPPORT_PAYLOAD	0xAA
#define TABLE_ID_TIM					0xB0
#define TABLE_ID_LL_FEC_PARITY_DATA_TABLE		0xB1
/* ATSC (FILLME) */
/* ISDB (FILLME) */
/* Unset */
#define TABLE_ID_UNSET 0xFF


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

/* some extra GstFlowReturn values used internally */
#define GST_FLOW_NEED_MORE_DATA   GST_FLOW_CUSTOM_SUCCESS
#define GST_FLOW_LOST_SYNC        GST_FLOW_CUSTOM_SUCCESS_1

#endif /* __GST_MPEG_DEFS_H__ */
