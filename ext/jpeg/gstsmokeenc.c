/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include "config.h"
#endif
#include <string.h>

#include "gstsmokeenc.h"
#include <gst/video/video.h>

/* elementfactory information */
GstElementDetails gst_smokeenc_details = {
  "Smoke image encoder",
  "Codec/Encoder/Image",
  "Encode images in the Smoke format",
  "Wim Taymans <wim@fluendo.com>",
};

GST_DEBUG_CATEGORY (smokeenc_debug);
#define GST_CAT_DEFAULT smokeenc_debug

#define SMOKEENC_DEFAULT_MIN_QUALITY 10
#define SMOKEENC_DEFAULT_MAX_QUALITY 85
#define SMOKEENC_DEFAULT_THRESHOLD 3000
#define SMOKEENC_DEFAULT_KEYFRAME 20

/* SmokeEnc signals and args */
enum
{
  FRAME_ENCODED,
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_MIN_QUALITY,
  ARG_MAX_QUALITY,
  ARG_THRESHOLD,
  ARG_KEYFRAME
      /* FILL ME */
};

static void gst_smokeenc_base_init (gpointer g_class);
static void gst_smokeenc_class_init (GstSmokeEnc * klass);
static void gst_smokeenc_init (GstSmokeEnc * smokeenc);

static void gst_smokeenc_chain (GstPad * pad, GstData * _data);
static GstPadLinkReturn gst_smokeenc_link (GstPad * pad, const GstCaps * caps);
static GstCaps *gst_smokeenc_getcaps (GstPad * pad);

static void gst_smokeenc_resync (GstSmokeEnc * smokeenc);
static void gst_smokeenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_smokeenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

//static guint gst_smokeenc_signals[LAST_SIGNAL] = { 0 };

GType
gst_smokeenc_get_type (void)
{
  static GType smokeenc_type = 0;

  if (!smokeenc_type) {
    static const GTypeInfo smokeenc_info = {
      sizeof (GstSmokeEncClass),
      gst_smokeenc_base_init,
      NULL,
      (GClassInitFunc) gst_smokeenc_class_init,
      NULL,
      NULL,
      sizeof (GstSmokeEnc),
      0,
      (GInstanceInitFunc) gst_smokeenc_init,
    };

    smokeenc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstSmokeEnc", &smokeenc_info,
        0);
  }
  return smokeenc_type;
}

static GstStaticPadTemplate gst_smokeenc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_smokeenc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-smoke, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (double) [ 1, MAX ]")
    );

static void
gst_smokeenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_smokeenc_sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_smokeenc_src_pad_template));
  gst_element_class_set_details (element_class, &gst_smokeenc_details);
}

static void
gst_smokeenc_class_init (GstSmokeEnc * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (gobject_class, ARG_MIN_QUALITY,
      g_param_spec_int ("qmin", "Qmin", "Minimum quality",
          0, 100, SMOKEENC_DEFAULT_MIN_QUALITY, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MAX_QUALITY,
      g_param_spec_int ("qmax", "Qmax", "Maximum quality",
          0, 100, SMOKEENC_DEFAULT_MAX_QUALITY, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_THRESHOLD,
      g_param_spec_int ("threshold", "Threshold", "Motion estimation threshold",
          0, 100000000, SMOKEENC_DEFAULT_THRESHOLD, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_KEYFRAME,
      g_param_spec_int ("keyframe", "Keyframe",
          "Insert keyframe every N frames", 1, 100000,
          SMOKEENC_DEFAULT_KEYFRAME, G_PARAM_READWRITE));

  gobject_class->set_property = gst_smokeenc_set_property;
  gobject_class->get_property = gst_smokeenc_get_property;

  GST_DEBUG_CATEGORY_INIT (smokeenc_debug, "smokeenc", 0,
      "Smoke encoding element");
}

static void
gst_smokeenc_init (GstSmokeEnc * smokeenc)
{
  /* create the sink and src pads */
  smokeenc->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_smokeenc_sink_pad_template), "sink");
  gst_pad_set_chain_function (smokeenc->sinkpad, gst_smokeenc_chain);
  gst_pad_set_getcaps_function (smokeenc->sinkpad, gst_smokeenc_getcaps);
  gst_pad_set_link_function (smokeenc->sinkpad, gst_smokeenc_link);
  gst_element_add_pad (GST_ELEMENT (smokeenc), smokeenc->sinkpad);

  smokeenc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_smokeenc_src_pad_template), "src");
  gst_pad_set_getcaps_function (smokeenc->sinkpad, gst_smokeenc_getcaps);
  gst_pad_set_link_function (smokeenc->sinkpad, gst_smokeenc_link);
  gst_element_add_pad (GST_ELEMENT (smokeenc), smokeenc->srcpad);

  /* reset the initial video state */
  smokeenc->width = 0;
  smokeenc->height = 0;
  smokeenc->frame = 0;

  gst_smokeenc_resync (smokeenc);

  smokeenc->min_quality = SMOKEENC_DEFAULT_MIN_QUALITY;
  smokeenc->max_quality = SMOKEENC_DEFAULT_MAX_QUALITY;
  smokeenc->threshold = SMOKEENC_DEFAULT_THRESHOLD;
  smokeenc->keyframe = SMOKEENC_DEFAULT_KEYFRAME;
}

