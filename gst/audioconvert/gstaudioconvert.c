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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <string.h>

#if 0
static void
print_caps (GstCaps *caps)
{
  GValue v = { 0, };
  GValue s = { 0, };
  g_value_init (&v, GST_TYPE_CAPS);
  g_value_init (&s, G_TYPE_STRING);
  g_value_set_boxed (&v, caps);
  g_value_transform (&v, &s);
  g_print ("%s\n", g_value_get_string (&s));
  g_value_unset (&v);
  g_value_unset (&s);
}
#endif

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
struct _GstAudioConvertCaps {
  /* general caps */
  gint     endianness;
  gint     width;
  gint     rate;
  gint     channels;

  gboolean is_float; /* true iff a pad is carrying float data */

  /* int audio caps */
  gint     depth;
  gboolean is_signed;

  /* float audio caps */
  guint    buffer_frames;
};

struct _GstAudioConvert {
  GstElement element;

  /* pads */
  GstPad * sink;
  GstPad * src;

  /* properties */
  gboolean aggressive;
  guint    min_rate, max_rate, rate_steps;

  /* caps: 0 = sink, 1 = src, so always convert from 0 to 1 */
  gboolean caps_set[2];
  GstAudioConvertCaps caps[2];

  gint                law[2];
  gint                endian[2];
  gint                sign[2];
  gint                depth[2];        /* in BITS */
  gint                width[2];        /* in BYTES */
  gint                rate[2];
  gint                channels[2];

  /* conversion functions */
  GstBuffer * (* convert_internal) (GstAudioConvert *this, GstBuffer *buf);
};

struct _GstAudioConvertClass {
  GstElementClass parent_class;
};

static GstElementDetails audio_convert_details = {
  "Audio Conversion",
  "Filter/Converter/Audio",
  "Convert audio to different formats",
  "Benjamin Otte <in7y118@public.uni-hamburg.de",
};

/* type functions */
static GType gst_audio_convert_get_type     (void);
static void  gst_audio_convert_base_init    (gpointer g_class);
static void  gst_audio_convert_class_init   (GstAudioConvertClass *klass);
static void  gst_audio_convert_init         (GstAudioConvert *audio_convert);
static void  gst_audio_convert_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void  gst_audio_convert_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/* gstreamer functions */
static void                  gst_audio_convert_chain        (GstPad *pad, GstData *_data);
static GstPadLinkReturn      gst_audio_convert_link         (GstPad *pad, const GstCaps *caps);
static GstElementStateReturn gst_audio_convert_change_state (GstElement *element);

/* actual work */
#if 0
static gboolean    gst_audio_convert_set_caps (GstPad *pad);
#endif

static GstBuffer * gst_audio_convert_buffer_to_default_format   (GstAudioConvert *this, GstBuffer *buf);
static GstBuffer * gst_audio_convert_buffer_from_default_format (GstAudioConvert *this, GstBuffer *buf);

static GstBuffer * gst_audio_convert_channels (GstAudioConvert *this, GstBuffer *buf);

/* AudioConvert signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_AGGRESSIVE,
  ARG_MIN_RATE,
  ARG_MAX_RATE,
  ARG_RATE_STEPS
};

static GstElementClass *parent_class = NULL;
/*static guint gst_audio_convert_signals[LAST_SIGNAL] = { 0 }; */

/*** GSTREAMER PROTOTYPES *****************************************************/

static GstStaticPadTemplate gst_audio_convert_src_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    GST_AUDIO_INT_PAD_TEMPLATE_CAPS "; "
    GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS
  )
);

static GstStaticPadTemplate gst_audio_convert_sink_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    GST_AUDIO_INT_PAD_TEMPLATE_CAPS "; "
    GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS
  )
);

/*** TYPE FUNCTIONS ***********************************************************/

