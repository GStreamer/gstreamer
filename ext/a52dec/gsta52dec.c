/* GStreamer
 * Copyright (C) <2001> David I. Lehn <dlehn@users.sourceforge.net>
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

#include <string.h>

#include <stdlib.h>
#include <stdint.h>
#include <gst/gst.h>
#include <a52dec/a52.h>
#include <a52dec/mm_accel.h>
#include "gsta52dec.h"

/* elementfactory information */
static GstElementDetails gst_a52dec_details = {
  "ATSC A/52 audio decoder",
  "Codec/Audio/Decoder",
  "Decodes ATSC A/52 encoded audio streams",
  VERSION,
  "David I. Lehn <dlehn@users.sourceforge.net>",
  "(C) 2001",
};


/* A52Dec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DRC
};

/*
 * "audio/a52" and "audio/ac3" are the same format.  The name
 * "ac3" is now deprecated and should not be used in new code.
 */
GST_PAD_TEMPLATE_FACTORY (sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "a52dec_sink",
    "audio/a52",
     NULL
  ), 
  GST_CAPS_NEW (
    "ac3dec_sink", 
    "audio/ac3", 
    NULL
  )
);

GST_PAD_TEMPLATE_FACTORY (src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "a52dec_src",
      "audio/raw",
       "format", 	GST_PROPS_STRING ("int"),
       "law", 		GST_PROPS_INT (0),
       "endianness", 	GST_PROPS_INT (G_BYTE_ORDER),
       "signed", 	GST_PROPS_BOOLEAN (TRUE),
       "width",	 	GST_PROPS_INT (16),
       "depth", 	GST_PROPS_INT (16),
       "rate", 		GST_PROPS_INT_RANGE (4000, 48000),
       "channels", 	GST_PROPS_INT_RANGE (1, 6)
  )
);

static void 		gst_a52dec_class_init 	(GstA52DecClass * klass);
static void 		gst_a52dec_init 	(GstA52Dec * a52dec);

static void 		gst_a52dec_loop 	(GstElement * element);
static GstElementStateReturn 
			gst_a52dec_change_state (GstElement * element);

static void 		gst_a52dec_set_property (GObject * object, guint prop_id,
				    		 const GValue * value, GParamSpec * pspec);
static void 		gst_a52dec_get_property (GObject * object, guint prop_id,
				    		 GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;
/* static guint gst_a52dec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_a52dec_get_type (void)
{
  static GType a52dec_type = 0;

  if (!a52dec_type) {
    static const GTypeInfo a52dec_info = {
      sizeof (GstA52DecClass), NULL, NULL, (GClassInitFunc) gst_a52dec_class_init,
      NULL,
      NULL,
      sizeof (GstA52Dec),
      0,
      (GInstanceInitFunc) gst_a52dec_init,
    };

    a52dec_type = g_type_register_static (GST_TYPE_ELEMENT, "GstA52Dec", &a52dec_info, 0);
  }
  return a52dec_type;
}

static void
gst_a52dec_class_init (GstA52DecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DRC,
    g_param_spec_boolean ("drc", "Dynamic Range Compression",
			  "Use Dynamic Range Compression", FALSE,
			  G_PARAM_READWRITE));

  gobject_class->set_property = gst_a52dec_set_property;
  gobject_class->get_property = gst_a52dec_get_property;

  gstelement_class->change_state = gst_a52dec_change_state;
}

static void
gst_a52dec_init (GstA52Dec * a52dec)
{
  /* create the sink and src pads */
  a52dec->sinkpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (a52dec), a52dec->sinkpad);
  gst_element_set_loop_function ((GstElement *) a52dec, gst_a52dec_loop);

  a52dec->srcpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (a52dec), a52dec->srcpad);

  a52dec->dynamic_range_compression = FALSE;
}

/* BEGIN modified a52dec conversion code */

