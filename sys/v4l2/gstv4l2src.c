/* G-Streamer Video4linux2 video-capture plugin
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include <sys/time.h>
#include "v4l2src_calls.h"
#include "gstv4l2tuner.h"

GST_DEBUG_CATEGORY (v4l2src_debug);
#define GST_CAT_DEFAULT v4l2src_debug

/* elementfactory details */
static GstElementDetails gst_v4l2src_details = {
  "Video (video4linux2) Source",
  "Source/Video",
  "Reads frames (compressed or uncompressed) from a video4linux2 device",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>"
};

/* V4l2Src signals and args */
enum
{
  SIGNAL_FRAME_CAPTURE,
  SIGNAL_FRAME_DROP,
  SIGNAL_FRAME_INSERT,
  SIGNAL_FRAME_LOST,
  LAST_SIGNAL
};

/* arguments */
enum
{
  ARG_0,
  ARG_NUMBUFS,
  ARG_BUFSIZE,
  ARG_USE_FIXED_FPS
};

guint32 gst_v4l2_formats[] = {
  /* from Linux 2.6.0 videodev2.h */
  V4L2_PIX_FMT_RGB332,          /*  8  RGB-3-3-2     */
  V4L2_PIX_FMT_RGB555,          /* 16  RGB-5-5-5     */
  V4L2_PIX_FMT_RGB565,          /* 16  RGB-5-6-5     */
  V4L2_PIX_FMT_RGB555X,         /* 16  RGB-5-5-5 BE  */
  V4L2_PIX_FMT_RGB565X,         /* 16  RGB-5-6-5 BE  */
  V4L2_PIX_FMT_BGR24,           /* 24  BGR-8-8-8     */
  V4L2_PIX_FMT_RGB24,           /* 24  RGB-8-8-8     */
  V4L2_PIX_FMT_BGR32,           /* 32  BGR-8-8-8-8   */
  V4L2_PIX_FMT_RGB32,           /* 32  RGB-8-8-8-8   */
  V4L2_PIX_FMT_GREY,            /*  8  Greyscale     */
  V4L2_PIX_FMT_YVU410,          /*  9  YVU 4:1:0     */
  V4L2_PIX_FMT_YVU420,          /* 12  YVU 4:2:0     */
  V4L2_PIX_FMT_YUYV,            /* 16  YUV 4:2:2     */
  V4L2_PIX_FMT_UYVY,            /* 16  YUV 4:2:2     */
  V4L2_PIX_FMT_YUV422P,         /* 16  YVU422 planar */
  V4L2_PIX_FMT_YUV411P,         /* 16  YVU411 planar */
  V4L2_PIX_FMT_Y41P,            /* 12  YUV 4:1:1     */
  V4L2_PIX_FMT_NV12,            /* 12  Y/CbCr 4:2:0  */
  V4L2_PIX_FMT_NV21,            /* 12  Y/CrCb 4:2:0  */
  V4L2_PIX_FMT_YUV410,          /*  9  YUV 4:1:0     */
  V4L2_PIX_FMT_YUV420,          /* 12  YUV 4:2:0     */
  V4L2_PIX_FMT_YYUV,            /* 16  YUV 4:2:2     */
  V4L2_PIX_FMT_HI240,           /*  8  8-bit color   */
  V4L2_PIX_FMT_MJPEG,           /* Motion-JPEG       */
  V4L2_PIX_FMT_JPEG,            /* JFIF JPEG         */
  V4L2_PIX_FMT_DV,              /* 1394              */
  V4L2_PIX_FMT_MPEG,            /* MPEG              */
  V4L2_PIX_FMT_WNVA             /* Winnov hw compres */
};

#define GST_V4L2_FORMAT_COUNT (G_N_ELEMENTS (gst_v4l2_formats))

GST_FORMATS_FUNCTION (GstPad *, gst_v4l2src_get_formats,
    GST_FORMAT_TIME, GST_FORMAT_DEFAULT);
