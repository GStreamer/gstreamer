/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
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
/* Element-Checklist-Version: 5 */

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
#include <string.h>
#include "plugin.h"

GST_DEBUG_CATEGORY_STATIC (audio_convert_debug);
#define GST_CAT_DEFAULT (audio_convert_debug)

/*** DEFINITIONS **************************************************************/

#define GST_TYPE_AUDIO_CONVERT          (gst_audio_convert_get_type())
#define GST_AUDIO_CONVERT(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_CONVERT,GstAudioConvert))
#define GST_AUDIO_CONVERT_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_CONVERT,GstAudioConvert))
#define GST_IS_AUDIO_CONVERT(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_CONVERT))
#define GST_IS_AUDIO_CONVERT_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_CONVERT))

typedef struct _GstAudioConvert GstAudioConvert;
typedef struct _GstAudioConvertCaps GstAudioConvertCaps;
typedef struct _GstAudioConvertClass GstAudioConvertClass;

/* this struct is a handy way of passing around all the caps info ... */
struct _GstAudioConvertCaps
{
  /* general caps */
  gboolean is_int;
  gint endianness;
  gint width;
  gint rate;
  gint channels;

  /* int audio caps */
  gboolean sign;
  gint depth;

  /* float audio caps */
  gint buffer_frames;
};

struct _GstAudioConvert
{
  GstElement element;

  /* pads */
  GstPad *sink;
  GstPad *src;

  GstAudioConvertCaps srccaps;
  GstAudioConvertCaps sinkcaps;

  /* conversion functions */
  GstBuffer *(*convert_internal) (GstAudioConvert * this, GstBuffer * buf);
};

struct _GstAudioConvertClass
{
  GstElementClass parent_class;
};

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

/* gstreamer functions */
static void gst_audio_convert_chain (GstPad * pad, GstData * _data);
static GstPadLinkReturn gst_audio_convert_link (GstPad * pad,
    const GstCaps * caps);
static GstCaps *gst_audio_convert_fixate (GstPad * pad, const GstCaps * caps);
static GstCaps *gst_audio_convert_getcaps (GstPad * pad);
static GstElementStateReturn gst_audio_convert_change_state (GstElement *
    element);

static GstBuffer *gst_audio_convert_buffer_to_default_format (GstAudioConvert *
    this, GstBuffer * buf);
static GstBuffer *gst_audio_convert_buffer_from_default_format (GstAudioConvert
    * this, GstBuffer * buf);

static GstBuffer *gst_audio_convert_channels (GstAudioConvert * this,
    GstBuffer * buf);

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

GST_BOILERPLATE_FULL (GstAudioConvert, gst_audio_convert, GstElement,
    GST_TYPE_ELEMENT, DEBUG_INIT);

/*** GSTREAMER PROTOTYPES *****************************************************/

#define STATIC_CAPS \
GST_STATIC_CAPS ( \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 2 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 8, " \
    "depth = (int) [ 1, 8 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 2 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 16, " \
    "depth = (int) [ 1, 16 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 2 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 32, " \
    "depth = (int) [ 1, 32 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-float, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 2 ], " \
    "endianness = (int) BYTE_ORDER, " \
    "width = (int) 32, " \
    "buffer-frames = (int) [ 0, MAX ]" \
)

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
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->change_state = gst_audio_convert_change_state;
}

static void
gst_audio_convert_init (GstAudioConvert * this)
{
  /* sinkpad */
  this->sink =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_audio_convert_sink_template), "sink");
  gst_pad_set_getcaps_function (this->sink, gst_audio_convert_getcaps);
  gst_pad_set_link_function (this->sink, gst_audio_convert_link);
  gst_pad_set_fixate_function (this->sink, gst_audio_convert_fixate);
  gst_element_add_pad (GST_ELEMENT (this), this->sink);

  /* srcpad */
  this->src =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_audio_convert_src_template), "src");
  gst_pad_set_getcaps_function (this->src, gst_audio_convert_getcaps);
  gst_pad_set_link_function (this->src, gst_audio_convert_link);
  gst_pad_set_fixate_function (this->src, gst_audio_convert_fixate);
  gst_element_add_pad (GST_ELEMENT (this), this->src);

  gst_pad_set_chain_function (this->sink, gst_audio_convert_chain);

  /* clear important variables */
  this->convert_internal = NULL;
}