static inline int16_t
convert (int32_t i)
{
  if (i > 0x43c07fff)
    return 32767;
  else if (i < 0x43bf8000)
    return -32768;
  else
    return i - 0x43c00000;
}

static inline void
float_to_int (float *_f, int16_t * s16, int flags)
{
  int i;
  int32_t *f = (int32_t *) _f;

  switch (flags) {
    case A52_MONO:
      for (i = 0; i < 256; i++) {
	s16[5 * i] = s16[5 * i + 1] = s16[5 * i + 2] = s16[5 * i + 3] = 0;
	s16[5 * i + 4] = convert (f[i]);
      }
      break;
    case A52_CHANNEL:
    case A52_STEREO:
    case A52_DOLBY:
      for (i = 0; i < 256; i++) {
	s16[2 * i] = convert (f[i]);
	s16[2 * i + 1] = convert (f[i + 256]);
      }
      break;
    case A52_3F:
      for (i = 0; i < 256; i++) {
	s16[5 * i] = convert (f[i]);
	s16[5 * i + 1] = convert (f[i + 512]);
	s16[5 * i + 2] = s16[5 * i + 3] = 0;
	s16[5 * i + 4] = convert (f[i + 256]);
      }
      break;
    case A52_2F2R:
      for (i = 0; i < 256; i++) {
	s16[4 * i] = convert (f[i]);
	s16[4 * i + 1] = convert (f[i + 256]);
	s16[4 * i + 2] = convert (f[i + 512]);
	s16[4 * i + 3] = convert (f[i + 768]);
      }
      break;
    case A52_3F2R:
      for (i = 0; i < 256; i++) {
	s16[5 * i] = convert (f[i]);
	s16[5 * i + 1] = convert (f[i + 512]);
	s16[5 * i + 2] = convert (f[i + 768]);
	s16[5 * i + 3] = convert (f[i + 1024]);
	s16[5 * i + 4] = convert (f[i + 256]);
      }
      break;
    case A52_MONO | A52_LFE:
      for (i = 0; i < 256; i++) {
	s16[6 * i] = s16[6 * i + 1] = s16[6 * i + 2] = s16[6 * i + 3] = 0;
	s16[6 * i + 4] = convert (f[i + 256]);
	s16[6 * i + 5] = convert (f[i]);
      }
      break;
    case A52_CHANNEL | A52_LFE:
    case A52_STEREO | A52_LFE:
    case A52_DOLBY | A52_LFE:
      for (i = 0; i < 256; i++) {
	s16[6 * i] = convert (f[i + 256]);
	s16[6 * i + 1] = convert (f[i + 512]);
	s16[6 * i + 2] = s16[6 * i + 3] = s16[6 * i + 4] = 0;
	s16[6 * i + 5] = convert (f[i]);
      }
      break;
    case A52_3F | A52_LFE:
      for (i = 0; i < 256; i++) {
	s16[6 * i] = convert (f[i + 256]);
	s16[6 * i + 1] = convert (f[i + 768]);
	s16[6 * i + 2] = s16[6 * i + 3] = 0;
	s16[6 * i + 4] = convert (f[i + 512]);
	s16[6 * i + 5] = convert (f[i]);
      }
      break;
    case A52_2F2R | A52_LFE:
      for (i = 0; i < 256; i++) {
	s16[6 * i] = convert (f[i + 256]);
	s16[6 * i + 1] = convert (f[i + 512]);
	s16[6 * i + 2] = convert (f[i + 768]);
	s16[6 * i + 3] = convert (f[i + 1024]);
	s16[6 * i + 4] = 0;
	s16[6 * i + 5] = convert (f[i]);
      }
      break;
    case A52_3F2R | A52_LFE:
      for (i = 0; i < 256; i++) {
	s16[6 * i] = convert (f[i + 256]);
	s16[6 * i + 1] = convert (f[i + 768]);
	s16[6 * i + 2] = convert (f[i + 1024]);
	s16[6 * i + 3] = convert (f[i + 1280]);
	s16[6 * i + 4] = convert (f[i + 512]);
	s16[6 * i + 5] = convert (f[i]);
      }
      break;
  }
}