GST_QUERY_TYPE_FUNCTION (GstPad *, gst_v4l2src_get_query_types,
    GST_QUERY_POSITION);

/* init functions */
static void gst_v4l2src_class_init (gpointer g_class, gpointer class_data);
static void gst_v4l2src_base_init (gpointer g_class);
static void gst_v4l2src_init (GTypeInstance * instance, gpointer g_class);

/* signal functions */
static void gst_v4l2src_open (GstElement * element, const gchar * device);
static void gst_v4l2src_close (GstElement * element, const gchar * device);

/* pad/buffer functions */
static const GstCaps *gst_v4l2src_get_all_caps (void);
static GstPadLinkReturn gst_v4l2src_link (GstPad * pad, const GstCaps * caps);
static GstCaps *gst_v4l2src_getcaps (GstPad * pad);
static GstCaps *gst_v4l2src_fixate (GstPad * pad, const GstCaps * caps);
static GstData *gst_v4l2src_get (GstPad * pad);
static gboolean gst_v4l2src_src_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_v4l2src_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value);

/* get/set params */
static void gst_v4l2src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_v4l2src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

/* state handling */
static GstElementStateReturn gst_v4l2src_change_state (GstElement * element);

/* set_clock function for A/V sync */
static void gst_v4l2src_set_clock (GstElement * element, GstClock * clock);

static GstElementClass *parent_class = NULL;
static guint gst_v4l2src_signals[LAST_SIGNAL] = { 0 };


GType
gst_v4l2src_get_type (void)
{
  static GType v4l2src_type = 0;

  if (!v4l2src_type) {
    static const GTypeInfo v4l2src_info = {
      sizeof (GstV4l2SrcClass),
      gst_v4l2src_base_init,
      NULL,
      gst_v4l2src_class_init,
      NULL,
      NULL,
      sizeof (GstV4l2Src),
      0,
      gst_v4l2src_init,
      NULL
    };

    v4l2src_type = g_type_register_static (GST_TYPE_V4L2ELEMENT,
        "GstV4l2Src", &v4l2src_info, 0);
    GST_DEBUG_CATEGORY_INIT (v4l2src_debug, "v4l2src", 0, "v4l2src element");
  }
  return v4l2src_type;
}

static void
gst_v4l2src_base_init (gpointer g_class)
{
  GstPadTemplate *template;
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_v4l2src_details);

  template = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_caps_copy (gst_v4l2src_get_all_caps ()));

  gst_element_class_add_pad_template (gstelement_class, template);
}

