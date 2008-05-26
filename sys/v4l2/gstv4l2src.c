/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@indt.org.br>
 *
 * gstv4l2src.c: Video4Linux2 source element
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

/**
 * SECTION:element-v4l2src
 *
 * <refsect2>
 * v4l2src can be used to capture video from v4l2 devices, like webcams and tv cards.
 * <title>Example launch lines</title>
 * <para>
 * <programlisting>
 * gst-launch v4l2src ! xvimagesink
 * </programlisting>
 * This pipeline shows the video captured from /dev/video0 tv card and for
 * webcams.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch-0.10 v4l2src ! jpegdec ! xvimagesink
 * </programlisting>
 * This pipeline shows the video captured from a webcam that delivers jpeg
 * images.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/time.h>
#include "v4l2src_calls.h"
#include <unistd.h>

#include "gstv4l2colorbalance.h"
#include "gstv4l2tuner.h"
#if 0                           /* overlay is still not implemented #ifdef HAVE_XVIDEO */
#include "gstv4l2xoverlay.h"
#endif
#include "gstv4l2vidorient.h"

static const GstElementDetails gst_v4l2src_details =
GST_ELEMENT_DETAILS ("Video (video4linux2/raw) Source",
    "Source/Video",
    "Reads raw frames from a video4linux2 (BT8x8) device",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>,"
    " Edgard Lima <edgard.lima@indt.org.br>");

GST_DEBUG_CATEGORY (v4l2src_debug);
#define GST_CAT_DEFAULT v4l2src_debug

#define DEFAULT_PROP_ALWAYS_COPY        TRUE

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS,
  PROP_QUEUE_SIZE,
  PROP_ALWAYS_COPY
};

static const guint32 gst_v4l2_formats[] = {
  /* from Linux 2.6.15 videodev2.h */
  V4L2_PIX_FMT_RGB332,
  V4L2_PIX_FMT_RGB555,
  V4L2_PIX_FMT_RGB565,
  V4L2_PIX_FMT_RGB555X,
  V4L2_PIX_FMT_RGB565X,
  V4L2_PIX_FMT_BGR24,
  V4L2_PIX_FMT_RGB24,
  V4L2_PIX_FMT_BGR32,
  V4L2_PIX_FMT_RGB32,
  V4L2_PIX_FMT_GREY,
  V4L2_PIX_FMT_YVU410,
  V4L2_PIX_FMT_YVU420,
  V4L2_PIX_FMT_YUYV,
  V4L2_PIX_FMT_UYVY,
  V4L2_PIX_FMT_YUV422P,
  V4L2_PIX_FMT_YUV411P,
  V4L2_PIX_FMT_Y41P,

  /* two planes -- one Y, one Cr + Cb interleaved  */
  V4L2_PIX_FMT_NV12,
  V4L2_PIX_FMT_NV21,

  /*  The following formats are not defined in the V4L2 specification */
  V4L2_PIX_FMT_YUV410,
  V4L2_PIX_FMT_YUV420,
  V4L2_PIX_FMT_YYUV,
  V4L2_PIX_FMT_HI240,

  /* see http://www.siliconimaging.com/RGB%20Bayer.htm */
#ifdef V4L2_PIX_FMT_SBGGR8
  V4L2_PIX_FMT_SBGGR8,
#endif

  /* compressed formats */
  V4L2_PIX_FMT_MJPEG,
  V4L2_PIX_FMT_JPEG,
  V4L2_PIX_FMT_DV,
  V4L2_PIX_FMT_MPEG,

  /*  Vendor-specific formats   */
  V4L2_PIX_FMT_WNVA,

#ifdef V4L2_PIX_FMT_SN9C10X
  V4L2_PIX_FMT_SN9C10X,
#endif
#ifdef V4L2_PIX_FMT_PWC1
  V4L2_PIX_FMT_PWC1,
#endif
#ifdef V4L2_PIX_FMT_PWC2
  V4L2_PIX_FMT_PWC2,
#endif
};

#define GST_V4L2_FORMAT_COUNT (G_N_ELEMENTS (gst_v4l2_formats))

GST_IMPLEMENT_V4L2_PROBE_METHODS (GstV4l2SrcClass, gst_v4l2src);
GST_IMPLEMENT_V4L2_COLOR_BALANCE_METHODS (GstV4l2Src, gst_v4l2src);
GST_IMPLEMENT_V4L2_TUNER_METHODS (GstV4l2Src, gst_v4l2src);
#if 0                           /* overlay is still not implemented #ifdef HAVE_XVIDEO */
GST_IMPLEMENT_V4L2_XOVERLAY_METHODS (GstV4l2Src, gst_v4l2src);
#endif
GST_IMPLEMENT_V4L2_VIDORIENT_METHODS (GstV4l2Src, gst_v4l2src);

