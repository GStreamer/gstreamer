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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/floatcast/floatcast.h>
#include <string.h>

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
struct _GstAudioConvertCaps {
  /* general caps */
  gboolean		is_int;
  gint			endianness;
  gint			width;
  gint			rate;
  gint			channels;

  /* int audio caps */
  gboolean		sign;
  gint			depth;

  /* float audio caps */
  gint			buffer_frames;
};

struct _GstAudioConvert {
  GstElement		element;

  /* pads */
  GstPad *		sink;
  GstPad *		src;

  GstAudioConvertCaps	srccaps;
  GstAudioConvertCaps	sinkcaps;

  /* conversion functions */
  GstBuffer *		(* convert_internal) (GstAudioConvert *this, GstBuffer *buf);

  /* for int2float */
  GstBuffer *		output;
  gint			output_samples_needed;
};

struct _GstAudioConvertClass {
  GstElementClass parent_class;
};

static GstElementDetails audio_convert_details = {
  "Audio Conversion",
  "Filter/Converter/Audio",
  "Convert audio to different formats",
  "Benjamin Otte <in7y118@public.uni-hamburg.de>",
};

/* type functions */
static GType gst_audio_convert_get_type     (void);
static void  gst_audio_convert_base_init    (gpointer g_class);
static void  gst_audio_convert_class_init   (GstAudioConvertClass *klass);
static void  gst_audio_convert_init         (GstAudioConvert *audio_convert);

/* gstreamer functions */
static void                  gst_audio_convert_chain        (GstPad *pad, GstData *_data);
static void                  gst_audio_convert_chain_int2float (GstPad *pad, GstData *_data);
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

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (audio_convert_debug, "audioconvert", 0, "audio conversion element");
  
GST_BOILERPLATE_FULL (GstAudioConvert, gst_audio_convert, GstElement, GST_TYPE_ELEMENT, DEBUG_INIT);

/*** GSTREAMER PROTOTYPES *****************************************************/

static GstStaticPadTemplate gst_audio_convert_src_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, MAX ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) { 8, 16, 32 }, " \
    "depth = (int) [ 1, 32 ], " \
    "signed = (boolean) { true, false }; " 

    "audio/x-raw-float, " \
    "rate = (int) [ 1, MAX ], "
    "channels = (int) [ 1, MAX ], "
    "endianness = (int) BYTE_ORDER, "
    "width = (int) 32, "
    "buffer-frames = (int) [ 0, MAX ]"
  )
);

static GstStaticPadTemplate gst_audio_convert_sink_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, MAX ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) { 8, 16, 32 }, " \
    "depth = (int) [ 1, 32 ], " \
    "signed = (boolean) { true, false }; " 

    "audio/x-raw-float, "
    "rate = (int) [ 1, MAX ],"
    "channels = (int) [ 1, MAX ], "
    "endianness = (int) BYTE_ORDER, "
    "width = (int) 32, "
    "buffer-frames = (int) [ 0, MAX ]"
  )
);

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
gst_audio_convert_class_init (GstAudioConvertClass *klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->change_state = gst_audio_convert_change_state;
}

static void
gst_audio_convert_init (GstAudioConvert *this)
{
  /* sinkpad */
  this->sink = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_audio_convert_sink_template), "sink");
  gst_pad_set_getcaps_function (this->sink, gst_audio_convert_getcaps);
  gst_pad_set_link_function (this->sink, gst_audio_convert_link);
  gst_element_add_pad (GST_ELEMENT(this), this->sink);

  /* srcpad */
  this->src = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_audio_convert_src_template), "src");
  gst_pad_set_getcaps_function (this->src, gst_audio_convert_getcaps);
  gst_pad_set_link_function (this->src, gst_audio_convert_link);
  gst_element_add_pad (GST_ELEMENT(this), this->src);

  gst_pad_set_chain_function(this->sink, gst_audio_convert_chain);

  /* clear important variables */
  this->convert_internal = NULL;
}

/*** GSTREAMER FUNCTIONS ******************************************************/