static void
gst_v4l2src_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  GstV4l2ElementClass *v4l2_class = GST_V4L2ELEMENT_CLASS (g_class);

  parent_class = g_type_class_peek_parent (g_class);

  gobject_class->set_property = gst_v4l2src_set_property;
  gobject_class->get_property = gst_v4l2src_get_property;

  g_object_class_install_property (gobject_class, ARG_NUMBUFS,
      g_param_spec_int ("num_buffers", "num_buffers", "num_buffers",
          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BUFSIZE,
      g_param_spec_int ("buffer_size", "buffer_size", "buffer_size",
          G_MININT, G_MAXINT, 0, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, ARG_USE_FIXED_FPS,
      g_param_spec_boolean ("use_fixed_fps", "Use Fixed FPS",
          "Drop/Insert frames to reach a certain FPS (TRUE) "
          "or adapt FPS to suit the number of frabbed frames",
          TRUE, G_PARAM_READWRITE));

  /* signals */
  gst_v4l2src_signals[SIGNAL_FRAME_CAPTURE] =
      g_signal_new ("frame-capture", G_TYPE_FROM_CLASS (g_class),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstV4l2SrcClass, frame_capture), NULL,
      NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_v4l2src_signals[SIGNAL_FRAME_DROP] =
      g_signal_new ("frame-drop", G_TYPE_FROM_CLASS (g_class),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstV4l2SrcClass, frame_drop), NULL,
      NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_v4l2src_signals[SIGNAL_FRAME_INSERT] =
      g_signal_new ("frame_insert", G_TYPE_FROM_CLASS (g_class),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstV4l2SrcClass, frame_insert), NULL,
      NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_v4l2src_signals[SIGNAL_FRAME_LOST] =
      g_signal_new ("frame-lost", G_TYPE_FROM_CLASS (g_class),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstV4l2SrcClass, frame_lost), NULL,
      NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  gstelement_class->change_state = gst_v4l2src_change_state;

  v4l2_class->open = gst_v4l2src_open;
  v4l2_class->close = gst_v4l2src_close;

  gstelement_class->set_clock = gst_v4l2src_set_clock;
}


static void
gst_v4l2src_init (GTypeInstance * instance, gpointer g_class)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (instance);

  GST_FLAG_SET (GST_ELEMENT (v4l2src), GST_ELEMENT_THREAD_SUGGESTED);

  v4l2src->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_GET_CLASS (v4l2src), "src"), "src");
  gst_element_add_pad (GST_ELEMENT (v4l2src), v4l2src->srcpad);

  gst_pad_set_get_function (v4l2src->srcpad, gst_v4l2src_get);
  gst_pad_set_link_function (v4l2src->srcpad, gst_v4l2src_link);
  gst_pad_set_getcaps_function (v4l2src->srcpad, gst_v4l2src_getcaps);
  gst_pad_set_fixate_function (v4l2src->srcpad, gst_v4l2src_fixate);
  gst_pad_set_convert_function (v4l2src->srcpad, gst_v4l2src_src_convert);
  gst_pad_set_formats_function (v4l2src->srcpad, gst_v4l2src_get_formats);
  gst_pad_set_query_function (v4l2src->srcpad, gst_v4l2src_src_query);
  gst_pad_set_query_type_function (v4l2src->srcpad,
      gst_v4l2src_get_query_types);

  v4l2src->breq.count = 0;

  v4l2src->formats = NULL;

  /* no clock */
  v4l2src->clock = NULL;

  /* fps */
  v4l2src->use_fixed_fps = TRUE;
}


static void
gst_v4l2src_open (GstElement * element, const gchar * device)
{
  gst_v4l2src_fill_format_list (GST_V4L2SRC (element));
}


static void
gst_v4l2src_close (GstElement * element, const gchar * device)
{
  gst_v4l2src_clear_format_list (GST_V4L2SRC (element));
}


static gfloat
gst_v4l2src_get_fps (GstV4l2Src * v4l2src)
{
  v4l2_std_id norm;
  const GList *item;

  if (!v4l2src->use_fixed_fps && v4l2src->clock != NULL && v4l2src->handled > 0) {
    /* try to get time from clock master and calculate fps */
    GstClockTime time = gst_clock_get_time (v4l2src->clock) -
        v4l2src->substract_time;
    return v4l2src->handled * GST_SECOND / time;
  }

  /* if that failed ... */

  if (!GST_V4L2_IS_OPEN (GST_V4L2ELEMENT (v4l2src)))
    return 0.;

  if (!gst_v4l2_get_norm (GST_V4L2ELEMENT (v4l2src), &norm))
    return 0.;
  for (item = GST_V4L2ELEMENT (v4l2src)->norms; item != NULL; item = item->next) {
    GstV4l2TunerNorm *v4l2norm = item->data;

    if (v4l2norm->index == norm)
      return GST_TUNER_NORM (v4l2norm)->fps;
  }

  return 0.;
}

