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

#include <gst/gst.h>
#include <string.h>
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

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

#define GST_TYPE_AUDIO_CONVERT		(gst_audio_convert_get_type())
#define GST_AUDIO_CONVERT(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_CONVERT,GstAudioConvert))
#define GST_AUDIO_CONVERT_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_CONVERT,GstAudioConvert))
#define GST_IS_AUDIO_CONVERT(obj)	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_CONVERT))
#define GST_IS_AUDIO_CONVERT_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_CONVERT))

typedef struct _GstAudioConvert GstAudioConvert;
typedef struct _GstAudioConvertClass GstAudioConvertClass;

struct _GstAudioConvert {
  GstElement	element;
  /* pads */
  GstPad *	sink;
  GstPad *	src;
  /* properties */
  gboolean	aggressive;
  /* caps: 0 = sink, 1 = src, so always convert from 0 to 1 */
  gboolean	caps_set[2];
  gint		law[2];
  gint		endian[2];
  gint		sign[2];
  gint		depth[2];	/* in BITS */
  gint		width[2];	/* in BYTES */
  gint		rate[2];
  gint		channels[2];
};

struct _GstAudioConvertClass {
  GstElementClass parent_class;
};

/* type functions */
static GType		gst_audio_convert_get_type		(void);
static void		gst_audio_convert_class_init		(GstAudioConvertClass *klass);
static void		gst_audio_convert_init			(GstAudioConvert *audio_convert);
static void		gst_audio_convert_set_property		(GObject *object, 
								 guint prop_id,
								 const GValue *value,
								 GParamSpec *pspec);
static void		gst_audio_convert_get_property		(GObject *object,
								 guint prop_id,
								 GValue *value,
								 GParamSpec *pspec);
/* gstreamer functions */	
static void		gst_audio_convert_chain			(GstPad *pad,
								 GstBuffer *buf);
static GstPadLinkReturn	gst_audio_convert_link			(GstPad *pad,
								 GstCaps *caps);
static GstElementStateReturn gst_audio_convert_change_state	(GstElement *element);
/* actual work */
static gboolean		gst_audio_convert_set_caps		(GstPad *pad);
static GstBuffer *	gst_audio_convert_buffer_to_default_format (GstAudioConvert *this, 
								 GstBuffer *buf);
static GstBuffer *	gst_audio_convert_buffer_from_default_format (GstAudioConvert *this, 
								 GstBuffer *buf);
static GstBuffer *	gst_audio_convert_channels		(GstAudioConvert *this, 
								 GstBuffer *buf);



static GstElementClass *parent_class = NULL;
/*static guint gst_audio_convert_signals[LAST_SIGNAL] = { 0 }; */

/* AudioConvert signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};
enum {
  ARG_0,
  ARG_AGGRESSIVE,
};

/*** GSTREAMER PROTOTYPES *****************************************************/

GST_PAD_TEMPLATE_FACTORY (audio_convert_src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "audio_convert_src",
    "audio/raw",
      "format",		GST_PROPS_STRING ("int"),
      "law",		GST_PROPS_INT (0),
      "endianness",	GST_PROPS_LIST (
				GST_PROPS_INT (G_LITTLE_ENDIAN),
				GST_PROPS_INT (G_BIG_ENDIAN)
			),
      "signed",		GST_PROPS_LIST (
				GST_PROPS_BOOLEAN (FALSE),
				GST_PROPS_BOOLEAN (TRUE)
			),
      "depth",		GST_PROPS_INT_RANGE (1, 32),
      "width",		GST_PROPS_LIST (
				GST_PROPS_INT (8),
				GST_PROPS_INT (16),
				GST_PROPS_INT (24),
				GST_PROPS_INT (32)
			),
      "rate",		GST_PROPS_INT_RANGE (8000, 192000),
      "channels",	GST_PROPS_INT_RANGE (1, 2)
  )
)
GST_PAD_TEMPLATE_FACTORY (audio_convert_sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "audio_convert_sink",
    "audio/raw",
      "format",		GST_PROPS_STRING ("int"),
      "law",		GST_PROPS_INT (0),
      "endianness",	GST_PROPS_LIST (
				GST_PROPS_INT (G_LITTLE_ENDIAN),
				GST_PROPS_INT (G_BIG_ENDIAN)
			),
      "signed",		GST_PROPS_LIST (
				GST_PROPS_BOOLEAN (FALSE),
				GST_PROPS_BOOLEAN (TRUE)
			),
      "depth",		GST_PROPS_INT_RANGE (1, 32),
      "width",		GST_PROPS_LIST (
				GST_PROPS_INT (8),
				GST_PROPS_INT (16),
				GST_PROPS_INT (24),
				GST_PROPS_INT (32)
			),
      "rate",		GST_PROPS_INT_RANGE (8000, 192000),
      "channels",	GST_PROPS_INT_RANGE (1, 2)
  )
)


/*** TYPE FUNCTIONS ***********************************************************/

