/* GStreamer GdkPixbuf overlay
 * Copyright (C) 2012 Tim-Philipp Müller <tim centricular net>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gdkpixbufoverlay
 *
 * The gdkpixbufoverlay element overlays an image loaded from file onto
 * a video stream.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! gdkpixbufoverlay location=image.png ! autovideosink
 * ]|
 * Overlays the image in image.png onto the test video picture produced by
 * videotestsrc.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstgdkpixbufoverlay.h"

GST_DEBUG_CATEGORY_STATIC (gdkpixbufoverlay_debug);
#define GST_CAT_DEFAULT gdkpixbufoverlay_debug

static void gst_gdk_pixbuf_overlay_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_gdk_pixbuf_overlay_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_gdk_pixbuf_overlay_finalize (GObject * object);

static gboolean gst_gdk_pixbuf_overlay_start (GstBaseTransform * trans);
static gboolean gst_gdk_pixbuf_overlay_stop (GstBaseTransform * trans);
static GstFlowReturn
gst_gdk_pixbuf_overlay_transform_ip (GstBaseTransform * trans, GstBuffer * buf);
static gboolean
gst_gdk_pixbuf_overlay_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);

enum
{
  PROP_0,
  PROP_LOCATION
};

#define VIDEO_CAPS \
    GST_VIDEO_CAPS_BGRx ";" \
    GST_VIDEO_CAPS_RGB ";" \
    GST_VIDEO_CAPS_BGR ";" \
    GST_VIDEO_CAPS_RGBx ";" \
    GST_VIDEO_CAPS_xRGB ";" \
    GST_VIDEO_CAPS_xBGR ";" \
    GST_VIDEO_CAPS_RGBA ";" \
    GST_VIDEO_CAPS_BGRA ";" \
    GST_VIDEO_CAPS_ARGB ";" \
    GST_VIDEO_CAPS_ABGR ";" \
    GST_VIDEO_CAPS_YUV ("{I420, YV12, AYUV, YUY2, UYVY, v308, v210," \
        " v216, Y41B, Y42B, Y444, Y800, Y16, NV12, NV21, UYVP, A420," \
        " YUV9, IYU1}")

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS)
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS)
    );

GST_BOILERPLATE (GstGdkPixbufOverlay, gst_gdk_pixbuf_overlay,
    GstVideoFilter, GST_TYPE_VIDEO_FILTER);

static void
gst_gdk_pixbuf_overlay_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_details_simple (element_class,
      "GdkPixbuf Overlay", "Filter/Effect/Video",
      "Overlay an image onto a video stream",
      "Tim-Philipp Müller <tim centricular net>");
}

static void
gst_gdk_pixbuf_overlay_class_init (GstGdkPixbufOverlayClass * klass)
{
  GstBaseTransformClass *basetrans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_gdk_pixbuf_overlay_set_property;
  gobject_class->get_property = gst_gdk_pixbuf_overlay_get_property;
  gobject_class->finalize = gst_gdk_pixbuf_overlay_finalize;

  basetrans_class->start = GST_DEBUG_FUNCPTR (gst_gdk_pixbuf_overlay_start);
  basetrans_class->stop = GST_DEBUG_FUNCPTR (gst_gdk_pixbuf_overlay_stop);
  basetrans_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_gdk_pixbuf_overlay_set_caps);
  basetrans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_gdk_pixbuf_overlay_transform_ip);

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "location",
          "location of image file to overlay", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gdkpixbufoverlay_debug, "gdkpixbufoverlay", 0,
      "debug category for gdkpixbufoverlay element");
}

static void
gst_gdk_pixbuf_overlay_init (GstGdkPixbufOverlay * overlay,
    GstGdkPixbufOverlayClass * overlay_class)
{
  /* nothing to do here for now */
}