static gboolean
gst_v4l2src_src_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  GstV4l2Src *v4l2src;
  gdouble fps;

  v4l2src = GST_V4L2SRC (gst_pad_get_parent (pad));

  if ((fps = gst_v4l2src_get_fps (v4l2src)) == 0)
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * fps / GST_SECOND;
          break;
        default:
          return FALSE;
      }
      break;

    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / fps;
          break;
        default:
          return FALSE;
      }
      break;

    default:
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_v4l2src_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (gst_pad_get_parent (pad));
  gboolean res = TRUE;
  gdouble fps;

  if ((fps = gst_v4l2src_get_fps (v4l2src)) == 0)
    return FALSE;

  switch (type) {
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_TIME:
          *value = v4l2src->handled * GST_SECOND / fps;
          break;
        case GST_FORMAT_DEFAULT:
          *value = v4l2src->handled;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

static GstStructure *
gst_v4l2src_v4l2fourcc_to_caps (guint32 fourcc)
{
  GstStructure *structure = NULL;

  switch (fourcc) {
    case V4L2_PIX_FMT_MJPEG:   /* Motion-JPEG */
    case V4L2_PIX_FMT_JPEG:    /* JFIF JPEG */
      structure = gst_structure_new ("video/x-jpeg", NULL);
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
    case V4L2_PIX_FMT_YUV422P: /* 16  YVU422 planar */
    case V4L2_PIX_FMT_YUV411P: /* 16  YVU411 planar */
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
    case V4L2_PIX_FMT_Y41P:{
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
          fcc = GST_MAKE_FOURCC ('Y', '4', '1', 'B');
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
      GST_DEBUG ("Unknown fourcc 0x%08x " GST_FOURCC_FORMAT,
          fourcc, GST_FOURCC_ARGS (fourcc));
      break;
  }
#if 0
  gst_caps_set_simple (caps,
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height, "framerate", G_TYPE_DOUBLE, fps, NULL);
#endif
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

static guint32
gst_v4l2_fourcc_from_structure (GstStructure * structure)
{
  guint32 fourcc = 0;
  const gchar *mimetype = gst_structure_get_name (structure);

  if (!strcmp (mimetype, "video/x-raw-yuv") ||
      !strcmp (mimetype, "video/x-raw-rgb")) {
    if (!strcmp (mimetype, "video/x-raw-rgb"))
      fourcc = GST_MAKE_FOURCC ('R', 'G', 'B', ' ');
    else
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
      case GST_MAKE_FOURCC ('R', 'G', 'B', ' '):{
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
      default:
        break;
      }
    }
  } else if (strcmp (mimetype, "video/x-dv") == 0) {
    fourcc = V4L2_PIX_FMT_DV;
  } else if (strcmp (mimetype, "video/x-jpeg") == 0) {
    fourcc = V4L2_PIX_FMT_JPEG;
  }

  return fourcc;
}

static struct v4l2_fmtdesc *
gst_v4l2_caps_to_v4l2fourcc (GstV4l2Src * v4l2src, GstStructure * structure)
{
  return gst_v4l2src_get_format_from_fourcc (v4l2src,
      gst_v4l2_fourcc_from_structure (structure));
}

static const GstCaps *
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
            "width", GST_TYPE_INT_RANGE, 1, 4096,
            "height", GST_TYPE_INT_RANGE, 1, 4096,
            "framerate", GST_TYPE_DOUBLE_RANGE, (double) 0, G_MAXDOUBLE, NULL);

        gst_caps_append_structure (caps, structure);
      }
    }
  }

  return caps;
}

static GstCaps *
gst_v4l2src_fixate (GstPad * pad, const GstCaps * const_caps)
{
  gint i;
  GstStructure *structure;
  G_GNUC_UNUSED gchar *caps_str;
  gboolean changed = FALSE;
  GstCaps *caps = gst_caps_copy (const_caps);

  caps_str = gst_caps_to_string (caps);
  GST_DEBUG_OBJECT (gst_pad_get_parent (pad), "fixating caps %s", caps_str);
  g_free (caps_str);

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);
    changed |=
        gst_caps_structure_fixate_field_nearest_int (structure, "width",
        G_MAXINT);
  }
  if (changed)
    return caps;

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);
    changed |=
        gst_caps_structure_fixate_field_nearest_int (structure, "height",
        G_MAXINT);
  }
  if (changed)
    return caps;

  gst_caps_free (caps);
  return NULL;
}