GType
gst_audio_convert_get_type(void) {
  static GType audio_convert_type = 0;

  if (!audio_convert_type) {
    static const GTypeInfo audio_convert_info = {
      sizeof(GstAudioConvertClass),      NULL,
      NULL,
      (GClassInitFunc)gst_audio_convert_class_init,
      NULL,
      NULL,
      sizeof(GstAudioConvert),
      0,
      (GInstanceInitFunc)gst_audio_convert_init,
    };
    audio_convert_type = g_type_register_static(GST_TYPE_ELEMENT, "GstAudioConvert", &audio_convert_info, 0);
  }
  return audio_convert_type;
}

static void
gst_audio_convert_class_init (GstAudioConvertClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_AGGRESSIVE,
    g_param_spec_boolean("aggressive","aggressive mode","if true, tries any possible format before giving up",
                         FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  gobject_class->set_property = gst_audio_convert_set_property;
  gobject_class->get_property = gst_audio_convert_get_property;

  gstelement_class->change_state = gst_audio_convert_change_state;
}
static void
gst_audio_convert_init (GstAudioConvert *this)
{
  /* sinkpad */
  this->sink = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (
               audio_convert_sink_template_factory), "sink");
  gst_pad_set_link_function (this->sink, gst_audio_convert_link);
  gst_element_add_pad (GST_ELEMENT(this), this->sink);
  /* srcpad */
  this->src = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (
              audio_convert_src_template_factory), "src");
  gst_pad_set_link_function (this->src, gst_audio_convert_link);
  gst_element_add_pad (GST_ELEMENT(this), this->src);

  gst_pad_set_chain_function(this->sink, gst_audio_convert_chain);
  gst_pad_set_chain_function(this->sink, gst_audio_convert_chain);
  /* clear important variables */
  this->caps_set[0] = this->caps_set[1] = FALSE;
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
gst_audio_convert_chain (GstPad *pad, GstBuffer *buf)
{
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

  if (!this->caps_set[1]) {
    if (!gst_audio_convert_set_caps (this->src)) {
      gst_element_error (GST_ELEMENT (this), "AudioConvert: could not set caps on pad %s", 
                         GST_PAD_NAME(this->src));
      return;
    }
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

  gst_pad_push (this->src, buf);
}
static GstPadLinkReturn
gst_audio_convert_link (GstPad *pad, GstCaps *caps)
{
  GstAudioConvert *this;
  gint nr = 0;
  gint rate, endianness, depth, width, channels;
  gboolean sign;

  g_return_val_if_fail(GST_IS_PAD(pad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail(GST_IS_AUDIO_CONVERT(GST_OBJECT_PARENT (pad)), GST_PAD_LINK_REFUSED);
  this = GST_AUDIO_CONVERT(GST_OBJECT_PARENT (pad));
  
  /* could we do better? */
  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  if (pad == this->sink) {
   nr = 0;
  } else if (pad == this->src) {
   nr = 1;
  } else {
   g_assert_not_reached ();
  }

  if (!gst_caps_get (caps, "rate", &rate,
                           "channels", &channels,
  			   "signed", &sign,
  			   "depth", &depth,
  			   "width", &width,
                           NULL
     ))
    return GST_PAD_LINK_DELAYED;
  if (!gst_caps_get_int (caps, "endianness", &endianness)) {
    if (width == 1) {
      endianness = G_BYTE_ORDER;  
    } else {
      return GST_PAD_LINK_DELAYED;
    }
  }
  /* we cannot yet convert this, so check */
  if (this->caps_set[1 - nr]) {
    if (rate != this->rate[1 - nr])
      return GST_PAD_LINK_REFUSED;
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

static GstCaps*
make_caps (gint endianness, gboolean sign, gint depth, gint width, gint rate, gint channels)
{
  if (width == 1) {
    return GST_CAPS_NEW (
      "audio_convert_caps",
      "audio/raw",
        "format",  	GST_PROPS_STRING ("int"),
        "law",		GST_PROPS_INT (0),
        "signed",	GST_PROPS_BOOLEAN (sign),
        "depth",	GST_PROPS_INT (depth),
        "width",	GST_PROPS_INT (width * 8),
        "rate",		GST_PROPS_INT (rate),
        "channels",	GST_PROPS_INT (channels)
    );  
  } else {
    return GST_CAPS_NEW (
      "audio_convert_caps",
      "audio/raw",
        "format",  	GST_PROPS_STRING ("int"),
        "law",		GST_PROPS_INT (0),
        "endianness",	GST_PROPS_INT (endianness),
        "signed",	GST_PROPS_BOOLEAN (sign),
        "depth",	GST_PROPS_INT (depth),
        "width",	GST_PROPS_INT (width * 8),
        "rate",		GST_PROPS_INT (rate),
        "channels",	GST_PROPS_INT (channels)
    );  
  }
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
  nr = this->src == pad ? 1 : this->sink == pad ? 0 : -1;
  g_assert (nr > -1);

  /* try 1:1 first */
  caps = make_caps (this->endian[1 - nr], this->sign[1 - nr], this->depth[1 - nr], 
                    this->width[1 - nr], this->rate[1 - nr], this->channels[1 - nr]);
  ret = gst_pad_try_set_caps (pad, caps);
  if (ret == GST_PAD_LINK_DONE || ret == GST_PAD_LINK_OK)
    goto success;
 
  /* now do some iterating, this is gonna be fun */
  /* stereo is most important */
  channels = 2;
  while (channels > 0) {
  
    /* endianness comes second */
    endianness = 0;
    do {
      if (endianness == G_BIG_ENDIAN)
	break;
      endianness = endianness == 0 ? G_LITTLE_ENDIAN : G_BIG_ENDIAN;
      
      /* signedness */
      sign = TRUE;
      do {
	sign = !sign;

      	/* depth */
        for (width = 4; width >= 1; width--) { 
	
	  /* width */
	  for (depth = width * 8; depth >= 1; depth -= this->aggressive ? 1 : 8) {

	    /* rate - not supported yet*/

	    caps = make_caps (endianness, sign, depth, 
                              width, this->rate[1 - nr], channels);
            ret = gst_pad_try_set_caps (pad, caps);
            if (ret == GST_PAD_LINK_DONE || ret == GST_PAD_LINK_OK) {
              goto success;
            }
    
    	  }

	}

      } while (sign != TRUE);

    } while TRUE;
  
    channels--;
  }
  
  
  goto fail;  
fail:
  return FALSE;

success:
  g_assert (gst_audio_convert_link (pad, caps) == GST_PAD_LINK_OK);
  return TRUE;  
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
      case 3:
	if (this->sign[0]) {
	  gint32 value;
	  if (this->endian[0] == G_BIG_ENDIAN) {
	    gpointer p = &value;
	    p++;
	    memcpy (p, src, 3);	  
	    value = GINT32_FROM_BE (value);
	  } else if (this->endian[0] == G_LITTLE_ENDIAN) {
	    memcpy (&value, src, 3);
	    value = GINT32_FROM_LE (value);
	  } else {
	    g_assert_not_reached();
	  }
	  cur = value;
	} else {
	  guint32 value;
	  if (this->endian[0] == G_BIG_ENDIAN) {
	    gpointer p = &value;
	    p++;
	    memcpy (p, src, 3);	  
	    value = GUINT32_FROM_BE (value);
	  } else if (this->endian[0] == G_LITTLE_ENDIAN) {
	    memcpy (&value, src, 3);
	    value = GUINT32_FROM_LE (value);
	  } else {
	    g_assert_not_reached();
	  }
	  cur = (gint64) value - (1 << 23);
	}
        src -= 3;
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
#define POPULATE(format, be_func, le_func) G_STMT_START{			\
  format val;									\
  format* p = (format *) dest;							\
  int_value >>= (32 - this->depth[1]);						\
  val = (format) int_value;							\
  switch (this->endian[1]) {							\
    case G_LITTLE_ENDIAN:							\
      val = le_func (val);							\
      break;									\
    case G_BIG_ENDIAN:								\
      val = be_func (val);							\
      break;									\
    default:									\
      g_assert_not_reached ();							\
  };										\
  *p = val;									\
  p ++;										\
  dest = (guint8 *) p;								\
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
      case 3:
        if (this->sign[1]) {
	  gpointer p;
          gint32 val = (gint32) int_value;
          switch (this->endian[1]) {
            case G_LITTLE_ENDIAN:
              val = GINT32_TO_LE (val);
              break;
            case G_BIG_ENDIAN:
              val = GINT32_TO_BE (val);
              break;
            default:
              g_assert_not_reached ();
          };
	  p = &val;
	  if (this->endian[1] == G_BIG_ENDIAN)
	    p++;
          memcpy (dest, p, 3);
	  dest += 3;
	} else {
	  gpointer p;
          guint32 val = (guint32) int_value;
          switch (this->endian[1]) {
            case G_LITTLE_ENDIAN:
              val = GUINT32_TO_LE (val);
              break;
            case G_BIG_ENDIAN:
              val = GUINT32_TO_BE (val);
              break;
            default:
              g_assert_not_reached ();
          };
	  p = &val;
	  if (this->endian[1] == G_BIG_ENDIAN)
	    p++;
          memcpy (dest, p, 3);
	  dest += 3;
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
  guint i, count;
  guint32 *src, *dest;

  if (this->channels[0] == this->channels[1])
    return buf;

  ret = gst_audio_convert_get_buffer (buf, buf->size / this->channels[0] * this->channels[1]);
  count = ret->size / 4 / this->channels[1];
  src = (guint32 *) buf->data;
  dest = (guint32 *) ret->data;
  
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

static GstElementDetails audio_convert_details = {
  "Audio Conversion",
  "Filter/Convert",
  "LGPL",
  "Convert audio to different formats",
  VERSION,
  "Benjamin Otte <in7y118@public.uni-hamburg.de",
  "(C) 2003",
};

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new("audioconvert", GST_TYPE_AUDIO_CONVERT,
                                    &audio_convert_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, 
		  GST_PAD_TEMPLATE_GET (audio_convert_src_template_factory));
  gst_element_factory_add_pad_template (factory, 
		  GST_PAD_TEMPLATE_GET (audio_convert_sink_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstaudioconvert",
  plugin_init
};
