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
#include "gstxvidenc.h"
#include <gst/video/video.h>
#include <xvid.h>

/* elementfactory information */
GstElementDetails gst_xvidenc_details = {
  "Xvid encoder",
  "Codec/Video/Encoder",
  "Xvid encoder based on xvidencore",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
};

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    GST_VIDEO_YUV_PAD_TEMPLATE_CAPS ("{ I420, YUY2, YV12, YVYU, UYVY }") "; "
    GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_24_32 "; "
    GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_15_16
  )
);

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "video/x-xvid, "
    "width = (int) [ 0, MAX ], "
    "height = (int) [ 0, MAX ], "
    "framerate = (double) [ 0.0, MAX ]"
  )
);


/* XvidEnc signals and args */
enum {
  FRAME_ENCODED,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_BITRATE,
  ARG_MAXKEYINTERVAL,
  ARG_BUFSIZE
};

static void             gst_xvidenc_base_init    (gpointer g_class);
static void             gst_xvidenc_class_init   (GstXvidEncClass *klass);
static void             gst_xvidenc_init         (GstXvidEnc      *xvidenc);
static void             gst_xvidenc_chain        (GstPad          *pad,
                                                  GstData         *data);
static GstPadLinkReturn gst_xvidenc_link	 (GstPad          *pad,
                                                  const GstCaps  *vscapslist);

/* properties */
static void             gst_xvidenc_set_property (GObject         *object,
                                                  guint            prop_id,
                                                  const GValue    *value,
                                                  GParamSpec      *pspec);
static void             gst_xvidenc_get_property (GObject         *object,
                                                  guint            prop_id,
                                                  GValue          *value,
                                                  GParamSpec      *pspec);

static GstElementClass *parent_class = NULL;
static guint gst_xvidenc_signals[LAST_SIGNAL] = { 0 };


GType
gst_xvidenc_get_type(void)
{
  static GType xvidenc_type = 0;

  if (!xvidenc_type)
  {
    static const GTypeInfo xvidenc_info = {
      sizeof(GstXvidEncClass),
      gst_xvidenc_base_init,
      NULL,
      (GClassInitFunc) gst_xvidenc_class_init,
      NULL,
      NULL,
      sizeof(GstXvidEnc),
      0,
      (GInstanceInitFunc) gst_xvidenc_init,
    };
    xvidenc_type = g_type_register_static(GST_TYPE_ELEMENT,
                                          "GstXvidEnc",
                                          &xvidenc_info, 0);
  }
  return xvidenc_type;
}

static void
gst_xvidenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&src_template));
  gst_element_class_set_details (element_class, &gst_xvidenc_details);
}