static GstPadLinkReturn
gst_v4l2src_link (GstPad * pad, const GstCaps * caps)
{
  GstV4l2Src *v4l2src;
  GstV4l2Element *v4l2element;
  struct v4l2_fmtdesc *format;
  int w, h;
  GstStructure *structure;

  v4l2src = GST_V4L2SRC (gst_pad_get_parent (pad));
  v4l2element = GST_V4L2ELEMENT (v4l2src);

  structure = gst_caps_get_structure (caps, 0);

  /* clean up if we still haven't cleaned up our previous
   * capture session */
  if (GST_V4L2_IS_ACTIVE (v4l2element)) {
    if (!gst_v4l2src_capture_deinit (v4l2src))
      return GST_PAD_LINK_REFUSED;
  } else if (!GST_V4L2_IS_OPEN (v4l2element)) {
    return GST_PAD_LINK_DELAYED;
  }

  /* we want our own v4l2 type of fourcc codes */
  if (!(format = gst_v4l2_caps_to_v4l2fourcc (v4l2src, structure))) {
    return GST_PAD_LINK_REFUSED;
  }

  gst_structure_get_int (structure, "width", &w);
  gst_structure_get_int (structure, "height", &h);

  /* we found the pixelformat! - try it out */
  if (gst_v4l2src_set_capture (v4l2src, format, w, h)) {
    if (gst_v4l2src_capture_init (v4l2src)) {
      return GST_PAD_LINK_OK;
    }
  }

  return GST_PAD_LINK_REFUSED;
}


static GstCaps *
gst_v4l2src_getcaps (GstPad * pad)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (gst_pad_get_parent (pad));
  GstCaps *caps;
  struct v4l2_fmtdesc *format;
  int min_w, max_w, min_h, max_h;
  GSList *walk;
  GstStructure *structure;

  if (!GST_V4L2_IS_OPEN (GST_V4L2ELEMENT (v4l2src))) {
    return gst_caps_new_any ();
  }

  /* build our own capslist */
  caps = gst_caps_new_empty ();
  walk = v4l2src->formats;
  while (walk) {
    format = (struct v4l2_fmtdesc *) walk->data;
    walk = g_slist_next (walk);

    /* get size delimiters */
    if (!gst_v4l2src_get_size_limits (v4l2src, format,
            &min_w, &max_w, &min_h, &max_h)) {
      continue;
    }

    /* add to list */
    structure = gst_v4l2src_v4l2fourcc_to_caps (format->pixelformat);

    if (structure) {
      gst_structure_set (structure,
          "width", GST_TYPE_INT_RANGE, min_w, max_w,
          "height", GST_TYPE_INT_RANGE, min_h, max_h,
          "framerate", GST_TYPE_DOUBLE_RANGE, (double) 0, G_MAXDOUBLE, NULL);

      gst_caps_append_structure (caps, structure);
    }
  }

  return caps;
}

