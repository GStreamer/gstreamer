/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Library       <2001> Thomas Vander Stichele <thomas@apestaart.org>
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

#include <gst/gst.h>

#ifndef __GST_AUDIO_AUDIO_H__
#define __GST_AUDIO_AUDIO_H__

#include <gst/audio/multichannel.h>

G_BEGIN_DECLS

/**
 * GstAudioFormat:
 * @GST_AUDIO_FORMAT_UNKNOWN: unknown audio format
 * @GST_AUDIO_FORMAT_S8: sample
 * @GST_AUDIO_FORMAT_U8: sample
 * @GST_AUDIO_FORMAT_S16_LE: sample
 * @GST_AUDIO_FORMAT_S16_BE: sample
 * @GST_AUDIO_FORMAT_U16_LE: sample
 * @GST_AUDIO_FORMAT_U16_BE: sample
 * @GST_AUDIO_FORMAT_S24_LE: sample
 * @GST_AUDIO_FORMAT_S24_BE: sample
 * @GST_AUDIO_FORMAT_U24_LE: sample
 * @GST_AUDIO_FORMAT_U24_BE: sample
 * @GST_AUDIO_FORMAT_S32_LE: sample
 * @GST_AUDIO_FORMAT_S32_BE: sample
 * @GST_AUDIO_FORMAT_U32_LE: sample
 * @GST_AUDIO_FORMAT_U32_BE: sample
 * @GST_AUDIO_FORMAT_S24_3LE: sample
 * @GST_AUDIO_FORMAT_S24_3BE: sample
 * @GST_AUDIO_FORMAT_U24_3LE: sample
 * @GST_AUDIO_FORMAT_U24_3BE: sample
 * @GST_AUDIO_FORMAT_S20_3LE: sample
 * @GST_AUDIO_FORMAT_S20_3BE: sample
 * @GST_AUDIO_FORMAT_U20_3LE: sample
 * @GST_AUDIO_FORMAT_U20_3BE: sample
 * @GST_AUDIO_FORMAT_S18_3LE: sample
 * @GST_AUDIO_FORMAT_S18_3BE: sample
 * @GST_AUDIO_FORMAT_U18_3LE: sample
 * @GST_AUDIO_FORMAT_U18_3BE: sample
 * @GST_AUDIO_FORMAT_F32_LE: sample
 * @GST_AUDIO_FORMAT_F32_BE: sample
 * @GST_AUDIO_FORMAT_F64_LE: sample
 * @GST_AUDIO_FORMAT_F64_BE: sample
 *
 * Enum value describing the most common audio formats.
 *
 * Since: 0.10.36
 */
