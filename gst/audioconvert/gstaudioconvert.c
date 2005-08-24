/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 * Copyright (C) 2005 Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gstaudioconvert.c: Convert audio to different audio formats automatically
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

/*
 * design decisions:
 * - audioconvert converts buffers in a set of supported caps. If it supports
 *   a caps, it supports conversion from these caps to any other caps it
 *   supports. (example: if it does A=>B and A=>C, it also does B=>C)
 * - audioconvert does not save state between buffers. Every incoming buffer is
 *   converted and the converted buffer is pushed out.
 * conclusion:
 * audioconvert is not supposed to be a one-element-does-anything solution for
 * audio conversions.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/multichannel.h>
#include <string.h>
#include "gstchannelmix.h"
#include "plugin.h"

GST_DEBUG_CATEGORY (audio_convert_debug);

/* int to float conversion: int2float(i) = 1 / (2^31-1) * i */
#define INT2FLOAT(i) (4.6566128752457969e-10 * ((gfloat)i))


/*** DEFINITIONS **************************************************************/

static GstElementDetails audio_convert_details = {
  "Audio Conversion",
  "Filter/Converter/Audio",
  "Convert audio to different formats",
  "Benjamin Otte <in7y118@public.uni-hamburg.de>",
};

/* type functions */
static void gst_audio_convert_base_init (gpointer g_class);
static void gst_audio_convert_class_init (GstAudioConvertClass * klass);
static void gst_audio_convert_init (GstAudioConvert * audio_convert);
static void gst_audio_convert_dispose (GObject * obj);

/* gstreamer functions */
static GstBuffer *gst_audio_convert_buffer_to_default_format (GstAudioConvert *
    this, GstBuffer * buf);
static GstBuffer *gst_audio_convert_buffer_from_default_format (GstAudioConvert
    * this, GstBuffer * buf);

static GstBuffer *gst_audio_convert_channels (GstAudioConvert * this,
    GstBuffer * buf);

static gboolean gst_audio_convert_parse_caps (const GstCaps * gst_caps,
    GstAudioConvertCaps * caps);

gboolean audio_convert_get_unit_size (GstBaseTransform * base, GstCaps * caps,
    guint * size);
GstCaps *audio_convert_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps);
void audio_convert_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
gboolean audio_convert_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps);
static GstFlowReturn
audio_convert_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf);

/* AudioConvert signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_AGGRESSIVE
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (audio_convert_debug, "audioconvert", 0, "audio conversion element");

GST_BOILERPLATE_FULL (GstAudioConvert, gst_audio_convert, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

/*** GSTREAMER PROTOTYPES *****************************************************/

#define STATIC_CAPS \
GST_STATIC_CAPS ( \
  "audio/x-raw-float, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) BYTE_ORDER, " \
    "width = (int) 32, " \
    "buffer-frames = (int) [ 0, MAX ];" \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 32, " \
    "depth = (int) [ 1, 32 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 24, " \
    "depth = (int) [ 1, 24 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 16, " \
    "depth = (int) [ 1, 16 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 8, " \
    "depth = (int) [ 1, 8 ], " \
    "signed = (boolean) { true, false } " \
)

static GstAudioChannelPosition *supported_positions;

static GstStaticCaps gst_audio_convert_static_caps = STATIC_CAPS;

static GstStaticPadTemplate gst_audio_convert_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    STATIC_CAPS);

static GstStaticPadTemplate gst_audio_convert_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    STATIC_CAPS);

/*** TYPE FUNCTIONS ***********************************************************/

static void
gst_audio_convert_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_audio_convert_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_audio_convert_sink_template));
  gst_element_class_set_details (element_class, &audio_convert_details);
}