static GstData *
gst_v4l2src_get (GstPad * pad)
{
  GstV4l2Src *v4l2src;
  GstBuffer *buf;
  gint i, num = -1;
  gdouble fps = 0;

  v4l2src = GST_V4L2SRC (gst_pad_get_parent (pad));

  if (v4l2src->use_fixed_fps && (fps = gst_v4l2src_get_fps (v4l2src)) == 0) {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS, (NULL),
        ("could not get frame rate for element"));
    return NULL;
  }

  if (v4l2src->need_writes > 0) {
    /* use last frame */
    buf = v4l2src->cached_buffer;
    v4l2src->need_writes--;
  } else {
    GstClockTime time;

    /* grab a frame from the device */
    num = gst_v4l2src_grab_frame (v4l2src);
    if (num == -1)
      return NULL;

    /* to check if v4l2 sets the correct time */
    time = GST_TIMEVAL_TO_TIME (v4l2src->pool->buffers[num].buffer.timestamp);
    if (v4l2src->clock && v4l2src->use_fixed_fps && time != 0) {
      gboolean have_frame = FALSE;

      do {
        /* FIXME: isn't this v4l2 timestamp its own clock?! */
        /* by default, we use the frame once */
        v4l2src->need_writes = 1;

        g_assert (time >= v4l2src->substract_time);
        time -= v4l2src->substract_time;

        /* first check whether we lost any frames according to the device */
        if (v4l2src->last_seq != 0) {
          if (v4l2src->pool->buffers[num].buffer.sequence - v4l2src->last_seq >
              1) {
            v4l2src->need_writes =
                v4l2src->pool->buffers[num].buffer.sequence - v4l2src->last_seq;
            g_signal_emit (G_OBJECT (v4l2src),
                gst_v4l2src_signals[SIGNAL_FRAME_LOST], 0,
                v4l2src->need_writes - 1);
          }
        }
        v4l2src->last_seq = v4l2src->pool->buffers[num].buffer.sequence;

        /* decide how often we're going to write the frame - set
         * v4lmjpegsrc->need_writes to (that-1) and have_frame to TRUE
         * if we're going to write it - else, just continue.
         * 
         * time is generally the system or audio clock. Let's
         * say that we've written one second of audio, then we want
         * to have written one second of video too, within the same
         * timeframe. This means that if time - begin_time = X sec,
         * we want to have written X*fps frames. If we've written
         * more - drop, if we've written less - dup... */
        if (v4l2src->handled * (GST_SECOND / fps) - time >
            1.5 * (GST_SECOND / fps)) {
          /* yo dude, we've got too many frames here! Drop! DROP! */
          v4l2src->need_writes--;       /* -= (v4l2src->handled - (time / fps)); */
          g_signal_emit (G_OBJECT (v4l2src),
              gst_v4l2src_signals[SIGNAL_FRAME_DROP], 0);
        } else if (v4l2src->handled * (GST_SECOND / fps) - time <
            -1.5 * (GST_SECOND / fps)) {
          /* this means we're lagging far behind */
          v4l2src->need_writes++;       /* += ((time / fps) - v4l2src->handled); */
          g_signal_emit (G_OBJECT (v4l2src),
              gst_v4l2src_signals[SIGNAL_FRAME_INSERT], 0);
        }

        if (v4l2src->need_writes > 0) {
          have_frame = TRUE;
          v4l2src->need_writes--;
        } else {
          if (!gst_v4l2src_queue_frame (v4l2src, num))
            return NULL;
          num = gst_v4l2src_grab_frame (v4l2src);
          if (num == -1)
            return NULL;
        }
      } while (!have_frame);
    }

    g_assert (num != -1);
    GST_LOG_OBJECT (v4l2src, "buffer %d needs %d writes", num,
        v4l2src->need_writes + 1);
    i = v4l2src->pool->buffers[num].buffer.bytesused >
        0 ? v4l2src->pool->buffers[num].buffer.bytesused : v4l2src->pool->
        buffers[num].length;
    /* check if this is the last buffer in the queue. If so do a memcpy to put it back asap
       to avoid framedrops and deadlocks because of stupid elements */
    if (gst_atomic_int_read (&v4l2src->pool->refcount) == v4l2src->breq.count) {
      GST_LOG_OBJECT (v4l2src, "using memcpy'd buffer");
      buf = gst_buffer_new_and_alloc (i);
      memcpy (GST_BUFFER_DATA (buf), v4l2src->pool->buffers[num].start, i);
      if (!gst_v4l2src_queue_frame (v4l2src, num)) {
        gst_data_unref (GST_DATA (buf));
        return NULL;
      }
    } else {
      GST_LOG_OBJECT (v4l2src, "using mmap'd buffer");
      buf = gst_buffer_new ();
      GST_BUFFER_DATA (buf) = v4l2src->pool->buffers[num].start;
      GST_BUFFER_SIZE (buf) = i;
      GST_BUFFER_FREE_DATA_FUNC (buf) = gst_v4l2src_free_buffer;
      GST_BUFFER_PRIVATE (buf) = &v4l2src->pool->buffers[num];
      /* no need to be careful here, both are > 0, because the element uses them */
      gst_atomic_int_inc (&v4l2src->pool->buffers[num].refcount);
      gst_atomic_int_inc (&v4l2src->pool->refcount);
    }
    GST_BUFFER_MAXSIZE (buf) = v4l2src->pool->buffers[num].length;
    if (v4l2src->use_fixed_fps) {
      GST_BUFFER_TIMESTAMP (buf) = v4l2src->handled * GST_SECOND / fps;
      GST_BUFFER_DURATION (buf) = GST_SECOND / fps;
    } else {
      /* calculate time based on our own clock */
      GST_BUFFER_TIMESTAMP (buf) =
          GST_TIMEVAL_TO_TIME (v4l2src->pool->buffers[num].buffer.timestamp) -
          v4l2src->substract_time;
    }
    if (v4l2src->need_writes > 0) {
      v4l2src->cached_buffer = buf;
      for (i = 0; i < v4l2src->need_writes; i++) {
        gst_data_ref (GST_DATA (buf));
      }
    }
  }

  v4l2src->handled++;
  g_signal_emit (G_OBJECT (v4l2src), gst_v4l2src_signals[SIGNAL_FRAME_CAPTURE],
      0);

  return GST_DATA (buf);
}