/*** GSTREAMER FUNCTIONS ******************************************************/

static void
gst_audio_convert_chain (GstPad * pad, GstData * data)
{
  GstBuffer *buf = GST_BUFFER (data);
  GstAudioConvert *this;

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  g_return_if_fail (GST_IS_AUDIO_CONVERT (GST_OBJECT_PARENT (pad)));
  this = GST_AUDIO_CONVERT (GST_OBJECT_PARENT (pad));

  /* FIXME */
  if (GST_IS_EVENT (buf)) {
    gst_pad_event_default (pad, GST_EVENT (buf));
    return;
  }

  if (!gst_pad_is_negotiated (this->sink)) {
    GST_ELEMENT_ERROR (this, CORE, NEGOTIATION, (NULL),
        ("Sink pad (connected to %s:%s) not negotiated before chain function",
            GST_DEBUG_PAD_NAME (gst_pad_get_peer (this->sink))));
    return;
  }
  if (!gst_pad_is_negotiated (this->src)) {
    gst_data_unref (data);
    return;
  }

  /**
   * Theory of operation:
   * - convert the format (endianness, signedness, width, depth) to
   *   (G_BYTE_ORDER, TRUE, 32, 32)
   * - convert rate and channels
   * - convert back to output format
   */

  buf = gst_audio_convert_buffer_to_default_format (this, buf);

  buf = gst_audio_convert_channels (this, buf);

  buf = gst_audio_convert_buffer_from_default_format (this, buf);

  gst_pad_push (this->src, GST_DATA (buf));
}

/* this function is complicated now, but it will be unnecessary when we convert
 * rate. */
static GstCaps *
gst_audio_convert_getcaps (GstPad * pad)
{
  GstAudioConvert *this;
  GstPad *otherpad;
  GstStructure *structure;
  GstCaps *othercaps, *caps;
  const GstCaps *templcaps;
  int i, size;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);
  g_return_val_if_fail (GST_IS_AUDIO_CONVERT (GST_OBJECT_PARENT (pad)), NULL);
  this = GST_AUDIO_CONVERT (GST_OBJECT_PARENT (pad));

  otherpad = (pad == this->src) ? this->sink : this->src;

  /* all we want to find out is the rate */
  templcaps = gst_pad_get_pad_template_caps (pad);
  othercaps = gst_pad_get_allowed_caps (otherpad);

  size = gst_caps_get_size (othercaps);

  for (i = size - 1; i >= 0; i--) {
    structure = gst_caps_get_structure (othercaps, i);
    gst_structure_remove_field (structure, "channels");
    gst_structure_remove_field (structure, "endianness");
    gst_structure_remove_field (structure, "width");
    gst_structure_remove_field (structure, "depth");
    gst_structure_remove_field (structure, "signed");
    structure = gst_structure_copy (structure);
    if (strcmp (gst_structure_get_name (structure), "audio/x-raw-int") == 0) {
      gst_structure_set_name (structure, "audio/x-raw-float");
      if (pad == this->sink) {
        gst_structure_set (structure, "buffer-frames", GST_TYPE_INT_RANGE, 0,
            G_MAXINT, NULL);
      } else {
        gst_structure_set (structure, "buffer-frames", G_TYPE_INT, 0, NULL);
      }
    } else {
      gst_structure_set_name (structure, "audio/x-raw-int");
      gst_structure_remove_field (structure, "buffer-frames");
    }
    gst_caps_append_structure (othercaps, structure);
  }
  caps = gst_caps_intersect (othercaps, templcaps);
  gst_caps_free (othercaps);

  return caps;
}

static gboolean
gst_audio_convert_parse_caps (const GstCaps * gst_caps,
    GstAudioConvertCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (gst_caps, 0);

  g_return_val_if_fail (gst_caps_is_fixed (gst_caps), FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);

  caps->endianness = G_BYTE_ORDER;
  caps->is_int =
      (strcmp (gst_structure_get_name (structure), "audio/x-raw-int") == 0);
  if (!gst_structure_get_int (structure, "channels", &caps->channels)
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
    return FALSE;
  }
  if (caps->is_int && caps->depth > caps->width) {
    GST_DEBUG ("width > depth, not allowed - make us advertise correct caps");
    return FALSE;
  }
  return TRUE;
}