static void
gst_xvidenc_class_init (GstXvidEncClass *klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gst_xvid_init();

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BITRATE,
    g_param_spec_ulong("bitrate","Bitrate",
                       "Target video bitrate",
                       0,G_MAXULONG,0,G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MAXKEYINTERVAL,
    g_param_spec_int("max_key_interval","Max. Key Interval",
                     "Maximum number of frames between two keyframes",
                     0,G_MAXINT,0,G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFSIZE,
    g_param_spec_ulong("buffer_size", "Buffer Size",
                       "Size of the video buffers",
                       0,G_MAXULONG,0,G_PARAM_READWRITE));

  gobject_class->set_property = gst_xvidenc_set_property;
  gobject_class->get_property = gst_xvidenc_get_property;

  gst_xvidenc_signals[FRAME_ENCODED] =
    g_signal_new ("frame_encoded", G_TYPE_FROM_CLASS(klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstXvidEncClass, frame_encoded),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}


static void
gst_xvidenc_init (GstXvidEnc *xvidenc)
{
  /* create the sink pad */
  xvidenc->sinkpad = gst_pad_new_from_template(
                       gst_static_pad_template_get (&sink_template),
                       "sink");
  gst_element_add_pad(GST_ELEMENT(xvidenc), xvidenc->sinkpad);

  gst_pad_set_chain_function(xvidenc->sinkpad, gst_xvidenc_chain);
  gst_pad_set_link_function(xvidenc->sinkpad, gst_xvidenc_link);

  /* create the src pad */
  xvidenc->srcpad = gst_pad_new_from_template(
                      gst_static_pad_template_get (&src_template),
                      "src");
  gst_element_add_pad(GST_ELEMENT(xvidenc), xvidenc->srcpad);

  /* bitrate, etc. */
  xvidenc->width = xvidenc->height = xvidenc->csp = -1;
  xvidenc->bitrate = 512 * 1024;
  xvidenc->max_key_interval = -1; /* default - 2*fps */
  xvidenc->buffer_size = 512 * 1024;

  /* set xvid handle to NULL */
  xvidenc->handle = NULL;
}


static gboolean
gst_xvidenc_setup (GstXvidEnc *xvidenc)
{
  XVID_ENC_PARAM xenc;
  int ret;

  /* set up xvid codec parameters - grab docs from
   * xvid.org for more info */
  memset(&xenc, 0, sizeof(XVID_ENC_PARAM));
  xenc.width = xvidenc->width;
  xenc.height = xvidenc->height;
  xenc.fincr = (int)(xvidenc->fps * 1000);
  xenc.fbase = 1000;
  xenc.rc_bitrate = xvidenc->bitrate;
  xenc.rc_reaction_delay_factor = -1;
  xenc.rc_averaging_period = -1;
  xenc.rc_buffer = -1;
  xenc.min_quantizer = 1;
  xenc.max_quantizer = 31;
  xenc.max_key_interval = (xvidenc->max_key_interval == -1) ?
                            (2 * xenc.fincr / xenc.fbase) :
                              xvidenc->max_key_interval;
  xenc.handle = NULL;

  if ((ret = xvid_encore(NULL, XVID_ENC_CREATE,
                         &xenc, NULL)) != XVID_ERR_OK) {
    gst_element_error(GST_ELEMENT(xvidenc),
                      "Error setting up xvid encoder: %s (%d)",
		      gst_xvid_error(ret), ret);
    return FALSE;
  }

  xvidenc->handle = xenc.handle;

  return TRUE;
}


static void
gst_xvidenc_chain (GstPad    *pad,
                   GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstXvidEnc *xvidenc;
  GstBuffer *outbuf;
  XVID_ENC_FRAME xframe;
  int ret;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  xvidenc = GST_XVIDENC(GST_OBJECT_PARENT(pad));

  outbuf = gst_buffer_new_and_alloc(xvidenc->buffer_size);
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);

  /* encode and so ... */
  xframe.image = GST_BUFFER_DATA(buf);
  xframe.bitstream = (void *) GST_BUFFER_DATA(outbuf);
  xframe.length = GST_BUFFER_MAXSIZE(outbuf);
  xframe.intra = -1;
  xframe.quant = 0;
  xframe.colorspace = xvidenc->csp;
  xframe.general = XVID_H263QUANT |
                   XVID_INTER4V |
                   XVID_HALFPEL;
  xframe.motion = PMV_EARLYSTOP16 |
                  PMV_HALFPELREFINE16 |
                  PMV_EXTSEARCH16 |
                  PMV_EARLYSTOP8 |
                  PMV_HALFPELREFINE8;

  if ((ret = xvid_encore(xvidenc->handle, XVID_ENC_ENCODE,
                         &xframe, NULL)) != XVID_ERR_OK) {
    gst_element_error(GST_ELEMENT(xvidenc),
                      "Error encoding xvid frame: %s (%d)",
		      gst_xvid_error(ret), ret);
    gst_buffer_unref(buf);
    return;
  }

  GST_BUFFER_SIZE(outbuf) = xframe.length;
  if (xframe.intra)
    GST_BUFFER_FLAG_SET(outbuf, GST_BUFFER_KEY_UNIT);

  /* go out, multiply! */
  gst_pad_push(xvidenc->srcpad, GST_DATA (outbuf));

  /* proclaim destiny */
  g_signal_emit(G_OBJECT(xvidenc),gst_xvidenc_signals[FRAME_ENCODED], 0);

  /* until the final judgement */
  gst_buffer_unref(buf);
}


static GstPadLinkReturn
gst_xvidenc_link (GstPad  *pad,
                  const GstCaps *vscaps)
{
  GstXvidEnc *xvidenc;
  GstStructure *structure;
  gint w,h,d;
  double fps;
  guint32 fourcc;
  gint xvid_cs = -1;

  xvidenc = GST_XVIDENC(gst_pad_get_parent (pad));

  /* if there's something old around, remove it */
  if (xvidenc->handle) {
    xvid_encore(xvidenc->handle, XVID_ENC_DESTROY, NULL, NULL);
    xvidenc->handle = NULL;
  }

  g_return_val_if_fail (gst_caps_get_size (vscaps) == 1, GST_PAD_LINK_REFUSED);
  structure = gst_caps_get_structure (vscaps, 0);

  gst_structure_get_int (structure, "width", &w);
  gst_structure_get_int (structure, "height", &h);
  gst_structure_get_double (structure, "framerate", &fps);
  if (gst_structure_has_field_typed (structure, "format", GST_TYPE_FOURCC))
    gst_structure_get_fourcc (structure, "format", &fourcc);
  else
    fourcc = GST_MAKE_FOURCC('R','G','B',' ');

  switch (fourcc)
  {
    case GST_MAKE_FOURCC('I','4','2','0'):
    case GST_MAKE_FOURCC('I','Y','U','V'):
      xvid_cs = XVID_CSP_I420;
      break;
    case GST_MAKE_FOURCC('Y','U','Y','2'):
      xvid_cs = XVID_CSP_YUY2;
      break;
    case GST_MAKE_FOURCC('Y','V','1','2'):
      xvid_cs = XVID_CSP_YV12;
      break;
    case GST_MAKE_FOURCC('U','Y','V','Y'):
      xvid_cs = XVID_CSP_UYVY;
      break;
    case GST_MAKE_FOURCC('Y','V','Y','U'):
      xvid_cs = XVID_CSP_YVYU;
      break;
    case GST_MAKE_FOURCC('R','G','B',' '):
      gst_structure_get_int(structure, "depth", &d);
      switch (d) {
        case 15:
          xvid_cs = XVID_CSP_RGB555;
          break;
        case 16:
          xvid_cs = XVID_CSP_RGB565;
          break;
        case 24:
          xvid_cs = XVID_CSP_RGB24;
          break;
        case 32:
          xvid_cs = XVID_CSP_RGB32;
          break;
      }
      break;
  }

  g_return_val_if_fail (xvid_cs != -1, GST_PAD_LINK_REFUSED);

  xvidenc->csp = xvid_cs;
  xvidenc->width = w;
  xvidenc->height = h;
  xvidenc->fps = fps;

  if (gst_xvidenc_setup(xvidenc)) {
    GstPadLinkReturn ret;
    GstCaps *new_caps;

    new_caps = gst_caps_new_simple(
                            "video/x-xvid",
                            "width",  G_TYPE_INT, w,
                            "height", G_TYPE_INT, h,
			    "framerate", G_TYPE_DOUBLE, fps);

    ret = gst_pad_try_set_caps(xvidenc->srcpad, new_caps);

    if (ret <= 0) {
      if (xvidenc->handle) {
        xvid_encore(xvidenc->handle, XVID_ENC_DESTROY, NULL, NULL);
        xvidenc->handle = NULL;
      }
    }

    return ret;
  }

  /* if we got here - it's not good */
  return GST_PAD_LINK_REFUSED;
}


static void
gst_xvidenc_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  GstXvidEnc *xvidenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_XVIDENC (object));
  xvidenc = GST_XVIDENC(object);

  switch (prop_id)
  {
    case ARG_BITRATE:
      xvidenc->bitrate = g_value_get_ulong(value);
      break;
    case ARG_BUFSIZE:
      xvidenc->buffer_size = g_value_get_ulong(value);
      break;
    case ARG_MAXKEYINTERVAL:
      xvidenc->max_key_interval = g_value_get_int(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_xvidenc_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  GstXvidEnc *xvidenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_XVIDENC (object));
  xvidenc = GST_XVIDENC(object);

  switch (prop_id) {
    case ARG_BITRATE:
      g_value_set_ulong(value, xvidenc->bitrate);
      break;
    case ARG_BUFSIZE:
      g_value_set_ulong(value, xvidenc->buffer_size);
      break;
    case ARG_MAXKEYINTERVAL:
      g_value_set_int(value, xvidenc->max_key_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
