/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Library       <2001> Thomas Vander Stichele <thomas@apestaart.org>
 *               <2011> Wim Taymans <wim.taymans@gmail.com>
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
 */
typedef enum
{
  GST_AUDIO_FORMAT_FLAG_INTEGER  = (1 << 0),
  GST_AUDIO_FORMAT_FLAG_FLOAT    = (1 << 1),
  GST_AUDIO_FORMAT_FLAG_SIGNED   = (1 << 2),
  GST_AUDIO_FORMAT_FLAG_COMPLEX  = (1 << 4)
} GstAudioFormatFlags;

/**
 * GstAudioFormatUnpack:
 * @info: a #GstAudioFormatInfo
 * @dest: a destination array
 * @data: pointer to the audio data
 * @length: the amount of samples to unpack.
 *
 * Unpacks @length samples from the given data of format @info.
 * The samples will be unpacked into @dest which each channel
 * interleaved. @dest should at least be big enough to hold @length *
 * channels * unpack_size bytes.
 */
typedef void (*GstAudioFormatUnpack)         (GstAudioFormatInfo *info, gpointer dest,
                                              const gpointer data, gint length);
/**
 * GstAudioFormatPack:
 * @info: a #GstAudioFormatInfo
 * @src: a source array
 * @data: pointer to the destination data
 * @length: the amount of samples to pack.
 *
 * Packs @length samples from @src to the data array in format @info.
 * The samples from source have each channel interleaved
 * and will be packed into @data.
 */
typedef void (*GstAudioFormatPack)           (GstAudioFormatInfo *info, const gpointer src,
                                              gpointer data, gint length);

/**
 * GstAudioFormatInfo:
 * @format: #GstAudioFormat
 * @name: string representation of the format
 * @flags: #GstAudioFormatFlags
 * @endianness: the endianness
 * @width: amount of bits used for one sample
 * @depth: amount of valid bits in @width
 * @silence: @width/8 bytes with 1 silent sample
 * @unpack_size: number of bytes for the unpack functions
 * @unpack_func: function to unpack samples
 * @pack_func: function to pack samples
 *
 * Information for an audio format.
 */
struct _GstAudioFormatInfo {
  GstAudioFormat format;
  const gchar *name;
  GstAudioFormatFlags flags;
  gint endianness;
  gint width;
  gint depth;
  guint8 silence[8];
  guint unpack_size;
  GstAudioFormatUnpack unpack_func;
  GstAudioFormatPack pack_func;
};

#define GST_AUDIO_FORMAT_INFO_FORMAT(info)       ((info)->format)
#define GST_AUDIO_FORMAT_INFO_NAME(info)         ((info)->name)
#define GST_AUDIO_FORMAT_INFO_FLAGS(info)        ((info)->flags)

#define GST_AUDIO_FORMAT_INFO_IS_INTEGER(info)   ((info)->flags & GST_AUDIO_FORMAT_FLAG_INTEGER)
#define GST_AUDIO_FORMAT_INFO_IS_FLOAT(info)     ((info)->flags & GST_AUDIO_FORMAT_FLAG_FLOAT)
#define GST_AUDIO_FORMAT_INFO_IS_SIGNED(info)    ((info)->flags & GST_AUDIO_FORMAT_FLAG_SIGNED)

#define GST_AUDIO_FORMAT_INFO_ENDIANNESS(info)   ((info)->endianness)
#define GST_AUDIO_FORMAT_INFO_IS_LE(info)        ((info)->endianness == G_LITTLE_ENDIAN)
#define GST_AUDIO_FORMAT_INFO_IS_BE(info)        ((info)->endianness == G_BIG_ENDIAN)
#define GST_AUDIO_FORMAT_INFO_WIDTH(info)        ((info)->width)
#define GST_AUDIO_FORMAT_INFO_DEPTH(info)        ((info)->depth)


GstAudioFormat gst_audio_format_build_integer    (gboolean sign, gint endianness,
                                                  gint width, gint depth) G_GNUC_CONST;

GstAudioFormat gst_audio_format_from_string      (const gchar *format) G_GNUC_CONST;
const gchar *  gst_audio_format_to_string        (GstAudioFormat format) G_GNUC_CONST;

