/* GStreamer divx encoder plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include "gstdivxenc.h"
#include <gst/video/video.h>
#include <encore2.h>

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YUY2, YV12, YVYU, UYVY }")
        /* FIXME: 15/16/24/32bpp RGB */
    )
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-divx, "
        "divxversion = (int) 5, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (fraction) [0/1, MAX]")
    );


/* DivxEnc signals and args */
enum
{
  FRAME_ENCODED,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_BITRATE,
  ARG_MAXKEYINTERVAL,
  ARG_BUFSIZE,
  ARG_QUALITY
};


static void gst_divxenc_class_init (GstDivxEncClass * klass);
static void gst_divxenc_base_init (GstDivxEncClass * klass);
static void gst_divxenc_init (GstDivxEnc * divxenc);
static void gst_divxenc_dispose (GObject * object);
static GstFlowReturn gst_divxenc_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_divxenc_setcaps (GstPad * pad, GstCaps * caps);

/* properties */
static void gst_divxenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_divxenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;
static guint gst_divxenc_signals[LAST_SIGNAL] = { 0 };


static const gchar *
gst_divxenc_error (int errorcode)
{
  const gchar *error;

  switch (errorcode) {
    case ENC_BUFFER:
      error = "Invalid buffer";
      break;
    case ENC_FAIL:
      error = "Operation failed";
      break;
    case ENC_OK:
      error = "No error";
      break;
    case ENC_MEMORY:
      error = "Bad memory location";
      break;
    case ENC_BAD_FORMAT:
      error = "Invalid format";
      break;
    case ENC_INTERNAL:
      error = "Internal error";
      break;
    default:
      error = "Unknown error";
      break;
  }

  return error;
}


GType
gst_divxenc_get_type (void)
{
  static GType divxenc_type = 0;

  if (!divxenc_type) {
    static const GTypeInfo divxenc_info = {
      sizeof (GstDivxEncClass),
      (GBaseInitFunc) gst_divxenc_base_init,
      NULL,
      (GClassInitFunc) gst_divxenc_class_init,
      NULL,
      NULL,
      sizeof (GstDivxEnc),
      0,
      (GInstanceInitFunc) gst_divxenc_init,
    };

    divxenc_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstDivxEnc", &divxenc_info, 0);
  }
  return divxenc_type;
}


