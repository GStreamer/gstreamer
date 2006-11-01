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
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch v4l2src ! xvimagesink
 * </programlisting>
 * This pipeline shows the video captured from /dev/video0 tv card and for
 * webcams.
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
#include "gstv4l2xoverlay.h"
#include "gstv4l2vidorient.h"

static const GstElementDetails gst_v4l2src_details =
GST_ELEMENT_DETAILS ("Video (video4linux2/raw) Source",
    "Source/Video",
    "Reads raw frames from a video4linux2 (BT8x8) device",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>,"
    " Edgard Lima <edgard.lima@indt.org.br>");


GST_DEBUG_CATEGORY (v4l2src_debug);
#define GST_CAT_DEFAULT v4l2src_debug

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS,
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

/* basesrc methods */
static gboolean gst_v4l2src_start (GstBaseSrc * src);
static gboolean gst_v4l2src_stop (GstBaseSrc * src);
static gboolean gst_v4l2src_set_caps (GstBaseSrc * src, GstCaps * caps);
static GstCaps *gst_v4l2src_get_caps (GstBaseSrc * src);
static GstFlowReturn gst_v4l2src_create (GstPushSrc * src, GstBuffer ** out);

static void gst_v4l2src_fixate (GstPad * pad, GstCaps * caps);

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
  gobject_class->set_property = gst_v4l2src_set_property;
  gobject_class->get_property = gst_v4l2src_get_property;

  gst_v4l2_object_install_properties_helper (gobject_class);

  basesrc_class->get_caps = gst_v4l2src_get_caps;
  basesrc_class->set_caps = gst_v4l2src_set_caps;
  basesrc_class->start = gst_v4l2src_start;
  basesrc_class->stop = gst_v4l2src_stop;

  pushsrc_class->create = gst_v4l2src_create;
}