const GstAudioFormatInfo *
               gst_audio_format_get_info         (GstAudioFormat format) G_GNUC_CONST;

void           gst_audio_format_fill_silence     (const GstAudioFormatInfo *info,
                                                  gpointer dest, gsize length);
/**
 * GstAudioFlags:
 * @GST_AUDIO_FLAG_NONE: no valid flag
 * @GST_AUDIO_FLAG_UNPOSITIONED: unpositioned audio layout, position array
 *     contains the default layout.
 *
 * Extra audio flags
 */
typedef enum {
  GST_AUDIO_FLAG_NONE         = 0,
  GST_AUDIO_FLAG_UNPOSITIONED = (1 << 0)
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
#define GST_AUDIO_INFO_IS_UNPOSITIONED(info) ((info)->flags & GST_AUDIO_FLAG_UNPOSITIONED)

#define GST_AUDIO_INFO_RATE(info)            ((info)->rate)
#define GST_AUDIO_INFO_CHANNELS(info)        ((info)->channels)
#define GST_AUDIO_INFO_BPF(info)             ((info)->bpf)
#define GST_AUDIO_INFO_POSITION(info,c)      ((info)->position[c])

void         gst_audio_info_init        (GstAudioInfo *info);
void         gst_audio_info_set_format  (GstAudioInfo *info, GstAudioFormat format,
                                         gint rate, gint channels);

gboolean     gst_audio_info_from_caps   (GstAudioInfo *info, const GstCaps *caps);
GstCaps *    gst_audio_info_to_caps     (GstAudioInfo *info);

gboolean     gst_audio_info_convert     (GstAudioInfo * info,
                                         GstFormat src_fmt, gint64 src_val,
                                         GstFormat dest_fmt, gint64 * dest_val);



#define GST_AUDIO_RATE_RANGE "(int) [ 1, max ]"
#define GST_AUDIO_CHANNELS_RANGE "(int) [ 1, max ]"

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
# define GST_AUDIO_NE(s) G_STRINGIFY(s)"_LE"
# define GST_AUDIO_OE(s) G_STRINGIFY(s)"_BE"
#else
# define GST_AUDIO_NE(s) G_STRINGIFY(s)"_BE"
# define GST_AUDIO_OE(s) G_STRINGIFY(s)"_LE"
#endif

#define GST_AUDIO_FORMATS_ALL " { S8, U8, " \
    "S16_LE, S16_BE, U16_LE, U16_BE, " \
    "S24_LE, S24_BE, U24_LE, U24_BE, " \
    "S32_LE, S32_BE, U32_LE, U32_BE, " \
    "S24_3LE, S24_3BE, U24_3LE, U24_3BE, " \
    "S20_3LE, S20_3BE, U20_3LE, U20_3BE, " \
    "S18_3LE, S18_3BE, U18_3LE, U18_3BE, " \
    "F32_LE, F32_BE, F64_LE, F64_BE }"

/**
 * GST_AUDIO_CAPS_MAKE:
 * @format: string format that describes the pixel layout, as string
 *     (e.g. "S16_LE", "S8", etc.)
 *
 * Generic caps string for audio, for use in pad templates.
 */
#define GST_AUDIO_CAPS_MAKE(format)                                    \
    "audio/x-raw, "                                                    \
    "format = (string) " format ", "                                   \
    "rate = " GST_AUDIO_RATE_RANGE ", "                                \
    "channels = " GST_AUDIO_CHANNELS_RANGE

/**
 * GST_AUDIO_DEF_RATE:
 *
 * Standard sampling rate used in consumer audio.
 */
#define GST_AUDIO_DEF_RATE 44100
/**
 * GST_AUDIO_DEF_CHANNELS:
 *
 * Standard number of channels used in consumer audio.
 */
#define GST_AUDIO_DEF_CHANNELS 2
/**
 * GST_AUDIO_DEF_FORMAT:
 *
 * Standard format used in consumer audio.
 */
#define GST_AUDIO_DEF_FORMAT "S16_LE"

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

/*
 * this library defines and implements some helper functions for audio
 * handling
 */

GstBuffer *    gst_audio_buffer_clip     (GstBuffer *buffer, GstSegment *segment,
                                          gint rate, gint bpf);

G_END_DECLS

#endif /* __GST_AUDIO_AUDIO_H__ */
