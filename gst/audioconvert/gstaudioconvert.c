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

GST_DEBUG_CATEGORY_STATIC (audio_convert_debug);
#define GST_CAT_DEFAULT (audio_convert_debug)

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

  /* int audio caps */
  gint     depth;
  gboolean is_signed;
};

struct _GstAudioConvert {
  GstElement element;

  /* pads */
  GstPad * sink;
  GstPad * src;

  /* properties */
  gboolean aggressive;

  /* caps: 0 = sink, 1 = src, so always convert from 0 to 1 */
  gboolean caps_set[2];
  GstAudioConvertCaps caps[2];

  gint                law[2];
  gint                endian[2];
  gint                sign[2];
  gint                depth[2];        /* in BITS */
  gint                width[2];        /* in BITS */
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
static GstCaps *             gst_audio_convert_getcaps      (GstPad *pad);
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
    GST_AUDIO_INT_PAD_TEMPLATE_CAPS
  )
);

static GstStaticPadTemplate gst_audio_convert_sink_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    GST_AUDIO_INT_PAD_TEMPLATE_CAPS
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
  GST_DEBUG_CATEGORY_INIT (audio_convert_debug, "audioconvert", 0, "audio conversion element");

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

  gstelement_class->change_state = gst_audio_convert_change_state;
}

static GstCaps *
gst_audioconvert_getcaps (GstPad *pad)
{
  GstAudioConvert *this;
  GstCaps *othercaps;
  GstCaps *caps;
  GstPad *otherpad;
  int i;

  GST_DEBUG ("gst_audioconvert_getcaps");
  this = GST_AUDIO_CONVERT (gst_pad_get_parent (pad));

  otherpad = (pad == this->src) ? this->sink : this->src;
  othercaps = gst_pad_get_allowed_caps (otherpad);

  GST_DEBUG_CAPS ("othercaps are", othercaps);

  caps = gst_caps_copy (othercaps);
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    gst_audio_structure_set_int (structure, GST_AUDIO_FIELD_CHANNELS      |
                                            GST_AUDIO_FIELD_ENDIANNESS    |
                                            GST_AUDIO_FIELD_WIDTH         |
                                            GST_AUDIO_FIELD_DEPTH         |
                                            GST_AUDIO_FIELD_SIGNED);

    /* FIXME:
     * since gst_structure_set doesn't handle lists, we need to do lists
     * manually; would be nice if this could be done more easily */
  }
  return caps;
}

static void
gst_audio_convert_init (GstAudioConvert *this)
{
  /* sinkpad */
  this->sink = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_audio_convert_sink_template), "sink");
  gst_pad_set_getcaps_function (this->sink, gst_audio_convert_getcaps);
  gst_pad_set_link_function (this->sink, gst_audio_convert_link);
  gst_pad_set_getcaps_function (this->sink, gst_audioconvert_getcaps);
  gst_element_add_pad (GST_ELEMENT(this), this->sink);

  /* srcpad */
  this->src = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_audio_convert_src_template), "src");
  gst_pad_set_getcaps_function (this->src, gst_audio_convert_getcaps);
  gst_pad_set_link_function (this->src, gst_audio_convert_link);
  gst_pad_set_getcaps_function (this->src, gst_audioconvert_getcaps);
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

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  g_return_if_fail (GST_IS_AUDIO_CONVERT (GST_OBJECT_PARENT (pad)));
  this = GST_AUDIO_CONVERT (GST_OBJECT_PARENT (pad));

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