static int
gst_a52dec_channels (int flags)
{
  int chans = 0;

  if (flags & A52_LFE) {
    chans += 1;
  }
  flags &= A52_CHANNEL_MASK;
  switch (flags) {
    case A52_3F2R:
      chans += 5;
      break;
    case A52_2F2R:
    case A52_3F1R:
      chans += 4;
      break;
    case A52_2F1R:
    case A52_3F:
      chans += 3;
    case A52_CHANNEL:
    case A52_STEREO:
    case A52_DOLBY:
      chans += 2;
      break;
    default:
      /* error */
      fprintf (stderr, "a52dec invalid flags?");
      return 0;
  }
  return chans;
}

static int
gst_a52dec_push (GstPad * srcpad, int flags, sample_t * _samples, gint64 timestamp)
{
  GstBuffer *buf;
  int chans;

#ifdef LIBA52_DOUBLE
  float samples[256 * 6];
  int i;

  for (i = 0; i < 256 * 6; i++)
    samples[i] = _samples[i];
#else
  float *samples = _samples;
#endif

  flags &= A52_CHANNEL_MASK | A52_LFE;

  chans = gst_a52dec_channels (flags);
  if (!chans) {
    return 1;
  }

  buf = gst_buffer_new ();
  GST_BUFFER_SIZE (buf) = sizeof (int16_t) * 256 * chans;
  GST_BUFFER_DATA (buf) = g_malloc (GST_BUFFER_SIZE (buf));
  GST_BUFFER_TIMESTAMP (buf) = timestamp;
  float_to_int (samples, (int16_t *) GST_BUFFER_DATA (buf), flags);

  gst_pad_push (srcpad, buf);

  return 0;
}

/* END modified a52dec conversion code */

static void
gst_a52dec_reneg (GstPad * pad, int channels, int rate)
{
  GST_INFO (GST_CAT_PLUGIN_INFO, "a52dec: reneg channels:%d rate:%d\n", channels, rate);

  gst_pad_try_set_caps (pad, 
		  GST_CAPS_NEW ("a52dec_src_caps",
			        "audio/raw",
				  "format", 	GST_PROPS_STRING ("int"),
				  "law", 	GST_PROPS_INT (0),
				  "endianness",	GST_PROPS_INT (G_BYTE_ORDER),
				  "signed", 	GST_PROPS_BOOLEAN (TRUE),
				  "width", 	GST_PROPS_INT (16),
				  "depth", 	GST_PROPS_INT (16),
				  "channels", 	GST_PROPS_INT (channels),
				  "rate", 	GST_PROPS_INT (rate))
		    );
}

static void
gst_a52dec_handle_event (GstA52Dec *a52dec)
{
  guint32 remaining;
  GstEvent *event;

  gst_bytestream_get_status (a52dec->bs, &remaining, &event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
      gst_bytestream_flush_fast (a52dec->bs, remaining);
    default:
      gst_pad_event_default (a52dec->sinkpad, event);
      break;
  }
}