static void
gst_audio_convert_class_init (GstAudioConvertClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gint i;

  gobject_class->dispose = gst_audio_convert_dispose;

  supported_positions = g_new0 (GstAudioChannelPosition,
      GST_AUDIO_CHANNEL_POSITION_NUM);
  for (i = 0; i < GST_AUDIO_CHANNEL_POSITION_NUM; i++)
    supported_positions[i] = i;

  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size =
      GST_DEBUG_FUNCPTR (audio_convert_get_unit_size);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      GST_DEBUG_FUNCPTR (audio_convert_transform_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->fixate_caps =
      GST_DEBUG_FUNCPTR (audio_convert_fixate_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps =
      GST_DEBUG_FUNCPTR (audio_convert_set_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->transform =
      GST_DEBUG_FUNCPTR (audio_convert_transform);
}

static void
gst_audio_convert_init (GstAudioConvert * this)
{
  /* clear important variables */
  this->convert_internal = NULL;
  this->sinkcaps.pos = NULL;
  this->srccaps.pos = NULL;
  this->matrix = NULL;
}

static void
gst_audio_convert_dispose (GObject * obj)
{
  GstAudioConvert *this = GST_AUDIO_CONVERT (obj);

  if (this->sinkcaps.pos) {
    g_free (this->sinkcaps.pos);
    this->sinkcaps.pos = NULL;
  }

  if (this->srccaps.pos) {
    g_free (this->srccaps.pos);
    this->srccaps.pos = NULL;
  }
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

/*** GSTREAMER FUNCTIONS ******************************************************/

/* BaseTransform vmethods */
gboolean
audio_convert_get_unit_size (GstBaseTransform * base, GstCaps * caps,
    guint * size)
{
  GstAudioConvertCaps ac_caps;

  g_return_val_if_fail (size, FALSE);

  memset (&ac_caps, 0, sizeof (ac_caps));

  if (!gst_audio_convert_parse_caps (caps, &ac_caps))
    return FALSE;

  *size = ac_caps.width * ac_caps.channels / 8;
  return TRUE;
}

/* audioconvert can convert anything except sample rate; so return template
 * caps with rate fixed */
/* FIXME:
 * it would be smart here to return the caps with the same width as the first
 */
GstCaps *
audio_convert_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps)
{
  int i;
  const GValue *rate;

  g_return_val_if_fail (GST_CAPS_IS_SIMPLE (caps), NULL);

  GstStructure *structure = gst_caps_get_structure (caps, 0);

  GstCaps *ret = gst_static_caps_get (&gst_audio_convert_static_caps);

  ret = gst_caps_make_writable (ret);

  rate = gst_structure_get_value (structure, "rate");
  if (!rate) {
    return ret;
  }

  for (i = 0; i < gst_caps_get_size (ret); ++i) {
    structure = gst_caps_get_structure (ret, i);
    gst_structure_set_value (structure, "rate", rate);
  }
  return ret;
}

/* try to keep as many of the structure members the same by fixating the
 * possible ranges; this way we convert the least amount of things as possible
 */
void
audio_convert_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  gint rate, endianness, depth;
  gboolean signedness;

  g_return_if_fail (gst_caps_is_fixed (caps));

  GST_DEBUG_OBJECT (base, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  if (gst_structure_get_int (ins, "rate", &rate)) {
    if (gst_structure_has_field (outs, "rate")) {
      gst_caps_structure_fixate_field_nearest_int (outs, "rate", rate);
    }
  }
  if (gst_structure_get_int (ins, "endianness", &endianness)) {
    if (gst_structure_has_field (outs, "endianness")) {
      gst_caps_structure_fixate_field_nearest_int (outs, "endianness",
          endianness);
    }
  }
  if (gst_structure_get_int (ins, "depth", &depth)) {
    if (gst_structure_has_field (outs, "depth")) {
      gst_caps_structure_fixate_field_nearest_int (outs, "depth", depth);
    }
  }
  if (gst_structure_get_boolean (ins, "signed", &signedness)) {
    if (gst_structure_has_field (outs, "signed")) {
      gst_caps_structure_fixate_field_boolean (outs, "signed", signedness);
    }
  }

  GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);
}

gboolean
audio_convert_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstAudioConvertCaps in_ac_caps = { 0 };
  GstAudioConvertCaps out_ac_caps = { 0 };
  GstAudioConvert *this = GST_AUDIO_CONVERT (base);

  GST_DEBUG_OBJECT (base, "incaps %" GST_PTR_FORMAT ", outcaps %"
      GST_PTR_FORMAT, incaps, outcaps);

  in_ac_caps.pos = NULL;
  if (!gst_audio_convert_parse_caps (incaps, &in_ac_caps))
    return FALSE;

  out_ac_caps.pos = NULL;
  if (!gst_audio_convert_parse_caps (outcaps, &out_ac_caps))
    return FALSE;

  this->sinkcaps = in_ac_caps;
  this->srccaps = out_ac_caps;

  GST_DEBUG ("setting up matrix");
  gst_audio_convert_setup_matrix (this);
  GST_DEBUG ("set up matrix, %p", this->matrix);

  return TRUE;
}

static GstFlowReturn
audio_convert_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstAudioConvert *this = GST_AUDIO_CONVERT (base);
  GstBuffer *buf;

  /*
   * Theory of operation:
   * - convert the format (endianness, signedness, width, depth) to
   *   (G_BYTE_ORDER, TRUE, 32, 32)
   * - convert rate and channels
   * - convert back to output format
   */

  /* FIXME: optimize for copying */
  buf = gst_buffer_copy (inbuf);
  buf = gst_audio_convert_buffer_to_default_format (this, buf);
  buf = gst_audio_convert_channels (this, buf);
  buf = gst_audio_convert_buffer_from_default_format (this, buf);
  memcpy (GST_BUFFER_DATA (outbuf), GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (outbuf));
  gst_buffer_unref (buf);

  return GST_FLOW_OK;
}