static gboolean
gst_v4l2src_iface_supported (GstImplementsInterface * iface, GType iface_type)
{
  GstV4l2Object *v4l2object = GST_V4L2SRC (iface)->v4l2object;

#if 0                           /* overlay is still not implemented #ifdef HAVE_XVIDEO */
  g_assert (iface_type == GST_TYPE_TUNER ||
      iface_type == GST_TYPE_X_OVERLAY ||
      iface_type == GST_TYPE_COLOR_BALANCE ||
      iface_type == GST_TYPE_VIDEO_ORIENTATION);
#else
  g_assert (iface_type == GST_TYPE_TUNER ||
      iface_type == GST_TYPE_COLOR_BALANCE ||
      iface_type == GST_TYPE_VIDEO_ORIENTATION);
#endif

  if (v4l2object->video_fd == -1)
    return FALSE;

#if 0                           /* overlay is still not implemented #ifdef HAVE_XVIDEO */
  if (iface_type == GST_TYPE_X_OVERLAY && !GST_V4L2_IS_OVERLAY (v4l2object))
    return FALSE;
#endif

  return TRUE;
}

static void
gst_v4l2src_interface_init (GstImplementsInterfaceClass * klass)
{
  /*
   * default virtual functions 
   */
  klass->supported = gst_v4l2src_iface_supported;
}

void
gst_v4l2src_init_interfaces (GType type)
{
  static const GInterfaceInfo v4l2iface_info = {
    (GInterfaceInitFunc) gst_v4l2src_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo v4l2_tuner_info = {
    (GInterfaceInitFunc) gst_v4l2src_tuner_interface_init,
    NULL,
    NULL,
  };
#if 0                           /* overlay is still not implemented #ifdef HAVE_XVIDEO */
  static const GInterfaceInfo v4l2_xoverlay_info = {
    (GInterfaceInitFunc) gst_v4l2src_xoverlay_interface_init,
    NULL,
    NULL,
  };
#endif
  static const GInterfaceInfo v4l2_colorbalance_info = {
    (GInterfaceInitFunc) gst_v4l2src_color_balance_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo v4l2_videoorientation_info = {
    (GInterfaceInitFunc) gst_v4l2src_video_orientation_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo v4l2_propertyprobe_info = {
    (GInterfaceInitFunc) gst_v4l2src_property_probe_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type,
      GST_TYPE_IMPLEMENTS_INTERFACE, &v4l2iface_info);
  g_type_add_interface_static (type, GST_TYPE_TUNER, &v4l2_tuner_info);
#if 0                           /* overlay is still not implemented #ifdef HAVE_XVIDEO */
  g_type_add_interface_static (type, GST_TYPE_X_OVERLAY, &v4l2_xoverlay_info);
#endif
  g_type_add_interface_static (type,
      GST_TYPE_COLOR_BALANCE, &v4l2_colorbalance_info);
  g_type_add_interface_static (type,
      GST_TYPE_VIDEO_ORIENTATION, &v4l2_videoorientation_info);
  g_type_add_interface_static (type, GST_TYPE_PROPERTY_PROBE,
      &v4l2_propertyprobe_info);
}

GST_BOILERPLATE_FULL (GstV4l2Src, gst_v4l2src, GstPushSrc, GST_TYPE_PUSH_SRC,
    gst_v4l2src_init_interfaces);

static void gst_v4l2src_dispose (GObject * object);
static void gst_v4l2src_finalize (GstV4l2Src * v4l2src);

/* basesrc methods */
static gboolean gst_v4l2src_start (GstBaseSrc * src);
static gboolean gst_v4l2src_stop (GstBaseSrc * src);
static gboolean gst_v4l2src_set_caps (GstBaseSrc * src, GstCaps * caps);
static GstCaps *gst_v4l2src_get_caps (GstBaseSrc * src);
static gboolean gst_v4l2src_query (GstBaseSrc * bsrc, GstQuery * query);
static GstFlowReturn gst_v4l2src_create (GstPushSrc * src, GstBuffer ** out);

static void gst_v4l2src_fixate (GstBaseSrc * basesrc, GstCaps * caps);

static void gst_v4l2src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_v4l2src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_v4l2src_get_all_caps (void);

static void
gst_v4l2src_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  GstV4l2SrcClass *gstv4l2src_class = GST_V4L2SRC_CLASS (g_class);

  gstv4l2src_class->v4l2_class_devices = NULL;

  GST_DEBUG_CATEGORY_INIT (v4l2src_debug, "v4l2src", 0, "V4L2 source element");

  gst_element_class_set_details (gstelement_class, &gst_v4l2src_details);

  gst_element_class_add_pad_template
      (gstelement_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_v4l2src_get_all_caps ()));
}

static void
gst_v4l2src_class_init (GstV4l2SrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *basesrc_class;
  GstPushSrcClass *pushsrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  basesrc_class = GST_BASE_SRC_CLASS (klass);
  pushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->dispose = gst_v4l2src_dispose;
  gobject_class->finalize = (GObjectFinalizeFunc) gst_v4l2src_finalize;
  gobject_class->set_property = gst_v4l2src_set_property;
  gobject_class->get_property = gst_v4l2src_get_property;

  gst_v4l2_object_install_properties_helper (gobject_class);
  g_object_class_install_property (gobject_class, PROP_QUEUE_SIZE,
      g_param_spec_uint ("queue-size", "Queue size",
          "Number of buffers to be enqueud in the driver",
          GST_V4L2_MIN_BUFFERS, GST_V4L2_MAX_BUFFERS, GST_V4L2_MIN_BUFFERS,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_ALWAYS_COPY,
      g_param_spec_boolean ("always-copy", "Always Copy",
          "If the buffer will or not be used directly from mmap",
          DEFAULT_PROP_ALWAYS_COPY, G_PARAM_READWRITE));

  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_v4l2src_get_caps);
  basesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_v4l2src_set_caps);
  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_v4l2src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_v4l2src_stop);
  basesrc_class->query = GST_DEBUG_FUNCPTR (gst_v4l2src_query);
  basesrc_class->fixate = GST_DEBUG_FUNCPTR (gst_v4l2src_fixate);

  pushsrc_class->create = GST_DEBUG_FUNCPTR (gst_v4l2src_create);
}