static void
gst_a52dec_loop (GstElement *element)
{
  GstA52Dec *a52dec;
  guint8 *data;
  int i, length, flags, sample_rate, bit_rate;
  int stream_channels;
  GstBuffer *buf;
  guint8 got_bytes;

  a52dec = GST_A52DEC (element);

  /* find and read header */
  while (1) {
    got_bytes = gst_bytestream_peek_bytes (a52dec->bs, &data, 7);
    if (got_bytes < 7) {
      gst_a52dec_handle_event (a52dec);
      return;
    }
    length = a52_syncinfo (data, &flags, &sample_rate, &bit_rate);
    if (length == 0) {
      /* slide window to next 7 bytesa */
      gst_bytestream_flush_fast (a52dec->bs, 1);
    }
    else
      break;

    /* FIXME this can potentially be an infinite loop, we have to
     * insert a yield operations here */
  }

  /* check if params have changed since last call */
  stream_channels = flags & A52_CHANNEL_MASK;

  if ((sample_rate     != a52dec->sample_rate) ||
      (bit_rate        != a52dec->bit_rate) || 
      (stream_channels != a52dec->stream_channels)) 
  {
    a52dec->sample_rate = sample_rate;
    a52dec->bit_rate = bit_rate;
    a52dec->stream_channels = stream_channels;
    /* FIXME force stereo for now */
    a52dec->req_channels = A52_STEREO;
    gst_a52dec_reneg (a52dec->srcpad,
		      gst_a52dec_channels (a52dec->req_channels), a52dec->sample_rate);
  }

  /* read the header + rest of frame */
  got_bytes = gst_bytestream_read (a52dec->bs, &buf, length);
  if (got_bytes < length) {
    gst_a52dec_handle_event (a52dec);
    return;
  }
  data = GST_BUFFER_DATA (buf);

  /* process */
  flags = a52dec->req_channels | A52_ADJUST_LEVEL;
  a52dec->level = 1;

  if (a52_frame (a52dec->state, data, &flags, &a52dec->level, a52dec->bias)) {
    fprintf (stderr, "a52dec a52_frame error\n");
    goto end;
  }

  if ((flags & A52_CHANNEL_MASK) != a52dec->req_channels) {
    gst_a52dec_reneg (a52dec->srcpad, gst_a52dec_channels (flags), a52dec->sample_rate);
  }

  if (a52dec->dynamic_range_compression == FALSE) {
    a52_dynrng (a52dec->state, NULL, NULL);
  }

  for (i = 0; i < 6; i++) {
    if (a52_block (a52dec->state)) {
      g_warning ("a52dec a52_block error %d\n", i);
      continue;
    }
    /* push on */
    gst_a52dec_push (a52dec->srcpad, a52dec->req_channels, a52dec->samples, GST_BUFFER_TIMESTAMP (buf));
  }

end:
  /* FIXME, this is a possible leak */
  gst_buffer_unref (buf);
}

static GstElementStateReturn
gst_a52dec_change_state (GstElement * element)
{
  GstA52Dec *a52dec = GST_A52DEC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      a52dec->bs = gst_bytestream_new (a52dec->sinkpad);
      a52dec->state = a52_init (0);	/* mm_accel()); */
      a52dec->samples = a52_samples (a52dec->state);
      a52dec->bit_rate = -1;
      a52dec->sample_rate = -1;
      a52dec->stream_channels = -1;
      a52dec->req_channels = -1;
      a52dec->level = 1;
      a52dec->bias = 384;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (a52dec->bs);
      a52dec->bs = NULL;
      a52dec->samples = NULL;
      a52_free (a52dec->state);
      a52dec->state = NULL;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;

  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_a52dec_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstA52Dec *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_A52DEC (object));
  src = GST_A52DEC (object);

  switch (prop_id) {
    case ARG_DRC:
      src->dynamic_range_compression = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_a52dec_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstA52Dec *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_A52DEC (object));
  src = GST_A52DEC (object);

  switch (prop_id) {
    case ARG_DRC:
      g_value_set_boolean (value, src->dynamic_range_compression);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule * module, GstPlugin * plugin)
{
  GstElementFactory *factory;

  /* this filter needs the bytestream package */
  if (!gst_library_load ("gstbytestream")) {
    gst_info ("a52dec:: could not load support library: 'gstbytestream'\n");
    return FALSE;
  }

  /* create an elementfactory for the a52dec element */
  factory = gst_element_factory_new ("a52dec", GST_TYPE_A52DEC, &gst_a52dec_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "a52dec",
  plugin_init
};