void
gst_gdk_pixbuf_overlay_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGdkPixbufOverlay *overlay = GST_GDK_PIXBUF_OVERLAY (object);

  switch (property_id) {
    case PROP_LOCATION:
      g_free (overlay->location);
      overlay->location = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_gdk_pixbuf_overlay_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstGdkPixbufOverlay *overlay = GST_GDK_PIXBUF_OVERLAY (object);

  switch (property_id) {
    case PROP_LOCATION:
      g_value_set_string (value, overlay->location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_gdk_pixbuf_overlay_finalize (GObject * object)
{
  GstGdkPixbufOverlay *overlay = GST_GDK_PIXBUF_OVERLAY (object);

  g_free (overlay->location);
  overlay->location = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_gdk_pixbuf_overlay_load_image (GstGdkPixbufOverlay * overlay, GError ** err)
{
  GdkPixbuf *pixbuf;
  guint8 *pixels, *p;
  gint width, height, stride, w, h;

  pixbuf = gdk_pixbuf_new_from_file (overlay->location, err);

  if (pixbuf == NULL)
    return FALSE;

  if (!gdk_pixbuf_get_has_alpha (pixbuf)) {
    GdkPixbuf *alpha_pixbuf;

    /* FIXME: we could do this much more efficiently ourselves below, but
     * we're lazy for now */
    /* FIXME: perhaps expose substitute_color via properties */
    alpha_pixbuf = gdk_pixbuf_add_alpha (pixbuf, FALSE, 0, 0, 0);
    g_object_unref (pixbuf);
    pixbuf = alpha_pixbuf;
  }

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  stride = gdk_pixbuf_get_rowstride (pixbuf);
  pixels = gdk_pixbuf_get_pixels (pixbuf);

  /* the memory layout in GdkPixbuf is R-G-B-A, we want:
   *  - B-G-R-A on little-endian platforms
   *  - A-R-G-B on big-endian platforms
   */
  for (h = 0; h < height; ++h) {
    p = pixels + (h * stride);
    for (w = 0; w < width; ++w) {
      guint8 tmp;

      /* R-G-B-A ==> B-G-R-A */
      tmp = p[0];
      p[0] = p[2];
      p[2] = tmp;

      if (G_BYTE_ORDER == G_BIG_ENDIAN) {
        /* B-G-R-A ==> A-R-G-B */
        /* we can probably assume sane alignment */
        *((guint32 *) p) = GUINT32_SWAP_LE_BE (*((guint32 *) p));
      }

      p += 4;
    }
  }

  overlay->pixels = gst_buffer_new ();
  GST_BUFFER_DATA (overlay->pixels) = pixels;
  /* assume we have row padding even for the last row */
  GST_BUFFER_SIZE (overlay->pixels) = height * stride;
  /* transfer ownership of pixbuf to buffer */
  GST_BUFFER_MALLOCDATA (overlay->pixels) = (guint8 *) pixbuf;
  GST_BUFFER_FREE_FUNC (overlay->pixels) = (GFreeFunc) g_object_unref;

  overlay->pixels_width = width;
  overlay->pixels_height = height;
  overlay->pixels_stride = stride;

  overlay->update_composition = TRUE;

  GST_INFO_OBJECT (overlay, "Loaded image, %d x %d", width, height);
  return TRUE;
}

static gboolean
gst_gdk_pixbuf_overlay_start (GstBaseTransform * trans)
{
  GstGdkPixbufOverlay *overlay = GST_GDK_PIXBUF_OVERLAY (trans);
  GError *err = NULL;

  if (overlay->location != NULL) {
    if (!gst_gdk_pixbuf_overlay_load_image (overlay, &err))
      goto error_loading_image;

    gst_base_transform_set_passthrough (trans, FALSE);
  } else {
    GST_WARNING_OBJECT (overlay, "no image location set, doing nothing");
    gst_base_transform_set_passthrough (trans, TRUE);
  }

  return TRUE;

/* ERRORS */
error_loading_image:
  {
    GST_ELEMENT_ERROR (overlay, RESOURCE, OPEN_READ,
        ("Could not load overlay image."), ("%s", err->message));
    g_error_free (err);
    return FALSE;
  }
}

static gboolean
gst_gdk_pixbuf_overlay_stop (GstBaseTransform * trans)
{
  GstGdkPixbufOverlay *overlay = GST_GDK_PIXBUF_OVERLAY (trans);

  if (overlay->comp) {
    gst_video_overlay_composition_unref (overlay->comp);
    overlay->comp = NULL;
  }

  gst_buffer_replace (&overlay->pixels, NULL);

  return TRUE;
}

static gboolean
gst_gdk_pixbuf_overlay_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGdkPixbufOverlay *overlay = GST_GDK_PIXBUF_OVERLAY (trans);
  GstVideoFormat video_format;
  int w, h;

  if (!gst_video_format_parse_caps (incaps, &video_format, &w, &h))
    return FALSE;

  overlay->format = video_format;
  overlay->width = w;
  overlay->height = h;
  return TRUE;
}

static void
gst_gdk_pixbuf_overlay_update_composition (GstGdkPixbufOverlay * overlay)
{
  GstVideoOverlayComposition *comp;
  GstVideoOverlayRectangle *rect;

  /* FIXME: add properties for position and render width and height */
  rect = gst_video_overlay_rectangle_new_argb (overlay->pixels,
      overlay->pixels_width, overlay->pixels_height, overlay->pixels_stride,
      0, 0, overlay->pixels_width, overlay->pixels_height,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);

  comp = gst_video_overlay_composition_new (rect);
  gst_video_overlay_rectangle_unref (rect);

  if (overlay->comp)
    gst_video_overlay_composition_unref (overlay->comp);
  overlay->comp = comp;
}

static GstFlowReturn
gst_gdk_pixbuf_overlay_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstGdkPixbufOverlay *overlay;

  overlay = GST_GDK_PIXBUF_OVERLAY (trans);

  if (G_UNLIKELY (overlay->update_composition)) {
    gst_gdk_pixbuf_overlay_update_composition (overlay);
    overlay->update_composition = FALSE;
  }

  gst_video_overlay_composition_blend (overlay->comp, buf);

  return GST_FLOW_OK;
}