typedef enum {
  GST_AUDIO_FORMAT_UNKNOWN,
  /* 8 bit */
  GST_AUDIO_FORMAT_S8,
  GST_AUDIO_FORMAT_U8,
  /* 16 bit */
  GST_AUDIO_FORMAT_S16_LE,
  GST_AUDIO_FORMAT_S16_BE,
  GST_AUDIO_FORMAT_U16_LE,
  GST_AUDIO_FORMAT_U16_BE,
  /* 24 bit in low 3 bytes of 32 bits*/
  GST_AUDIO_FORMAT_S24_LE,
  GST_AUDIO_FORMAT_S24_BE,
  GST_AUDIO_FORMAT_U24_LE,
  GST_AUDIO_FORMAT_U24_BE,
  /* 32 bit */
  GST_AUDIO_FORMAT_S32_LE,
  GST_AUDIO_FORMAT_S32_BE,
  GST_AUDIO_FORMAT_U32_LE,
  GST_AUDIO_FORMAT_U32_BE,
  /* 24 bit in 3 bytes*/
  GST_AUDIO_FORMAT_S24_3LE,
  GST_AUDIO_FORMAT_S24_3BE,
  GST_AUDIO_FORMAT_U24_3LE,
  GST_AUDIO_FORMAT_U24_3BE,
  /* 20 bit in 3 bytes*/
  GST_AUDIO_FORMAT_S20_3LE,
  GST_AUDIO_FORMAT_S20_3BE,
  GST_AUDIO_FORMAT_U20_3LE,
  GST_AUDIO_FORMAT_U20_3BE,
  /* 18 bit in 3 bytes*/
  GST_AUDIO_FORMAT_S18_3LE,
  GST_AUDIO_FORMAT_S18_3BE,
  GST_AUDIO_FORMAT_U18_3LE,
  GST_AUDIO_FORMAT_U18_3BE,
  /* float */
  GST_AUDIO_FORMAT_F32_LE,
  GST_AUDIO_FORMAT_F32_BE,
  GST_AUDIO_FORMAT_F64_LE,
  GST_AUDIO_FORMAT_F64_BE,
#if G_BYTE_ORDER == G_BIG_ENDIAN
  GST_AUDIO_FORMAT_S16 = GST_AUDIO_FORMAT_S16_BE,
  GST_AUDIO_FORMAT_U16 = GST_AUDIO_FORMAT_U16_BE,
  GST_AUDIO_FORMAT_S24 = GST_AUDIO_FORMAT_S24_BE,
  GST_AUDIO_FORMAT_U24 = GST_AUDIO_FORMAT_U24_BE,
  GST_AUDIO_FORMAT_S32 = GST_AUDIO_FORMAT_S32_BE,
  GST_AUDIO_FORMAT_U32 = GST_AUDIO_FORMAT_U32_BE,
  GST_AUDIO_FORMAT_S24_3 = GST_AUDIO_FORMAT_S24_3BE,
  GST_AUDIO_FORMAT_U24_3 = GST_AUDIO_FORMAT_U24_3BE,
  GST_AUDIO_FORMAT_S20_3 = GST_AUDIO_FORMAT_S20_3BE,
  GST_AUDIO_FORMAT_U20_3 = GST_AUDIO_FORMAT_U20_3BE,
  GST_AUDIO_FORMAT_S18_3 = GST_AUDIO_FORMAT_S18_3BE,
  GST_AUDIO_FORMAT_U18_3 = GST_AUDIO_FORMAT_U18_3BE,
  GST_AUDIO_FORMAT_F32 = GST_AUDIO_FORMAT_F32_BE,
  GST_AUDIO_FORMAT_F64 = GST_AUDIO_FORMAT_F64_BE
#elif G_BYTE_ORDER == G_LITTLE_ENDIAN
  GST_AUDIO_FORMAT_S16 = GST_AUDIO_FORMAT_S16_LE,
  GST_AUDIO_FORMAT_U16 = GST_AUDIO_FORMAT_U16_LE,
  GST_AUDIO_FORMAT_S24 = GST_AUDIO_FORMAT_S24_LE,
  GST_AUDIO_FORMAT_U24 = GST_AUDIO_FORMAT_U24_LE,
  GST_AUDIO_FORMAT_S32 = GST_AUDIO_FORMAT_S32_LE,
  GST_AUDIO_FORMAT_U32 = GST_AUDIO_FORMAT_U32_LE,
  GST_AUDIO_FORMAT_S24_3 = GST_AUDIO_FORMAT_S24_3LE,
  GST_AUDIO_FORMAT_U24_3 = GST_AUDIO_FORMAT_U24_3LE,
  GST_AUDIO_FORMAT_S20_3 = GST_AUDIO_FORMAT_S20_3LE,
  GST_AUDIO_FORMAT_U20_3 = GST_AUDIO_FORMAT_U20_3LE,
  GST_AUDIO_FORMAT_S18_3 = GST_AUDIO_FORMAT_S18_3LE,
  GST_AUDIO_FORMAT_U18_3 = GST_AUDIO_FORMAT_U18_3LE,
  GST_AUDIO_FORMAT_F32 = GST_AUDIO_FORMAT_F32_LE,
  GST_AUDIO_FORMAT_F64 = GST_AUDIO_FORMAT_F64_LE
#endif
} GstAudioFormat;

/* FIXME: need GTypes */
typedef struct _GstAudioFormatInfo GstAudioFormatInfo;
typedef struct _GstAudioInfo GstAudioInfo;