/* convert the given GstCaps to our ghetto format */
static gboolean
gst_audio_convert_parse_caps (const GstCaps * gst_caps,
    GstAudioConvertCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (gst_caps, 0);

  GST_DEBUG ("parse caps %p and %" GST_PTR_FORMAT, gst_caps, gst_caps);

  g_return_val_if_fail (gst_caps_is_fixed (gst_caps), FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);

  /* cleanup old */
  if (caps->pos) {
    g_free (caps->pos);
    caps->pos = NULL;
  }

  caps->endianness = G_BYTE_ORDER;
  caps->is_int =
      (strcmp (gst_structure_get_name (structure), "audio/x-raw-int") == 0);
  if (!gst_structure_get_int (structure, "channels", &caps->channels)
      || !(caps->pos = gst_audio_get_channel_positions (structure))
      || !gst_structure_get_int (structure, "width", &caps->width)
      || !gst_structure_get_int (structure, "rate", &caps->rate)
      || (caps->is_int
          && (!gst_structure_get_boolean (structure, "signed", &caps->sign)
              || !gst_structure_get_int (structure, "depth", &caps->depth)
              || (caps->width != 8
                  && !gst_structure_get_int (structure, "endianness",
                      &caps->endianness)))) || (!caps->is_int
          && !gst_structure_get_int (structure, "buffer-frames",
              &caps->buffer_frames))) {
    GST_DEBUG ("could not get some values from structure");
    g_free (caps->pos);
    caps->pos = NULL;
    return FALSE;
  }
  if (caps->is_int && caps->depth > caps->width) {
    GST_DEBUG ("width > depth, not allowed - make us advertise correct caps");
    g_free (caps->pos);
    caps->pos = NULL;
    return FALSE;
  }
  return TRUE;
}

/* return a writable buffer of size which ideally is the same as before
   - You must unref the new buffer
   - The size of the old buffer is undefined after this operation */
static GstBuffer *
gst_audio_convert_get_buffer (GstBuffer * buf, guint size)
{
  GstBuffer *ret;

  g_assert (GST_IS_BUFFER (buf));

  GST_LOG
      ("new buffer of size %u requested. Current is: data: %p - size: %u",
      size, buf->data, buf->size);
  if (buf->size >= size && gst_buffer_is_writable (buf)) {
    gst_buffer_ref (buf);
    buf->size = size;
    GST_LOG
        ("returning same buffer with adjusted values. data: %p - size: %u",
        buf->data, buf->size);
    return buf;
  } else {
    ret = gst_buffer_new_and_alloc (size);
    g_assert (ret);
    gst_buffer_stamp (ret, buf);
    GST_LOG ("returning new buffer. data: %p - size: %u", ret->data, ret->size);
    return ret;
  }
}

static inline guint8
GUINT8_IDENTITY (guint8 x)
{
  return x;
}
static inline guint8
GINT8_IDENTITY (gint8 x)
{
  return x;
}

#define CONVERT_TO(to, from, type, sign, endianness, LE_FUNC, BE_FUNC)	\
G_STMT_START {								\
  type value;								\
  memcpy (&value, from, sizeof (type));					\
  from -= sizeof (type);						\
  value = (endianness == G_LITTLE_ENDIAN) ?				\
      LE_FUNC (value) : BE_FUNC (value);				\
  if (sign) {								\
    to = value;								\
  } else {								\
    to = (gint64) value - (1 << (sizeof (type) * 8 - 1));		\
  }									\
} G_STMT_END;

