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

G_BEGIN_DECLS

#include <gst/audio/audio-enumtypes.h>

#if G_BYTE_ORDER == G_BIG_ENDIAN
#define _GST_AUDIO_FORMAT_NE(fmt) GST_AUDIO_FORMAT_ ## fmt ## BE
#elif G_BYTE_ORDER == G_LITTLE_ENDIAN
#define _GST_AUDIO_FORMAT_NE(fmt) GST_AUDIO_FORMAT_ ## fmt ## LE
#endif

/**
 * GstAudioFormat:
 * @GST_AUDIO_FORMAT_UNKNOWN: unknown audio format
 * @GST_AUDIO_FORMAT_S8: 8 bits in 8 bits, signed
 * @GST_AUDIO_FORMAT_U8: 8 bits in 8 bits, unsigned
 * @GST_AUDIO_FORMAT_S16LE: 16 bits in 16 bits, signed, little endian
 * @GST_AUDIO_FORMAT_S16BE: 16 bits in 16 bits, signed, big endian
 * @GST_AUDIO_FORMAT_U16LE: 16 bits in 16 bits, unsigned, little endian
 * @GST_AUDIO_FORMAT_U16BE: 16 bits in 16 bits, unsigned, big endian
 * @GST_AUDIO_FORMAT_S24_32LE: 24 bits in 32 bits, signed, little endian
 * @GST_AUDIO_FORMAT_S24_32BE: 24 bits in 32 bits, signed, big endian
 * @GST_AUDIO_FORMAT_U24_32LE: 24 bits in 32 bits, unsigned, little endian
 * @GST_AUDIO_FORMAT_U24_32BE: 24 bits in 32 bits, unsigned, big endian
 * @GST_AUDIO_FORMAT_S32LE: 32 bits in 32 bits, signed, little endian
 * @GST_AUDIO_FORMAT_S32BE: 32 bits in 32 bits, signed, big endian
 * @GST_AUDIO_FORMAT_U32LE: 32 bits in 32 bits, unsigned, little endian
 * @GST_AUDIO_FORMAT_U32BE: 32 bits in 32 bits, unsigned, big endian
 * @GST_AUDIO_FORMAT_S24LE: 24 bits in 24 bits, signed, little endian
 * @GST_AUDIO_FORMAT_S24BE: 24 bits in 24 bits, signed, big endian
 * @GST_AUDIO_FORMAT_U24LE: 24 bits in 24 bits, unsigned, little endian
 * @GST_AUDIO_FORMAT_U24BE: 24 bits in 24 bits, unsigned, big endian
 * @GST_AUDIO_FORMAT_S20LE: 20 bits in 24 bits, signed, little endian
 * @GST_AUDIO_FORMAT_S20BE: 20 bits in 24 bits, signed, big endian
 * @GST_AUDIO_FORMAT_U20LE: 20 bits in 24 bits, unsigned, little endian
 * @GST_AUDIO_FORMAT_U20BE: 20 bits in 24 bits, unsigned, big endian
 * @GST_AUDIO_FORMAT_S18LE: 18 bits in 24 bits, signed, little endian
 * @GST_AUDIO_FORMAT_S18BE: 18 bits in 24 bits, signed, big endian
 * @GST_AUDIO_FORMAT_U18LE: 18 bits in 24 bits, unsigned, little endian
 * @GST_AUDIO_FORMAT_U18BE: 18 bits in 24 bits, unsigned, big endian
 * @GST_AUDIO_FORMAT_F32LE: 32-bit floating point samples, little endian
 * @GST_AUDIO_FORMAT_F32BE: 32-bit floating point samples, big endian
 * @GST_AUDIO_FORMAT_F64LE: 64-bit floating point samples, little endian
 * @GST_AUDIO_FORMAT_F64BE: 64-bit floating point samples, big endian
 * @GST_AUDIO_FORMAT_S16: 16 bits in 16 bits, signed, native endianness
 * @GST_AUDIO_FORMAT_U16: 16 bits in 16 bits, unsigned, native endianness
 * @GST_AUDIO_FORMAT_S24_32: 24 bits in 32 bits, signed, native endianness
 * @GST_AUDIO_FORMAT_U24_32: 24 bits in 32 bits, unsigned, native endianness
 * @GST_AUDIO_FORMAT_S32: 32 bits in 32 bits, signed, native endianness
 * @GST_AUDIO_FORMAT_U32: 32 bits in 32 bits, unsigned, native endianness
 * @GST_AUDIO_FORMAT_S24: 24 bits in 24 bits, signed, native endianness
 * @GST_AUDIO_FORMAT_U24: 24 bits in 24 bits, unsigned, native endianness
 * @GST_AUDIO_FORMAT_S20: 20 bits in 24 bits, signed, native endianness
 * @GST_AUDIO_FORMAT_U20: 20 bits in 24 bits, unsigned, native endianness
 * @GST_AUDIO_FORMAT_S18: 18 bits in 24 bits, signed, native endianness
 * @GST_AUDIO_FORMAT_U18: 18 bits in 24 bits, unsigned, native endianness
 * @GST_AUDIO_FORMAT_F32: 32-bit floating point samples, native endianness
 * @GST_AUDIO_FORMAT_F64: 64-bit floating point samples, native endianness
 *
 * Enum value describing the most common audio formats.
 */