static void
gst_audio_convert_chain (GstPad *pad, GstData *data)
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

  if (!gst_pad_is_negotiated (this->sink))
  {
    GST_ELEMENT_ERROR (this, CORE, NEGOTIATION, NULL,
                       ("Sink pad not negotiated before chain function"));
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

/* 1 / (2^31-1) * i */
#define INT2FLOAT(i) (4.6566128752457969e-10 * ((gfloat)i))

/* This custom chain handler exists because if buffer-frames is nonzero, one int
 * buffer probably doesn't correspond to one float buffer */
static void
gst_audio_convert_chain_int2float (GstPad *pad, GstData *data)
{
  GstBuffer *buf = GST_BUFFER (data);
  GstAudioConvert *this;
  gint buffer_samples, samples_remaining, i;
  gint32 *in;
  gfloat *out;

  this = GST_AUDIO_CONVERT (GST_OBJECT_PARENT (pad));

  /* FIXME */
  if (GST_IS_EVENT (buf)) {
    gst_pad_event_default (pad, GST_EVENT (buf));
    return;
  }

  /* we know we're negotiated, because it's the link function that set the
     custom chain handler */
  
  /* FIXME: this runs into scheduling problems if the next element is loop-based
   * (the bufpen fills up until infinity because we push multiple buffers per
   * chain, in the normal situation). The fix is either to make the opt
   * scheduler choose the loop group as its entry, or to make this a loop
   * plugin. But I want to commit, will fix this later. */

  /**
   * Theory of operation:
   * - convert the format (endianness, signedness, width, depth) to
   *   (G_BYTE_ORDER, TRUE, 32, 32)
   * - convert rate and channels
   * - if buffer-frames is zero, convert and push.
   * - if we have an output buffer, fill it. if it becomes full, push it.
   * - while buffer-frames is less than the number of frames remaining in the
   *   input, create sub-buffers, convert and push.
   * - if there are leftover frames in the input, create an output buffer and
   *   fill it partially.
   */

  buf = gst_audio_convert_buffer_to_default_format (this, buf);

  buf = gst_audio_convert_channels (this, buf);

  /* we know buf is writable */
  buffer_samples = this->srccaps.buffer_frames * this->srccaps.channels;
  in = (gint32*)GST_BUFFER_DATA (buf);
  out = (gfloat*)GST_BUFFER_DATA (buf);
  samples_remaining = buf->size / sizeof(gint32);

  if (!buffer_samples ||
      (!this->output && samples_remaining == buffer_samples)) {
    for (i=samples_remaining; i; i--)
      *(out++) = INT2FLOAT (*(in++));
    gst_pad_push (this->src, GST_DATA (buf));
    return;
  }

  if (this->output) {
    GstBuffer *output = this->output;
    gint to_process = MIN (this->output_samples_needed, samples_remaining);

    out = ((gfloat*)GST_BUFFER_DATA (output) + 
           (buffer_samples - this->output_samples_needed));
    
    for (i=to_process; i; i--)
      *(out++) = INT2FLOAT (*(in++));
    this->output_samples_needed -= to_process;
    samples_remaining -= to_process;
    
    /* one of the two of these ifs will be true, and possibly both of them */
    if (!this->output_samples_needed) {
      this->output = NULL;
      gst_pad_push (this->src, GST_DATA (output));
    }

    if (!samples_remaining) {
      gst_buffer_unref (buf);
      return;
    }
    
    /* we have some leftover frames in buf, let's take care of them */
    out = (gfloat*)in;
  }
  
  while (samples_remaining > buffer_samples) {
    GstBuffer *sub_buf;
    sub_buf = gst_buffer_create_sub (buf,
                                     (GST_BUFFER_SIZE (buf) -
                                      samples_remaining * sizeof(gint32)),
                                     buffer_samples * sizeof(gfloat));
    /* `out' should be positioned correctly */
    for (i=buffer_samples; i; i--)
      *(out++) = INT2FLOAT (*(in++));
    samples_remaining -= buffer_samples;

    gst_pad_push (this->src, GST_DATA (sub_buf));
  }
    
  if (samples_remaining) {
    GstBuffer *output;
    output = this->output = gst_buffer_new_and_alloc (buffer_samples * sizeof(gfloat));
    out = (gfloat*)GST_BUFFER_DATA (output);
    for (i=samples_remaining; i; i--)
      *(out++) = INT2FLOAT (*(in++));
    this->output = output;
    this->output_samples_needed = buffer_samples - samples_remaining;
    samples_remaining = 0; /* just so we know */
  }
    
  gst_buffer_unref (buf);
  return;
}

/* this function is complicated now, but it will be unnecessary when we convert
 * rate. */
static GstCaps *
gst_audio_convert_getcaps (GstPad *pad)
{
  GstAudioConvert *this;
  GstPad *otherpad;
  GstStructure *structure;
  GstCaps *othercaps, *caps;
  const GstCaps *templcaps;
  gboolean has_float = FALSE, has_int = FALSE;
  int i;

  g_return_val_if_fail(GST_IS_PAD(pad), NULL);
  g_return_val_if_fail(GST_IS_AUDIO_CONVERT(GST_OBJECT_PARENT (pad)), NULL);
  this = GST_AUDIO_CONVERT(GST_OBJECT_PARENT (pad));

  otherpad = (pad == this->src) ? this->sink : this->src;

  /* all we want to find out is the rate */
  templcaps = gst_pad_get_pad_template_caps (pad);
  othercaps = gst_pad_get_allowed_caps (otherpad);

  for (i=0;i<gst_caps_get_size (othercaps); i++) {
    structure = gst_caps_get_structure (othercaps, i);
    gst_structure_remove_field (structure, "channels");
    gst_structure_remove_field (structure, "endianness");
    gst_structure_remove_field (structure, "width");
    if (strcmp (gst_structure_get_name (structure), "audio/x-raw-int") == 0) {
      if (!has_int) has_int = TRUE;
      gst_structure_remove_field (structure, "depth");
      gst_structure_remove_field (structure, "signed");
    } else {
      if (!has_float) has_float = TRUE;
      gst_structure_remove_field (structure, "buffer-frames");
    }
  }
  caps = gst_caps_intersect (othercaps, templcaps);
  gst_caps_free (othercaps);

  /* the intersection probably lost either float or int. so we take the rate
   * property and set it on a copy of the templcaps struct. */
  if (!has_int) {
    structure = gst_structure_copy (gst_caps_get_structure (templcaps, 0));
    gst_structure_set_value (structure, "rate",
                             gst_structure_get_value (gst_caps_get_structure (caps, 0),
                                                      "rate"));
    gst_caps_append_structure (caps, structure);
  }
  if (!has_float) {
    structure = gst_structure_copy (gst_caps_get_structure (templcaps, 1));
    gst_structure_set_value (structure, "rate",
                             gst_structure_get_value (gst_caps_get_structure (caps, 0),
                                                      "rate"));
    gst_caps_append_structure (caps, structure);
  }
  
  return caps;
}

static gboolean
gst_audio_convert_parse_caps (const GstCaps* gst_caps, GstAudioConvertCaps *caps)
{
  GstStructure *structure = gst_caps_get_structure (gst_caps, 0);
  g_return_val_if_fail (gst_caps_is_fixed (gst_caps), FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);
  
  caps->endianness = G_BYTE_ORDER;
  caps->is_int = (strcmp (gst_structure_get_name (structure), "audio/x-raw-int") == 0);
  if (!gst_structure_get_int (structure, "channels", &caps->channels) ||
      !gst_structure_get_int (structure, "width", &caps->width) ||
      !gst_structure_get_int (structure, "rate", &caps->rate) ||
      (caps->is_int &&
       (!gst_structure_get_boolean (structure, "signed", &caps->sign) ||
        !gst_structure_get_int (structure, "depth", &caps->depth) ||
        (caps->width != 8 &&
         !gst_structure_get_int (structure, "endianness", &caps->endianness)))) ||
      (!caps->is_int &&
       !gst_structure_get_int (structure, "buffer-frames", &caps->buffer_frames))) {
    GST_DEBUG ("could not get some values from structure");
    return FALSE;
  }
  return TRUE;
}

static GstPadLinkReturn
gst_audio_convert_link (GstPad *pad, const GstCaps *caps)
{
  GstAudioConvert *this;
  GstPad *otherpad;
  GstAudioConvertCaps ac_caps, other_ac_caps;
  GstCaps *othercaps;
  guint i;
  GstPadLinkReturn ret;

  g_return_val_if_fail(GST_IS_PAD(pad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail(GST_IS_AUDIO_CONVERT(GST_OBJECT_PARENT (pad)), GST_PAD_LINK_REFUSED);
  
  this = GST_AUDIO_CONVERT(GST_OBJECT_PARENT (pad));
  otherpad = (pad == this->src ? this->sink : this->src);

  /* negotiate sinkpad first */
  if (pad == this->src &&
      !gst_pad_is_negotiated (this->sink))
    return GST_PAD_LINK_DELAYED;
    
  if (!gst_audio_convert_parse_caps (caps, &ac_caps))
    return GST_PAD_LINK_REFUSED;
  
  /* try setting our caps on the other side first */
  if (gst_pad_try_set_caps (otherpad, caps) >= GST_PAD_LINK_OK) {
    this->srccaps = ac_caps;
    this->sinkcaps = ac_caps;
    return GST_PAD_LINK_OK;
  }
  
  /* ok, not those - try setting "any" caps */
  othercaps = gst_pad_get_allowed_caps (otherpad);
  for (i = 0; i < gst_caps_get_size (othercaps); i++) {
    GstStructure *structure = gst_caps_get_structure (othercaps, i);
    gst_structure_set (structure, "rate", G_TYPE_INT, ac_caps.rate, NULL);
  }
  ret = gst_pad_try_set_caps_nonfixed (otherpad, othercaps);
  gst_caps_free (othercaps);
  if (ret < GST_PAD_LINK_OK)
    return ret;
  if (!gst_audio_convert_parse_caps (caps, &other_ac_caps))
    return GST_PAD_LINK_REFUSED;
  
  /* woohoo, got it */
  if (!gst_audio_convert_parse_caps (gst_pad_get_negotiated_caps (otherpad),
                                     &other_ac_caps)) {
    g_critical ("internal negotiation error");
    return GST_PAD_LINK_REFUSED;
  }

  if (!other_ac_caps.is_int && !ac_caps.is_int) {
    GST_DEBUG ("we don't do float-float conversions yet");
    return GST_PAD_LINK_REFUSED;
  } else if ((this->sink == pad) ? !other_ac_caps.is_int : ac_caps.is_int) {
    GST_DEBUG ("int-float conversion, setting custom chain handler");
    gst_pad_set_chain_function (this->sink, gst_audio_convert_chain_int2float);
  }
  /* float2int conversion is handled like other int formats */

  if (this->sink == pad) {
    this->srccaps = other_ac_caps;
    this->sinkcaps = ac_caps;
  } else {
    this->srccaps = ac_caps;
    this->sinkcaps = other_ac_caps;
  }

  return GST_PAD_LINK_OK;
}

static GstElementStateReturn
gst_audio_convert_change_state (GstElement *element)
{
  GstAudioConvert *this = GST_AUDIO_CONVERT (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      this->convert_internal = NULL;
      GST_DEBUG ("resetting chain function to the default");
      gst_pad_set_chain_function (this->sink, gst_audio_convert_chain);
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
  GST_LOG ("new buffer of size %u requested. Current is: data: %p - size: %u - maxsize: %u\n", 
      size, buf->data, buf->size, buf->maxsize);
  if (buf->maxsize >= size && gst_buffer_is_writable (buf)) {
    gst_buffer_ref (buf);
    buf->size = size;
    GST_LOG ("returning same buffer with adjusted values. data: %p - size: %u - maxsize: %u\n", 
	buf->data, buf->size, buf->maxsize);
    return buf;
  } else {
    ret = gst_buffer_new_and_alloc (size);
    g_assert (ret);
    gst_buffer_stamp (ret, buf);
    GST_LOG ("returning new buffer. data: %p - size: %u - maxsize: %u\n", 
	ret->data, ret->size, ret->maxsize);
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

  if (this->sinkcaps.is_int) {
    if (this->sinkcaps.width == 32 && this->sinkcaps.depth == 32 &&
        this->sinkcaps.endianness == G_BYTE_ORDER && this->sinkcaps.sign == TRUE)
      return buf;

    ret = gst_audio_convert_get_buffer (buf, buf->size * 32 / this->sinkcaps.width);

    count = ret->size / 4;
    src = buf->data + (count - 1) * (this->sinkcaps.width / 8);
    dest = (gint32 *) ret->data;
    for (i = count - 1; i >= 0; i--) {
      switch (this->sinkcaps.width) {
      case 8:
        if (this->sinkcaps.sign) {
          CONVERT_TO (cur, src, gint8, this->sinkcaps.sign, this->sinkcaps.endianness, GINT8_IDENTITY, GINT8_IDENTITY);
        } else {
          CONVERT_TO (cur, src, guint16, this->sinkcaps.sign, this->sinkcaps.endianness, GUINT8_IDENTITY, GUINT8_IDENTITY);
        }
        break;
      case 16:
        if (this->sinkcaps.sign) {
          CONVERT_TO (cur, src, gint16, this->sinkcaps.sign, this->sinkcaps.endianness, GINT16_FROM_LE, GINT16_FROM_BE);
        } else {
          CONVERT_TO (cur, src, guint16, this->sinkcaps.sign, this->sinkcaps.endianness, GUINT16_FROM_LE, GUINT16_FROM_BE);
        }
        break;
      case 32:
        if (this->sinkcaps.sign) {
          CONVERT_TO (cur, src, gint32, this->sinkcaps.sign, this->sinkcaps.endianness, GINT32_FROM_LE, GINT32_FROM_BE);
        } else {
          CONVERT_TO (cur, src, guint32, this->sinkcaps.sign, this->sinkcaps.endianness, GUINT32_FROM_LE, GUINT32_FROM_BE);
        }
        break;
      default:
        g_assert_not_reached ();
      }
      cur = cur * ((gint64) 1 << (32 - this->sinkcaps.depth));
      cur = CLAMP (cur, -((gint64)1 << 32), (gint64) 0x7FFFFFFF);
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

    in = (gfloat*)GST_BUFFER_DATA (buf);
    out = (gint32*)GST_BUFFER_DATA (ret);
    /* increment `in' via the for, cause CLAMP duplicates the first arg */
    for (i = buf->size / sizeof(float); i; i--, in++)
      *(out++) = (gint32) gst_cast_float(CLAMP (*in, -1.f, 1.f) * 2147483647.0F);
  }

  gst_buffer_unref (buf);
  return ret;
}

#define POPULATE(format, be_func, le_func) G_STMT_START{			\
  format val;									\
  format* p = (format *) dest;							\
  int_value >>= (32 - this->srccaps.depth);					\
  val = (format) int_value;							\
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
gst_audio_convert_buffer_from_default_format (GstAudioConvert *this, GstBuffer *buf)
{
  GstBuffer *ret;
  guint8 *dest;
  guint count, i;
  gint32 *src;

  if (this->srccaps.width == 32 && this->srccaps.depth == 32 &&
      this->srccaps.endianness == G_BYTE_ORDER && this->srccaps.sign == TRUE)
    return buf;

  count = buf->size / 4; /* size is undefined after gst_audio_convert_get_buffer! */
  ret = gst_audio_convert_get_buffer (buf, buf->size * this->srccaps.width / 32);

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

  gst_buffer_unref(buf);
  return ret;
}

static GstBuffer *
gst_audio_convert_channels (GstAudioConvert *this, GstBuffer *buf)
{
  GstBuffer *ret;
  gint i, count;
  guint32 *src, *dest;

  if (this->sinkcaps.channels == this->srccaps.channels)
    return buf;

  count = GST_BUFFER_SIZE (buf) / 4 / this->sinkcaps.channels;
  ret = gst_audio_convert_get_buffer (buf, count * 4 * this->srccaps.channels);
  src = (guint32 *) GST_BUFFER_DATA (buf);
  dest = (guint32 *) GST_BUFFER_DATA (ret);

  if (this->sinkcaps.channels > this->srccaps.channels) {
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
  if (!gst_element_register (plugin, "audioconvert", GST_RANK_PRIMARY, GST_TYPE_AUDIO_CONVERT))
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
