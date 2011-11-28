/* GStreamer
 *
 * Copyright (C) 2006-2009 Lutz Mueller <lutz@topfrose.de>
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
 * SECTION:element-cairorender
 *
 * cairorender encodes a video stream into PDF, SVG, PNG or Postscript
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch videotestsrc num-buffers=3 ! cairorender ! "application/pdf" ! filesink location=test.pdf
 * ]|
 * </refsect2>
 */

#include "gstcairorender.h"

#include <cairo.h>
#include <cairo-features.h>
#ifdef CAIRO_HAS_PS_SURFACE
#include <cairo-ps.h>
#endif
#ifdef CAIRO_HAS_PDF_SURFACE
#include <cairo-pdf.h>
#endif
#ifdef CAIRO_HAS_SVG_SURFACE
#include <cairo-svg.h>
#endif

#include <gst/video/video.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (cairo_render_debug);
#define GST_CAT_DEFAULT cairo_render_debug

static gboolean
gst_cairo_render_event (GstPad * pad, GstEvent * e)
{
  GstCairoRender *c = GST_CAIRO_RENDER (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (e)) {
    case GST_EVENT_EOS:
      if (c->surface)
        cairo_surface_finish (c->surface);
      break;
    default:
      break;
  }
  return gst_pad_event_default (pad, e);
}

static cairo_status_t
write_func (void *closure, const unsigned char *data, unsigned int length)
{
  GstCairoRender *c = GST_CAIRO_RENDER (closure);
  GstBuffer *buf;
  GstFlowReturn r;

  buf = gst_buffer_new ();
  gst_buffer_set_data (buf, (guint8 *) data, length);
  gst_buffer_set_caps (buf, GST_PAD_CAPS (c->src));
  if ((r = gst_pad_push (c->src, buf)) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (c, "Could not pass on buffer: %s.",
        gst_flow_get_name (r));
    return CAIRO_STATUS_WRITE_ERROR;
  }
  return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
read_buffer (void *closure, unsigned char *data, unsigned int length)
{
  GstBuffer *buf = GST_BUFFER (closure);

  if (GST_BUFFER_OFFSET (buf) + length > GST_BUFFER_SIZE (buf))
    return CAIRO_STATUS_READ_ERROR;
  memcpy (data, GST_BUFFER_DATA (buf) + GST_BUFFER_OFFSET (buf), length);
  GST_BUFFER_OFFSET (buf) += length;
  return CAIRO_STATUS_SUCCESS;
}

static gboolean
gst_cairo_render_push_surface (GstCairoRender * c, cairo_surface_t * surface)
{
  cairo_status_t s = 0;
  cairo_t *cr;

  if (!c->surface) {
    s = cairo_surface_write_to_png_stream (surface, write_func, c);
    cairo_surface_destroy (surface);
    if (s != CAIRO_STATUS_SUCCESS) {
      GST_DEBUG_OBJECT (c, "Could not create PNG stream: %s.",
          cairo_status_to_string (s));
      return FALSE;
    }
    return TRUE;
  }

  cr = cairo_create (c->surface);
  cairo_set_source_surface (cr, surface, 0, 0);
  cairo_paint (cr);
  cairo_show_page (cr);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);
  return (TRUE);
}