/**
 * GstAudioFormatFlags:
 * @GST_AUDIO_FORMAT_FLAG_INTEGER: integer samples
 * @GST_AUDIO_FORMAT_FLAG_FLOAT: float samples
 * @GST_AUDIO_FORMAT_FLAG_SIGNED: signed samples
 * @GST_AUDIO_FORMAT_FLAG_COMPLEX: complex layout
 *
 * The different audio flags that a format info can have.
 *
 * Since: 0.10.36
 */
typedef enum
{
  GST_AUDIO_FORMAT_FLAG_INTEGER  = (1 << 0),
  GST_AUDIO_FORMAT_FLAG_FLOAT    = (1 << 1),
  GST_AUDIO_FORMAT_FLAG_SIGNED   = (1 << 2),
  GST_AUDIO_FORMAT_FLAG_COMPLEX  = (1 << 4)
} GstAudioFormatFlags;

/**
 * GstAudioFormatInfo:
 * @format: #GstAudioFormat
 * @name: string representation of the format
 * @flags: #GstAudioFormatFlags
 * @endianness: the endianness
 * @width: amount of bits used for one sample
 * @depth: amount of valid bits in @width
 * @silence: @width/8 bytes with 1 silent sample
 *
 * Information for an audio format.
 *
 * Since: 0.10.36
 */
struct _GstAudioFormatInfo {
  GstAudioFormat      format;
  const gchar *       name;
  GstAudioFormatFlags flags;
  gint                endianness;
  gint                width;
  gint                depth;
  guint8              silence[8];
  /*< private >*/
  guint               padding_i[4];
  gpointer            padding_p[4];
};

#define GST_AUDIO_FORMAT_INFO_FORMAT(info)       ((info)->format)
#define GST_AUDIO_FORMAT_INFO_NAME(info)         ((info)->name)
#define GST_AUDIO_FORMAT_INFO_FLAGS(info)        ((info)->flags)

#define GST_AUDIO_FORMAT_INFO_IS_INTEGER(info)   !!((info)->flags & GST_AUDIO_FORMAT_FLAG_INTEGER)
#define GST_AUDIO_FORMAT_INFO_IS_FLOAT(info)     !!((info)->flags & GST_AUDIO_FORMAT_FLAG_FLOAT)
#define GST_AUDIO_FORMAT_INFO_IS_SIGNED(info)    !!((info)->flags & GST_AUDIO_FORMAT_FLAG_SIGNED)

#define GST_AUDIO_FORMAT_INFO_ENDIANNESS(info)   ((info)->endianness)
#define GST_AUDIO_FORMAT_INFO_IS_LE(info)        ((info)->endianness == G_LITTLE_ENDIAN)
#define GST_AUDIO_FORMAT_INFO_IS_BE(info)        ((info)->endianness == G_BIG_ENDIAN)
#define GST_AUDIO_FORMAT_INFO_WIDTH(info)        ((info)->width)
#define GST_AUDIO_FORMAT_INFO_DEPTH(info)        ((info)->depth)

const GstAudioFormatInfo * gst_audio_format_get_info (GstAudioFormat format) G_GNUC_CONST;

/**
 * GstAudioFlags:
 * @GST_AUDIO_FLAG_NONE: no valid flag
 * @GST_AUDIO_FLAG_DEFAULT_POSITIONS: unpositioned audio layout, position array
 *     contains the default layout (meaning that the channel layout was not
 *     explicitly specified in the caps)
 *
 * Extra audio flags
 *
 * Since: 0.10.36
 */
typedef enum {
  GST_AUDIO_FLAG_NONE         = 0,
  GST_AUDIO_FLAG_DEFAULT_POSITIONS = (1 << 0)
} GstAudioFlags;

/**
 * GstAudioInfo:
 * @finfo: the format info of the audio
 * @flags: additional audio flags
 * @rate: the audio sample rate
 * @channels: the number of channels
 * @bpf: the number of bytes for one frame, this is the size of one
 *         sample * @channels
 * @positions: the positions for each channel
 *
 * Information describing audio properties. This information can be filled
 * in from GstCaps with gst_audio_info_from_caps().
 *
 * Use the provided macros to access the info in this structure.
 *
 * Since: 0.10.36
 */