static void
gst_v4l2src_init (GstV4l2Src * v4l2src, GstV4l2SrcClass * klass)
{
  /* fixme: give an update_fps_function */
  v4l2src->v4l2object = gst_v4l2_object_new (GST_ELEMENT (v4l2src),
      gst_v4l2_get_input, gst_v4l2_set_input, NULL);

  /* number of buffers requested */
  v4l2src->num_buffers = GST_V4L2_MIN_BUFFERS;

  v4l2src->always_copy = DEFAULT_PROP_ALWAYS_COPY;

  v4l2src->formats = NULL;

  v4l2src->is_capturing = FALSE;

  gst_base_src_set_format (GST_BASE_SRC (v4l2src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (v4l2src), TRUE);

  v4l2src->fps_d = 0;
  v4l2src->fps_n = 0;
}


static void
gst_v4l2src_dispose (GObject * object)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (object);

  if (v4l2src->formats) {
    gst_v4l2src_clear_format_list (v4l2src);
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
gst_v4l2src_finalize (GstV4l2Src * v4l2src)
{
  gst_v4l2_object_destroy (v4l2src->v4l2object);

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (v4l2src));
}


static void
gst_v4l2src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (object);

  if (!gst_v4l2_object_set_property_helper (v4l2src->v4l2object,
          prop_id, value, pspec)) {
    switch (prop_id) {
      case PROP_QUEUE_SIZE:
        v4l2src->num_buffers = g_value_get_uint (value);
        break;
      case PROP_ALWAYS_COPY:
        v4l2src->always_copy = g_value_get_boolean (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
  }
}


static void
gst_v4l2src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (object);

  if (!gst_v4l2_object_get_property_helper (v4l2src->v4l2object,
          prop_id, value, pspec)) {
    switch (prop_id) {
      case PROP_QUEUE_SIZE:
        g_value_set_uint (value, v4l2src->num_buffers);
        break;
      case PROP_ALWAYS_COPY:
        g_value_set_boolean (value, v4l2src->always_copy);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
  }
}


/* this function is a bit of a last resort */
static void
gst_v4l2src_fixate (GstBaseSrc * basesrc, GstCaps * caps)
{
  GstStructure *structure;
  gint i;

  GST_DEBUG_OBJECT (basesrc, "fixating caps %" GST_PTR_FORMAT, caps);

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    structure = gst_caps_get_structure (caps, i);
    const GValue *v;

    /* FIXME such sizes? we usually fixate to something in the 320x200
     * range... */
    /* We are fixating to greater possble size (limited to GST_V4L2_MAX_SIZE)
       and framerate closer to 15/2 that is common in web-cams */
    gst_structure_fixate_field_nearest_int (structure, "width",
        GST_V4L2_MAX_SIZE);
    gst_structure_fixate_field_nearest_int (structure, "height",
        GST_V4L2_MAX_SIZE);
    gst_structure_fixate_field_nearest_fraction (structure, "framerate", 15, 2);

    v = gst_structure_get_value (structure, "format");
    if (v && G_VALUE_TYPE (v) != GST_TYPE_FOURCC) {
      guint32 fourcc;

      g_return_if_fail (G_VALUE_TYPE (v) == GST_TYPE_LIST);

      fourcc = gst_value_get_fourcc (gst_value_list_get_value (v, 0));
      gst_structure_set (structure, "format", GST_TYPE_FOURCC, fourcc, NULL);
    }
  }
}

static GstStructure *
gst_v4l2src_v4l2fourcc_to_structure (guint32 fourcc)
{
  GstStructure *structure = NULL;

  /* FIXME: new FourCCs
     camera: ZC0301 PC Camera
     driver: zc0301
     BA81, S910, PWC1, PWC2

     camera:
     driver:
   */

  switch (fourcc) {
    case V4L2_PIX_FMT_MJPEG:   /* Motion-JPEG */
    case V4L2_PIX_FMT_JPEG:    /* JFIF JPEG */
      structure = gst_structure_new ("image/jpeg", NULL);
      break;
    case V4L2_PIX_FMT_RGB332:
    case V4L2_PIX_FMT_RGB555:
    case V4L2_PIX_FMT_RGB555X:
    case V4L2_PIX_FMT_RGB565:
    case V4L2_PIX_FMT_RGB565X:
    case V4L2_PIX_FMT_RGB24:
    case V4L2_PIX_FMT_BGR24:
    case V4L2_PIX_FMT_RGB32:
    case V4L2_PIX_FMT_BGR32:{
      guint depth = 0, bpp = 0;
      gint endianness = 0;
      guint32 r_mask = 0, b_mask = 0, g_mask = 0;

      switch (fourcc) {
        case V4L2_PIX_FMT_RGB332:
          bpp = depth = 8;
          endianness = G_BYTE_ORDER;    /* 'like, whatever' */
          r_mask = 0xe0;
          g_mask = 0x1c;
          b_mask = 0x03;
          break;
        case V4L2_PIX_FMT_RGB555:
        case V4L2_PIX_FMT_RGB555X:
          bpp = 16;
          depth = 15;
          endianness =
              fourcc == V4L2_PIX_FMT_RGB555X ? G_BIG_ENDIAN : G_LITTLE_ENDIAN;
          r_mask = 0x7c00;
          g_mask = 0x03e0;
          b_mask = 0x001f;
          break;
        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_RGB565X:
          bpp = depth = 16;
          endianness =
              fourcc == V4L2_PIX_FMT_RGB565X ? G_BIG_ENDIAN : G_LITTLE_ENDIAN;
          r_mask = 0xf800;
          g_mask = 0x07e0;
          b_mask = 0x001f;
          break;
        case V4L2_PIX_FMT_RGB24:
          bpp = depth = 24;
          endianness = G_BIG_ENDIAN;
          r_mask = 0xff0000;
          g_mask = 0x00ff00;
          b_mask = 0x0000ff;
          break;
        case V4L2_PIX_FMT_BGR24:
          bpp = depth = 24;
          endianness = G_BIG_ENDIAN;
          r_mask = 0x0000ff;
          g_mask = 0x00ff00;
          b_mask = 0xff0000;
          break;
        case V4L2_PIX_FMT_RGB32:
          bpp = depth = 32;
          endianness = G_BIG_ENDIAN;
          r_mask = 0xff000000;
          g_mask = 0x00ff0000;
          b_mask = 0x0000ff00;
          break;
        case V4L2_PIX_FMT_BGR32:
          bpp = depth = 32;
          endianness = G_BIG_ENDIAN;
          r_mask = 0x000000ff;
          g_mask = 0x0000ff00;
          b_mask = 0x00ff0000;
          break;
        default:
          g_assert_not_reached ();
          break;
      }
      structure = gst_structure_new ("video/x-raw-rgb",
          "bpp", G_TYPE_INT, bpp,
          "depth", G_TYPE_INT, depth,
          "red_mask", G_TYPE_INT, r_mask,
          "green_mask", G_TYPE_INT, g_mask,
          "blue_mask", G_TYPE_INT, b_mask,
          "endianness", G_TYPE_INT, endianness, NULL);
      break;
    }
    case V4L2_PIX_FMT_GREY:    /*  8  Greyscale     */
    case V4L2_PIX_FMT_YYUV:    /* 16  YUV 4:2:2     */
    case V4L2_PIX_FMT_HI240:   /*  8  8-bit color   */
      /* FIXME: get correct fourccs here */
      break;
    case V4L2_PIX_FMT_NV12:    /* 12  Y/CbCr 4:2:0  */
    case V4L2_PIX_FMT_NV21:    /* 12  Y/CrCb 4:2:0  */
    case V4L2_PIX_FMT_YVU410:
    case V4L2_PIX_FMT_YUV410:
    case V4L2_PIX_FMT_YUV420:  /* I420/IYUV */
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_Y41P:
    case V4L2_PIX_FMT_YUV422P:
    case V4L2_PIX_FMT_YUV411P:{
      guint32 fcc = 0;

      switch (fourcc) {
        case V4L2_PIX_FMT_NV12:
          fcc = GST_MAKE_FOURCC ('N', 'V', '1', '2');
          break;
        case V4L2_PIX_FMT_NV21:
          fcc = GST_MAKE_FOURCC ('N', 'V', '2', '1');
          break;
        case V4L2_PIX_FMT_YVU410:
          fcc = GST_MAKE_FOURCC ('Y', 'V', 'U', '9');
          break;
        case V4L2_PIX_FMT_YUV410:
          fcc = GST_MAKE_FOURCC ('Y', 'U', 'V', '9');
          break;
        case V4L2_PIX_FMT_YUV420:
          fcc = GST_MAKE_FOURCC ('I', '4', '2', '0');
          break;
        case V4L2_PIX_FMT_YUYV:
          fcc = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
          break;
        case V4L2_PIX_FMT_YVU420:
          fcc = GST_MAKE_FOURCC ('Y', 'V', '1', '2');
          break;
        case V4L2_PIX_FMT_UYVY:
          fcc = GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
          break;
        case V4L2_PIX_FMT_Y41P:
          fcc = GST_MAKE_FOURCC ('Y', '4', '1', 'P');
          break;
        case V4L2_PIX_FMT_YUV411P:
          fcc = GST_MAKE_FOURCC ('Y', '4', '1', 'B');
          break;
        case V4L2_PIX_FMT_YUV422P:
          fcc = GST_MAKE_FOURCC ('Y', '4', '2', 'B');
          break;
        default:
          g_assert_not_reached ();
          break;
      }
      structure = gst_structure_new ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, fcc, NULL);
      break;
    }
    case V4L2_PIX_FMT_DV:
      structure =
          gst_structure_new ("video/x-dv", "systemstream", G_TYPE_BOOLEAN, TRUE,
          NULL);
      break;
    case V4L2_PIX_FMT_MPEG:    /* MPEG          */
      /* someone figure out the MPEG format used... */
      break;
    case V4L2_PIX_FMT_WNVA:    /* Winnov hw compres */
      break;
    case V4L2_PIX_FMT_SBGGR8:
      structure = gst_structure_new ("video/x-raw-bayer", NULL);
      break;
    default:
      GST_DEBUG ("Unknown fourcc 0x%08x %" GST_FOURCC_FORMAT,
          fourcc, GST_FOURCC_ARGS (fourcc));
      break;
  }

  return structure;
}

