/* GStreamer xvid encoder plugin
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

#include <xvid.h>

#include <gst/video/video.h>
#include "gstxvidenc.h"

/* elementfactory information */
GstElementDetails gst_xvidenc_details = {
  "Xvid encoder",
  "Codec/Video/Encoder",
  "Xvid encoder based on xvidcore",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YUY2, YV12, YVYU, UYVY }")
	"; " RGB_24_32_STATIC_CAPS (32, 0x00ff0000, 0x0000ff00,
	    0x000000ff) "; " RGB_24_32_STATIC_CAPS (32, 0xff000000, 0x00ff0000,
	    0x0000ff00) "; " RGB_24_32_STATIC_CAPS (32, 0x0000ff00, 0x00ff0000,
	    0xff000000) "; " RGB_24_32_STATIC_CAPS (32, 0x000000ff, 0x0000ff00,
	    0x00ff0000) "; " RGB_24_32_STATIC_CAPS (24, 0x0000ff, 0x00ff00,
	    0xff0000) "; " GST_VIDEO_CAPS_RGB_15 "; " GST_VIDEO_CAPS_RGB_16)
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-xvid, "
	"width = (int) [ 0, MAX ], "
	"height = (int) [ 0, MAX ], " "framerate = (double) [ 0.0, MAX ]")
    );


/* XvidEnc signals and args */
enum
{
  FRAME_ENCODED,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_PROFILE,
  ARG_BITRATE,
  ARG_MAXKEYINTERVAL,
  ARG_BUFSIZE
      /* FILL ME:
       *  - ME
       *  - VOP
       *  - VOL
       *  - PAR
       *  - max b frames
       */
};

static void gst_xvidenc_base_init (gpointer g_class);
static void gst_xvidenc_class_init (GstXvidEncClass * klass);
static void gst_xvidenc_init (GstXvidEnc * xvidenc);
static void gst_xvidenc_chain (GstPad * pad, GstData * data);
static GstPadLinkReturn
gst_xvidenc_link (GstPad * pad, const GstCaps * vscapslist);

/* properties */
static void gst_xvidenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_xvidenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_xvidenc_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;
static guint gst_xvidenc_signals[LAST_SIGNAL] = { 0 };

#define GST_TYPE_XVIDENC_PROFILE (gst_xvidenc_profile_get_type ())

static GType
gst_xvidenc_profile_get_type (void)
{
  static GType xvidenc_profile_type = 0;

  if (!xvidenc_profile_type) {
    static const GEnumValue xvidenc_profiles[] = {
      {XVID_PROFILE_S_L0, "S_L0", "Simple profile, L0"},
      {XVID_PROFILE_S_L1, "S_L1", "Simple profile, L1"},
      {XVID_PROFILE_S_L2, "S_L2", "Simple profile, L2"},
      {XVID_PROFILE_S_L3, "S_L3", "Simple profile, L3"},
      {XVID_PROFILE_ARTS_L1, "ARTS_L1",
	  "Advanced real-time simple profile, L1"},
      {XVID_PROFILE_ARTS_L2, "ARTS_L2",
	  "Advanced real-time simple profile, L2"},
      {XVID_PROFILE_ARTS_L3, "ARTS_L3",
	  "Advanced real-time simple profile, L3"},
      {XVID_PROFILE_ARTS_L4, "ARTS_L4",
	  "Advanced real-time simple profile, L4"},
      {XVID_PROFILE_AS_L0, "AS_L0", "Advanced simple profile, L0"},
      {XVID_PROFILE_AS_L1, "AS_L1", "Advanced simple profile, L1"},
      {XVID_PROFILE_AS_L2, "AS_L2", "Advanced simple profile, L2"},
      {XVID_PROFILE_AS_L3, "AS_L3", "Advanced simple profile, L3"},
      {XVID_PROFILE_AS_L4, "AS_L4", "Advanced simple profile, L4"},
      {0, NULL, NULL},
    };

    xvidenc_profile_type =
	g_enum_register_static ("GstXvidEncProfiles", xvidenc_profiles);
  }

  return xvidenc_profile_type;
}

