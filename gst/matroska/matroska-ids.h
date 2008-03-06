/* GStreamer Matroska muxer/demuxer
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * matroska-ids.h: matroska file/stream data IDs
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
 */

#ifndef __GST_MATROSKA_IDS_H__
#define __GST_MATROSKA_IDS_H__

#include <gst/gst.h>

#include "ebml-ids.h"

/*
 * Matroska element IDs. max. 32-bit.
 */

/* toplevel segment */
#define GST_MATROSKA_ID_SEGMENT    0x18538067

/* matroska top-level master IDs */
#define GST_MATROSKA_ID_INFO       0x1549A966
#define GST_MATROSKA_ID_TRACKS     0x1654AE6B
#define GST_MATROSKA_ID_CUES       0x1C53BB6B
#define GST_MATROSKA_ID_TAGS       0x1254C367
#define GST_MATROSKA_ID_SEEKHEAD   0x114D9B74
#define GST_MATROSKA_ID_CLUSTER    0x1F43B675

/* IDs in the info master */
#define GST_MATROSKA_ID_TIMECODESCALE 0x2AD7B1
#define GST_MATROSKA_ID_DURATION   0x4489
#define GST_MATROSKA_ID_WRITINGAPP 0x5741
#define GST_MATROSKA_ID_MUXINGAPP  0x4D80
#define GST_MATROSKA_ID_DATEUTC    0x4461
#define GST_MATROSKA_ID_SEGMENTUID 0x73A4

/* ID in the tracks master */
#define GST_MATROSKA_ID_TRACKENTRY 0xAE

/* IDs in the trackentry master */
#define GST_MATROSKA_ID_TRACKNUMBER 0xD7
#define GST_MATROSKA_ID_TRACKUID   0x73C5
#define GST_MATROSKA_ID_TRACKTYPE  0x83
#define GST_MATROSKA_ID_TRACKAUDIO 0xE1
#define GST_MATROSKA_ID_TRACKVIDEO 0xE0
#define GST_MATROSKA_ID_CODECID    0x86
#define GST_MATROSKA_ID_CODECPRIVATE 0x63A2
#define GST_MATROSKA_ID_CODECNAME  0x258688
#define GST_MATROSKA_ID_CODECINFOURL 0x3B4040
#define GST_MATROSKA_ID_CODECDOWNLOADURL 0x26B240
#define GST_MATROSKA_ID_TRACKNAME  0x536E
#define GST_MATROSKA_ID_TRACKLANGUAGE 0x22B59C
#define GST_MATROSKA_ID_TRACKFLAGENABLED 0xB9
#define GST_MATROSKA_ID_TRACKFLAGDEFAULT 0x88
#define GST_MATROSKA_ID_TRACKFLAGLACING 0x9C
#define GST_MATROSKA_ID_TRACKMINCACHE 0x6DE7
#define GST_MATROSKA_ID_TRACKMAXCACHE 0x6DF8
#define GST_MATROSKA_ID_TRACKDEFAULTDURATION 0x23E383
#define GST_MATROSKA_ID_CONTENTENCODINGS 0x6D80

/* IDs in the trackvideo master */
#define GST_MATROSKA_ID_VIDEOFRAMERATE 0x2383E3
#define GST_MATROSKA_ID_VIDEODISPLAYWIDTH 0x54B0
#define GST_MATROSKA_ID_VIDEODISPLAYHEIGHT 0x54BA
#define GST_MATROSKA_ID_VIDEOPIXELWIDTH 0xB0
#define GST_MATROSKA_ID_VIDEOPIXELHEIGHT 0xBA
#define GST_MATROSKA_ID_VIDEOFLAGINTERLACED 0x9A
#define GST_MATROSKA_ID_VIDEOSTEREOMODE 0x53B9
#define GST_MATROSKA_ID_VIDEOASPECTRATIO 0x54B3
#define GST_MATROSKA_ID_VIDEOCOLOURSPACE 0x2EB524

/* IDs in the trackaudio master */
#define GST_MATROSKA_ID_AUDIOSAMPLINGFREQ 0xB5
#define GST_MATROSKA_ID_AUDIOBITDEPTH 0x6264
#define GST_MATROSKA_ID_AUDIOCHANNELS 0x9F

/* ID in the cues master */
#define GST_MATROSKA_ID_POINTENTRY 0xBB

/* IDs in the pointentry master */
#define GST_MATROSKA_ID_CUETIME    0xB3
#define GST_MATROSKA_ID_CUETRACKPOSITION 0xB7

