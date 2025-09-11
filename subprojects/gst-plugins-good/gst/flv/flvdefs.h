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
  SEQUENCE_START = 0,
  CODED_FRAMES = 1,
  SEQUENCE_END = 2,
  RESERVED = 3,
  MULTICHANNELCONFIG = 4,
  MULTITRACK = 5,
  MODEX = 6,
  RESERVED_AUDIOPACKETTYPE = 15,
} GstEFlvAudioPacketType;

typedef enum
{
  ONETRACK = 0,
  MANYTRACKS = 1,
  MANYTRACKS_MANYCODECS = 2,
  RESERVED_AVMULTI_TRACKTYPE = 15,
} GstEFlvAvMultiTrackType;

typedef enum
{
  LINEAR_PCM = 0,                                         /* 0 = Linear PCM, platform-endian */
  ADPCM = 1,                                              /* 1 = ADPCM */
  MP3 = 2,                                                /* 2 = MP3 */
  LINEAR_PCM_LE = 3,                                      /* 3 = Linear PCM, little-endian */
  NELLYMOSER_16K = 4,                                     /* 4 = Nellymoser 16 kHz mono  */
  NELLYMOSER_8K = 5,                                      /* 5 = Nellymoser 8 kHz mono  */
  NELLYMOSER = 6,                                         /* 6 = Nellymoser */
  G711_ALAW = 7,                                          /* 7 = G.711 A-law logarithmic PCM  */
  G711_MULAW = 8,                                         /* 8 = G.711 mu-law logarithmic PCM */
  EXTENDED_AUDIO_HEADER = 9,                              /* 9 = ExHeader (eFLV)  */
  AAC = 10,                                                /* 10 = AAC */
  SPEEX = 11,                                              /* 11 = Speex  */
  RESERVED_12 = 12,                                        /* 12 = Reserved */
  RESERVED_13 = 13,                                        /* 13 = Reserved  */
  MP3_8K = 14,                                             /* 14 = MP3 8 kHz */
  NATIVE = 15,                                             /* 15 = Device-specific sound*/
} GstFlvSoundFormat;

typedef enum
{
  UNSPECIFIED = 0,
  NATIVE_CH_ORDER = 1,
  CUSTOM_CH_ORDER = 2,
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