static void
gst_v4l2src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4l2Src *v4l2src;

  g_return_if_fail (GST_IS_V4L2SRC (object));
  v4l2src = GST_V4L2SRC (object);

  switch (prop_id) {
    case ARG_NUMBUFS:
      if (!GST_V4L2_IS_ACTIVE (GST_V4L2ELEMENT (v4l2src))) {
        v4l2src->breq.count = g_value_get_int (value);
      }
      break;

    case ARG_USE_FIXED_FPS:
      if (!GST_V4L2_IS_ACTIVE (GST_V4L2ELEMENT (v4l2src))) {
        v4l2src->use_fixed_fps = g_value_get_boolean (value);
      }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_v4l2src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4l2Src *v4l2src;

  g_return_if_fail (GST_IS_V4L2SRC (object));
  v4l2src = GST_V4L2SRC (object);

  switch (prop_id) {
    case ARG_NUMBUFS:
      g_value_set_int (value, v4l2src->breq.count);
      break;

    case ARG_BUFSIZE:
      g_value_set_int (value, v4l2src->format.fmt.pix.sizeimage);
      break;

    case ARG_USE_FIXED_FPS:
      g_value_set_boolean (value, v4l2src->use_fixed_fps);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstElementStateReturn
gst_v4l2src_change_state (GstElement * element)
{
  GstV4l2Src *v4l2src;
  gint transition = GST_STATE_TRANSITION (element);
  GstElementStateReturn parent_return;
  GTimeVal time;

  g_return_val_if_fail (GST_IS_V4L2SRC (element), GST_STATE_FAILURE);
  v4l2src = GST_V4L2SRC (element);

  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    parent_return = GST_ELEMENT_CLASS (parent_class)->change_state (element);
    if (parent_return != GST_STATE_SUCCESS)
      return parent_return;
  }

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      if (!gst_v4l2src_get_capture (v4l2src))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_PAUSED:
      v4l2src->handled = 0;
      v4l2src->need_writes = 0;
      v4l2src->substract_time = 0;
      /* buffer setup moved to capsnego */
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* queue all buffer, start streaming capture */
      if (!gst_v4l2src_capture_start (v4l2src))
        return GST_STATE_FAILURE;
      g_get_current_time (&time);
      v4l2src->substract_time = GST_TIMEVAL_TO_TIME (time) -
          v4l2src->substract_time;
      v4l2src->last_seq = 0;
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      g_get_current_time (&time);
      v4l2src->substract_time = GST_TIMEVAL_TO_TIME (time) -
          v4l2src->substract_time;
      /* de-queue all queued buffers */
      if (!gst_v4l2src_capture_stop (v4l2src))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* stop capturing, unmap all buffers */
      if (!gst_v4l2src_capture_deinit (v4l2src))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  return GST_STATE_SUCCESS;
}


static void
gst_v4l2src_set_clock (GstElement * element, GstClock * clock)
{
  GST_V4L2SRC (element)->clock = clock;
}