struct _GstAudioInfo {
  const GstAudioFormatInfo *finfo;
  GstAudioFlags             flags;
  gint                      rate;
  gint                      channels;
  gint                      bpf;
  GstAudioChannelPosition   position[64];
};

#define GST_AUDIO_INFO_FORMAT(i)             (GST_AUDIO_FORMAT_INFO_FORMAT((i)->finfo))
#define GST_AUDIO_INFO_NAME(i)               (GST_AUDIO_FORMAT_INFO_NAME((i)->finfo))
#define GST_AUDIO_INFO_WIDTH(i)              (GST_AUDIO_FORMAT_INFO_WIDTH((i)->finfo))
#define GST_AUDIO_INFO_DEPTH(i)              (GST_AUDIO_FORMAT_INFO_DEPTH((i)->finfo))
#define GST_AUDIO_INFO_BPS(info)             (GST_AUDIO_INFO_DEPTH(info) >> 3)

#define GST_AUDIO_INFO_FLAGS(info)           ((info)->flags)
#define GST_AUDIO_INFO_HAS_DEFAULT_POSITIONS(info) ((info)->flags & GST_AUDIO_FLAG_DEFAULT_POSITIONS)

#define GST_AUDIO_INFO_RATE(info)            ((info)->rate)
#define GST_AUDIO_INFO_CHANNELS(info)        ((info)->channels)
#define GST_AUDIO_INFO_BPF(info)             ((info)->bpf)
#define GST_AUDIO_INFO_POSITION(info,c)      ((info)->position[c])

void           gst_audio_info_init  (GstAudioInfo * info);
void           gst_audio_info_clear (GstAudioInfo * info);

GstAudioInfo * gst_audio_info_copy  (GstAudioInfo * info);
void           gst_audio_info_free  (GstAudioInfo * info);

gboolean       gst_audio_info_from_caps (GstAudioInfo * info, const GstCaps * caps);
GstCaps *      gst_audio_info_to_caps   (GstAudioInfo * info);

gboolean       gst_audio_info_convert   (GstAudioInfo * info,
                                         GstFormat src_fmt,  gint64   src_val,
                                         GstFormat dest_fmt, gint64 * dest_val);

/* For people that are looking at this source: the purpose of these defines is
 * to make GstCaps a bit easier, in that you don't have to know all of the
 * properties that need to be defined. you can just use these macros. currently
 * (8/01) the only plugins that use these are the passthrough, speed, volume,
 * adder, and [de]interleave plugins. These are for convenience only, and do not
 * specify the 'limits' of GStreamer. you might also use these definitions as a
 * base for making your own caps, if need be.
 *
 * For example, to make a source pad that can output streams of either mono
 * float or any channel int:
 *
 *  template = gst_pad_template_new
 *    ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
 *    gst_caps_append(gst_caps_new ("sink_int",  "audio/x-raw-int",
 *                                  GST_AUDIO_INT_PAD_TEMPLATE_PROPS),
 *                    gst_caps_new ("sink_float", "audio/x-raw-float",
 *                                  GST_AUDIO_FLOAT_PAD_TEMPLATE_PROPS)),
 *    NULL);
 *
 *  sinkpad = gst_pad_new_from_template(template, "sink");
 *
 * Andy Wingo, 18 August 2001
 * Thomas, 6 September 2002 */

/* conversion macros */
/**
 * GST_FRAMES_TO_CLOCK_TIME:
 * @frames: sample frames
 * @rate: sampling rate
 * 
 * Calculate clocktime from sample @frames and @rate.
 */
#define GST_FRAMES_TO_CLOCK_TIME(frames, rate) \
  ((GstClockTime) gst_util_uint64_scale_round (frames, GST_SECOND, rate))

/**
 * GST_CLOCK_TIME_TO_FRAMES:
 * @clocktime: clock time
 * @rate: sampling rate
 * 
 * Calculate frames from @clocktime and sample @rate.
 */
#define GST_CLOCK_TIME_TO_FRAMES(clocktime, rate) \
  gst_util_uint64_scale_round (clocktime, rate, GST_SECOND)