/* IDs in the cuetrackposition master */
#define GST_MATROSKA_ID_CUETRACK   0xF7
#define GST_MATROSKA_ID_CUECLUSTERPOSITION 0xF1

/* IDs in the tags master */
#define GST_MATROSKA_ID_TAG        0x7373

/* in the tag master */
#define GST_MATROSKA_ID_SIMPLETAG  0x67C8

/* in the simpletag master */
#define GST_MATROSKA_ID_TAGNAME    0x45A3
#define GST_MATROSKA_ID_TAGSTRING  0x4487

/* IDs in the seekhead master */
#define GST_MATROSKA_ID_SEEKENTRY  0x4DBB

/* IDs in the seekpoint master */
#define GST_MATROSKA_ID_SEEKID     0x53AB
#define GST_MATROSKA_ID_SEEKPOSITION 0x53AC

/* IDs in the cluster master */
#define GST_MATROSKA_ID_CLUSTERTIMECODE 0xE7
#define GST_MATROSKA_ID_BLOCKGROUP 0xA0
#define GST_MATROSKA_ID_SIMPLEBLOCK 0xA3
#define GST_MATROSKA_ID_REFERENCEBLOCK 0xFB

/* IDs in the blockgroup master */
#define GST_MATROSKA_ID_BLOCK      0xA1
#define GST_MATROSKA_ID_BLOCKDURATION 0x9B

/* IDs in the contentencodings master */
#define GST_MATROSKA_ID_CONTENTENCODING 0x6240

/* IDS IN THE CONTENTENCODING MASTER */
#define GST_MATROSKA_ID_CONTENTENCODINGORDER 0X5031
#define GST_MATROSKA_ID_CONTENTENCODINGSCOPE 0X5032
#define GST_MATROSKA_ID_CONTENTENCODINGTYPE  0X5033
#define GST_MATROSKA_ID_CONTENTCOMPRESSION   0X5034
#define GST_MATROSKA_ID_CONTENTENCRYPTION    0X5035

/* IDS IN THE CONTENTCOMPRESSION MASTER */
#define GST_MATROSKA_ID_CONTENTCOMPALGO      0X4254
#define GST_MATROSKA_ID_CONTENTCOMPSETTINGS  0X4255


/*
 * Matroska Codec IDs. Strings.
 */

#define GST_MATROSKA_CODEC_ID_VIDEO_VFW_FOURCC   "V_MS/VFW/FOURCC"
#define GST_MATROSKA_CODEC_ID_VIDEO_UNCOMPRESSED "V_UNCOMPRESSED"
#define GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_SP     "V_MPEG4/ISO/SP"
#define GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP    "V_MPEG4/ISO/ASP"
#define GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_AP     "V_MPEG4/ISO/AP"
#define GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_AVC    "V_MPEG4/ISO/AVC"
#define GST_MATROSKA_CODEC_ID_VIDEO_MSMPEG4V3    "V_MPEG4/MS/V3"
#define GST_MATROSKA_CODEC_ID_VIDEO_MPEG1        "V_MPEG1"
#define GST_MATROSKA_CODEC_ID_VIDEO_MPEG2        "V_MPEG2"
#define GST_MATROSKA_CODEC_ID_VIDEO_MJPEG        "V_MJPEG"
#define GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO1   "V_REAL/RV10"
#define GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO2   "V_REAL/RV20"
#define GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO3   "V_REAL/RV30"
#define GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO4   "V_REAL/RV40"
#define GST_MATROSKA_CODEC_ID_VIDEO_THEORA       "V_THEORA"
#define GST_MATROSKA_CODEC_ID_VIDEO_DIRAC        "V_DIRAC"
/* TODO: Quicktime */