static GstCaps *
gst_smokeenc_getcaps (GstPad * pad)
{
  GstSmokeEnc *smokeenc = GST_SMOKEENC (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstCaps *caps;
  const char *name;
  int i;
  GstStructure *structure = NULL;

  /* we want to proxy properties like width, height and framerate from the
     other end of the element */
  otherpad = (pad == smokeenc->srcpad) ? smokeenc->sinkpad : smokeenc->srcpad;
  caps = gst_pad_get_allowed_caps (otherpad);
  if (pad == smokeenc->srcpad) {
    name = "image/x-smoke";
  } else {
    name = "video/x-raw-yuv";
  }
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    gst_structure_set_name (structure, name);
    gst_structure_remove_field (structure, "format");
    /* ... but for the sink pad, we only do I420 anyway, so add that */
    if (pad == smokeenc->sinkpad) {
      gst_structure_set (structure, "format", GST_TYPE_FOURCC,
          GST_STR_FOURCC ("I420"), NULL);
    }
  }

  return caps;
}

static GstPadLinkReturn
gst_smokeenc_link (GstPad * pad, const GstCaps * caps)
{
  GstSmokeEnc *smokeenc = GST_SMOKEENC (gst_pad_get_parent (pad));
  GstStructure *structure;
  GstPadLinkReturn ret;
  GstCaps *othercaps;
  GstPad *otherpad;

  otherpad = (pad == smokeenc->srcpad) ? smokeenc->sinkpad : smokeenc->srcpad;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_double (structure, "framerate", &smokeenc->fps);
  gst_structure_get_int (structure, "width", &smokeenc->width);
  gst_structure_get_int (structure, "height", &smokeenc->height);

  othercaps = gst_caps_copy (gst_pad_get_pad_template_caps (otherpad));
  gst_caps_set_simple (othercaps,
      "width", G_TYPE_INT, smokeenc->width,
      "height", G_TYPE_INT, smokeenc->height,
      "framerate", G_TYPE_DOUBLE, smokeenc->fps, NULL);

  ret = gst_pad_try_set_caps (smokeenc->srcpad, othercaps);
  gst_caps_free (othercaps);

  if (GST_PAD_LINK_SUCCESSFUL (ret)) {
    gst_smokeenc_resync (smokeenc);
  }

  return ret;
}

static void
gst_smokeenc_resync (GstSmokeEnc * smokeenc)
{
  GST_DEBUG ("gst_smokeenc_resync: resync");

  smokecodec_encode_new (&smokeenc->info, smokeenc->width, smokeenc->height);
  smokecodec_set_quality (smokeenc->info, smokeenc->min_quality,
      smokeenc->max_quality);

  GST_DEBUG ("gst_smokeenc_resync: resync done");
}

static void
gst_smokeenc_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstSmokeEnc *smokeenc;
  guchar *data, *outdata;
  gulong size;
  gint outsize, encsize;
  GstBuffer *outbuf;
  SmokeCodecFlags flags;

  smokeenc = GST_SMOKEENC (GST_OBJECT_PARENT (pad));

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_DEBUG ("gst_smokeenc_chain: got buffer of %ld bytes in '%s'", size,
      GST_OBJECT_NAME (smokeenc));

  outbuf = gst_buffer_new ();
  outsize = smokeenc->width * smokeenc->height * 3;
  outdata = GST_BUFFER_DATA (outbuf) = g_malloc (outsize);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);

  flags = 0;
  if (smokeenc->frame == 0) {
    flags |= SMOKECODEC_KEYFRAME;
  }

  smokeenc->frame = (smokeenc->frame + 1) % smokeenc->keyframe;

  smokecodec_set_quality (smokeenc->info, smokeenc->min_quality,
      smokeenc->max_quality);
  smokecodec_set_threshold (smokeenc->info, smokeenc->threshold);
  smokecodec_encode (smokeenc->info, data, flags, outdata, &encsize);
  gst_buffer_unref (buf);

  GST_BUFFER_SIZE (outbuf) = encsize;
  //memset(GST_BUFFER_DATA(outbuf)+encsize, 0, outsize - encsize); 

  gst_pad_push (smokeenc->srcpad, GST_DATA (outbuf));
}

static void
gst_smokeenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSmokeEnc *smokeenc;

  g_return_if_fail (GST_IS_SMOKEENC (object));
  smokeenc = GST_SMOKEENC (object);

  switch (prop_id) {
    case ARG_MIN_QUALITY:
      smokeenc->min_quality = g_value_get_int (value);
      break;
    case ARG_MAX_QUALITY:
      smokeenc->max_quality = g_value_get_int (value);
      break;
    case ARG_THRESHOLD:
      smokeenc->threshold = g_value_get_int (value);
      break;
    case ARG_KEYFRAME:
      smokeenc->keyframe = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_smokeenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSmokeEnc *smokeenc;

  g_return_if_fail (GST_IS_SMOKEENC (object));
  smokeenc = GST_SMOKEENC (object);

  switch (prop_id) {
    case ARG_MIN_QUALITY:
      g_value_set_int (value, smokeenc->min_quality);
      break;
    case ARG_MAX_QUALITY:
      g_value_set_int (value, smokeenc->max_quality);
      break;
    case ARG_THRESHOLD:
      g_value_set_int (value, smokeenc->threshold);
      break;
    case ARG_KEYFRAME:
      g_value_set_int (value, smokeenc->keyframe);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
