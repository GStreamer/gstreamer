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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>

/*#define DEBUG_ENABLED */
#include <gstqcamsrc.h>
#include <gst/video/video.h>

#include "qcamip.h"

/* elementfactory information */
static GstElementDetails gst_qcamsrc_details = GST_ELEMENT_DETAILS (
  "QCam Source",
  "Source/Video",
  "Read from a QuickCam device",
  "Wim Taymans <wim.taymans@chello.be>"
);

#define AE_NONE			3

#define DEF_WIDTH 		320
#define DEF_HEIGHT 		224
#define DEF_BRIGHTNESS		226
#define DEF_WHITEBAL		128
#define DEF_CONTRAST		72
#define DEF_TOP			1
#define DEF_LEFT		14
#define DEF_TRANSFER_SCALE	2
#define DEF_DEPTH		6
#define DEF_PORT		0x378
#define DEF_AUTOEXP		AE_NONE

static GstStaticPadTemplate gst_qcamsrc_src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV("I420"))
);

#define GST_TYPE_AUTOEXP_MODE (gst_autoexp_mode_get_type())
static GType
gst_autoexp_mode_get_type (void)
{
  static GType autoexp_mode_type = 0;
  static GEnumValue autoexp_modes[] = {
    { AE_ALL_AVG, "0", "Average Picture" },
    { AE_CTR_AVG, "1", "Average Center" },
    { AE_STD_AVG, "2", "Standard Deviation" },
    { AE_NONE,    "3", "None" },
    { 0, NULL, NULL },
  };
  if (!autoexp_mode_type) {
    autoexp_mode_type = g_enum_register_static ("GstAutoExposureMode", autoexp_modes);
  }
  return autoexp_mode_type;
}

/* QCamSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_BRIGHTNESS,
  ARG_WHITEBAL,
  ARG_CONTRAST,
  ARG_TOP,
  ARG_LEFT,
  ARG_TRANSFER_SCALE,
  ARG_DEPTH,
  ARG_PORT,
  ARG_AUTOEXP,
};

static void			gst_qcamsrc_base_init		(gpointer g_class);
static void			gst_qcamsrc_class_init		(GstQCamSrcClass *klass);
static void			gst_qcamsrc_init		(GstQCamSrc *qcamsrc);

static void			gst_qcamsrc_set_property	(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void			gst_qcamsrc_get_property	(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

static GstElementStateReturn	gst_qcamsrc_change_state	(GstElement *element);
static void			gst_qcamsrc_close		(GstQCamSrc *src);
static gboolean			gst_qcamsrc_open		(GstQCamSrc *src);

static GstData*		gst_qcamsrc_get			(GstPad *pad);

static GstElementClass *parent_class = NULL;
/*//static guint gst_qcamsrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_qcamsrc_get_type (void)
{
  static GType qcamsrc_type = 0;

  if (!qcamsrc_type) {
    static const GTypeInfo qcamsrc_info = {
      sizeof(GstQCamSrcClass),      
      gst_qcamsrc_base_init,
      NULL,
      (GClassInitFunc)gst_qcamsrc_class_init,
      NULL,
      NULL,
      sizeof(GstQCamSrc),
      0,
      (GInstanceInitFunc)gst_qcamsrc_init,
      NULL
    };
    qcamsrc_type = g_type_register_static(GST_TYPE_ELEMENT, "GstQCamSrc", &qcamsrc_info, 0);
  }
  return qcamsrc_type;
}
static void
gst_qcamsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&gst_qcamsrc_src_factory));
  gst_element_class_set_details (element_class, &gst_qcamsrc_details);
}
static void
gst_qcamsrc_class_init (GstQCamSrcClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WIDTH,
    g_param_spec_int ("width", "width", "width",
                      0, 320, DEF_WIDTH, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS(klass), ARG_HEIGHT,
    g_param_spec_int ("height", "height", "height",
                      0, 240, DEF_HEIGHT, G_PARAM_READWRITE)); 
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BRIGHTNESS,
    g_param_spec_int ("brightness", "brightness", "brightness",
                      0, 255, DEF_BRIGHTNESS, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_WHITEBAL,
    g_param_spec_int ("whitebal", "whitebal", "whitebal",
                      0, 255, DEF_WHITEBAL, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CONTRAST,
    g_param_spec_int ("contrast", "contrast", "contrast",
                      0, 255, DEF_CONTRAST, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_TOP,
    g_param_spec_int ("top", "top", "top",
                      0, 240, DEF_TOP, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_LEFT,
    g_param_spec_int ("left", "left", "left",
                      0, 320, DEF_LEFT, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_TRANSFER_SCALE,
    g_param_spec_int ("transfer_scale", "transfer_scale", "transfer_scale",
                      1, 4, DEF_TRANSFER_SCALE, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEPTH,
    g_param_spec_int ("depth", "depth", "depth",
                      4, 6, DEF_DEPTH, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PORT,
    g_param_spec_int ("port","port","port",
                      0, G_MAXINT, DEF_PORT, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_AUTOEXP,
    g_param_spec_enum ("autoexposure", "autoexposure", "autoexposure",
                       GST_TYPE_AUTOEXP_MODE, DEF_AUTOEXP, G_PARAM_READWRITE));

  gobject_class->set_property = gst_qcamsrc_set_property;
  gobject_class->get_property = gst_qcamsrc_get_property;

  gstelement_class->change_state = gst_qcamsrc_change_state;
}

static void
gst_qcamsrc_init (GstQCamSrc *qcamsrc)
{
  qcamsrc->srcpad = gst_pad_new_from_template (
		  gst_static_pad_template_get (&gst_qcamsrc_src_factory), "src");
  gst_element_add_pad(GST_ELEMENT(qcamsrc),qcamsrc->srcpad);
  gst_pad_set_get_function (qcamsrc->srcpad,gst_qcamsrc_get);

  /* if the destination cannot say what it wants, we give this */
  qcamsrc->qcam = qc_init();
  qcamsrc->qcam->port = DEF_PORT;
  qc_setwidth (qcamsrc->qcam, DEF_WIDTH);
  qc_setheight (qcamsrc->qcam, DEF_HEIGHT);
  qc_setbrightness (qcamsrc->qcam, DEF_BRIGHTNESS);
  qc_setwhitebal (qcamsrc->qcam, DEF_WHITEBAL);
  qc_setcontrast (qcamsrc->qcam, DEF_CONTRAST);
  qc_settop (qcamsrc->qcam, DEF_TOP);
  qc_setleft (qcamsrc->qcam, DEF_LEFT);
  qc_settransfer_scale (qcamsrc->qcam, DEF_TRANSFER_SCALE);
  qc_setbitdepth (qcamsrc->qcam, DEF_DEPTH);
  qcamsrc->autoexposure = DEF_AUTOEXP;
  if (qcamsrc->autoexposure != AE_NONE) 
    qcip_set_autoexposure_mode (qcamsrc->autoexposure);
}

static GstData*
gst_qcamsrc_get (GstPad *pad)
{
  GstQCamSrc *qcamsrc;
  GstBuffer *buf;
  scanbuf *scan;
  guchar *outdata;
  gint i, frame, scale, convert;

  g_return_val_if_fail (pad != NULL, NULL);

  qcamsrc = GST_QCAMSRC (gst_pad_get_parent (pad));

  scale = qc_gettransfer_scale (qcamsrc->qcam);

  frame = qcamsrc->qcam->width * qcamsrc->qcam->height / (scale * scale);

  buf = gst_buffer_new();
  outdata = GST_BUFFER_DATA(buf) = g_malloc0((frame * 3) / 2);
  GST_BUFFER_SIZE(buf) = (frame * 3) / 2;

  qc_set (qcamsrc->qcam);
  if (!GST_PAD_CAPS (pad)) {
    gst_pad_try_set_caps (pad, gst_caps_new_simple("video/x-raw-yuv",
	  "format",    GST_TYPE_FOURCC, "I420",
	  "width",     G_TYPE_INT, qcamsrc->qcam->width / scale,
	  "height",    G_TYPE_INT, qcamsrc->qcam->height / scale,
	  "framerate", G_TYPE_DOUBLE, 10., NULL));
  }
  scan = qc_scan (qcamsrc->qcam);

  /* FIXME, this doesn't seem to work... */
  /*fixdark(qcamsrc->qcam, scan); */
  
  if (qcamsrc->autoexposure != AE_NONE) 
    qcip_autoexposure(qcamsrc->qcam, scan);

  convert = (qcamsrc->qcam->bpp==4?4:2);

  for (i=frame; i; i--) {
    outdata[i] = scan[i]<<convert; 
  }
  memset (outdata+frame, 128, frame>>1);
  g_free (scan);

  return GST_DATA (buf);
}

static void
gst_qcamsrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstQCamSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_QCAMSRC(object));
  src = GST_QCAMSRC(object);

  switch (prop_id) {
    case ARG_WIDTH:
      qc_setwidth (src->qcam, g_value_get_int (value));
      break;
    case ARG_HEIGHT:
      qc_setheight (src->qcam, g_value_get_int (value));
      break;
    case ARG_BRIGHTNESS:
      qc_setbrightness (src->qcam, g_value_get_int (value));
      break;
    case ARG_WHITEBAL:
      qc_setwhitebal (src->qcam, g_value_get_int (value));
      break;
    case ARG_CONTRAST:
      qc_setcontrast (src->qcam, g_value_get_int (value));
      break;
    case ARG_TOP:
      qc_settop (src->qcam, g_value_get_int (value));
      break;
    case ARG_LEFT:
      qc_setleft (src->qcam, g_value_get_int (value));
      break;
    case ARG_TRANSFER_SCALE:
      qc_settransfer_scale (src->qcam, g_value_get_int (value));
      break;
    case ARG_DEPTH:
      qc_setbitdepth (src->qcam, g_value_get_int (value));
      break;
    case ARG_PORT:
      src->qcam->port = g_value_get_int (value);
      break;
    case ARG_AUTOEXP:
      src->autoexposure = g_value_get_enum (value);
      if (src->autoexposure != AE_NONE) 
        qcip_set_autoexposure_mode (src->autoexposure);
      break;
    default:
      break;
  }
}

static void
gst_qcamsrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstQCamSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_QCAMSRC(object));
  src = GST_QCAMSRC(object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int (value, qc_getwidth (src->qcam));
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, qc_getheight (src->qcam));
      break;
    case ARG_BRIGHTNESS:
      g_value_set_int (value, qc_getbrightness (src->qcam));
      break;
    case ARG_WHITEBAL:
      g_value_set_int (value, qc_getwhitebal (src->qcam));
      break;
    case ARG_CONTRAST:
      g_value_set_int (value, qc_getcontrast (src->qcam));
      break;
    case ARG_TOP:
      g_value_set_int (value, qc_gettop (src->qcam));
      break;
    case ARG_LEFT:
      g_value_set_int (value, qc_getleft (src->qcam));
      break;
    case ARG_TRANSFER_SCALE:
      g_value_set_int (value, qc_gettransfer_scale (src->qcam));
      break;
    case ARG_DEPTH:
      g_value_set_int (value, qc_getbitdepth (src->qcam));
      break;
    case ARG_PORT:
      g_value_set_int (value, src->qcam->port);
      break;
    case ARG_AUTOEXP:
      g_value_set_enum (value, src->autoexposure);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_qcamsrc_change_state (GstElement *element)
{
  g_return_val_if_fail(GST_IS_QCAMSRC(element), FALSE);

  /* if going down into NULL state, close the file if it's open */
  if (GST_STATE_PENDING(element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET(element,GST_QCAMSRC_OPEN))
      gst_qcamsrc_close(GST_QCAMSRC(element));
  /* otherwise (READY or higher) we need to open the sound card */
  } else {
    if (!GST_FLAG_IS_SET(element,GST_QCAMSRC_OPEN)) {
      gst_info ("qcamsrc: opening\n");
      if (!gst_qcamsrc_open(GST_QCAMSRC(element))) {
	gst_info ("qcamsrc: open failed\n");
        return GST_STATE_FAILURE;
      }
    }
  }

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element);

  return GST_STATE_SUCCESS;
}

static gboolean
gst_qcamsrc_open (GstQCamSrc *qcamsrc)
{
  if (qc_open (qcamsrc->qcam)) {
    g_warning("qcamsrc: Cannot open QuickCam.\n");
    return FALSE;
  }

  GST_FLAG_SET(qcamsrc, GST_QCAMSRC_OPEN);

  return TRUE;
}

static void
gst_qcamsrc_close (GstQCamSrc *src)
{
  qc_close (src->qcam);
  GST_FLAG_UNSET(src, GST_QCAMSRC_OPEN);
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "qcamsrc", GST_RANK_NONE, GST_TYPE_QCAMSRC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "qcamsrc",
  "Read from a QuickCam device",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)