static GstPadLinkReturn
gst_audio_convert_link (GstPad * pad, const GstCaps * caps)
{
  GstAudioConvert *this;
  GstPad *otherpad;
  GstAudioConvertCaps ac_caps, other_ac_caps;
  GstCaps *othercaps;
  guint i;
  GstPadLinkReturn ret;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_AUDIO_CONVERT (GST_OBJECT_PARENT (pad)),
      GST_PAD_LINK_REFUSED);

  this = GST_AUDIO_CONVERT (GST_OBJECT_PARENT (pad));
  otherpad = (pad == this->src ? this->sink : this->src);

  /* negotiate sinkpad first */
  if (pad == this->src && !gst_pad_is_negotiated (this->sink))
    return GST_PAD_LINK_DELAYED;

  if (!gst_audio_convert_parse_caps (caps, &ac_caps))
    return GST_PAD_LINK_REFUSED;

  /* ok, not those - try setting "any" caps */
  othercaps = gst_pad_get_allowed_caps (otherpad);
  for (i = 0; i < gst_caps_get_size (othercaps); i++) {
    GstStructure *structure = gst_caps_get_structure (othercaps, i);

    gst_structure_set (structure, "rate", G_TYPE_INT, ac_caps.rate, NULL);
    if (strcmp (gst_structure_get_name (structure), "audio/x-raw-float") == 0) {
      if (!ac_caps.is_int) {
        gst_structure_set (structure, "buffer-frames", G_TYPE_INT,
            ac_caps.buffer_frames, NULL);
      } else {
        gst_structure_set (structure, "buffer-frames", G_TYPE_INT, 0, NULL);
      }
    }
  }
  if (this->sink == pad) {
    this->sinkcaps = ac_caps;
  } else {
    this->srccaps = ac_caps;
  }
  GST_LOG_OBJECT (this, "trying to set caps to %" GST_PTR_FORMAT, othercaps);
  ret = gst_pad_try_set_caps_nonfixed (otherpad, othercaps);
  gst_caps_free (othercaps);
  if (ret < GST_PAD_LINK_OK)
    return ret;

  /* woohoo, got it */
  othercaps = (GstCaps *) gst_pad_get_negotiated_caps (otherpad);
  if (othercaps) {
    if (!gst_audio_convert_parse_caps (othercaps, &other_ac_caps)) {
      g_critical ("internal negotiation error");
      return GST_PAD_LINK_REFUSED;
    }
  } else {
    other_ac_caps = ac_caps;
  }

  if (this->sink == pad) {
    this->srccaps = other_ac_caps;
    this->sinkcaps = ac_caps;
  } else {
    this->srccaps = ac_caps;
    this->sinkcaps = other_ac_caps;
  }

  GST_DEBUG_OBJECT (this, "negotiated pad to %" GST_PTR_FORMAT, caps);
  return GST_PAD_LINK_OK;
}

/* tries to fixate the given field of the given caps to the given int value */
gboolean
_fixate_caps_to_int (GstCaps ** caps, const gchar * field, gint value)
{
  GstCaps *try, *isect_lower, *isect_higher;
  gboolean ret = FALSE;
  guint i;

  /* First try to see if we can fixate by intersecting given caps with
   * simple audio caps with ranges starting/ending with value */
  try = gst_caps_new_simple ("audio/x-raw-int", field, GST_TYPE_INT_RANGE,
      G_MININT, value - 1, NULL);
  gst_caps_append (try, gst_caps_new_simple ("audio/x-raw-float", field,
          GST_TYPE_INT_RANGE, G_MININT, value - 1, NULL));
  isect_lower = gst_caps_intersect (*caps, try);
  gst_caps_free (try);

  if (!gst_caps_is_empty (isect_lower)) {
    try = gst_caps_new_simple ("audio/x-raw-int", field, GST_TYPE_INT_RANGE,
        value, G_MAXINT, NULL);
    gst_caps_append (try, gst_caps_new_simple ("audio/x-raw-float", field,
            GST_TYPE_INT_RANGE, value, G_MAXINT, NULL));
    isect_higher = gst_caps_intersect (*caps, try);
    /* FIXME: why choose to end up with the higher range, and not the fixed
     * value ? */
    if (!gst_caps_is_empty (isect_higher)) {
      gst_caps_free (*caps);
      *caps = isect_higher;
      ret = TRUE;
    } else {
      gst_caps_free (isect_higher);
    }
  }
  gst_caps_free (isect_lower);

  /* FIXME: why don't we already return here when ret == TRUE ? */
  for (i = 0; i < gst_caps_get_size (*caps); i++) {
    GstStructure *structure = gst_caps_get_structure (*caps, i);

    if (gst_structure_has_field (structure, field))
      ret |=
          gst_caps_structure_fixate_field_nearest_int (structure, field, value);
  }
  return ret;
}