GType
gst_xvidenc_get_type (void)
{
  static GType xvidenc_type = 0;

  if (!xvidenc_type) {
    static const GTypeInfo xvidenc_info = {
      sizeof (GstXvidEncClass),
      gst_xvidenc_base_init,
      NULL,
      (GClassInitFunc) gst_xvidenc_class_init,
      NULL,
      NULL,
      sizeof (GstXvidEnc),
      0,
      (GInstanceInitFunc) gst_xvidenc_init,
    };
    xvidenc_type = g_type_register_static (GST_TYPE_ELEMENT,
	"GstXvidEnc", &xvidenc_info, 0);
  }
  return xvidenc_type;
}

static void
gst_xvidenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_set_details (element_class, &gst_xvidenc_details);
}

static void
gst_xvidenc_class_init (GstXvidEncClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  /* encoding profile */
  g_object_class_install_property (gobject_class, ARG_PROFILE,
      g_param_spec_enum ("profile", "Profile", "XviD/MPEG-4 encoding profile",
	  GST_TYPE_XVIDENC_PROFILE, XVID_PROFILE_S_L0, G_PARAM_READWRITE));

  /* bitrate */
  g_object_class_install_property (gobject_class, ARG_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate",
	  "Target video bitrate (kbps)", 0, G_MAXINT, 512, G_PARAM_READWRITE));

  /* keyframe interval */
  g_object_class_install_property (gobject_class, ARG_MAXKEYINTERVAL,
      g_param_spec_int ("max_key_interval", "Max. Key Interval",
	  "Maximum number of frames between two keyframes",
	  0, G_MAXINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_BUFSIZE,
      g_param_spec_ulong ("buffer_size", "Buffer Size",
	  "Size of the video buffers", 0, G_MAXULONG, 0, G_PARAM_READWRITE));

  gobject_class->set_property = gst_xvidenc_set_property;
  gobject_class->get_property = gst_xvidenc_get_property;
  gstelement_class->change_state = gst_xvidenc_change_state;

  gst_xvidenc_signals[FRAME_ENCODED] =
      g_signal_new ("frame-encoded", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstXvidEncClass, frame_encoded),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}


static void
gst_xvidenc_init (GstXvidEnc * xvidenc)
{
  gst_xvid_init ();

  /* create the sink pad */
  xvidenc->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_element_add_pad (GST_ELEMENT (xvidenc), xvidenc->sinkpad);

  gst_pad_set_chain_function (xvidenc->sinkpad, gst_xvidenc_chain);
  gst_pad_set_link_function (xvidenc->sinkpad, gst_xvidenc_link);

  /* create the src pad */
  xvidenc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_element_add_pad (GST_ELEMENT (xvidenc), xvidenc->srcpad);

  /* bitrate, etc. */
  xvidenc->width = xvidenc->height = xvidenc->csp = xvidenc->stride = -1;
  xvidenc->profile = XVID_PROFILE_S_L0;
  xvidenc->bitrate = 512;
  xvidenc->max_b_frames = 2;
  xvidenc->max_key_interval = -1;	/* default - 2*fps */
  xvidenc->buffer_size = 512;

  /* set xvid handle to NULL */
  xvidenc->handle = NULL;
}