GType
gst_audio_convert_get_type(void) {
  static GType audio_convert_type = 0;

  if (!audio_convert_type) {
    static const GTypeInfo audio_convert_info = {
      sizeof(GstAudioConvertClass),
      gst_audio_convert_base_init,
      NULL,
      (GClassInitFunc)gst_audio_convert_class_init,
      NULL,
      NULL,
      sizeof(GstAudioConvert),
      0,
      (GInstanceInitFunc)gst_audio_convert_init,
    };
    audio_convert_type = g_type_register_static(GST_TYPE_ELEMENT,
                                                "GstAudioConvert",
                                                &audio_convert_info, 0);
  }
  return audio_convert_type;
}

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
gst_audio_convert_class_init (GstAudioConvertClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_audio_convert_set_property;
  gobject_class->get_property = gst_audio_convert_get_property;

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_AGGRESSIVE,
    g_param_spec_boolean("aggressive", "aggressive mode",
                         "if true, tries any possible format before giving up",
                         FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MIN_RATE,
    g_param_spec_uint("min-rate", "minimum rate allowed",
                      "defines the lower bound for the audio rate",
                      0, G_MAXUINT, 8000, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MAX_RATE,
    g_param_spec_uint("max-rate", "maximum rate allowed",
                      "defines the upper bound for the audio rate",
                      0, G_MAXUINT, 192000, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_RATE_STEPS,
    g_param_spec_uint("rate-steps", "rate search steps",
                      "the number of steps used for searching between min and max rates",
                      0, G_MAXUINT, 32, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  gstelement_class->change_state = gst_audio_convert_change_state;
}

static void
gst_audio_convert_init (GstAudioConvert *this)
{
  /* sinkpad */
  this->sink = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_audio_convert_sink_template), "sink");
  gst_pad_set_link_function (this->sink, gst_audio_convert_link);
  gst_element_add_pad (GST_ELEMENT(this), this->sink);

  /* srcpad */
  this->src = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_audio_convert_src_template), "src");
  gst_pad_set_link_function (this->src, gst_audio_convert_link);
  gst_element_add_pad (GST_ELEMENT(this), this->src);

  gst_pad_set_chain_function(this->sink, gst_audio_convert_chain);

  /* clear important variables */
  this->caps_set[0] = this->caps_set[1] = FALSE;
  this->convert_internal = NULL;
}
static void
gst_audio_convert_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstAudioConvert *audio_convert;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_AUDIO_CONVERT(object));
  audio_convert = GST_AUDIO_CONVERT(object);

  switch (prop_id) {
    case ARG_AGGRESSIVE:
      audio_convert->aggressive = g_value_get_boolean (value);
      break;
    case ARG_MIN_RATE:
      audio_convert->min_rate = g_value_get_uint (value);
      break;
    case ARG_MAX_RATE:
      audio_convert->max_rate = g_value_get_uint (value);
      break;
    case ARG_RATE_STEPS:
      audio_convert->rate_steps = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_convert_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAudioConvert *audio_convert;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_AUDIO_CONVERT(object));
  audio_convert = GST_AUDIO_CONVERT(object);

  switch (prop_id) {
    case ARG_AGGRESSIVE:
      g_value_set_boolean (value, audio_convert->aggressive);
      break;
    case ARG_MIN_RATE:
      g_value_set_uint (value, audio_convert->min_rate);
      break;
    case ARG_MAX_RATE:
      g_value_set_uint (value, audio_convert->max_rate);
      break;
    case ARG_RATE_STEPS:
      g_value_set_uint (value, audio_convert->rate_steps);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/*** GSTREAMER FUNCTIONS ******************************************************/

static void
gst_audio_convert_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstAudioConvert *this;

  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);
  g_return_if_fail(GST_IS_AUDIO_CONVERT(GST_OBJECT_PARENT (pad)));
  this = GST_AUDIO_CONVERT(GST_OBJECT_PARENT (pad));

  /* FIXME */
  if (GST_IS_EVENT (buf)) {
    gst_pad_event_default (pad, GST_EVENT (buf));
    return;
  }

  g_assert(this->caps_set[0] && this->caps_set[1]);

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

