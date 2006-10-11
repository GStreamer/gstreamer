/* GStreamer simple deinterlacing plugin
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
/* based on the Area Based Deinterlacer (for RGB frames)        */
/* (a VirtualDub filter) from Gunnar Thalin <guth@home.se>      */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstdeinterlace.h"
#include <gst/video/video.h>

/* these macros are adapted from videotestsrc, paint_setup_I420() */
#define GST_VIDEO_I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define GST_VIDEO_I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define GST_VIDEO_I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(GST_VIDEO_I420_Y_ROWSTRIDE(width)))/2)

#define GST_VIDEO_I420_Y_OFFSET(w,h) (0)
#define GST_VIDEO_I420_U_OFFSET(w,h) (GST_VIDEO_I420_Y_OFFSET(w,h)+(GST_VIDEO_I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define GST_VIDEO_I420_V_OFFSET(w,h) (GST_VIDEO_I420_U_OFFSET(w,h)+(GST_VIDEO_I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define GST_VIDEO_I420_SIZE(w,h) (GST_VIDEO_I420_V_OFFSET(w,h)+(GST_VIDEO_I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

/* elementfactory information */
static const GstElementDetails deinterlace_details =
GST_ELEMENT_DETAILS ("Deinterlace",
    "Filter/Effect/Video",
    "Deinterlace video",
    "Wim Taymans <wim@fluendo.com>");

#define DEFAULT_DI_AREA_ONLY  FALSE
#define DEFAULT_BLEND         FALSE
#define DEFAULT_THRESHOLD     50
#define DEFAULT_EDGE_DETECT   25

enum
{
  ARG_0,
  ARG_DI_ONLY,
  ARG_BLEND,
  ARG_THRESHOLD,
  ARG_EDGE_DETECT
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

GST_BOILERPLATE (GstDeinterlace, gst_deinterlace, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static void gst_deinterlace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_deinterlace_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_deinterlace_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean gst_deinterlace_stop (GstBaseTransform * trans);
static gboolean gst_deinterlace_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstCaps *gst_deinterlace_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * incaps);

static void
gst_deinterlace_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &deinterlace_details);
}

static void
gst_deinterlace_class_init (GstDeinterlaceClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *basetransform_class;

  gobject_class = (GObjectClass *) klass;
  basetransform_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_deinterlace_set_property;
  gobject_class->get_property = gst_deinterlace_get_property;

  g_object_class_install_property (gobject_class, ARG_DI_ONLY,
      g_param_spec_boolean ("di-area-only", "di-area-only", "di-area-only",
          DEFAULT_DI_AREA_ONLY, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BLEND,
      g_param_spec_boolean ("blend", "blend", "blend", DEFAULT_BLEND,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_THRESHOLD,
      g_param_spec_int ("threshold", "threshold", "threshold", G_MININT,
          G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_EDGE_DETECT,
      g_param_spec_int ("edge-detect", "edge-detect", "edge-detect", G_MININT,
          G_MAXINT, 0, G_PARAM_READWRITE));

  basetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_deinterlace_transform_ip);
  basetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_deinterlace_transform_caps);
  basetransform_class->stop = GST_DEBUG_FUNCPTR (gst_deinterlace_stop);
  basetransform_class->set_caps = GST_DEBUG_FUNCPTR (gst_deinterlace_set_caps);
}

static void
gst_deinterlace_init (GstDeinterlace * filter, GstDeinterlaceClass * klass)
{
  filter->show_deinterlaced_area_only = DEFAULT_DI_AREA_ONLY;
  filter->blend = DEFAULT_BLEND;
  filter->threshold = DEFAULT_THRESHOLD;
  filter->edge_detect = DEFAULT_EDGE_DETECT;
  /*filter->threshold_blend = 0;  */

  filter->src = NULL;
  filter->picsize = 0;
}

static gboolean
gst_deinterlace_stop (GstBaseTransform * trans)
{
  GstDeinterlace *filter;

  filter = GST_DEINTERLACE (trans);

  g_free (filter->src);
  filter->src = NULL;
  filter->picsize = 0;
  filter->width = 0;
  filter->height = 0;

  return TRUE;
}

static GstCaps *
gst_deinterlace_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * incaps)
{
  return gst_caps_ref (incaps);
}

static gboolean
gst_deinterlace_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstDeinterlace *filter;
  GstStructure *s;

  filter = GST_DEINTERLACE (trans);

  g_assert (gst_caps_is_equal_fixed (incaps, outcaps));

  s = gst_caps_get_structure (incaps, 0);
  if (!gst_structure_get_int (s, "width", &filter->width) ||
      !gst_structure_get_int (s, "height", &filter->height)) {
    return FALSE;
  }

  GST_LOG_OBJECT (filter, "width x height = %d x %d", filter->width,
      filter->height);

  if (filter->picsize != GST_VIDEO_I420_SIZE (filter->width, filter->height)) {
    filter->picsize = GST_VIDEO_I420_SIZE (filter->width, filter->height);
    g_free (filter->src);       /* free + alloc avoids memcpy */
    filter->src = g_malloc0 (filter->picsize);
    GST_LOG_OBJECT (filter, "temp buffer size %d", filter->picsize);
  }

  return TRUE;
}