static gboolean
gst_xvidenc_setup (GstXvidEnc * xvidenc)
{
  xvid_enc_create_t xenc;
  xvid_enc_plugin_t xplugin;
  xvid_plugin_single_t xsingle;
  gint ret;

  /* see xvid.h for the meaning of all this. */
  gst_xvid_init_struct (xenc);
  xenc.profile = xvidenc->profile;
  xenc.width = xvidenc->width;
  xenc.height = xvidenc->height;
  xenc.max_bframes = xvidenc->max_b_frames;
  xenc.global = XVID_GLOBAL_PACKED;

  xenc.fbase = 1000000;
  xenc.fincr = (int) (xenc.fbase / xvidenc->fps);
  xenc.max_key_interval = (xvidenc->max_key_interval == -1) ?
      (2 * xenc.fbase / xenc.fincr) : xvidenc->max_key_interval;
  xenc.handle = NULL;

  /* CBR bitrate/quant for now */
  gst_xvid_init_struct (xsingle);
  xsingle.bitrate = xvidenc->bitrate << 10;
  xsingle.reaction_delay_factor = -1;
  xsingle.averaging_period = -1;
  xsingle.buffer = -1;

  /* set CBR plugin */
  xenc.num_plugins = 1;
  xenc.plugins = &xplugin;
  xenc.plugins[0].func = xvid_plugin_single;
  xenc.plugins[0].param = &xsingle;

  if ((ret = xvid_encore (NULL, XVID_ENC_CREATE, &xenc, NULL)) < 0) {
    GST_ELEMENT_ERROR (xvidenc, LIBRARY, INIT, (NULL),
	("Error setting up xvid encoder: %s (%d)", gst_xvid_error (ret), ret));
    return FALSE;
  }

  xvidenc->handle = xenc.handle;

  return TRUE;
}


static void
gst_xvidenc_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstXvidEnc *xvidenc = GST_XVIDENC (GST_OBJECT_PARENT (pad));
  GstBuffer *outbuf;
  xvid_enc_frame_t xframe;
  xvid_enc_stats_t xstats;
  gint ret;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));

  outbuf = gst_buffer_new_and_alloc (xvidenc->buffer_size << 10);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);

  /* encode and so ... */
  gst_xvid_init_struct (xframe);
  xframe.vol_flags = XVID_VOL_MPEGQUANT | XVID_VOL_GMC;
  xframe.par = XVID_PAR_11_VGA;
  xframe.vop_flags = XVID_VOP_TRELLISQUANT;
  xframe.motion = 0;
  xframe.input.csp = xvidenc->csp;
  if (xvidenc->width == xvidenc->stride) {
    xframe.input.plane[0] = GST_BUFFER_DATA (buf);
    xframe.input.plane[1] =
	xframe.input.plane[0] + (xvidenc->width * xvidenc->height);
    xframe.input.plane[2] =
	xframe.input.plane[1] + (xvidenc->width * xvidenc->height / 4);
    xframe.input.stride[0] = xvidenc->width;
    xframe.input.stride[1] = xvidenc->width / 2;
    xframe.input.stride[2] = xvidenc->width / 2;
  } else {
    xframe.input.plane[0] = GST_BUFFER_DATA (buf);
    xframe.input.stride[0] = xvidenc->stride;
  }
  xframe.type = XVID_TYPE_AUTO;
  xframe.bitstream = (void *) GST_BUFFER_DATA (outbuf);
  xframe.length = GST_BUFFER_MAXSIZE (outbuf);
  gst_xvid_init_struct (xstats);

  if ((ret = xvid_encore (xvidenc->handle, XVID_ENC_ENCODE,
	      &xframe, &xstats)) < 0) {
    GST_ELEMENT_ERROR (xvidenc, LIBRARY, ENCODE, (NULL),
	("Error encoding xvid frame: %s (%d)", gst_xvid_error (ret), ret));
    gst_buffer_unref (buf);
    gst_buffer_unref (outbuf);
    return;
  }

  GST_BUFFER_SIZE (outbuf) = xstats.length;
  if (xframe.out_flags & XVID_KEYFRAME)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_KEY_UNIT);

  /* go out, multiply! */
  gst_pad_push (xvidenc->srcpad, GST_DATA (outbuf));

  /* proclaim destiny */
  g_signal_emit (G_OBJECT (xvidenc), gst_xvidenc_signals[FRAME_ENCODED], 0);

  /* until the final judgement */
  gst_buffer_unref (buf);
}