static GstPadLinkReturn
gst_audio_convert_link (GstPad *pad, const GstCaps *caps)
{
  GstAudioConvert *this;
  gint nr = 0;
  gint rate, endianness, depth, width, channels;
  gboolean sign;
  GstStructure *structure;
  gboolean ret;

  g_return_val_if_fail(GST_IS_PAD(pad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail(GST_IS_AUDIO_CONVERT(GST_OBJECT_PARENT (pad)), GST_PAD_LINK_REFUSED);
  this = GST_AUDIO_CONVERT(GST_OBJECT_PARENT (pad));

  nr = (pad == this->sink) ? 0 : (pad == this->src) ? 1 : -1;
  g_assert (nr > -1);

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "channels", &channels);
  ret &= gst_structure_get_boolean (structure, "signed", &sign);
  ret &= gst_structure_get_int (structure, "depth", &depth);
  ret &= gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "rate", &rate);
  endianness = G_BYTE_ORDER;
  if (width != 8) {
    ret &= gst_structure_get_int (structure, "endianness", &endianness);
  }

  if (!ret) {
    return GST_PAD_LINK_REFUSED;
  }

  /* we can't convert rate changes yet */
  if ((this->caps_set[1 - nr]) &&
      (rate != this->rate[1 - nr])) {
    GstPad *otherpad;
    GstCaps *othercaps;
    GstPadLinkReturn ret;
    
    otherpad = (nr) ? this->src : this->sink;
    if (gst_pad_is_negotiated (otherpad)) {
      othercaps = gst_caps_copy (gst_pad_get_negotiated_caps (otherpad));

      gst_caps_set_simple (othercaps, "rate", G_TYPE_INT, rate, NULL);

      ret = gst_pad_try_set_caps (otherpad, othercaps);
      if (GST_PAD_LINK_FAILED (ret)) return ret;

      this->rate[1 - nr] = rate;
    }
  }

  this->caps_set[nr] = TRUE;
  this->rate[nr] = rate;
  this->channels[nr] = channels;
  this->sign[nr] = sign;
  this->endian[nr] = endianness;
  this->depth[nr] = depth;
  this->width[nr] = width / 8;

  return GST_PAD_LINK_OK;
}