typedef enum {
  GST_AUDIO_FORMAT_UNKNOWN,
  /* 8 bit */
  GST_AUDIO_FORMAT_S8,
  GST_AUDIO_FORMAT_U8,
  /* 16 bit */
  GST_AUDIO_FORMAT_S16LE,
  GST_AUDIO_FORMAT_S16BE,
  GST_AUDIO_FORMAT_U16LE,
  GST_AUDIO_FORMAT_U16BE,
  /* 24 bit in low 3 bytes of 32 bits*/
  GST_AUDIO_FORMAT_S24_32LE,
  GST_AUDIO_FORMAT_S24_32BE,
  GST_AUDIO_FORMAT_U24_32LE,
  GST_AUDIO_FORMAT_U24_32BE,
  /* 32 bit */
  GST_AUDIO_FORMAT_S32LE,
  GST_AUDIO_FORMAT_S32BE,
  GST_AUDIO_FORMAT_U32LE,
  GST_AUDIO_FORMAT_U32BE,
  /* 24 bit in 3 bytes*/
  GST_AUDIO_FORMAT_S24LE,
  GST_AUDIO_FORMAT_S24BE,
  GST_AUDIO_FORMAT_U24LE,
  GST_AUDIO_FORMAT_U24BE,
  /* 20 bit in 3 bytes*/
  GST_AUDIO_FORMAT_S20LE,
  GST_AUDIO_FORMAT_S20BE,
  GST_AUDIO_FORMAT_U20LE,
  GST_AUDIO_FORMAT_U20BE,
  /* 18 bit in 3 bytes*/
  GST_AUDIO_FORMAT_S18LE,
  GST_AUDIO_FORMAT_S18BE,
  GST_AUDIO_FORMAT_U18LE,
  GST_AUDIO_FORMAT_U18BE,
  /* float */
  GST_AUDIO_FORMAT_F32LE,
  GST_AUDIO_FORMAT_F32BE,
  GST_AUDIO_FORMAT_F64LE,
  GST_AUDIO_FORMAT_F64BE,
  /* native endianness equivalents */
  GST_AUDIO_FORMAT_S16 = _GST_AUDIO_FORMAT_NE(S16),
  GST_AUDIO_FORMAT_U16 = _GST_AUDIO_FORMAT_NE(U16),
  GST_AUDIO_FORMAT_S24_32 = _GST_AUDIO_FORMAT_NE(S24_32),
  GST_AUDIO_FORMAT_U24_32 = _GST_AUDIO_FORMAT_NE(U24_32),
  GST_AUDIO_FORMAT_S32 = _GST_AUDIO_FORMAT_NE(S32),
  GST_AUDIO_FORMAT_U32 = _GST_AUDIO_FORMAT_NE(U32),
  GST_AUDIO_FORMAT_S24 = _GST_AUDIO_FORMAT_NE(S24),
  GST_AUDIO_FORMAT_U24 = _GST_AUDIO_FORMAT_NE(U24),
  GST_AUDIO_FORMAT_S20 = _GST_AUDIO_FORMAT_NE(S20),
  GST_AUDIO_FORMAT_U20 = _GST_AUDIO_FORMAT_NE(U20),
  GST_AUDIO_FORMAT_S18 = _GST_AUDIO_FORMAT_NE(S18),
  GST_AUDIO_FORMAT_U18 = _GST_AUDIO_FORMAT_NE(U18),
  GST_AUDIO_FORMAT_F32 = _GST_AUDIO_FORMAT_NE(F32),
  GST_AUDIO_FORMAT_F64 = _GST_AUDIO_FORMAT_NE(F64)
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
 * channels * size(unpack_format) bytes.
 */
typedef void (*GstAudioFormatUnpack)         (const GstAudioFormatInfo *info, gpointer dest,
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
typedef void (*GstAudioFormatPack)           (const GstAudioFormatInfo *info, const gpointer src,
                                              gpointer data, gint length);

/**
 * GstAudioFormatInfo:
 * @format: #GstAudioFormat
 * @name: string representation of the format
 * @description: user readable description of the format
 * @flags: #GstAudioFormatFlags
 * @endianness: the endianness
 * @width: amount of bits used for one sample
 * @depth: amount of valid bits in @width
 * @silence: @width/8 bytes with 1 silent sample
 * @unpack_format: the format of the unpacked samples
 * @unpack_func: function to unpack samples
 * @pack_func: function to pack samples
 *
 * Information for an audio format.
 */
struct _GstAudioFormatInfo {
  GstAudioFormat format;
  const gchar *name;
  const gchar *description;
  GstAudioFormatFlags flags;
  gint endianness;
  gint width;
  gint depth;
  guint8 silence[8];

  GstAudioFormat unpack_format;
  GstAudioFormatUnpack unpack_func;
  GstAudioFormatPack pack_func;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_audio_format_info_get_type (void);

#define GST_AUDIO_FORMAT_INFO_FORMAT(info)           ((info)->format)
#define GST_AUDIO_FORMAT_INFO_NAME(info)             ((info)->name)
#define GST_AUDIO_FORMAT_INFO_FLAGS(info)            ((info)->flags)

#define GST_AUDIO_FORMAT_INFO_IS_INTEGER(info)       !!((info)->flags & GST_AUDIO_FORMAT_FLAG_INTEGER)
#define GST_AUDIO_FORMAT_INFO_IS_FLOAT(info)         !!((info)->flags & GST_AUDIO_FORMAT_FLAG_FLOAT)
#define GST_AUDIO_FORMAT_INFO_IS_SIGNED(info)        !!((info)->flags & GST_AUDIO_FORMAT_FLAG_SIGNED)

#define GST_AUDIO_FORMAT_INFO_ENDIANNESS(info)       ((info)->endianness)
#define GST_AUDIO_FORMAT_INFO_IS_LITTLE_ENDIAN(info) ((info)->endianness == G_LITTLE_ENDIAN)
#define GST_AUDIO_FORMAT_INFO_IS_BIG_ENDIAN(info)    ((info)->endianness == G_BIG_ENDIAN)
#define GST_AUDIO_FORMAT_INFO_WIDTH(info)            ((info)->width)
#define GST_AUDIO_FORMAT_INFO_DEPTH(info)            ((info)->depth)


GstAudioFormat gst_audio_format_build_integer    (gboolean sign, gint endianness,
                                                  gint width, gint depth) G_GNUC_CONST;

GstAudioFormat gst_audio_format_from_string      (const gchar *format) G_GNUC_CONST;
const gchar *  gst_audio_format_to_string        (GstAudioFormat format) G_GNUC_CONST;

const GstAudioFormatInfo *
               gst_audio_format_get_info         (GstAudioFormat format) G_GNUC_CONST;

void           gst_audio_format_fill_silence     (const GstAudioFormatInfo *info,
                                                  gpointer dest, gsize length);

/**
 * GstAudioChannelPosition:
 * @GST_AUDIO_CHANNEL_POSITION_MONO: Mono without direction;
 *     can only be used with 1 channel
 * @GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT: Front left
 * @GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT: Front right
 * @GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER: Front center
 * @GST_AUDIO_CHANNEL_POSITION_LFE1: Low-frequency effects 1 (subwoofer)
 * @GST_AUDIO_CHANNEL_POSITION_REAR_LEFT: Rear left
 * @GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT: Rear right
 * @GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER: Front left of center
 * @GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER: Front right of center
 * @GST_AUDIO_CHANNEL_POSITION_REAR_CENTER: Rear center
 * @GST_AUDIO_CHANNEL_POSITION_LFE2: Low-frequency effects 2 (subwoofer)
 * @GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT: Side left
 * @GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT: Side right
 * @GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT: Top front left
 * @GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT: Top front right
 * @GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER: Top front center
 * @GST_AUDIO_CHANNEL_POSITION_TOP_CENTER: Top center
 * @GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT: Top rear left
 * @GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT: Top rear right
 * @GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_LEFT: Top side right
 * @GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_RIGHT: Top rear right
 * @GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER: Top rear center
 * @GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_CENTER: Bottom front center
 * @GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_LEFT: Bottom front left
 * @GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_RIGHT: Bottom front right
 * @GST_AUDIO_CHANNEL_POSITION_WIDE_LEFT: Wide left (between front left and side left)
 * @GST_AUDIO_CHANNEL_POSITION_WIDE_RIGHT: Wide right (between front right and side right)
 * @GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT: Surround left (between rear left and side left)
 * @GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT: Surround right (between rear right and side right)
 * @GST_AUDIO_CHANNEL_POSITION_NONE: used for position-less channels, e.g.
 *     from a sound card that records 1024 channels; mutually exclusive with
 *     any other channel position
 * @GST_AUDIO_CHANNEL_POSITION_INVALID: invalid position
 *
 * Audio channel positions.
 *
 * These are the channels defined in SMPTE 2036-2-2008
 * Table 1 for 22.2 audio systems with the Surround and Wide channels from
 * DTS Coherent Acoustics (v.1.3.1) and 10.2 and 7.1 layouts. In the caps the
 * actual channel layout is expressed with a channel count and a channel mask,
 * which describes the existing channels. The positions in the bit mask correspond
 * to the enum values.
 * For negotiation it is allowed to have more bits set in the channel mask than
 * the number of channels to specify the allowed channel positions but this is
 * not allowed in negotiated caps. It is not allowed in any situation other
 * than the one mentioned below to have less bits set in the channel mask than
 * the number of channels.
 *
 * @GST_AUDIO_CHANNEL_POSITION_MONO can only be used with a single mono channel that
 * has no direction information and would be mixed into all directional channels.
 * This is expressed in caps by having a single channel and no channel mask.
 *
 * @GST_AUDIO_CHANNEL_POSITION_NONE can only be used if all channels have this position.
 * This is expressed in caps by having a channel mask with no bits set.
 *
 * As another special case it is allowed to have two channels without a channel mask.
 * This implicitely means that this is a stereo stream with a front left and front right
 * channel.
 */
typedef enum {
  /* These get negative indices to allow to use
   * the enum values of the normal cases for the
   * bit-mask position */
  GST_AUDIO_CHANNEL_POSITION_NONE = -3,
  GST_AUDIO_CHANNEL_POSITION_MONO = -2,
  GST_AUDIO_CHANNEL_POSITION_INVALID = -1,

  /* Normal cases */
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT = 0,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_LFE1,
  GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
  GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
  GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
  GST_AUDIO_CHANNEL_POSITION_LFE2,
  GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_TOP_CENTER,
  GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT,
  GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER,
  GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_WIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_WIDE_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
  GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT
} GstAudioChannelPosition;

#define GST_AUDIO_CHANNEL_POSITION_MASK(pos) (G_GUINT64_CONSTANT(1)<< GST_AUDIO_CHANNEL_POSITION_ ## pos)

/**
 * GstAudioFlags:
 * @GST_AUDIO_FLAG_NONE: no valid flag
 * @GST_AUDIO_FLAG_UNPOSITIONED: the position array explicitly
 *     contains unpositioned channels.
 *
 * Extra audio flags
 */
typedef enum {
  GST_AUDIO_FLAG_NONE              = 0,
  GST_AUDIO_FLAG_UNPOSITIONED      = (1 << 0)
} GstAudioFlags;

/**
 * GstAudioLayout:
 * @GST_AUDIO_LAYOUT_INTERLEAVED: interleaved audio
 * @GST_AUDIO_LAYOUT_NON_INTERLEAVED: non-interleaved audio
 *
 * Layout of the audio samples for the different channels.
 */
typedef enum {
  GST_AUDIO_LAYOUT_INTERLEAVED = 0,
  GST_AUDIO_LAYOUT_NON_INTERLEAVED
} GstAudioLayout;

/**
 * GstAudioInfo:
 * @finfo: the format info of the audio
 * @flags: additional audio flags
 * @layout: audio layout
 * @rate: the audio sample rate
 * @channels: the number of channels
 * @bpf: the number of bytes for one frame, this is the size of one
 *         sample * @channels
 * @position: the positions for each channel
 *
 * Information describing audio properties. This information can be filled
 * in from GstCaps with gst_audio_info_from_caps().
 *
 * Use the provided macros to access the info in this structure.
 */
struct _GstAudioInfo {
  const GstAudioFormatInfo *finfo;
  GstAudioFlags             flags;
  GstAudioLayout            layout;
  gint                      rate;
  gint                      channels;
  gint                      bpf;
  GstAudioChannelPosition   position[64];

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_audio_info_get_type        (void);

#define GST_AUDIO_INFO_IS_VALID(i)           ((i)->finfo != NULL && (i)->rate > 0 && (i)->channels > 0 && (i)->bpf > 0)

#define GST_AUDIO_INFO_FORMAT(i)             (GST_AUDIO_FORMAT_INFO_FORMAT((i)->finfo))
#define GST_AUDIO_INFO_NAME(i)               (GST_AUDIO_FORMAT_INFO_NAME((i)->finfo))
#define GST_AUDIO_INFO_WIDTH(i)              (GST_AUDIO_FORMAT_INFO_WIDTH((i)->finfo))
#define GST_AUDIO_INFO_DEPTH(i)              (GST_AUDIO_FORMAT_INFO_DEPTH((i)->finfo))
#define GST_AUDIO_INFO_BPS(info)             (GST_AUDIO_INFO_DEPTH(info) >> 3)

#define GST_AUDIO_INFO_IS_INTEGER(i)         (GST_AUDIO_FORMAT_INFO_IS_INTEGER((i)->finfo))
#define GST_AUDIO_INFO_IS_FLOAT(i)           (GST_AUDIO_FORMAT_INFO_IS_FLOAT((i)->finfo))
#define GST_AUDIO_INFO_IS_SIGNED(i)          (GST_AUDIO_FORMAT_INFO_IS_SIGNED((i)->finfo))

#define GST_AUDIO_INFO_ENDIANNESS(i)         (GST_AUDIO_FORMAT_INFO_ENDIANNES((i)->finfo))
#define GST_AUDIO_INFO_IS_LITTLE_ENDIAN(i)   (GST_AUDIO_FORMAT_INFO_IS_LITTLE_ENDIAN((i)->finfo))
#define GST_AUDIO_INFO_IS_BIG_ENDIAN(i)      (GST_AUDIO_FORMAT_INFO_IS_BIG_ENDIAN((i)->finfo))

#define GST_AUDIO_INFO_FLAGS(info)           ((info)->flags)
#define GST_AUDIO_INFO_IS_UNPOSITIONED(info) ((info)->flags & GST_AUDIO_FLAG_UNPOSITIONED)
#define GST_AUDIO_INFO_LAYOUT(info)          ((info)->layout)

#define GST_AUDIO_INFO_RATE(info)            ((info)->rate)
#define GST_AUDIO_INFO_CHANNELS(info)        ((info)->channels)
#define GST_AUDIO_INFO_BPF(info)             ((info)->bpf)
#define GST_AUDIO_INFO_POSITION(info,c)      ((info)->position[c])

GstAudioInfo * gst_audio_info_new         (void);
void           gst_audio_info_init        (GstAudioInfo *info);
GstAudioInfo * gst_audio_info_copy        (const GstAudioInfo *info);
void           gst_audio_info_free        (GstAudioInfo *info);

void           gst_audio_info_set_format  (GstAudioInfo *info, GstAudioFormat format,
                                           gint rate, gint channels,
                                           const GstAudioChannelPosition *position);

gboolean       gst_audio_info_from_caps   (GstAudioInfo *info, const GstCaps *caps);
GstCaps *      gst_audio_info_to_caps     (const GstAudioInfo *info);

gboolean       gst_audio_info_convert     (const GstAudioInfo * info,
                                           GstFormat src_fmt, gint64 src_val,
                                           GstFormat dest_fmt, gint64 * dest_val);



#define GST_AUDIO_RATE_RANGE "(int) [ 1, max ]"
#define GST_AUDIO_CHANNELS_RANGE "(int) [ 1, max ]"

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
# define GST_AUDIO_NE(s) G_STRINGIFY(s)"LE"
# define GST_AUDIO_OE(s) G_STRINGIFY(s)"BE"
#else
# define GST_AUDIO_NE(s) G_STRINGIFY(s)"BE"
# define GST_AUDIO_OE(s) G_STRINGIFY(s)"LE"
#endif

#define GST_AUDIO_FORMATS_ALL " { S8, U8, " \
    "S16LE, S16BE, U16LE, U16BE, " \
    "S24_32LE, S24_32BE, U24_32LE, U24_32BE, " \
    "S32LE, S32BE, U32LE, U32BE, " \
    "S24LE, S24BE, U24LE, U24BE, " \
    "S20LE, S20BE, U20LE, U20BE, " \
    "S18LE, S18BE, U18LE, U18BE, " \
    "F32LE, F32BE, F64LE, F64BE }"

/**
 * GST_AUDIO_CAPS_MAKE:
 * @format: string format that describes the pixel layout, as string
 *     (e.g. "S16LE", "S8", etc.)
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
#define GST_AUDIO_DEF_FORMAT "S16LE"

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


gboolean       gst_audio_buffer_reorder_channels (GstBuffer * buffer,
                                                  GstAudioFormat format, 
                                                  gint channels,
                                                  const GstAudioChannelPosition * from,
                                                  const GstAudioChannelPosition * to);

gboolean       gst_audio_reorder_channels        (gpointer data, gsize size,
                                                  GstAudioFormat format, 
                                                  gint channels,
                                                  const GstAudioChannelPosition * from,
                                                  const GstAudioChannelPosition * to);

gboolean       gst_audio_channel_positions_to_valid_order (GstAudioChannelPosition *position,
                                                           gint channels);

gboolean       gst_audio_check_valid_channel_positions (const GstAudioChannelPosition *position,
                                                        gint channels, gboolean force_order);

gboolean       gst_audio_channel_positions_to_mask  (const GstAudioChannelPosition *position,
                                                     gint channels, guint64 *channel_mask);

gboolean       gst_audio_channel_positions_from_mask (gint channels, guint64 channel_mask,
                                                      GstAudioChannelPosition * position);

gboolean       gst_audio_get_channel_reorder_map (gint channels,
                                                  const GstAudioChannelPosition * from,
                                                  const GstAudioChannelPosition * to,
                                                  gint *reorder_map);

G_END_DECLS

#endif /* __GST_AUDIO_AUDIO_H__ */