static GstCaps *
gst_audio_convert_getcaps (GstPad *pad)
{
  GstAudioConvert *this;
  GstPad *otherpad;
  GstCaps *othercaps;
  GstCaps *caps;
  int i;

  g_return_val_if_fail(GST_IS_PAD(pad), NULL);
  g_return_val_if_fail(GST_IS_AUDIO_CONVERT(GST_OBJECT_PARENT (pad)), NULL);
  this = GST_AUDIO_CONVERT(GST_OBJECT_PARENT (pad));

  otherpad = (pad == this->src) ? this->sink : this->src;

  othercaps = gst_pad_get_allowed_caps (otherpad);

  for (i=0;i<gst_caps_get_size (othercaps); i++) {
    GstStructure *structure;

    structure = gst_caps_get_structure (othercaps, i);
    gst_structure_remove_field (structure, "channels");
    gst_structure_remove_field (structure, "endianness");
    gst_structure_remove_field (structure, "width");
    gst_structure_remove_field (structure, "depth");
    gst_structure_remove_field (structure, "signed");
  }
  caps = gst_caps_intersect (othercaps, gst_pad_get_pad_template_caps (pad));
  gst_caps_free(othercaps);

  return caps;
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

  /* nr is 0 for sink pad, 1 for src pad */
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
    GST_DEBUG ("could not get some values from structure");
    return GST_PAD_LINK_REFUSED;
  }

  /* we don't convert rate changes, this is done by audioscale */
  /* 1 - nr is "the other caps" */
  if ((this->caps_set[1 - nr]) &&
      (rate != this->rate[1 - nr])) {
    GstPad *otherpad;
    GstPadLinkReturn ret;

    otherpad = (nr) ? this->src : this->sink;
    if (gst_pad_is_negotiated (otherpad))
    {
      GstCaps *othercaps = gst_caps_copy (othercaps);

      gst_caps_set_simple (othercaps, "rate", G_TYPE_INT, rate, NULL);

      ret = gst_pad_try_set_caps (otherpad, othercaps);
      if (GST_PAD_LINK_FAILED (ret))
      {
        GST_DEBUG ("could not gst_pad_try_set_caps on otherpad using othercaps");
        return GST_PAD_LINK_REFUSED;
      }
    }
    this->rate[1 - nr] = rate;
  }

  GST_DEBUG ("setting caps_set[%d] to TRUE", nr);
  this->caps_set[nr] = TRUE;
  this->rate[nr] = rate;
  this->channels[nr] = channels;
  this->sign[nr] = sign;
  this->endian[nr] = endianness;
  this->depth[nr] = depth;
  this->width[nr] = width;

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

  if (this->width[0] == 32 && this->depth[0] == 32 &&
      this->endian[0] == G_BYTE_ORDER && this->sign[0] == TRUE)
    return buf;

  ret = gst_audio_convert_get_buffer (buf, buf->size * 32 / this->width[0]);

  count = ret->size / 4;
  src = buf->data + (count - 1) * (this->width[0] / 8);
  dest = (gint32 *) ret->data;
  for (i = count - 1; i >= 0; i--) {
    switch (this->width[0]) {
    case 8:
    if (this->sign[0]) {
      CONVERT_TO (cur, src, gint8, this->sign[0], this->endian[0], GINT8_IDENTITY, GINT8_IDENTITY);
    } else {
      CONVERT_TO (cur, src, guint16, this->sign[0], this->endian[0], GUINT8_IDENTITY, GUINT8_IDENTITY);
    }
    break;
    case 16:
    if (this->sign[0]) {
      CONVERT_TO (cur, src, gint16, this->sign[0], this->endian[0], GINT16_FROM_LE, GINT16_FROM_BE);
    } else {
      CONVERT_TO (cur, src, guint16, this->sign[0], this->endian[0], GUINT16_FROM_LE, GUINT16_FROM_BE);
    }
    break;
    case 32:
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

  if (this->width[1] == 32 && this->depth[1] == 32 &&
      this->endian[1] == G_BYTE_ORDER && this->sign[1] == TRUE)
    return buf;

  ret = gst_audio_convert_get_buffer (buf, buf->size * this->width[1] / 32);

  dest = ret->data;
  src = (gint32 *) buf->data;

  count = ret->size / (this->width[1] / 8);

  for (i = 0; i < count; i++) {
    gint32 int_value = *src;
    src++;
    switch (this->width[1]) {
    case 8:
    if (this->sign[1]) {
      POPULATE (gint8, GINT8_IDENTITY, GINT8_IDENTITY);
    } else {
      POPULATE (guint8, GUINT8_IDENTITY, GUINT8_IDENTITY);
    }
    break;
    case 16:
    if (this->sign[1]) {
      POPULATE (gint16, GINT16_TO_BE, GINT16_TO_LE);
    } else {
      POPULATE (guint16, GUINT16_TO_BE, GUINT16_TO_LE);
    }
    break;
    case 32:
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
  if (!gst_plugin_load ("gstaudio")) return FALSE;

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