static GstCaps *
gst_audio_convert_fixate (GstPad * pad, const GstCaps * caps)
{
  GstAudioConvert *this =
      GST_AUDIO_CONVERT (gst_object_get_parent (GST_OBJECT (pad)));
  GstPad *otherpad = (pad == this->sink ? this->src : this->sink);
  GstAudioConvertCaps ac_caps =
      (pad == this->sink ? this->srccaps : this->sinkcaps);
  GstCaps *copy;

  /* only fixate when we're proxying, so we don't fixate to some crap the other side doesn't want */
  if (!GST_PAD_IS_NEGOTIATING (otherpad))
    return NULL;

  copy = gst_caps_copy (caps);
  if (_fixate_caps_to_int (&copy, "channels", ac_caps.channels))
    return copy;
  if (_fixate_caps_to_int (&copy, "width", ac_caps.is_int ? ac_caps.width : 16))
    return copy;
  if (_fixate_caps_to_int (&copy, "depth", ac_caps.is_int ? ac_caps.depth : 16))
    return copy;
  if (_fixate_caps_to_int (&copy, "endianness",
          ac_caps.is_int ? ac_caps.endianness : G_BYTE_ORDER))
    return copy;


  gst_caps_free (copy);
  return NULL;
}

static GstElementStateReturn
gst_audio_convert_change_state (GstElement * element)
{
  GstAudioConvert *this = GST_AUDIO_CONVERT (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      this->convert_internal = NULL;
      break;
    default:
      break;
  }

  if (parent_class->change_state) {
    return parent_class->change_state (element);
  } else {
    return GST_STATE_SUCCESS;
  }
}

/* return a writable buffer of size which ideally is the same as before
   - You must unref the new buffer
   - The size of the old buffer is undefined after this operation */