/**
 * GST_AUDIO_DEF_RATE:
 * 
 * Standard sampling rate used in consumer audio.
 */
#define GST_AUDIO_DEF_RATE 44100

/**
 * GST_AUDIO_INT_PAD_TEMPLATE_CAPS:
 * 
 * Template caps for integer audio. Can be used when defining a 
 * #GstStaticPadTemplate
 */
#define GST_AUDIO_INT_PAD_TEMPLATE_CAPS \
  "audio/x-raw-int, " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) [ 1, MAX ], " \
  "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
  "width = (int) { 8, 16, 24, 32 }, " \
  "depth = (int) [ 1, 32 ], " \
  "signed = (boolean) { true, false }"

/**
 * GST_AUDIO_INT_STANDARD_PAD_TEMPLATE_CAPS:
 * 
 * Template caps for 16bit integer stereo audio in native byte-order.
 * Can be used when defining a #GstStaticPadTemplate
 */
#define GST_AUDIO_INT_STANDARD_PAD_TEMPLATE_CAPS \
  "audio/x-raw-int, " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) 2, " \
  "endianness = (int) BYTE_ORDER, " \
  "width = (int) 16, " \
  "depth = (int) 16, " \
  "signed = (boolean) true"

/**
 * GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS:
 * 
 * Template caps for float audio. Can be used when defining a 
 * #GstStaticPadTemplate
 */
#define GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS \
  "audio/x-raw-float, " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) [ 1, MAX ], " \
  "endianness = (int) { LITTLE_ENDIAN , BIG_ENDIAN }, " \
  "width = (int) { 32, 64 }"

/**
 * GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_CAPS:
 * 
 * Template caps for 32bit float mono audio in native byte-order.
 * Can be used when defining a #GstStaticPadTemplate
 */
#define GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_CAPS \
  "audio/x-raw-float, " \
  "width = (int) 32, " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) 1, " \
  "endianness = (int) BYTE_ORDER"

/*
 * this library defines and implements some helper functions for audio
 * handling
 */

/* get byte size of audio frame (based on caps of pad */
int      gst_audio_frame_byte_size      (GstPad* pad);

/* get length in frames of buffer */
long     gst_audio_frame_length         (GstPad* pad, GstBuffer* buf);

GstClockTime gst_audio_duration_from_pad_buffer (GstPad * pad, GstBuffer * buf);

/* check if the buffer size is a whole multiple of the frame size */
gboolean gst_audio_is_buffer_framed     (GstPad* pad, GstBuffer* buf);

/* functions useful for _getcaps functions */
/**
 * GstAudioFieldFlag:
 * @GST_AUDIO_FIELD_RATE: add rate field to caps
 * @GST_AUDIO_FIELD_CHANNELS: add channels field to caps
 * @GST_AUDIO_FIELD_ENDIANNESS: add endianness field to caps
 * @GST_AUDIO_FIELD_WIDTH: add width field to caps
 * @GST_AUDIO_FIELD_DEPTH: add depth field to caps
 * @GST_AUDIO_FIELD_SIGNED: add signed field to caps
 *
 * Do not use anymore.
 *
 * Deprecated: use gst_structure_set() directly
 */
#ifndef GST_DISABLE_DEPRECATED
typedef enum {
  GST_AUDIO_FIELD_RATE          = (1 << 0),
  GST_AUDIO_FIELD_CHANNELS      = (1 << 1),
  GST_AUDIO_FIELD_ENDIANNESS    = (1 << 2),
  GST_AUDIO_FIELD_WIDTH         = (1 << 3),
  GST_AUDIO_FIELD_DEPTH         = (1 << 4),
  GST_AUDIO_FIELD_SIGNED        = (1 << 5)
} GstAudioFieldFlag;
#endif

#ifndef GST_DISABLE_DEPRECATED
void gst_audio_structure_set_int (GstStructure *structure, GstAudioFieldFlag flag);
#endif /* GST_DISABLE_DEPRECATED */

GstBuffer *gst_audio_buffer_clip (GstBuffer *buffer, GstSegment *segment, gint rate, gint frame_size);

G_END_DECLS

#endif /* __GST_AUDIO_AUDIO_H__ */