static GstPadLinkReturn
gst_xvidenc_link (GstPad * pad, const GstCaps * vscaps)
{
  GstXvidEnc *xvidenc;
  GstStructure *structure;
  const gchar *mime;
  gint w, h;
  double fps;
  gint xvid_cs = -1, stride = -1;

  xvidenc = GST_XVIDENC (gst_pad_get_parent (pad));

  /* if there's something old around, remove it */
  if (xvidenc->handle) {
    xvid_encore (xvidenc->handle, XVID_ENC_DESTROY, NULL, NULL);
    xvidenc->handle = NULL;
  }

  g_return_val_if_fail (gst_caps_get_size (vscaps) == 1, GST_PAD_LINK_REFUSED);
  structure = gst_caps_get_structure (vscaps, 0);

  gst_structure_get_int (structure, "width", &w);
  gst_structure_get_int (structure, "height", &h);
  gst_structure_get_double (structure, "framerate", &fps);
  mime = gst_structure_get_name (structure);

  xvid_cs = gst_xvid_structure_to_csp (structure, w, &stride, NULL);
  g_return_val_if_fail (xvid_cs != -1, GST_PAD_LINK_REFUSED);

  xvidenc->csp = xvid_cs;
  xvidenc->width = w;
  xvidenc->height = h;
  xvidenc->stride = stride;
  xvidenc->fps = fps;

  if (gst_xvidenc_setup (xvidenc)) {
    GstPadLinkReturn ret;
    GstCaps *new_caps;

    new_caps = gst_caps_new_simple ("video/x-xvid",
	"width", G_TYPE_INT, w,
	"height", G_TYPE_INT, h, "framerate", G_TYPE_DOUBLE, fps, NULL);
    ret = gst_pad_try_set_caps (xvidenc->srcpad, new_caps);
    if (GST_PAD_LINK_FAILED (ret)) {
      if (xvidenc->handle) {
	xvid_encore (xvidenc->handle, XVID_ENC_DESTROY, NULL, NULL);
	xvidenc->handle = NULL;
      }
    }

    return ret;
  }

  /* if we got here - it's not good */
  return GST_PAD_LINK_REFUSED;
}


static void
gst_xvidenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstXvidEnc *xvidenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_XVIDENC (object));
  xvidenc = GST_XVIDENC (object);

  switch (prop_id) {
    case ARG_PROFILE:
      xvidenc->profile = g_value_get_enum (value);
      break;
    case ARG_BITRATE:
      xvidenc->bitrate = g_value_get_int (value);
      break;
    case ARG_BUFSIZE:
      xvidenc->buffer_size = g_value_get_int (value);
      break;
    case ARG_MAXKEYINTERVAL:
      xvidenc->max_key_interval = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_xvidenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstXvidEnc *xvidenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_XVIDENC (object));
  xvidenc = GST_XVIDENC (object);

  switch (prop_id) {
    case ARG_PROFILE:
      g_value_set_enum (value, xvidenc->profile);
      break;
    case ARG_BITRATE:
      g_value_set_int (value, xvidenc->bitrate);
      break;
    case ARG_BUFSIZE:
      g_value_set_int (value, xvidenc->buffer_size);
      break;
    case ARG_MAXKEYINTERVAL:
      g_value_set_int (value, xvidenc->max_key_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_xvidenc_change_state (GstElement * element)
{
  GstXvidEnc *xvidenc = GST_XVIDENC (element);

  switch (GST_STATE_PENDING (element)) {
    case GST_STATE_PAUSED_TO_READY:
      if (xvidenc->handle) {
	xvid_encore (xvidenc->handle, XVID_ENC_DESTROY, NULL, NULL);
	xvidenc->handle = NULL;
      }
      break;
    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}