#define GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L1     "A_MPEG/L1"
#define GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L2     "A_MPEG/L2"
#define GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L3     "A_MPEG/L3"
#define GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE   "A_PCM/INT/BIG"
#define GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE   "A_PCM/INT/LIT"
#define GST_MATROSKA_CODEC_ID_AUDIO_PCM_FLOAT    "A_PCM/FLOAT/IEEE"
#define GST_MATROSKA_CODEC_ID_AUDIO_AC3          "A_AC3"
#define GST_MATROSKA_CODEC_ID_AUDIO_DTS          "A_DTS"
#define GST_MATROSKA_CODEC_ID_AUDIO_VORBIS       "A_VORBIS"
#define GST_MATROSKA_CODEC_ID_AUDIO_FLAC         "A_FLAC"
#define GST_MATROSKA_CODEC_ID_AUDIO_ACM          "A_MS/ACM"
#define GST_MATROSKA_CODEC_ID_AUDIO_MPEG2        "A_AAC/MPEG2/"
#define GST_MATROSKA_CODEC_ID_AUDIO_MPEG4        "A_AAC/MPEG4/"
#define GST_MATROSKA_CODEC_ID_AUDIO_TTA          "A_TTA1"
#define GST_MATROSKA_CODEC_ID_AUDIO_WAVPACK4     "A_WAVPACK4"
#define GST_MATROSKA_CODEC_ID_AUDIO_REAL_14_4    "A_REAL/28_8"
#define GST_MATROSKA_CODEC_ID_AUDIO_REAL_28_8    "A_REAL/28_8"
#define GST_MATROSKA_CODEC_ID_AUDIO_REAL_COOK    "A_REAL/COOK"
#define GST_MATROSKA_CODEC_ID_AUDIO_REAL_SIPR    "A_REAL/SIPR"
#define GST_MATROSKA_CODEC_ID_AUDIO_REAL_RALF    "A_REAL/RALF"
#define GST_MATROSKA_CODEC_ID_AUDIO_REAL_ATRC    "A_REAL/ATRC"

/* TODO: AC3-9/10 (?), Musepack, Quicktime */

#define GST_MATROSKA_CODEC_ID_SUBTITLE_UTF8      "S_TEXT/UTF8"
#define GST_MATROSKA_CODEC_ID_SUBTITLE_SSA       "S_TEXT/SSA"
#define GST_MATROSKA_CODEC_ID_SUBTITLE_ASS       "S_TEXT/ASS" 
#define GST_MATROSKA_CODEC_ID_SUBTITLE_USF       "S_TEXT/USF"
#define GST_MATROSKA_CODEC_ID_SUBTITLE_VOBSUB    "S_VOBSUB"

/*
 * Matrodka tags. Strings.
 */

#define GST_MATROSKA_TAG_ID_TITLE    "TITLE"
#define GST_MATROSKA_TAG_ID_AUTHOR   "AUTHOR"
#define GST_MATROSKA_TAG_ID_ALBUM    "ALBUM"
#define GST_MATROSKA_TAG_ID_COMMENTS "COMMENTS"
#define GST_MATROSKA_TAG_ID_BITSPS   "BITSPS"
#define GST_MATROSKA_TAG_ID_ENCODER  "ENCODER"
#define GST_MATROSKA_TAG_ID_DATE     "DATE"
#define GST_MATROSKA_TAG_ID_ISRC     "ISRC"
#define GST_MATROSKA_TAG_ID_COPYRIGHT "COPYRIGHT"

/*
 * Enumerations for various types (mapping from binary
 * value to what it actually means).
 */

typedef enum {
  GST_MATROSKA_TRACK_TYPE_VIDEO    = 0x1,
  GST_MATROSKA_TRACK_TYPE_AUDIO    = 0x2,
  GST_MATROSKA_TRACK_TYPE_COMPLEX  = 0x3,
  GST_MATROSKA_TRACK_TYPE_LOGO     = 0x10,
  GST_MATROSKA_TRACK_TYPE_SUBTITLE = 0x11,
  GST_MATROSKA_TRACK_TYPE_CONTROL  = 0x20,
} GstMatroskaTrackType;

typedef enum {
  GST_MATROSKA_EYE_MODE_MONO  = 0x0,
  GST_MATROSKA_EYE_MODE_RIGHT = 0x1,
  GST_MATROSKA_EYE_MODE_LEFT  = 0x2,
  GST_MATROSKA_EYE_MODE_BOTH  = 0x3,
} GstMatroskaEyeMode;

typedef enum {
  GST_MATROSKA_ASPECT_RATIO_MODE_FREE  = 0x0,
  GST_MATROSKA_ASPECT_RATIO_MODE_KEEP  = 0x1,
  GST_MATROSKA_ASPECT_RATIO_MODE_FIXED = 0x2,
} GstMatroskaAspectRatioMode;

/*
 * These aren't in any way "matroska-form" things,
 * it's just something I use in the muxer/demuxer.
 */

typedef enum {
  GST_MATROSKA_TRACK_ENABLED = (1<<0),
  GST_MATROSKA_TRACK_DEFAULT = (1<<1),
  GST_MATROSKA_TRACK_LACING  = (1<<2),
  GST_MATROSKA_TRACK_SHIFT   = (1<<16)
} GstMatroskaTrackFlags;

