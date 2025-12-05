/* GStreamer
 *
 * Copyright (c) 2025 Centricular Ltd
 *  @author: Taruntej Kanakamalla <tarun@centricular.com>
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

#ifndef __FLV_DEFS_H__
#define __FLV_DEFS_H__

typedef enum
{
  FLV_AUDIO_PACKET_TYPE_SEQUENCE_START = 0,
  FLV_AUDIO_PACKET_TYPE_CODED_FRAMES = 1,
  FLV_AUDIO_PACKET_TYPE_SEQUENCE_END = 2,
  FLV_AUDIO_PACKET_TYPE_RESERVED = 3,
  FLV_AUDIO_PACKET_TYPE_MULTICHANNELCONFIG = 4,
  FLV_AUDIO_PACKET_TYPE_MULTITRACK = 5,
  FLV_AUDIO_PACKET_TYPE_MODEX = 6,
} GstEFlvAudioPacketType;

typedef enum
{
  FLV_VIDEO_PACKET_TYPE_SEQUENCE_START = 0,
  FLV_VIDEO_PACKET_TYPE_CODED_FRAMES = 1,
  FLV_VIDEO_PACKET_TYPE_SEQUENCE_END = 2,
  FLV_VIDEO_PACKET_TYPE_CODED_FRAMES_X = 3,
  FLV_VIDEO_PACKET_TYPE_METADATA = 4,
  FLV_VIDEO_PACKET_TYPE_MPEG2TS_SEQUENCE_START = 5,
  FLV_VIDEO_PACKET_TYPE_MULTITRACK = 6,
  FLV_VIDEO_PACKET_TYPE_MODEX = 7,
} GstEFlvVideoPacketType;

typedef enum
{
  FLV_AV_MULTITRACK_TYPE_ONETRACK = 0,
  FLV_AV_MULTITRACK_TYPE_MANYTRACKS = 1,
  FLV_AV_MULTITRACK_TYPE_MANYTRACKS_MANYCODECS = 2,
} GstEFlvAvMultiTrackType;

typedef enum
{
  FLV_VIDEO_FRAME_TYPE_KEYFRAME = 1,
  FLV_VIDEO_FRAME_TYPE_INTERFRAME = 2,
  FLV_VIDEO_FRAME_TYPE_DISPOSABLE_INTERFAME = 3,
  FLV_VIDEO_FRAME_TYPE_GENERATED_KEYFRAME = 4,
  FLV_VIDEO_FRAME_TYPE_INFO_COMMAND = 5,
} GstFlvVideoFrameType;

typedef enum
{
  FLV_AUDIO_CODEC_LINEAR_PCM = 0,               /* 0 = Linear PCM, platform-endian */
  FLV_AUDIO_CODEC_ADPCM = 1,                    /* 1 = ADPCM */
  FLV_AUDIO_CODEC_MP3 = 2,                      /* 2 = MP3 */
  FLV_AUDIO_CODEC_LINEAR_PCM_LE = 3,            /* 3 = Linear PCM, little-endian */
  FLV_AUDIO_CODEC_NELLYMOSER_16K = 4,           /* 4 = Nellymoser 16 kHz mono  */
  FLV_AUDIO_CODEC_NELLYMOSER_8K = 5,            /* 5 = Nellymoser 8 kHz mono  */
  FLV_AUDIO_CODEC_NELLYMOSER = 6,               /* 6 = Nellymoser */
  FLV_AUDIO_CODEC_G711_ALAW = 7,                /* 7 = G.711 A-law logarithmic PCM  */
  FLV_AUDIO_CODEC_G711_MULAW = 8,               /* 8 = G.711 mu-law logarithmic PCM */
  FLV_EXTENDED_AUDIO_HEADER = 9,                /* 9 = ExHeader (eFLV)  */
  FLV_AUDIO_CODEC_AAC = 10,                     /* 10 = AAC */
  FLV_AUDIO_CODEC_SPEEX = 11,                   /* 11 = Speex  */
  FLV_AUDIO_CODEC_RESERVED_12 = 12,             /* 12 = Reserved */
  FLV_AUDIO_CODEC_RESERVED_13 = 13,             /* 13 = Reserved  */
  FLV_AUDIO_CODEC_MP3_8K = 14,                  /* 14 = MP3 8 kHz */
  FLV_AUDIO_CODEC_ATIVE = 15,                  /* 15 = Device-specific sound */
  // FOURCC codec ID as per Enhanced RTMP (V2)
  FLV_AUDIO_CODEC_MP3_FOURCC = GST_MAKE_FOURCC('.','m','p','3'),
  FLV_AUDIO_CODEC_AAC_FOURCC = GST_MAKE_FOURCC('m','p','4','a'),
} GstFlvSoundFormat;

typedef enum
{
  FLV_VIDEO_CODEC_FLASH_VIDEO = 2,
  FLV_VIDEO_CODEC_FLASH_SCREEN = 3,
  FLV_VIDEO_CODEC_VP6_FLASH = 4,
  FLV_VIDEO_CODEC_VP6_ALPHA = 5,
  FLV_VIDEO_CODEC_H264_AVC1 = 7,
  // rtmp enhanced spec - define by fourcc code
  FLV_VIDEO_CODEC_H265_HVC1_FOURCC = GST_MAKE_FOURCC('h','v','c','1'),
  FLV_VIDEO_CODEC_H264_AVC1_FOURCC = GST_MAKE_FOURCC('a','v','c','1'),
} GstFlvVideoCodec;

typedef enum
{
  UNSPECIFIED = 0,
  FLV_AUDIO_CHANNEL_ORDER_NATIVE = 1,
  FLV_AUDIO_CHANNEL_ORDER_CUSTOM = 2,
} GstFlvAudioChannelOrder;

#define MESSAGE_HEADER_LEN 11
#define EXHEADER_PLUS_PACKETYPE_LEN 1
#define MUTLITRACKTYPE_PLUS_PACKETYPE_LEN 1
#define FOURCC_LEN 4
#define TRACK_ID_LEN 1
#define TAG_SIZE_LEN 4
#define MAX_TRACKS  G_MAXUINT8 + 1
#define FOURCC_INVALID 0xFFFFFFFF

#endif /* __FLV_DEFS_H__ */