static GstBuffer *
gst_audio_convert_buffer_to_default_format (GstAudioConvert * this,
    GstBuffer * buf)
{
  GstBaseTransform *base = GST_BASE_TRANSFORM (this);
  GstBuffer *ret;
  gint i, count;
  gint64 cur = 0;
  gint32 write;
  gint32 *dest;
  guint8 *src;

  GST_LOG_OBJECT (base, "converting buffer of size %d to default format",
      GST_BUFFER_SIZE (buf));
  if (this->sinkcaps.is_int) {
    if (this->sinkcaps.width == 32 && this->sinkcaps.depth == 32 &&
        this->sinkcaps.endianness == G_BYTE_ORDER
        && this->sinkcaps.sign == TRUE)
      return buf;

    ret =
        gst_audio_convert_get_buffer (buf,
        buf->size * 32 / this->sinkcaps.width);
    gst_buffer_set_caps (ret, GST_PAD_CAPS (base->srcpad));

    count = ret->size / 4;
    src = buf->data + (count - 1) * (this->sinkcaps.width / 8);
    dest = (gint32 *) ret->data;
    for (i = count - 1; i >= 0; i--) {
      switch (this->sinkcaps.width) {
        case 8:
          if (this->sinkcaps.sign) {
            CONVERT_TO (cur, src, gint8, this->sinkcaps.sign,
                this->sinkcaps.endianness, GINT8_IDENTITY, GINT8_IDENTITY);
          } else {
            CONVERT_TO (cur, src, guint8, this->sinkcaps.sign,
                this->sinkcaps.endianness, GUINT8_IDENTITY, GUINT8_IDENTITY);
          }
          break;
        case 16:
          if (this->sinkcaps.sign) {
            CONVERT_TO (cur, src, gint16, this->sinkcaps.sign,
                this->sinkcaps.endianness, GINT16_FROM_LE, GINT16_FROM_BE);
          } else {
            CONVERT_TO (cur, src, guint16, this->sinkcaps.sign,
                this->sinkcaps.endianness, GUINT16_FROM_LE, GUINT16_FROM_BE);
          }
          break;
        case 24:
        {
          /* Read 24-bits LE/BE into signed 64 host-endian */
          if (this->sinkcaps.endianness == G_LITTLE_ENDIAN) {
            cur = src[0] | (src[1] << 8) | (src[2] << 16);
          } else {
            cur = src[2] | (src[1] << 8) | (src[0] << 16);
          }

          /* Sign extend */
          if ((this->sinkcaps.sign)
              && (cur & (1 << (this->sinkcaps.depth - 1))))
            cur |= ((gint64) (-1)) ^ ((1 << this->sinkcaps.depth) - 1);

          src -= 3;
        }
          break;
        case 32:
          if (this->sinkcaps.sign) {
            CONVERT_TO (cur, src, gint32, this->sinkcaps.sign,
                this->sinkcaps.endianness, GINT32_FROM_LE, GINT32_FROM_BE);
          } else {
            CONVERT_TO (cur, src, guint32, this->sinkcaps.sign,
                this->sinkcaps.endianness, GUINT32_FROM_LE, GUINT32_FROM_BE);
          }
          break;
        default:
          g_assert_not_reached ();
      }
      cur = cur * ((gint64) 1 << (32 - this->sinkcaps.depth));
      cur = CLAMP (cur, -((gint64) 1 << 32), (gint64) 0x7FFFFFFF);
      write = cur;
      memcpy (&dest[i], &write, 4);
    }
  } else {
    /* float2int */
    gfloat *in;
    gint32 *out;
    float temp;

    /* should just give the same buffer, unless it's not writable -- float is
     * already 32 bits */
    ret = gst_audio_convert_get_buffer (buf, buf->size);
    gst_buffer_set_caps (ret, GST_PAD_CAPS (base->srcpad));

    in = (gfloat *) GST_BUFFER_DATA (buf);
    out = (gint32 *) GST_BUFFER_DATA (ret);
    for (i = buf->size / sizeof (float); i > 0; i--) {
      temp = *in * 2147483647.0f + .5;
      *out = (gint32) CLAMP ((gint64) temp, -2147483648ll, 2147483647ll);
      out++;
      in++;
    }
  }

  gst_buffer_unref (buf);
  return ret;
}

#define POPULATE(out, format, be_func, le_func) G_STMT_START {		\
  format val;								\
  format* p = (format *) out;						\
  int_value >>= (32 - this->srccaps.depth);				\
  if (this->srccaps.sign) {						\
    val = (format) int_value;						\
  } else {								\
    val = (format) int_value + (1 << (this->srccaps.depth - 1));	\
  }									\
  switch (this->srccaps.endianness) {					\
    case G_LITTLE_ENDIAN:                                               \
      val = le_func (val);                                              \
      break;                                                            \
    case G_BIG_ENDIAN:                                                  \
      val = be_func (val);                                              \
      break;                                                            \
    default:                                                            \
      g_assert_not_reached ();                                          \
  };                                                                    \
  *p = val;                                                             \
  p ++;                                                                 \
  out = (guint8 *) p;                                                   \
}G_STMT_END