typedef enum {
  GST_MATROSKA_VIDEOTRACK_INTERLACED = (GST_MATROSKA_TRACK_SHIFT<<0)
} GstMatroskaVideoTrackFlags;

typedef struct _GstMatroskaTrackContext {
  GstPad       *pad;
  GstCaps      *caps;
  guint         index;
  GstFlowReturn last_flow;

  /* some often-used info */
  gchar        *codec_id, *codec_name, *name, *language;
  gpointer      codec_priv;
  guint         codec_priv_size;
  GstMatroskaTrackType type;
  guint         uid, num;
  GstMatroskaTrackFlags flags;
  guint64       default_duration;
  guint64       pos;

  gboolean      set_discont; /* TRUE = set DISCONT flag on next buffer */

  /* Special flag for Vorbis and Theora, for which we need to send
   * codec_priv first before sending any data, and just testing
   * for time == 0 is not enough to detect that. Used by demuxer */
  gboolean      send_xiph_headers;

  /* Special flag for Flac, for which we need to reconstruct the header
   * buffer from the codec_priv data before sending any data, and just
   * testing for time == 0 is not enough to detect that. Used by demuxer */
  gboolean      send_flac_headers;

  /* Special flag for VobSub, for which we have to send colour table info
   * (if available) first before sending any data, and just testing
   * for time == 0 is not enough to detect that. Used by demuxer */
  gboolean      send_dvd_event;

  /* Special counter for muxer to skip the first N vorbis/theora headers -
   * they are put into codec private data, not muxed into the stream */
  guint         xiph_headers_to_skip;

  /* Tags to send after newsegment event */
  GstTagList   *pending_tags;

  /* A GArray of GstMatroskaTrackEncoding structures which contain the
   * encoding (compression/encryption) settings for this track, if any */
  GArray       *encodings;
} GstMatroskaTrackContext;

typedef struct _GstMatroskaTrackVideoContext {
  GstMatroskaTrackContext parent;

  guint         pixel_width, pixel_height;
  guint         display_width, display_height;
  gdouble       default_fps;
  GstMatroskaEyeMode eye_mode;
  GstMatroskaAspectRatioMode asr_mode;
  guint32       fourcc;
} GstMatroskaTrackVideoContext;

typedef struct _GstMatroskaTrackAudioContext {
  GstMatroskaTrackContext parent;

  guint         samplerate, channels, bitdepth;
} GstMatroskaTrackAudioContext;

typedef struct _GstMatroskaTrackComplexContext {
  GstMatroskaTrackContext parent;

  /* nothing special goes here, apparently */
} GstMatroskaTrackComplexContext;

typedef struct _GstMatroskaTrackSubtitleContext {
  GstMatroskaTrackContext parent;

  gboolean    check_utf8;     /* buffers should be valid UTF-8 */
  gboolean    invalid_utf8;   /* work around broken files      */
} GstMatroskaTrackSubtitleContext;

typedef struct _GstMatroskaIndex {
  guint64        pos;   /* of the corresponding *cluster*! */
  guint16        track; /* reference to 'num' */
  guint64        time;  /* in nanoseconds */
} GstMatroskaIndex;

typedef struct _Wavpack4Header {
  guchar  ck_id [4];     /* "wvpk"                                         */
  guint32 ck_size;       /* size of entire frame (minus 8, of course)      */
  guint16 version;       /* 0x403 for now                                  */
  guint8  track_no;      /* track number (0 if not used, like now)         */
  guint8  index_no;      /* remember these? (0 if not used, like now)      */
  guint32 total_samples; /* for entire file (-1 if unknown)                */
  guint32 block_index;   /* index of first sample in block (to file begin) */
  guint32 block_samples; /* # samples in this block                        */
  guint32 flags;         /* various flags for id and decoding              */
  guint32 crc;           /* crc for actual decoded data                    */
} Wavpack4Header;

typedef struct _GstMatroskaTrackEncoding {
  guint   order;
  guint   scope     : 3;
  guint   type      : 1;
  guint   comp_algo : 2;
  guint8 *comp_settings;
  guint   comp_settings_length;
} GstMatroskaTrackEncoding;

gboolean gst_matroska_track_init_video_context    (GstMatroskaTrackContext ** p_context);
gboolean gst_matroska_track_init_audio_context    (GstMatroskaTrackContext ** p_context);
gboolean gst_matroska_track_init_subtitle_context (GstMatroskaTrackContext ** p_context);
gboolean gst_matroska_track_init_complex_context  (GstMatroskaTrackContext ** p_context);

#endif /* __GST_MATROSKA_IDS_H__ */