static GstFlowReturn
gst_deinterlace_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstDeinterlace *filter;
  gboolean bShowDeinterlacedAreaOnly;
  gint y0, y1, y2, y3;
  guchar *psrc1, *psrc2, *psrc3, *pdst1, *yuvptr, *src;
  gint iInterlaceValue0, iInterlaceValue1, iInterlaceValue2;
  gint x, y;
  gint y_line;
  guchar *y_dst, *y_src;
  gboolean bBlend;
  gint iThreshold;
  gint iEdgeDetect;
  gint width, height;

  /* g_assert (gst_buffer_is_writable (buf)); */

  filter = GST_DEINTERLACE (trans);

  GST_OBJECT_LOCK (filter);
  bBlend = filter->blend;
  iThreshold = filter->threshold;
  iEdgeDetect = filter->edge_detect;
  bShowDeinterlacedAreaOnly = filter->show_deinterlaced_area_only;
  GST_OBJECT_UNLOCK (filter);

  width = filter->width;
  height = filter->height;
  src = filter->src;
  yuvptr = GST_BUFFER_DATA (buf);

  memcpy (filter->src, yuvptr, filter->picsize);

  y_dst = yuvptr;               /* dst y pointer */
  /* we should not change u,v because one u, v value stands for */
  /* 2 pixels per 2 lines = 4 pixel and we don't want to change */
  /* the color of */

  y_line = GST_VIDEO_I420_Y_ROWSTRIDE (width);
  y_src = src;

  iThreshold = iThreshold * iThreshold * 4;
  /* We don't want an integer overflow in the interlace calculation. */
  if (iEdgeDetect > 180)
    iEdgeDetect = 180;
  iEdgeDetect = iEdgeDetect * iEdgeDetect;

  y1 = 0;                       /* Avoid compiler warning. The value is not used. */
  for (x = 0; x < width; x++) {
    psrc3 = y_src + x;
    y3 = *psrc3;
    psrc2 = psrc3 + y_line;
    y2 = *psrc2;
    pdst1 = y_dst + x;
    iInterlaceValue1 = iInterlaceValue2 = 0;
    for (y = 0; y <= height; y++) {
      psrc1 = psrc2;
      psrc2 = psrc3;
      psrc3 = psrc3 + y_line;
      y0 = y1;
      y1 = y2;
      y2 = y3;
      if (y < height - 1) {
        y3 = *psrc3;
      } else {
        y3 = y1;
      }

      iInterlaceValue0 = iInterlaceValue1;
      iInterlaceValue1 = iInterlaceValue2;

      if (y < height)
        iInterlaceValue2 = ((y1 - y2) * (y3 - y2) -
            ((iEdgeDetect * (y1 - y3) * (y1 - y3)) >> 12)) * 10;
      else
        iInterlaceValue2 = 0;

      if (y > 0) {
        if (iInterlaceValue0 + 2 * iInterlaceValue1 + iInterlaceValue2 >
            iThreshold) {
          if (bBlend) {
            *pdst1 = (unsigned char) ((y0 + 2 * y1 + y2) >> 2);
          } else {
            /* this method seems to work better than blending if the */
            /* quality is pretty bad and the half pics don't fit together */
            if ((y % 2) == 1) { /* if odd simply copy the value */
              *pdst1 = *psrc1;
              /**pdst1 = 0; // FIXME this is for adjusting an initial iThreshold */
            } else {            /* even interpolate the even line (upper + lower)/2 */
              *pdst1 = (unsigned char) ((y0 + y2) >> 1);
              /**pdst1 = 0; // FIXME this is for adjusting an initial iThreshold */
            }
          }
        } else {
          /* so we went below the treshold and therefore we don't have to  */
          /* change anything */
          if (bShowDeinterlacedAreaOnly) {
            /* this is for testing to see how we should tune the treshhold */
            /* and shows as the things that haven't change because the  */
            /* threshhold was to low?? (or shows that everything is ok :-) */
            *pdst1 = 0;         /* blank the point and so the interlac area */
          } else {
            *pdst1 = *psrc1;
          }
        }
        pdst1 = pdst1 + y_line;
      }
    }
  }

  return GST_FLOW_OK;
}

static void
gst_deinterlace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDeinterlace *filter;

  filter = GST_DEINTERLACE (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case ARG_DI_ONLY:
      filter->show_deinterlaced_area_only = g_value_get_boolean (value);
      break;
    case ARG_BLEND:
      filter->blend = g_value_get_boolean (value);
      break;
    case ARG_THRESHOLD:
      filter->threshold = g_value_get_int (value);
      break;
    case ARG_EDGE_DETECT:
      filter->edge_detect = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (filter);
}

static void
gst_deinterlace_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDeinterlace *filter;

  filter = GST_DEINTERLACE (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case ARG_DI_ONLY:
      g_value_set_boolean (value, filter->show_deinterlaced_area_only);
      break;
    case ARG_BLEND:
      g_value_set_boolean (value, filter->blend);
      break;
    case ARG_THRESHOLD:
      g_value_set_int (value, filter->threshold);
      break;
    case ARG_EDGE_DETECT:
      g_value_set_int (value, filter->edge_detect);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (filter);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "deinterlace", GST_RANK_NONE,
          gst_deinterlace_get_type ()))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gstinterlace",
    "Deinterlace video", plugin_init, PACKAGE_VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