static GstBuffer *
gst_audio_convert_get_buffer (GstBuffer * buf, guint size)
{
  GstBuffer *ret;

  GST_LOG
      ("new buffer of size %u requested. Current is: data: %p - size: %u - maxsize: %u",
      size, buf->data, buf->size, buf->maxsize);
  if (buf->maxsize >= size && gst_buffer_is_writable (buf)) {
    gst_buffer_ref (buf);
    buf->size = size;
    GST_LOG
        ("returning same buffer with adjusted values. data: %p - size: %u - maxsize: %u",
        buf->data, buf->size, buf->maxsize);
    return buf;
  } else {
    ret = gst_buffer_new_and_alloc (size);
    g_assert (ret);
    gst_buffer_stamp (ret, buf);
    GST_LOG ("returning new buffer. data: %p - size: %u - maxsize: %u",
        ret->data, ret->size, ret->maxsize);
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

#define CONVERT_TO(to, from, type, sign, endianness, LE_FUNC, BE_FUNC)		\
G_STMT_START{									\
  type value;									\
  memcpy (&value, from, sizeof (type));						\
  from -= sizeof (type);							\
  value = (endianness == G_LITTLE_ENDIAN) ? LE_FUNC (value) : BE_FUNC (value);	\
  if (sign) {									\
    to = value;									\
  } else {									\
    to = (gint64) value - (1 << (sizeof (type) * 8 - 1));			\
  }										\
}G_STMT_END;

static GstBuffer *
gst_audio_convert_buffer_to_default_format (GstAudioConvert * this,
    GstBuffer * buf)
{
  GstBuffer *ret;
  gint i, count;
  gint64 cur = 0;
  gint32 write;
  gint32 *dest;
  guint8 *src;

  if (this->sinkcaps.is_int) {
    if (this->sinkcaps.width == 32 && this->sinkcaps.depth == 32 &&
        this->sinkcaps.endianness == G_BYTE_ORDER
        && this->sinkcaps.sign == TRUE)
      return buf;

    ret =
        gst_audio_convert_get_buffer (buf,
        buf->size * 32 / this->sinkcaps.width);

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

    /* should just give the same buffer, unless it's not writable -- float is
     * already 32 bits */
    ret = gst_audio_convert_get_buffer (buf, buf->size);

    in = (gfloat *) GST_BUFFER_DATA (buf);
    out = (gint32 *) GST_BUFFER_DATA (ret);
    for (i = buf->size / sizeof (float); i > 0; i--) {
      *in *= 2147483647.0f + .5;
      *out = (gint32) CLAMP ((gint64) * in, -2147483648ll, 2147483647ll);
      out++;
      in++;
    }
  }

  gst_buffer_unref (buf);
  return ret;
}

#define POPULATE(format, be_func, le_func) G_STMT_START{			\
  format val;									\
  format* p = (format *) dest;							\
  int_value >>= (32 - this->srccaps.depth);					\
  if (this->srccaps.sign) {							\
    val = (format) int_value;							\
  } else {									\
    val = (format) int_value + (1 << (this->srccaps.depth - 1));		\
  }										\
  switch (this->srccaps.endianness) {						\
    case G_LITTLE_ENDIAN:                                                      \
      val = le_func (val);                                                     \
      break;                                                                   \
    case G_BIG_ENDIAN:                                                         \
      val = be_func (val);                                                     \
      break;                                                                   \
    default:                                                                   \
      g_assert_not_reached ();                                                 \
  };                                                                           \
  *p = val;                                                                    \
  p ++;                                                                        \
  dest = (guint8 *) p;                                                         \
}G_STMT_END

static GstBuffer *
gst_audio_convert_buffer_from_default_format (GstAudioConvert * this,
    GstBuffer * buf)
{
  GstBuffer *ret;
  guint count, i;
  gint32 *src;

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

    dest = ret->data;
    src = (gint32 *) buf->data;

    for (i = 0; i < count; i++) {
      gint32 int_value = *src;

      src++;
      switch (this->srccaps.width) {
        case 8:
          if (this->srccaps.sign) {
            POPULATE (gint8, GINT8_IDENTITY, GINT8_IDENTITY);
          } else {
            POPULATE (guint8, GUINT8_IDENTITY, GUINT8_IDENTITY);
          }
          break;
        case 16:
          if (this->srccaps.sign) {
            POPULATE (gint16, GINT16_TO_BE, GINT16_TO_LE);
          } else {
            POPULATE (guint16, GUINT16_TO_BE, GUINT16_TO_LE);
          }
          break;
        case 32:
          if (this->srccaps.sign) {
            POPULATE (gint32, GINT32_TO_BE, GINT32_TO_LE);
          } else {
            POPULATE (guint32, GUINT32_TO_BE, GUINT32_TO_LE);
          }
          break;
        default:
          g_assert_not_reached ();
      }
    }
  } else {
    gfloat *dest;

    /* 1 / (2^31-1) * i */
#define INT2FLOAT(i) (4.6566128752457969e-10 * ((gfloat)i))
    count = buf->size / 4;      /* size is undefined after gst_audio_convert_get_buffer! */
    ret =
        gst_audio_convert_get_buffer (buf,
        buf->size * this->srccaps.width / 32);

    dest = (gfloat *) ret->data;
    src = (gint32 *) buf->data;
    for (i = 0; i < count; i++) {
      *dest = (4.6566128752457969e-10 * ((gfloat) * src));
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
  GstBuffer *ret;
  gint i, count;
  gint32 *src, *dest;

  if (this->sinkcaps.channels == this->srccaps.channels)
    return buf;

  count = GST_BUFFER_SIZE (buf) / 4 / this->sinkcaps.channels;
  ret = gst_audio_convert_get_buffer (buf, count * 4 * this->srccaps.channels);
  src = (gint32 *) GST_BUFFER_DATA (buf);
  dest = (gint32 *) GST_BUFFER_DATA (ret);

  if (this->sinkcaps.channels > this->srccaps.channels) {
    for (i = 0; i < count; i++) {
      *dest = *src >> 1;
      src++;
      *dest += (*src >> 1) + (*src & 1);
      src++;
      dest++;
    }
  } else {
    for (i = count - 1; i >= 0; i--) {
      dest[2 * i] = dest[2 * i + 1] = src[i];
    }
  }

  gst_buffer_unref (buf);
  return ret;
}