static GstFlowReturn
gst_cairo_render_chain (GstPad * pad, GstBuffer * buf)
{
  GstCairoRender *c = GST_CAIRO_RENDER (GST_PAD_PARENT (pad));
  cairo_surface_t *s;
  gboolean success;

  if (G_UNLIKELY (c->width <= 0 || c->height <= 0 || c->stride <= 0))
    return GST_FLOW_NOT_NEGOTIATED;

  if (c->png) {
    GST_BUFFER_OFFSET (buf) = 0;
    s = cairo_image_surface_create_from_png_stream (read_buffer, buf);
  } else {
    if (c->format == CAIRO_FORMAT_ARGB32) {
      guint i, j;
      guint8 *data = GST_BUFFER_DATA (buf);

      buf = gst_buffer_make_writable (buf);

      /* Cairo ARGB is pre-multiplied with the alpha
       * value, i.e. 0x80008000 is half transparent
       * green
       */
      for (i = 0; i < c->height; i++) {
        for (j = 0; j < c->width; j++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
          guint8 alpha = data[3];

          data[0] = (data[0] * alpha) >> 8;
          data[1] = (data[1] * alpha) >> 8;
          data[2] = (data[2] * alpha) >> 8;
#else
          guint8 alpha = data[0];

          data[1] = (data[1] * alpha) >> 8;
          data[2] = (data[2] * alpha) >> 8;
          data[3] = (data[3] * alpha) >> 8;
#endif
          data += 4;
        }
      }
    }

    s = cairo_image_surface_create_for_data (GST_BUFFER_DATA (buf),
        c->format, c->width, c->height, c->stride);
  }

  success = gst_cairo_render_push_surface (c, s);
  gst_buffer_unref (buf);
  return success ? GST_FLOW_OK : GST_FLOW_ERROR;
}

static gboolean
gst_cairo_render_setcaps_sink (GstPad * pad, GstCaps * caps)
{
  GstCairoRender *c = GST_CAIRO_RENDER (GST_PAD_PARENT (pad));
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *mime = gst_structure_get_name (s);
  gint fps_n = 0, fps_d = 1;
  gint w, h;

  GST_DEBUG_OBJECT (c, "Got caps (%s).", mime);
  if ((c->png = !strcmp (mime, "image/png")))
    return TRUE;

  /* Width and height */
  if (!gst_structure_get_int (s, "width", &c->width) ||
      !gst_structure_get_int (s, "height", &c->height)) {
    GST_ERROR_OBJECT (c, "Invalid caps");
    return FALSE;
  }

  /* Colorspace */
  if (!strcmp (mime, "video/x-raw-yuv") || !strcmp (mime, "video/x-raw-grey")) {
    c->format = CAIRO_FORMAT_A8;
    c->stride = GST_ROUND_UP_4 (c->width);
  } else if (!strcmp (mime, "video/x-raw-rgb")) {
    gint bpp;

    if (!gst_structure_get_int (s, "bpp", &bpp)) {
      GST_ERROR_OBJECT (c, "Invalid caps");
      return FALSE;
    }

    c->format = (bpp == 32) ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
    c->stride = 4 * c->width;
  } else {
    GST_DEBUG_OBJECT (c, "Unknown mime type '%s'.", mime);
    return FALSE;
  }

  /* Framerate */
  gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d);

  /* Create output caps */
  caps = gst_pad_get_allowed_caps (c->src);
  caps = gst_caps_make_writable (caps);
  gst_caps_truncate (caps);
  s = gst_caps_get_structure (caps, 0);
  mime = gst_structure_get_name (s);
  gst_structure_set (s, "height", G_TYPE_INT, c->height, "width", G_TYPE_INT,
      c->width, "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);

  if (c->surface) {
    cairo_surface_destroy (c->surface);
    c->surface = NULL;
  }

  w = c->width;
  h = c->height;

  GST_DEBUG_OBJECT (c, "Setting src caps %" GST_PTR_FORMAT, caps);
  gst_pad_set_caps (c->src, caps);

#if CAIRO_HAS_PS_SURFACE
  if (!strcmp (mime, "application/postscript")) {
    c->surface = cairo_ps_surface_create_for_stream (write_func, c, w, h);
  } else
#endif
#if CAIRO_HAS_PDF_SURFACE
  if (!strcmp (mime, "application/pdf")) {
    c->surface = cairo_pdf_surface_create_for_stream (write_func, c, w, h);
  } else
#endif
#if CAIRO_HAS_SVG_SURFACE
  if (!strcmp (mime, "image/svg+xml")) {
    c->surface = cairo_svg_surface_create_for_stream (write_func, c, w, h);
  } else
#endif
  {
    gst_caps_unref (caps);
    return FALSE;
  }

  gst_caps_unref (caps);

  return TRUE;
}