static struct v4l2_fmtdesc *
gst_v4l2src_get_format_from_fourcc (GstV4l2Src * v4l2src, guint32 fourcc)
{
  struct v4l2_fmtdesc *fmt;
  GSList *walk;

  if (fourcc == 0)
    return NULL;

  walk = v4l2src->formats;
  while (walk) {
    fmt = (struct v4l2_fmtdesc *) walk->data;
    if (fmt->pixelformat == fourcc)
      return fmt;
    /* special case for jpeg */
    if ((fmt->pixelformat == V4L2_PIX_FMT_MJPEG && fourcc == V4L2_PIX_FMT_JPEG)
        || (fmt->pixelformat == V4L2_PIX_FMT_JPEG
            && fourcc == V4L2_PIX_FMT_MJPEG)) {
      return fmt;
    }
    walk = g_slist_next (walk);
  }

  return NULL;
}

static GstCaps *
gst_v4l2src_get_all_caps (void)
{
  static GstCaps *caps = NULL;

  if (caps == NULL) {
    GstStructure *structure;
    guint i;

    caps = gst_caps_new_empty ();
    for (i = 0; i < GST_V4L2_FORMAT_COUNT; i++) {
      structure = gst_v4l2src_v4l2fourcc_to_structure (gst_v4l2_formats[i]);
      if (structure) {
        gst_structure_set (structure,
            "width", GST_TYPE_INT_RANGE, 1, GST_V4L2_MAX_SIZE,
            "height", GST_TYPE_INT_RANGE, 1, GST_V4L2_MAX_SIZE,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 100, 1, NULL);
        gst_caps_append_structure (caps, structure);
      }
    }
  }

  return caps;
}