static GstBuffer *
gst_audio_convert_buffer_from_default_format (GstAudioConvert * this,
    GstBuffer * buf)
{
  GstBaseTransform *base;
  GstBuffer *ret;
  guint count, i;
  gint32 *src;

  base = GST_BASE_TRANSFORM (this);

  GST_LOG_OBJECT (base, "converting buffer of size %d from default format",
      GST_BUFFER_SIZE (buf));

  if (this->srccaps.is_int && this->srccaps.width == 32
      && this->srccaps.depth == 32 && this->srccaps.endianness == G_BYTE_ORDER
      && this->srccaps.sign == TRUE)
    return buf;

  if (this->srccaps.is_int) {
    guint8 *dest;

    count = buf->size / 4;      /* size is undefined after gst_audio_convert_get_buffer! */
    ret =
        gst_audio_convert_get_buffer (buf,
        buf->size * this->srccaps.width / 32);
    gst_buffer_set_caps (ret, GST_PAD_CAPS (base->srcpad));

    dest = ret->data;
    src = (gint32 *) buf->data;

    for (i = 0; i < count; i++) {
      gint32 int_value = *src;

      src++;
      switch (this->srccaps.width) {
        case 8:
          if (this->srccaps.sign) {
            POPULATE (dest, gint8, GINT8_IDENTITY, GINT8_IDENTITY);
          } else {
            POPULATE (dest, guint8, GUINT8_IDENTITY, GUINT8_IDENTITY);
          }
          break;
        case 16:
          if (this->srccaps.sign) {
            POPULATE (dest, gint16, GINT16_TO_BE, GINT16_TO_LE);
          } else {
            POPULATE (dest, guint16, GUINT16_TO_BE, GUINT16_TO_LE);
          }
          break;
        case 24:
        {
          guint8 tmp[4];
          guint8 *tmpp = tmp;

          /* Write out big endian array */
          if (this->srccaps.sign) {
            POPULATE (tmpp, gint32, GINT32_TO_BE, GINT32_TO_BE);
          } else {
            POPULATE (tmpp, guint32, GUINT32_TO_BE, GUINT32_TO_BE);
          }

          if (this->srccaps.endianness == G_LITTLE_ENDIAN) {
            dest[2] = tmp[1];
            dest[1] = tmp[2];
            dest[0] = tmp[3];
          } else {
            memcpy (dest, tmp + 1, 3);
          }
          dest += 3;
        }
          break;
        case 32:
          if (this->srccaps.sign) {
            POPULATE (dest, gint32, GINT32_TO_BE, GINT32_TO_LE);
          } else {
            POPULATE (dest, guint32, GUINT32_TO_BE, GUINT32_TO_LE);
          }
          break;
        default:
          g_assert_not_reached ();
      }
    }
  } else {
    gfloat *dest;

    count = buf->size / 4;      /* size is undefined after gst_audio_convert_get_buffer! */
    ret =
        gst_audio_convert_get_buffer (buf,
        buf->size * this->srccaps.width / 32);
    gst_buffer_set_caps (ret, GST_PAD_CAPS (base->srcpad));

    dest = (gfloat *) ret->data;
    src = (gint32 *) buf->data;
    for (i = 0; i < count; i++) {
      *dest = INT2FLOAT (*src);
      dest++;
      src++;
    }
  }

  gst_buffer_unref (buf);
  return ret;
}

static GstBuffer *
gst_audio_convert_channels (GstAudioConvert * this, GstBuffer * buf)
{
  GstBaseTransform *base = GST_BASE_TRANSFORM (this);
  GstBuffer *ret;
  gint units;                   /* one unit is one sample of audio for each channel, combined */

  g_assert (this->matrix != NULL);

  GST_LOG_OBJECT (base, "converting buffer of size %d for different channels",
      GST_BUFFER_SIZE (buf));

  /* check for passthrough */
  if (gst_audio_convert_passthrough (this))
    return buf;

  /* convert */
  GST_LOG_OBJECT (base, "%d sinkpad channels, %d srcpad channels",
      this->sinkcaps.channels, this->srccaps.channels);
  units = GST_BUFFER_SIZE (buf) / 4 / this->sinkcaps.channels;
  ret = gst_audio_convert_get_buffer (buf, units * 4 * this->srccaps.channels);
  gst_buffer_set_caps (ret, GST_PAD_CAPS (base->srcpad));
  gst_audio_convert_mix (this, (gint32 *) GST_BUFFER_DATA (buf),
      (gint32 *) GST_BUFFER_DATA (ret), units);
  gst_buffer_unref (buf);

  return ret;
}