#define SIZE_CAPS "width = (int) [ 1, MAX], height = (int) [ 1, MAX] "
#if CAIRO_HAS_PDF_SURFACE
#define PDF_CAPS "application/pdf, " SIZE_CAPS
#else
#define PDF_CAPS
#endif
#if CAIRO_HAS_PDF_SURFACE && (CAIRO_HAS_PS_SURFACE || CAIRO_HAS_SVG_SURFACE || CAIRO_HAS_PNG_FUNCTIONS)
#define JOIN1 ";"
#else
#define JOIN1
#endif
#if CAIRO_HAS_PS_SURFACE
#define PS_CAPS "application/postscript, " SIZE_CAPS
#else
#define PS_CAPS
#endif
#if (CAIRO_HAS_PDF_SURFACE || CAIRO_HAS_PS_SURFACE) && (CAIRO_HAS_SVG_SURFACE || CAIRO_HAS_PNG_FUNCTIONS)
#define JOIN2 ";"
#else
#define JOIN2
#endif
#if CAIRO_HAS_SVG_SURFACE
#define SVG_CAPS "image/svg+xml, " SIZE_CAPS
#else
#define SVG_CAPS
#endif
#if (CAIRO_HAS_PDF_SURFACE || CAIRO_HAS_PS_SURFACE || CAIRO_HAS_SVG_SURFACE) && CAIRO_HAS_PNG_FUNCTIONS
#define JOIN3 ";"
#else
#define JOIN3
#endif
#if CAIRO_HAS_PNG_FUNCTIONS
#define PNG_CAPS "image/png, " SIZE_CAPS
#define PNG_CAPS2 "; image/png, " SIZE_CAPS
#else
#define PNG_CAPS
#define PNG_CAPS2
#endif

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define ARGB_CAPS GST_VIDEO_CAPS_BGRx " ; " GST_VIDEO_CAPS_BGRA " ; "
#else
#define ARGB_CAPS GST_VIDEO_CAPS_xRGB " ; " GST_VIDEO_CAPS_ARGB " ; "
#endif
static GstStaticPadTemplate t_src = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (PDF_CAPS JOIN1 PS_CAPS JOIN2 SVG_CAPS JOIN3 PNG_CAPS));
static GstStaticPadTemplate t_snk = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS (ARGB_CAPS
        GST_VIDEO_CAPS_YUV ("Y800") " ; "
        "video/x-raw-gray, "
        "bpp = 8, "
        "depth = 8, "
        "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE ", " "framerate = " GST_VIDEO_FPS_RANGE
        PNG_CAPS2));

GST_BOILERPLATE (GstCairoRender, gst_cairo_render, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_cairo_render_init (GstCairoRender * c, GstCairoRenderClass * klass)
{
  /* The sink */
  c->snk = gst_pad_new_from_static_template (&t_snk, "sink");
  gst_pad_set_event_function (c->snk, gst_cairo_render_event);
  gst_pad_set_chain_function (c->snk, gst_cairo_render_chain);
  gst_pad_set_setcaps_function (c->snk, gst_cairo_render_setcaps_sink);
  gst_pad_use_fixed_caps (c->snk);
  gst_element_add_pad (GST_ELEMENT (c), c->snk);

  /* The source */
  c->src = gst_pad_new_from_static_template (&t_src, "src");
  gst_pad_use_fixed_caps (c->src);
  gst_element_add_pad (GST_ELEMENT (c), c->src);

  c->width = 0;
  c->height = 0;
  c->stride = 0;
}

static void
gst_cairo_render_base_init (gpointer g_class)
{
  GstElementClass *ec = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (ec, "Cairo encoder",
      "Codec/Encoder", "Encodes streams using Cairo",
      "Lutz Mueller <lutz@topfrose.de>");
  gst_element_class_add_static_pad_template (ec, &t_snk);
  gst_element_class_add_static_pad_template (ec, &t_src);
}

static void
gst_cairo_render_finalize (GObject * object)
{
  GstCairoRender *c = GST_CAIRO_RENDER (object);

  if (c->surface) {
    cairo_surface_destroy (c->surface);
    c->surface = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_cairo_render_class_init (GstCairoRenderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_cairo_render_finalize;

  GST_DEBUG_CATEGORY_INIT (cairo_render_debug, "cairo_render", 0,
      "Cairo encoder");
}