static void
gst_v4l2src_init (GstV4l2Src * v4l2src, GstV4l2SrcClass * klass)
{
  v4l2src->v4l2object = gst_v4l2_object_new (GST_ELEMENT (v4l2src),
      gst_v4l2_get_input, gst_v4l2_set_input, gst_v4l2src_update_fps);

  v4l2src->breq.count = 0;

  v4l2src->formats = NULL;

  /* fps */
  v4l2src->fps_n = 0;
  v4l2src->fps_d = 1;

  v4l2src->is_capturing = FALSE;

  gst_pad_set_fixatecaps_function (GST_BASE_SRC_PAD (v4l2src),
      gst_v4l2src_fixate);

  gst_base_src_set_format (GST_BASE_SRC (v4l2src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (v4l2src), TRUE);
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
gst_v4l2src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4l2Src *v4l2src;

  g_return_if_fail (GST_IS_V4L2SRC (object));
  v4l2src = GST_V4L2SRC (object);

  if (!gst_v4l2_object_set_property_helper (v4l2src->v4l2object,
          prop_id, value, pspec)) {
    switch (prop_id) {
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
  GstV4l2Src *v4l2src;

  g_return_if_fail (GST_IS_V4L2SRC (object));
  v4l2src = GST_V4L2SRC (object);

  if (!gst_v4l2_object_get_property_helper (v4l2src->v4l2object,
          prop_id, value, pspec)) {
    switch (prop_id) {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
  }
}


/* this function is a bit of a last resort */
static void
gst_v4l2src_fixate (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  gint i;
  G_GNUC_UNUSED gchar *caps_str;

  caps_str = gst_caps_to_string (caps);
  GST_DEBUG_OBJECT (GST_PAD_PARENT (pad), "fixating caps %s", caps_str);
  g_free (caps_str);

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    structure = gst_caps_get_structure (caps, i);
    const GValue *v;

    /* FIXME such sizes? we usually fixate to something in the 320x200
     * range... */
    /* We are fixating to greater possble size (limited to GST_V4L2_MAX_SIZE)
       and framarate closer to 15/2 that is common in web-cams */
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
gst_v4l2src_v4l2fourcc_to_caps (guint32 fourcc)
{
  GstStructure *structure = NULL;

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
    case V4L2_PIX_FMT_NV12:    /* 12  Y/CbCr 4:2:0  */
    case V4L2_PIX_FMT_NV21:    /* 12  Y/CrCb 4:2:0  */
    case V4L2_PIX_FMT_YYUV:    /* 16  YUV 4:2:2     */
    case V4L2_PIX_FMT_HI240:   /*  8  8-bit color   */
      /* FIXME: get correct fourccs here */
      break;
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
    default:
      GST_DEBUG ("Unknown fourcc 0x%08x %" GST_FOURCC_FORMAT,
          fourcc, GST_FOURCC_ARGS (fourcc));
      break;
  }

  return structure;
}

static guint32
gst_v4l2_fourcc_from_structure (GstStructure * structure)
{
  guint32 fourcc = 0;
  const gchar *mimetype = gst_structure_get_name (structure);

  if (!strcmp (mimetype, "video/x-raw-yuv")) {
    gst_structure_get_fourcc (structure, "format", &fourcc);

    switch (fourcc) {
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      case GST_MAKE_FOURCC ('I', 'Y', 'U', 'V'):
        fourcc = V4L2_PIX_FMT_YUV420;
        break;
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
        fourcc = V4L2_PIX_FMT_YUYV;
        break;
      case GST_MAKE_FOURCC ('Y', '4', '1', 'P'):
        fourcc = V4L2_PIX_FMT_Y41P;
        break;
      case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
        fourcc = V4L2_PIX_FMT_UYVY;
        break;
      case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
        fourcc = V4L2_PIX_FMT_YVU420;
        break;
      case GST_MAKE_FOURCC ('Y', '4', '1', 'B'):
        fourcc = V4L2_PIX_FMT_YUV411P;
        break;
      case GST_MAKE_FOURCC ('Y', '4', '2', 'B'):
        fourcc = V4L2_PIX_FMT_YUV422P;
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
  }

  return fourcc;
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

static struct v4l2_fmtdesc *
gst_v4l2_caps_to_v4l2fourcc (GstV4l2Src * v4l2src, GstStructure * structure)
{
  return gst_v4l2src_get_format_from_fourcc (v4l2src,
      gst_v4l2_fourcc_from_structure (structure));
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
      structure = gst_v4l2src_v4l2fourcc_to_caps (gst_v4l2_formats[i]);
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
  GstCaps *caps;
  struct v4l2_fmtdesc *format;
  int min_w, max_w, min_h, max_h;
  GSList *walk;
  GstStructure *structure;
  guint fps_n, fps_d;

  if (!GST_V4L2_IS_OPEN (v4l2src->v4l2object)) {
    return
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD
            (v4l2src)));
  }

  if (!v4l2src->formats)
    gst_v4l2src_fill_format_list (v4l2src);

  /* build our own capslist */
  caps = gst_caps_new_empty ();
  walk = v4l2src->formats;
  if (!gst_v4l2src_get_fps (v4l2src, &fps_n, &fps_d)) {
    GST_DEBUG_OBJECT (v4l2src, "frame rate is unknown.");
    fps_n = 0;
    fps_d = 1;
  }
  while (walk) {
    format = (struct v4l2_fmtdesc *) walk->data;
    walk = g_slist_next (walk);

    /* get size delimiters */
    if (!gst_v4l2src_get_size_limits (v4l2src, format,
            &min_w, &max_w, &min_h, &max_h)) {
      continue;
    }
    /* template, FIXME, why limit if the device reported correct results? */
    /* we are doing it right now to avoid unexpected results */
    min_w = CLAMP (min_w, 1, GST_V4L2_MAX_SIZE);
    min_h = CLAMP (min_h, 1, GST_V4L2_MAX_SIZE);
    max_w = CLAMP (max_w, min_w, GST_V4L2_MAX_SIZE);
    max_h = CLAMP (max_h, min_h, GST_V4L2_MAX_SIZE);

    /* add to list */
    structure = gst_v4l2src_v4l2fourcc_to_caps (format->pixelformat);

    if (structure) {
      gst_structure_set (structure,
          "width", GST_TYPE_INT_RANGE, min_w, max_w,
          "height", GST_TYPE_INT_RANGE, min_h, max_h, NULL);

      /* FIXME, why random range? */
      /* AFAIK: in v4l2 standard we still don't have a good way to enum
         for a range. Currently it is on discussion on v4l2 forum.
         Currently, something more smart could be done for tvcard devices.
         The maximum value could be determined by 'v4l2_standard.frameperiod'
         and minimum also to the same if V4L2_CAP_TIMEPERFRAME is not set. */
      /* another approach for web-cams would be to try to set a very
         high(100/1) and low(1/1) FPSs and get the values returned */
      gst_structure_set (structure, "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, 100, 1, NULL);

      gst_caps_append_structure (caps, structure);
    }
  }

  return caps;
}

static gboolean
gst_v4l2src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstV4l2Src *v4l2src;
  gint w = 0, h = 0;
  GstStructure *structure;
  struct v4l2_fmtdesc *format;
  const GValue *framerate;
  guint fps_n, fps_d;

  v4l2src = GST_V4L2SRC (src);

  /* if we're not open, punt -- we'll get setcaps'd later via negotiate */
  if (!GST_V4L2_IS_OPEN (v4l2src->v4l2object))
    return FALSE;

  /* make sure we stop capturing and dealloc buffers */
  if (GST_V4L2_IS_ACTIVE (v4l2src->v4l2object)) {
    if (!gst_v4l2src_capture_stop (v4l2src))
      return FALSE;
    if (!gst_v4l2src_capture_deinit (v4l2src))
      return FALSE;
  }

  structure = gst_caps_get_structure (caps, 0);

  /* we want our own v4l2 type of fourcc codes */
  if (!(format = gst_v4l2_caps_to_v4l2fourcc (v4l2src, structure))) {
    return FALSE;
  }

  gst_structure_get_int (structure, "width", &w);
  gst_structure_get_int (structure, "height", &h);
  framerate = gst_structure_get_value (structure, "framerate");

  GST_DEBUG_OBJECT (v4l2src, "trying to set_capture %dx%d, format %s",
      w, h, format->description);

  if (framerate) {
    fps_n = gst_value_get_fraction_numerator (framerate);
    fps_d = gst_value_get_fraction_denominator (framerate);
  } else {
    fps_n = 0;
    fps_d = 1;
  }

  if (!gst_v4l2src_set_capture (v4l2src, format, &w, &h, &fps_n, &fps_d)) {
    GST_WARNING_OBJECT (v4l2src, "could not set_capture %dx%d, format %s",
        w, h, format->description);
    return FALSE;
  }

  if (fps_n) {
    gst_structure_set (structure,
        "width", G_TYPE_INT, w,
        "height", G_TYPE_INT, h,
        "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);
  } else {
    gst_structure_set (structure,
        "width", G_TYPE_INT, w, "height", G_TYPE_INT, h, NULL);
  }

  if (!gst_v4l2src_capture_init (v4l2src))
    return FALSE;

  if (!gst_v4l2src_capture_start (v4l2src))
    return FALSE;

  if (v4l2src->fps_n != fps_n || v4l2src->fps_d != fps_d) {
    GST_WARNING_OBJECT (v4l2src,
        "framerate changed after start capturing from %u/%u to %u/%u", fps_n,
        fps_d, v4l2src->fps_n, v4l2src->fps_d);
    if (fps_n) {
      gst_structure_set (structure,
          "width", G_TYPE_INT, w,
          "height", G_TYPE_INT, h,
          "framerate", GST_TYPE_FRACTION, v4l2src->fps_n, v4l2src->fps_d, NULL);
    }
  }
  return TRUE;
}

/* start and stop are not symmetric -- start will open the device, but not start
 * capture. it's setcaps that will start capture, which is called via basesrc's
 * negotiate method. stop will both stop capture and close the device.
 */
static gboolean
gst_v4l2src_start (GstBaseSrc * src)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (src);

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

  if (!gst_v4l2_object_stop (v4l2src->v4l2object))
    return FALSE;

  return TRUE;
}

static GstFlowReturn
gst_v4l2src_get_read (GstV4l2Src * v4l2src, GstBuffer ** buf)
{
  gint amount;
  gint buffersize;

  buffersize = v4l2src->format.fmt.pix.sizeimage;

  do {
    *buf = gst_v4l2src_buffer_new (v4l2src, buffersize, NULL, NULL);

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
    } else
      goto short_read;
  } while (TRUE);

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
short_read:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SYNC,
        (_("Error reading from device '%s'"),
            v4l2src->v4l2object->videodev),
        ("Error read()ing a buffer on device %s: got only %d bytes instead of expected %d.",
            v4l2src->v4l2object->videodev, amount, buffersize));
    gst_buffer_unref (*buf);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2src_get_mmap (GstV4l2Src * v4l2src, GstBuffer ** buf)
{
  gint i, num;

  /* grab a frame from the device, post an error */
  num = gst_v4l2src_grab_frame (v4l2src);
  if (num == -1)
    goto grab_failed;

  i = v4l2src->format.fmt.pix.sizeimage;

  /* check if this is the last buffer in the queue. If so do a memcpy to put it back asap
     to avoid framedrops and deadlocks because of stupid elements */
  if (g_atomic_int_get (&v4l2src->pool->refcount) == v4l2src->breq.count) {
    GST_LOG_OBJECT (v4l2src, "using memcpy'd buffer");

    *buf = gst_v4l2src_buffer_new (v4l2src, i, NULL, NULL);
    memcpy (GST_BUFFER_DATA (*buf), v4l2src->pool->buffers[num].start, i);

    /* posts an error message if something went wrong */
    if (!gst_v4l2src_queue_frame (v4l2src, num))
      goto queue_failed;
  } else {
    GST_LOG_OBJECT (v4l2src, "using mmap'd buffer");
    *buf =
        gst_v4l2src_buffer_new (v4l2src, i, v4l2src->pool->buffers[num].start,
        &v4l2src->pool->buffers[num]);

    /* no need to be careful here, both are > 0, because the element uses them */
    g_atomic_int_inc (&v4l2src->pool->buffers[num].refcount);
    g_atomic_int_inc (&v4l2src->pool->refcount);
  }
  return GST_FLOW_OK;

  /* ERRORS */
grab_failed:
  {
    GST_DEBUG_OBJECT (v4l2src, "failed to grab a frame");
    return GST_FLOW_ERROR;
  }
queue_failed:
  {
    GST_DEBUG_OBJECT (v4l2src, "failed to queue frame");
    gst_buffer_unref (*buf);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2src_create (GstPushSrc * src, GstBuffer ** buf)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (src);
  GstFlowReturn ret;

  if (v4l2src->breq.memory == V4L2_MEMORY_MMAP) {
    ret = gst_v4l2src_get_mmap (v4l2src, buf);
  } else {
    ret = gst_v4l2src_get_read (v4l2src, buf);
  }
  return ret;
}