static GstElementStateReturn
gst_audio_convert_change_state (GstElement *element)
{
  GstAudioConvert *this = GST_AUDIO_CONVERT (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      this->caps_set[0] = this->caps_set[1] = FALSE;
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

/*** ACTUAL WORK **************************************************************/

#if 0
static GstCaps *
make_caps (gint endianness, gboolean sign, gint depth, gint width, gint rate, gint channels)
{
  GstCaps *caps;

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "signed", G_TYPE_BOOLEAN, sign,
      "depth", G_TYPE_INT, depth,
      "width", G_TYPE_INT, width * 8,
      "rate", G_TYPE_INT, rate,
      "channels", G_TYPE_INT, channels, NULL);

  if (width != 1) {
    gst_caps_set_simple (caps, 
        "endianness", G_TYPE_INT, endianness, NULL);
  }

  return caps;
}

static gboolean
gst_audio_convert_set_caps (GstPad *pad)
{
  GstCaps *caps;
  gint nr;
  GstPadLinkReturn ret;
  GstAudioConvert *this;
  gint channels, endianness, depth, width; /*, rate; */
  gboolean sign;

  this = GST_AUDIO_CONVERT (GST_PAD_PARENT (pad));
  nr = (this->src == pad) ? 1 : (this->sink == pad) ? 0 : -1;
  g_assert (nr > -1);

  /* try 1:1 first */
  caps = make_caps (this->endian[1 - nr], this->sign[1 - nr], this->depth[1 - nr], 
                    this->width[1 - nr], this->rate[1 - nr], this->channels[1 - nr]);
  ret = gst_pad_try_set_caps (pad, caps);
  if (ret == GST_PAD_LINK_DONE || ret == GST_PAD_LINK_OK) goto success;

  /* now do some iterating, this is gonna be fun */
  /* stereo is most important */
  channels = 2;
  while (channels > 0) {

    /* endianness comes second */
    endianness = 0;
    do {
      if (endianness == G_BIG_ENDIAN) break;
      endianness = endianness == 0 ? G_LITTLE_ENDIAN : G_BIG_ENDIAN;

      /* signedness */
      sign = TRUE;
      do {
        sign = !sign;

        /* width */
        for (width = 4; width >= 1; width--) {

          /* depth */
          for (depth = width * 8; depth >= 1; depth -= this->aggressive ? 1 : 8) {

            /* rate - not supported yet*/

              caps = make_caps (endianness, sign, depth, width, this->rate[1 - nr], channels);
              ret = gst_pad_try_set_caps (pad, caps);
              if (ret == GST_PAD_LINK_DONE || ret == GST_PAD_LINK_OK)
                goto success;
          }
        }
      } while (sign != TRUE);
    } while TRUE;
    channels--;
  }

  return FALSE;

 success:
  g_assert (gst_audio_convert_link (pad, caps) == GST_PAD_LINK_OK);
  return TRUE;
}
#endif

/* return a writable buffer of size which ideally is the same as before
   - You must unref the new buffer
   - The size of the old buffer is undefined after this operation */
static GstBuffer*
gst_audio_convert_get_buffer (GstBuffer *buf, guint size)
{
  GstBuffer *ret;
  if (buf->maxsize >= size && gst_buffer_is_writable (buf)) {
    gst_buffer_ref (buf);
    buf->size = size;
    return buf;
  } else if (buf->maxsize >= size) {
    buf = gst_buffer_copy (buf);
    buf->size = size;
    return buf;
  } else {
    g_assert ((ret = gst_buffer_new_and_alloc (size)));
    ret->timestamp = buf->timestamp;
    return ret;
  }
}

static inline guint8 GUINT8_IDENTITY (guint8 x) { return x; }
static inline guint8 GINT8_IDENTITY (gint8 x) { return x; }

#define CONVERT_TO(to, from, type, sign, endianness, LE_FUNC, BE_FUNC) G_STMT_START{\
  type value;                                                                  \
  memcpy (&value, from, sizeof (type));                                        \
  from -= sizeof (type);                                                       \
  value = (endianness == G_LITTLE_ENDIAN) ? LE_FUNC (value) : BE_FUNC (value); \
  if (sign) {                                                                  \
    to = value;                                                                \
  } else {                                                                     \
    to = (gint64) value - (1 << (sizeof (type) * 8 - 1));                      \
  }                                                                            \
}G_STMT_END;

static GstBuffer*
gst_audio_convert_buffer_to_default_format (GstAudioConvert *this, GstBuffer *buf)
{
  GstBuffer *ret;
  gint i, count;
  gint64 cur = 0;
  gint32 write;
  gint32 *dest;
  guint8 *src;

  if (this->width[0] == 4 && this->depth[0] == 32 &&
      this->endian[0] == G_BYTE_ORDER && this->sign[0] == TRUE)
    return buf;

  ret = gst_audio_convert_get_buffer (buf, buf->size * 4 / this->width[0]);

  count = ret->size / 4;
  src = buf->data + (count - 1) * this->width[0];
  dest = (gint32 *) ret->data;
  for (i = count - 1; i >= 0; i--) {
    switch (this->width[0]) {
    case 1:
    if (this->sign[0]) {
      CONVERT_TO (cur, src, gint8, this->sign[0], this->endian[0], GINT8_IDENTITY, GINT8_IDENTITY);
    } else {
      CONVERT_TO (cur, src, guint16, this->sign[0], this->endian[0], GUINT8_IDENTITY, GUINT8_IDENTITY);
    }
    break;
    case 2:
    if (this->sign[0]) {
      CONVERT_TO (cur, src, gint16, this->sign[0], this->endian[0], GINT16_FROM_LE, GINT16_FROM_BE);
    } else {
      CONVERT_TO (cur, src, guint16, this->sign[0], this->endian[0], GUINT16_FROM_LE, GUINT16_FROM_BE);
    }
    break;
    case 4:
    if (this->sign[0]) {
      CONVERT_TO (cur, src, gint32, this->sign[0], this->endian[0], GINT32_FROM_LE, GINT32_FROM_BE);
    } else {
      CONVERT_TO (cur, src, guint32, this->sign[0], this->endian[0], GUINT32_FROM_LE, GUINT32_FROM_BE);
    }
    break;
    default:
    g_assert_not_reached ();
    }
    cur = cur * ((gint64) 1 << (32 - this->depth[0]));
    cur = CLAMP (cur, -((gint64)1 << 32), (gint64) 0x7FFFFFFF);
    write = cur;
    memcpy (&dest[i], &write, 4);
  }

  gst_buffer_unref (buf);
  return ret;
}