static GstCaps *
gst_v4l2src_get_caps (GstBaseSrc * src)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (src);
  GstCaps *ret;
  GSList *walk;

  if (!GST_V4L2_IS_OPEN (v4l2src->v4l2object)) {
    /* FIXME: copy? */
    return
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD
            (v4l2src)));
  }

  if (v4l2src->probed_caps)
    return gst_caps_ref (v4l2src->probed_caps);

  if (!v4l2src->formats)
    gst_v4l2src_fill_format_list (v4l2src);

  ret = gst_caps_new_empty ();

  for (walk = v4l2src->formats; walk; walk = walk->next) {
    struct v4l2_fmtdesc *format;
    GstStructure *template;

    format = (struct v4l2_fmtdesc *) walk->data;

    template = gst_v4l2src_v4l2fourcc_to_structure (format->pixelformat);

    if (template) {
      GstCaps *tmp;

      tmp = gst_v4l2src_probe_caps_for_format (v4l2src, format->pixelformat,
          template);
      if (tmp)
        gst_caps_append (ret, tmp);

      gst_structure_free (template);
    } else {
      GST_DEBUG_OBJECT (v4l2src, "unknown format %u", format->pixelformat);
    }
  }

  v4l2src->probed_caps = gst_caps_ref (ret);

  GST_INFO_OBJECT (v4l2src, "probed caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

/* collect data for the given caps
 * @caps: given input caps
 * @format: location for the v4l format
 * @w/@h: location for width and height
 * @fps_n/@fps_d: location for framerate
 * @size: location for expected size of the frame or 0 if unknown
 */
static gboolean
gst_v4l2_get_caps_info (GstV4l2Src * v4l2src, GstCaps * caps,
    struct v4l2_fmtdesc **format, gint * w, gint * h, guint * fps_n,
    guint * fps_d, guint * size)
{
  GstStructure *structure;
  const GValue *framerate;
  guint32 fourcc;
  const gchar *mimetype;
  guint outsize;

  /* default unknown values */
  fourcc = 0;
  outsize = 0;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", w))
    return FALSE;

  if (!gst_structure_get_int (structure, "height", h))
    return FALSE;

  framerate = gst_structure_get_value (structure, "framerate");
  if (!framerate)
    return FALSE;

  *fps_n = gst_value_get_fraction_numerator (framerate);
  *fps_d = gst_value_get_fraction_denominator (framerate);

  mimetype = gst_structure_get_name (structure);

  if (!strcmp (mimetype, "video/x-raw-yuv")) {
    gst_structure_get_fourcc (structure, "format", &fourcc);

    switch (fourcc) {
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      case GST_MAKE_FOURCC ('I', 'Y', 'U', 'V'):
        fourcc = V4L2_PIX_FMT_YUV420;
        outsize = GST_ROUND_UP_4 (*w) * GST_ROUND_UP_2 (*h);
        outsize += 2 * ((GST_ROUND_UP_8 (*w) / 2) * (GST_ROUND_UP_2 (*h) / 2));
        break;
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
        fourcc = V4L2_PIX_FMT_YUYV;
        outsize = (GST_ROUND_UP_2 (*w) * 2) * *h;
        break;
      case GST_MAKE_FOURCC ('Y', '4', '1', 'P'):
        fourcc = V4L2_PIX_FMT_Y41P;
        outsize = (GST_ROUND_UP_2 (*w) * 2) * *h;
        break;
      case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
        fourcc = V4L2_PIX_FMT_UYVY;
        outsize = (GST_ROUND_UP_2 (*w) * 2) * *h;
        break;
      case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
        fourcc = V4L2_PIX_FMT_YVU420;
        outsize = GST_ROUND_UP_4 (*w) * GST_ROUND_UP_2 (*h);
        outsize += 2 * ((GST_ROUND_UP_8 (*w) / 2) * (GST_ROUND_UP_2 (*h) / 2));
        break;
      case GST_MAKE_FOURCC ('Y', '4', '1', 'B'):
        fourcc = V4L2_PIX_FMT_YUV411P;
        outsize = GST_ROUND_UP_4 (*w) * *h;
        outsize += 2 * ((GST_ROUND_UP_8 (*w) / 4) * *h);
        break;
      case GST_MAKE_FOURCC ('Y', '4', '2', 'B'):
        fourcc = V4L2_PIX_FMT_YUV422P;
        outsize = GST_ROUND_UP_4 (*w) * *h;
        outsize += 2 * ((GST_ROUND_UP_8 (*w) / 2) * *h);
        break;
      case GST_MAKE_FOURCC ('N', 'V', '1', '2'):
        fourcc = V4L2_PIX_FMT_NV12;
        outsize = GST_ROUND_UP_4 (*w) * GST_ROUND_UP_2 (*h);
        outsize += (GST_ROUND_UP_4 (*w) * *h) / 2;
        break;
      case GST_MAKE_FOURCC ('N', 'V', '2', '1'):
        fourcc = V4L2_PIX_FMT_NV21;
        outsize = GST_ROUND_UP_4 (*w) * GST_ROUND_UP_2 (*h);
        outsize += (GST_ROUND_UP_4 (*w) * *h) / 2;
        break;
    }
  } else if (!strcmp (mimetype, "video/x-raw-rgb")) {
    gint depth, endianness, r_mask;

    gst_structure_get_int (structure, "depth", &depth);
    gst_structure_get_int (structure, "endianness", &endianness);
    gst_structure_get_int (structure, "red_mask", &r_mask);

    switch (depth) {
      case 8:
        fourcc = V4L2_PIX_FMT_RGB332;
        break;
      case 15:
        fourcc = (endianness == G_LITTLE_ENDIAN) ?
            V4L2_PIX_FMT_RGB555 : V4L2_PIX_FMT_RGB555X;
        break;
      case 16:
        fourcc = (endianness == G_LITTLE_ENDIAN) ?
            V4L2_PIX_FMT_RGB565 : V4L2_PIX_FMT_RGB565X;
        break;
      case 24:
        fourcc = (r_mask == 0xFF) ? V4L2_PIX_FMT_BGR24 : V4L2_PIX_FMT_RGB24;
        break;
      case 32:
        fourcc = (r_mask == 0xFF) ? V4L2_PIX_FMT_BGR32 : V4L2_PIX_FMT_RGB32;
        break;
    }
  } else if (strcmp (mimetype, "video/x-dv") == 0) {
    fourcc = V4L2_PIX_FMT_DV;
  } else if (strcmp (mimetype, "image/jpeg") == 0) {
    fourcc = V4L2_PIX_FMT_JPEG;
  } else if (strcmp (mimetype, "video/x-raw-bayer") == 0) {
    fourcc = V4L2_PIX_FMT_SBGGR8;
  }

  if (fourcc == 0)
    return FALSE;

  *format = gst_v4l2src_get_format_from_fourcc (v4l2src, fourcc);
  *size = outsize;

  return TRUE;
}

static gboolean
gst_v4l2src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstV4l2Src *v4l2src;
  gint w = 0, h = 0;
  struct v4l2_fmtdesc *format;
  guint fps_n, fps_d;
  guint size;

  v4l2src = GST_V4L2SRC (src);

  /* if we're not open, punt -- we'll get setcaps'd later via negotiate */
  if (!GST_V4L2_IS_OPEN (v4l2src->v4l2object))
    return FALSE;

  /* make sure we stop capturing and dealloc buffers */
  if (GST_V4L2_IS_ACTIVE (v4l2src->v4l2object)) {
    /* both will throw an element-error on failure */
    if (!gst_v4l2src_capture_stop (v4l2src))
      return FALSE;
    if (!gst_v4l2src_capture_deinit (v4l2src))
      return FALSE;
  }

  /* we want our own v4l2 type of fourcc codes */
  if (!gst_v4l2_get_caps_info (v4l2src, caps, &format, &w, &h, &fps_n, &fps_d,
          &size)) {
    GST_DEBUG_OBJECT (v4l2src,
        "can't get capture format from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  GST_DEBUG_OBJECT (v4l2src, "trying to set_capture %dx%d at %d/%d fps, "
      "format %s", w, h, fps_n, fps_d, format->description);

  if (!gst_v4l2src_set_capture (v4l2src, format->pixelformat, w, h, fps_n,
          fps_d))
    /* error already posted */
    return FALSE;

  if (!gst_v4l2src_capture_init (v4l2src, caps))
    return FALSE;

  if (!gst_v4l2src_capture_start (v4l2src))
    return FALSE;

  /* now store the expected output size */
  v4l2src->frame_byte_size = size;

  return TRUE;
}

static gboolean
gst_v4l2src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstV4l2Src *src;
  gboolean res = FALSE;

  src = GST_V4L2SRC (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:{
      GstClockTime min_latency, max_latency;

      /* device must be open */
      if (!GST_V4L2_IS_OPEN (src->v4l2object))
        goto done;

      /* we must have a framerate */
      if (src->fps_n <= 0 || src->fps_d <= 0)
        goto done;

      /* min latency is the time to capture one frame */
      min_latency =
          gst_util_uint64_scale_int (GST_SECOND, src->fps_d, src->fps_n);

      /* max latency is total duration of the frame buffer */
      /* FIXME: what to use here? */
      max_latency = 1 * min_latency;

      GST_DEBUG_OBJECT (bsrc,
          "report latency min %" GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

      /* we are always live, the min latency is 1 frame and the max latency is
       * the complete buffer of frames. */
      gst_query_set_latency (query, TRUE, min_latency, max_latency);

      res = TRUE;
      break;
    }
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      break;
  }

done:

  return res;
}

/* start and stop are not symmetric -- start will open the device, but not start
 * capture. it's setcaps that will start capture, which is called via basesrc's
 * negotiate method. stop will both stop capture and close the device.
 */
static gboolean
gst_v4l2src_start (GstBaseSrc * src)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (src);

  /* open the device */
  if (!gst_v4l2_object_start (v4l2src->v4l2object))
    return FALSE;

  v4l2src->offset = 0;

  return TRUE;
}