static void
gst_divxenc_base_init (GstDivxEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_details_simple (element_class,
      "Divx4linux video encoder", "Codec/Encoder/Video",
      "Divx encoder based on divxencore",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
}


static void
gst_divxenc_class_init (GstDivxEncClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_divxenc_set_property;
  gobject_class->get_property = gst_divxenc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE,
      g_param_spec_ulong ("bitrate", "Bitrate",
          "Target video bitrate", 0, G_MAXULONG, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAXKEYINTERVAL,
      g_param_spec_int ("max-key-interval", "Max. Key Interval",
          "Maximum number of frames between two keyframes",
          -1, G_MAXINT, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFSIZE,
      g_param_spec_ulong ("buffer-size", "Buffer Size",
          "Size of the video buffers", 0, G_MAXULONG, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUALITY,
      g_param_spec_int ("quality", "Quality",
          "Amount of Motion Estimation", 1, 5, 3,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_class->dispose = gst_divxenc_dispose;

  gst_divxenc_signals[FRAME_ENCODED] =
      g_signal_new ("frame-encoded", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstDivxEncClass, frame_encoded),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}


static void
gst_divxenc_init (GstDivxEnc * divxenc)
{
  /* create the sink pad */
  divxenc->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (divxenc), divxenc->sinkpad);

  gst_pad_set_chain_function (divxenc->sinkpad, gst_divxenc_chain);
  gst_pad_set_setcaps_function (divxenc->sinkpad, gst_divxenc_setcaps);

  /* create the src pad */
  divxenc->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (divxenc->srcpad);
  gst_element_add_pad (GST_ELEMENT (divxenc), divxenc->srcpad);

  /* bitrate, etc. */
  divxenc->width = divxenc->height = divxenc->csp = divxenc->bitcnt = -1;
  divxenc->bitrate = 512 * 1024;
  divxenc->max_key_interval = -1;       /* default - 2*fps */
  divxenc->buffer_size = 512 * 1024;
  divxenc->quality = 3;

  /* set divx handle to NULL */
  divxenc->handle = NULL;
}


static gboolean
gst_divxenc_setup (GstDivxEnc * divxenc)
{
  void *handle = NULL;
  SETTINGS output;
  DivXBitmapInfoHeader input;
  int ret;

  /* set it up */
  memset (&input, 0, sizeof (DivXBitmapInfoHeader));
  input.biSize = sizeof (DivXBitmapInfoHeader);
  input.biWidth = divxenc->width;
  input.biHeight = divxenc->height;
  input.biBitCount = divxenc->bitcnt;
  input.biCompression = divxenc->csp;

  memset (&output, 0, sizeof (SETTINGS));
  output.vbr_mode = RCMODE_VBV_1PASS;
  output.bitrate = divxenc->bitrate;
  output.quantizer = 0;
  output.use_bidirect = 1;
  output.input_clock = 0;
  output.input_frame_period = 1000000;
  output.internal_timescale = (divxenc->fps_n / divxenc->fps_d) * 1000000;      /* FIX? */
  output.max_key_interval = (divxenc->max_key_interval == -1) ?
      150 : divxenc->max_key_interval;
  output.key_frame_threshold = 50;
  output.vbv_bitrate = 0;
  output.vbv_size = 0;
  output.vbv_occupancy = 0;
  output.complexity_modulation = 0;
  output.deinterlace = 0;
  output.quality = divxenc->quality;
  output.data_partitioning = 0;
  output.quarter_pel = 1;
  output.use_gmc = 1;
  output.psychovisual = 0;
  output.pv_strength_frame = 0;
  output.pv_strength_MB = 0;
  output.interlace_mode = 0;
  output.enable_crop = 0;
  output.enable_resize = 0;
  output.temporal_enable = 1;
  output.spatial_passes = 3;
  output.spatial_level = 1.0;
  output.temporal_level = 1.0;

  if ((ret = encore (&handle, ENC_OPT_INIT, &input, &output))) {
    GST_ELEMENT_ERROR (divxenc, LIBRARY, SETTINGS, (NULL),
        ("Error setting up divx encoder: %s (%d)",
            gst_divxenc_error (ret), ret));
    return FALSE;
  }

  divxenc->handle = handle;

  /* set buffer size to theoretical limit (see docs on divx.com) */
  divxenc->buffer_size = 6 * divxenc->width * divxenc->height;

  return TRUE;
}


static void
gst_divxenc_unset (GstDivxEnc * divxenc)
{
  if (divxenc->handle) {
    encore (divxenc->handle, ENC_OPT_RELEASE, NULL, NULL);
    divxenc->handle = NULL;
  }
}


static void
gst_divxenc_dispose (GObject * object)
{
  GstDivxEnc *divxenc = GST_DIVXENC (object);

  gst_divxenc_unset (divxenc);
  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static GstFlowReturn
gst_divxenc_chain (GstPad * pad, GstBuffer * buf)
{
  GstDivxEnc *divxenc;
  GstBuffer *outbuf;
  ENC_FRAME xframe;
  ENC_RESULT xres;
  int res;
  GstFlowReturn ret = GST_FLOW_OK;

  divxenc = GST_DIVXENC (gst_pad_get_parent (pad));

  outbuf = gst_buffer_new_and_alloc (divxenc->buffer_size);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

  /* encode and so ... */
  xframe.image = GST_BUFFER_DATA (buf);
  xframe.bitstream = (void *) GST_BUFFER_DATA (outbuf);
  xframe.length = GST_BUFFER_SIZE (outbuf);     /* GST_BUFFER_MAXSIZE */
  xframe.produce_empty_frame = 0;

  if ((res = encore (divxenc->handle, ENC_OPT_ENCODE, &xframe, &xres))) {
    goto not_encoding;
  }

  GST_BUFFER_SIZE (outbuf) = xframe.length;

  /* go out, multiply! */
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (divxenc->srcpad));
  gst_pad_push (divxenc->srcpad, outbuf);

  /* proclaim destiny */
  g_signal_emit (G_OBJECT (divxenc), gst_divxenc_signals[FRAME_ENCODED], 0);

  /* until the final judgement */
  goto done;

not_encoding:

  GST_ELEMENT_ERROR (divxenc, LIBRARY, ENCODE, (NULL),
      ("Error encoding divx frame: %s (%d)", gst_divxenc_error (res), res));
  ret = GST_FLOW_ERROR;
  gst_buffer_unref (outbuf);
  goto done;

done:
  gst_buffer_unref (buf);
  gst_object_unref (divxenc);
  return ret;

}

/* FIXME: moving broken bits here for others to fix */
      /* someone fix RGB please */
/*
    case GST_MAKE_FOURCC ('R', 'G', 'B', ' '):
      gst_caps_get_int (caps, "depth", &d);
      switch (d) {
	case 24:
	  divx_cs = 0;
	  bitcnt = 24;
	  break;
	case 32:
	  divx_cs = 0;
	  bitcnt = 32;
	  break;
*/

static gboolean
gst_divxenc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDivxEnc *divxenc;
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint w, h;
  const GValue *fps;
  guint32 fourcc;
  guint32 divx_cs;
  gint bitcnt = 0;
  gboolean ret = FALSE;

  divxenc = GST_DIVXENC (gst_pad_get_parent (pad));

  /* if there's something old around, remove it */
  gst_divxenc_unset (divxenc);

  gst_structure_get_int (structure, "width", &w);
  gst_structure_get_int (structure, "height", &h);
  gst_structure_get_fourcc (structure, "format", &fourcc);

  fps = gst_structure_get_value (structure, "framerate");
  if (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps)) {
    divxenc->fps_n = gst_value_get_fraction_numerator (fps);
    divxenc->fps_d = gst_value_get_fraction_denominator (fps);
  } else {
    divxenc->fps_n = -1;
  }

  switch (fourcc) {
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      divx_cs = GST_MAKE_FOURCC ('I', '4', '2', '0');
      break;
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      divx_cs = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
      break;
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      divx_cs = GST_MAKE_FOURCC ('Y', 'V', '1', '2');
      break;
    case GST_MAKE_FOURCC ('Y', 'V', 'Y', 'U'):
      divx_cs = GST_MAKE_FOURCC ('Y', 'V', 'Y', 'U');
      break;
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
      divx_cs = GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
      break;
    default:
      ret = FALSE;
      goto done;
  }

  divxenc->csp = divx_cs;
  divxenc->bitcnt = bitcnt;
  divxenc->width = w;
  divxenc->height = h;

  /* try it */
  if (gst_divxenc_setup (divxenc)) {
    GstCaps *new_caps = NULL;

    new_caps = gst_caps_new_simple ("video/x-divx",
        "divxversion", G_TYPE_INT, 5,
        "width", G_TYPE_INT, w,
        "height", G_TYPE_INT, h,
        "framerate", GST_TYPE_FRACTION, divxenc->fps_n, divxenc->fps_d, NULL);

    if (new_caps) {

      if (!gst_pad_set_caps (divxenc->srcpad, new_caps)) {
        gst_divxenc_unset (divxenc);
        ret = FALSE;
        goto done;
      }
      gst_caps_unref (new_caps);
      ret = TRUE;
      goto done;

    }

  }

  /* if we got here - it's not good */

  ret = FALSE;

done:
  gst_object_unref (divxenc);
  return ret;
}


static void
gst_divxenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstDivxEnc *divxenc = GST_DIVXENC (object);

  GST_OBJECT_LOCK (divxenc);

  switch (prop_id) {
    case ARG_BITRATE:
      divxenc->bitrate = g_value_get_ulong (value);
      break;
    case ARG_MAXKEYINTERVAL:
      divxenc->max_key_interval = g_value_get_int (value);
      break;
    case ARG_QUALITY:
      divxenc->quality = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (divxenc);
}


static void
gst_divxenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstDivxEnc *divxenc = GST_DIVXENC (object);

  GST_OBJECT_LOCK (divxenc);

  switch (prop_id) {
    case ARG_BITRATE:
      g_value_set_ulong (value, divxenc->bitrate);
      break;
    case ARG_BUFSIZE:
      g_value_set_ulong (value, divxenc->buffer_size);
      break;
    case ARG_MAXKEYINTERVAL:
      g_value_set_int (value, divxenc->max_key_interval);
      break;
    case ARG_QUALITY:
      g_value_set_int (value, divxenc->quality);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (divxenc);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  int lib_version;

  lib_version = encore (NULL, ENC_OPT_VERSION, 0, 0);
  if (lib_version != ENCORE_VERSION) {
    g_warning ("Version mismatch! This plugin was compiled for "
        "DivX version %d, while your library has version %d!",
        ENCORE_VERSION, lib_version);
    return FALSE;
  }

  /* create an elementfactory for the v4lmjpegsrcparse element */
  return gst_element_register (plugin, "divxenc",
      GST_RANK_NONE, GST_TYPE_DIVXENC);
}


GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "divxenc",
    "DivX encoder",
    plugin_init,
    "5.03", GST_LICENSE_UNKNOWN, "divx4linux", "http://www.divx.com/");