#define POPULATE(format, be_func, le_func) G_STMT_START{                       \
  format val;                                                                  \
  format* p = (format *) dest;                                                 \
  int_value >>= (32 - this->depth[1]);                                         \
  val = (format) int_value;                                                    \
  switch (this->endian[1]) {                                                   \
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
gst_audio_convert_buffer_from_default_format (GstAudioConvert *this, GstBuffer *buf)
{
  GstBuffer *ret;
  guint8 *dest;
  guint count, i;
  gint32 *src;

  if (this->width[1] == 4 && this->depth[1] == 32 &&
      this->endian[1] == G_BYTE_ORDER && this->sign[1] == TRUE)
    return buf;

  ret = gst_audio_convert_get_buffer (buf, buf->size * this->width[1] / 4);

  dest = ret->data;
  src = (gint32 *) buf->data;

  count = ret->size / this->width[1];

  for (i = 0; i < count; i++) {
    gint32 int_value = *src;
    src++;
    switch (this->width[1]) {
    case 1:
    if (this->sign[1]) {
      POPULATE (gint8, GINT8_IDENTITY, GINT8_IDENTITY);
    } else {
      POPULATE (guint8, GUINT8_IDENTITY, GUINT8_IDENTITY);
    }
    break;
    case 2:
    if (this->sign[1]) {
      POPULATE (gint16, GINT16_TO_BE, GINT16_TO_LE);
    } else {
      POPULATE (guint16, GUINT16_TO_BE, GUINT16_TO_LE);
    }
    break;
    case 4:
    if (this->sign[1]) {
      POPULATE (gint32, GINT32_TO_BE, GINT32_TO_LE);
    } else {
      POPULATE (guint32, GUINT32_TO_BE, GUINT32_TO_LE);
    }
    break;
    default:
    g_assert_not_reached ();
    }
  }

  gst_buffer_unref(buf);
  return ret;
}

static GstBuffer *
gst_audio_convert_channels (GstAudioConvert *this, GstBuffer *buf)
{
  GstBuffer *ret;
  gint i, count;
  guint32 *src, *dest;

  if (this->channels[0] == this->channels[1])
    return buf;

  count = GST_BUFFER_SIZE (buf) / 4 / this->channels[0];
  ret = gst_audio_convert_get_buffer (buf, count * 4 * this->channels[1]);
  src = (guint32 *) GST_BUFFER_DATA (buf);
  dest = (guint32 *) GST_BUFFER_DATA (ret);

  if (this->channels[0] > this->channels[1]) {
    for (i = 0; i < count; i++) {
      *dest = *src >> 1;
      src++;
      *dest += (*src + 1) >> 1;
      src++;
      dest++;
    }
  } else {
    for (i = count - 1; i >= 0; i--) {
      dest[2 * i] = dest[2 * i + 1] = src[i];
    }
  }

  gst_buffer_unref(buf);
  return ret;
}

/*** PLUGIN DETAILS ***********************************************************/

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "audioconvert", GST_RANK_NONE, GST_TYPE_AUDIO_CONVERT))
    return FALSE;
  
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstaudioconvert",
  "Convert audio to different formats",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN)