static gboolean
gst_v4l2src_stop (GstBaseSrc * src)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (src);

  if (GST_V4L2_IS_ACTIVE (v4l2src->v4l2object)
      && !gst_v4l2src_capture_stop (v4l2src))
    return FALSE;

  if (v4l2src->v4l2object->buffer != NULL) {
    if (!gst_v4l2src_capture_deinit (v4l2src))
      return FALSE;
  }

  /* close the device */
  if (!gst_v4l2_object_stop (v4l2src->v4l2object))
    return FALSE;

  v4l2src->fps_d = 0;
  v4l2src->fps_n = 0;

  return TRUE;
}

static GstFlowReturn
gst_v4l2src_get_read (GstV4l2Src * v4l2src, GstBuffer ** buf)
{
  gint amount;
  gint buffersize;

  buffersize = v4l2src->frame_byte_size;

  *buf = gst_buffer_new_and_alloc (buffersize);

  do {
    amount =
        read (v4l2src->v4l2object->video_fd, GST_BUFFER_DATA (*buf),
        buffersize);
    if (amount == buffersize) {
      break;
    } else if (amount == -1) {
      if (errno == EAGAIN || errno == EINTR) {
        continue;
      } else
        goto read_error;
    } else {
      /* short reads can happen if a signal interrupts the read */
      continue;
    }
  } while (TRUE);

  GST_BUFFER_OFFSET (*buf) = v4l2src->offset++;
  GST_BUFFER_OFFSET_END (*buf) = v4l2src->offset;
  /* timestamps, LOCK to get clock and base time. */
  {
    GstClock *clock;
    GstClockTime timestamp;

    GST_OBJECT_LOCK (v4l2src);
    if ((clock = GST_ELEMENT_CLOCK (v4l2src))) {
      /* we have a clock, get base time and ref clock */
      timestamp = GST_ELEMENT (v4l2src)->base_time;
      gst_object_ref (clock);
    } else {
      /* no clock, can't set timestamps */
      timestamp = GST_CLOCK_TIME_NONE;
    }
    GST_OBJECT_UNLOCK (v4l2src);

    if (clock) {
      GstClockTime latency;

      /* the time now is the time of the clock minus the base time */
      timestamp = gst_clock_get_time (clock) - timestamp;
      gst_object_unref (clock);

      latency =
          gst_util_uint64_scale_int (GST_SECOND, v4l2src->fps_d,
          v4l2src->fps_n);

      if (timestamp > latency)
        timestamp -= latency;
      else
        timestamp = 0;
    }

    /* FIXME: use the timestamp from the buffer itself! */
    GST_BUFFER_TIMESTAMP (*buf) = timestamp;
  }

  return GST_FLOW_OK;

  /* ERRORS */
read_error:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SYNC,
        (_("Error read()ing %d bytes on device '%s'."),
            buffersize, v4l2src->v4l2object->videodev), GST_ERROR_SYSTEM);
    gst_buffer_unref (*buf);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2src_get_mmap (GstV4l2Src * v4l2src, GstBuffer ** buf)
{
  GstBuffer *temp;
  GstFlowReturn ret;
  guint size;
  guint count;

  count = 0;

again:
  ret = gst_v4l2src_grab_frame (v4l2src, &temp);
  if (ret != GST_FLOW_OK)
    goto done;

  if (v4l2src->frame_byte_size > 0) {
    size = GST_BUFFER_SIZE (temp);

    /* if size does not match what we expected, try again */
    if (size != v4l2src->frame_byte_size) {
      GST_ELEMENT_WARNING (v4l2src, RESOURCE, READ,
          (_("Got unexpected frame size of %u instead of %u."),
              size, v4l2src->frame_byte_size), (NULL));
      gst_buffer_unref (temp);
      if (count++ > 50)
        goto size_error;

      goto again;
    }
  }

  *buf = temp;
done:
  return ret;

  /* ERRORS */
size_error:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("Error read()ing %d bytes on device '%s'."),
            v4l2src->frame_byte_size, v4l2src->v4l2object->videodev), (NULL));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2src_create (GstPushSrc * src, GstBuffer ** buf)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (src);
  GstFlowReturn ret;

  if (v4l2src->use_mmap) {
    ret = gst_v4l2src_get_mmap (v4l2src, buf);
  } else {
    ret = gst_v4l2src_get_read (v4l2src, buf);
  }
  return ret;
}
