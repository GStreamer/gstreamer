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

/**
 * SECTION:element-alphacolor
 *
 * The alphacolor element does memory-efficient (in-place) colourspace
 * conversion from RGBA to AYUV or AYUV to RGBA while preserving the
 * alpha channel.
 *
 * Sample pipeline:
 * |[
 * gst-launch videotestsrc ! "video/x-raw-yuv,format=(fourcc)AYUV" ! \
 *   alphacolor ! "video/x-raw-rgb" ! ffmpegcolorspace ! autovideosink
 * ]|
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstalphacolor.h"

#include <gst/gst.h>
#include <gst/video/video.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (alpha_color_debug);
#define GST_CAT_DEFAULT alpha_color_debug

/* elementfactory information */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_ABGR ";"
        GST_VIDEO_CAPS_YUV ("AYUV"))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_ABGR ";"
        GST_VIDEO_CAPS_YUV ("AYUV"))
    );

GST_BOILERPLATE (GstAlphaColor, gst_alpha_color, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER);

static GstCaps *gst_alpha_color_transform_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_alpha_color_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_alpha_color_transform_ip (GstBaseTransform * btrans,
    GstBuffer * inbuf);

static void
gst_alpha_color_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Alpha color filter",
      "Filter/Converter/Video",
      "ARGB from/to AYUV colorspace conversion preserving the alpha channel",
      "Wim Taymans <wim@fluendo.com>");

  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);
}

static void
gst_alpha_color_class_init (GstAlphaColorClass * klass)
{
  GstBaseTransformClass *gstbasetransform_class =
      (GstBaseTransformClass *) klass;

  gstbasetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_alpha_color_transform_caps);
  gstbasetransform_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_alpha_color_set_caps);
  gstbasetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_alpha_color_transform_ip);

  GST_DEBUG_CATEGORY_INIT (alpha_color_debug, "alphacolor", 0,
      "ARGB<->AYUV colorspace conversion preserving the alpha channels");
}

static void
gst_alpha_color_init (GstAlphaColor * alpha, GstAlphaColorClass * g_class)
{
  GstBaseTransform *btrans = GST_BASE_TRANSFORM (alpha);

  btrans->always_in_place = TRUE;
}

static GstCaps *
gst_alpha_color_transform_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps)
{
  const GstCaps *tmpl_caps = NULL;
  GstCaps *result = NULL, *local_caps = NULL;
  guint i;

  local_caps = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure =
        gst_structure_copy (gst_caps_get_structure (caps, i));

    /* Remove any specific parameter from the structure */
    gst_structure_remove_field (structure, "format");
    gst_structure_remove_field (structure, "endianness");
    gst_structure_remove_field (structure, "depth");
    gst_structure_remove_field (structure, "bpp");
    gst_structure_remove_field (structure, "red_mask");
    gst_structure_remove_field (structure, "green_mask");
    gst_structure_remove_field (structure, "blue_mask");
    gst_structure_remove_field (structure, "alpha_mask");
    gst_structure_remove_field (structure, "color-matrix");
    gst_structure_remove_field (structure, "chroma-site");

    gst_structure_set_name (structure, "video/x-raw-rgb");
    gst_caps_append_structure (local_caps, gst_structure_copy (structure));
    gst_structure_set_name (structure, "video/x-raw-yuv");
    gst_caps_append_structure (local_caps, structure);
  }

  /* Get the appropriate template */
  if (direction == GST_PAD_SINK) {
    tmpl_caps = gst_static_pad_template_get_caps (&src_template);
  } else if (direction == GST_PAD_SRC) {
    tmpl_caps = gst_static_pad_template_get_caps (&sink_template);
  }

  /* Intersect with our template caps */
  result = gst_caps_intersect (local_caps, tmpl_caps);

  gst_caps_unref (local_caps);
  gst_caps_do_simplify (result);

  GST_LOG_OBJECT (btrans, "transformed %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT,
      caps, result);

  return result;
}

/* Generated by -bad/ext/cog/generate_tables */
static const int cog_ycbcr_to_rgb_matrix_8bit_hdtv[] = {
  298, 0, 459, -63514,
  298, -55, -136, 19681,
  298, 541, 0, -73988,
};

static const int cog_ycbcr_to_rgb_matrix_8bit_sdtv[] = {
  298, 0, 409, -57068,
  298, -100, -208, 34707,
  298, 516, 0, -70870,
};

static const gint cog_rgb_to_ycbcr_matrix_8bit_hdtv[] = {
  47, 157, 16, 4096,
  -26, -87, 112, 32768,
  112, -102, -10, 32768,
};

static const gint cog_rgb_to_ycbcr_matrix_8bit_sdtv[] = {
  66, 129, 25, 4096,
  -38, -74, 112, 32768,
  112, -94, -18, 32768,
};

static const gint cog_ycbcr_sdtv_to_ycbcr_hdtv_matrix_8bit[] = {
  256, -30, -53, 10600,
  0, 261, 29, -4367,
  0, 19, 262, -3289,
};

static const gint cog_ycbcr_hdtv_to_ycbcr_sdtv_matrix_8bit[] = {
  256, 25, 49, -9536,
  0, 253, -28, 3958,
  0, -19, 252, 2918,
};

#define DEFINE_ARGB_AYUV_FUNCTIONS(name, A, R, G, B) \
static void \
transform_##name##_ayuv (guint8 * data, gint size, const gint *matrix) \
{ \
  gint y, u, v; \
  gint yc[4]; \
  gint uc[4]; \
  gint vc[4]; \
  \
  memcpy (yc, matrix, 4 * sizeof (gint)); \
  memcpy (uc, matrix + 4, 4 * sizeof (gint)); \
  memcpy (vc, matrix + 8, 4 * sizeof (gint)); \
  \
  while (size > 0) { \
    y = (data[R] * yc[0] + data[G] * yc[1] + data[B] * yc[2] + yc[3]) >> 8; \
    u = (data[R] * uc[0] + data[G] * uc[1] + data[B] * uc[2] + uc[3]) >> 8; \
    v = (data[R] * vc[0] + data[G] * vc[1] + data[B] * vc[2] + vc[3]) >> 8; \
    \
    data[0] = data[A]; \
    data[1] = y; \
    data[2] = u; \
    data[3] = v; \
    \
    data += 4; \
    size -= 4; \
  } \
} \
\
static void \
transform_ayuv_##name (guint8 * data, gint size, const gint *matrix) \
{ \
  gint r, g, b; \
  gint rc[4]; \
  gint gc[4]; \
  gint bc[4]; \
  \
  memcpy (rc, matrix, 4 * sizeof (gint)); \
  memcpy (gc, matrix + 4, 4 * sizeof (gint)); \
  memcpy (bc, matrix + 8, 4 * sizeof (gint)); \
  \
  while (size > 0) { \
    r = (data[1] * rc[0] + data[2] * rc[1] + data[3] * rc[2] + rc[3]) >> 8; \
    g = (data[1] * gc[0] + data[2] * gc[1] + data[3] * gc[2] + gc[3]) >> 8; \
    b = (data[1] * bc[0] + data[2] * bc[1] + data[3] * bc[2] + bc[3]) >> 8; \
    \
    data[A] = data[0]; \
    data[R] = CLAMP (r, 0, 255); \
    data[G] = CLAMP (g, 0, 255); \
    data[B] = CLAMP (b, 0, 255); \
    \
    data += 4; \
    size -= 4; \
  } \
}

DEFINE_ARGB_AYUV_FUNCTIONS (rgba, 3, 0, 1, 2);
DEFINE_ARGB_AYUV_FUNCTIONS (bgra, 3, 2, 1, 0);
DEFINE_ARGB_AYUV_FUNCTIONS (argb, 0, 1, 2, 3);
DEFINE_ARGB_AYUV_FUNCTIONS (abgr, 0, 3, 2, 1);

static void
transform_ayuv_ayuv (guint8 * data, gint size, const gint * matrix)
{
  gint y, u, v;
  gint yc[4];
  gint uc[4];
  gint vc[4];

  if (matrix == NULL)
    return;

  memcpy (yc, matrix, 4 * sizeof (gint));
  memcpy (uc, matrix + 4, 4 * sizeof (gint));
  memcpy (vc, matrix + 8, 4 * sizeof (gint));

  while (size > 0) {
    y = (data[1] * yc[0] + data[2] * yc[1] + data[3] * yc[2] + yc[3]) >> 8;
    u = (data[1] * uc[0] + data[2] * uc[1] + data[3] * uc[2] + uc[3]) >> 8;
    v = (data[1] * vc[0] + data[2] * vc[1] + data[3] * vc[2] + vc[3]) >> 8;

    data[1] = y;
    data[2] = u;
    data[3] = v;

    data += 4;
    size -= 4;
  }
}

static void
transform_argb_bgra (guint8 * data, gint size, const gint * matrix)
{
  gint r, g, b;

  while (size > 0) {
    r = data[1];
    g = data[2];
    b = data[3];

    data[3] = data[0];
    data[0] = b;
    data[1] = g;
    data[2] = r;

    data += 4;
    size -= 4;
  }
}

#define transform_abgr_rgba transform_argb_bgra

static void
transform_argb_abgr (guint8 * data, gint size, const gint * matrix)
{
  gint r, g, b;

  while (size > 0) {
    r = data[1];
    g = data[2];
    b = data[3];

    /* data[0] = data[0]; */
    data[1] = b;
    data[2] = g;
    data[3] = r;

    data += 4;
    size -= 4;
  }
}

#define transform_abgr_argb transform_argb_abgr

static void
transform_rgba_bgra (guint8 * data, gint size, const gint * matrix)
{
  gint r, g, b;

  while (size > 0) {
    r = data[0];
    g = data[1];
    b = data[2];

    /* data[3] = data[3] */ ;
    data[0] = b;
    data[1] = g;
    data[2] = r;

    data += 4;
    size -= 4;
  }
}

#define transform_bgra_rgba transform_rgba_bgra

static void
transform_argb_rgba (guint8 * data, gint size, const gint * matrix)
{
  gint r, g, b;

  while (size > 0) {
    r = data[1];
    g = data[2];
    b = data[3];

    data[3] = data[0];
    data[0] = r;
    data[1] = g;
    data[2] = b;

    data += 4;
    size -= 4;
  }
}

#define transform_abgr_bgra transform_argb_rgba

static void
transform_bgra_argb (guint8 * data, gint size, const gint * matrix)
{
  gint r, g, b;

  while (size > 0) {
    r = data[2];
    g = data[1];
    b = data[0];

    data[0] = data[3];
    data[1] = r;
    data[2] = g;
    data[3] = b;

    data += 4;
    size -= 4;
  }
}

#define transform_rgba_abgr transform_bgra_argb

static void
transform_rgba_argb (guint8 * data, gint size, const gint * matrix)
{
  gint r, g, b;

  while (size > 0) {
    r = data[0];
    g = data[1];
    b = data[2];

    data[0] = data[3];
    data[1] = r;
    data[2] = g;
    data[3] = b;

    data += 4;
    size -= 4;
  }
}

#define transform_bgra_abgr transform_rgba_argb

static gboolean
gst_alpha_color_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstAlphaColor *alpha = GST_ALPHA_COLOR (btrans);
  gboolean ret;
  gint w, h;
  gint w2, h2;
  GstVideoFormat in_format, out_format;
  const gchar *matrix;
  gboolean in_sdtv, out_sdtv;

  alpha->process = NULL;
  alpha->matrix = NULL;

  ret = gst_video_format_parse_caps (incaps, &in_format, &w, &h);
  ret &= gst_video_format_parse_caps (outcaps, &out_format, &w2, &h2);

  if (!ret || w != w2 || h != h2) {
    GST_DEBUG_OBJECT (alpha, "incomplete or invalid caps!");
    return FALSE;
  }

  matrix = gst_video_parse_caps_color_matrix (incaps);
  in_sdtv = matrix ? g_str_equal (matrix, "sdtv") : TRUE;
  matrix = gst_video_parse_caps_color_matrix (outcaps);
  out_sdtv = matrix ? g_str_equal (matrix, "sdtv") : TRUE;

  alpha->in_format = in_format;
  alpha->out_format = out_format;
  alpha->width = w;
  alpha->height = h;

  switch (alpha->in_format) {
    case GST_VIDEO_FORMAT_ARGB:
      switch (alpha->out_format) {
        case GST_VIDEO_FORMAT_ARGB:
          alpha->process = NULL;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_BGRA:
          alpha->process = transform_argb_bgra;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_ABGR:
          alpha->process = transform_argb_abgr;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_RGBA:
          alpha->process = transform_argb_rgba;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_AYUV:
          alpha->process = transform_argb_ayuv;
          alpha->matrix =
              out_sdtv ? cog_rgb_to_ycbcr_matrix_8bit_sdtv :
              cog_rgb_to_ycbcr_matrix_8bit_hdtv;
          break;
        default:
          alpha->process = NULL;
          alpha->matrix = NULL;
          break;
      }
      break;
    case GST_VIDEO_FORMAT_BGRA:
      switch (alpha->out_format) {
        case GST_VIDEO_FORMAT_BGRA:
          alpha->process = NULL;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_ARGB:
          alpha->process = transform_bgra_argb;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_ABGR:
          alpha->process = transform_bgra_abgr;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_RGBA:
          alpha->process = transform_bgra_rgba;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_AYUV:
          alpha->process = transform_bgra_ayuv;
          alpha->matrix =
              out_sdtv ? cog_rgb_to_ycbcr_matrix_8bit_sdtv :
              cog_rgb_to_ycbcr_matrix_8bit_hdtv;
          break;
        default:
          alpha->process = NULL;
          alpha->matrix = NULL;
          break;
      }
      break;
    case GST_VIDEO_FORMAT_ABGR:
      switch (alpha->out_format) {
        case GST_VIDEO_FORMAT_ABGR:
          alpha->process = NULL;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_RGBA:
          alpha->process = transform_abgr_rgba;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_ARGB:
          alpha->process = transform_abgr_argb;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_BGRA:
          alpha->process = transform_abgr_bgra;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_AYUV:
          alpha->process = transform_abgr_ayuv;
          alpha->matrix =
              out_sdtv ? cog_rgb_to_ycbcr_matrix_8bit_sdtv :
              cog_rgb_to_ycbcr_matrix_8bit_hdtv;
          break;
        default:
          alpha->process = NULL;
          alpha->matrix = NULL;
          break;
      }
      break;
    case GST_VIDEO_FORMAT_RGBA:
      switch (alpha->out_format) {
        case GST_VIDEO_FORMAT_RGBA:
          alpha->process = NULL;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_ARGB:
          alpha->process = transform_rgba_argb;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_ABGR:
          alpha->process = transform_rgba_abgr;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_BGRA:
          alpha->process = transform_rgba_bgra;
          alpha->matrix = NULL;
          break;
        case GST_VIDEO_FORMAT_AYUV:
          alpha->process = transform_rgba_ayuv;
          alpha->matrix =
              out_sdtv ? cog_rgb_to_ycbcr_matrix_8bit_sdtv :
              cog_rgb_to_ycbcr_matrix_8bit_hdtv;
          break;
        default:
          alpha->process = NULL;
          alpha->matrix = NULL;
          break;
      }
      break;
    case GST_VIDEO_FORMAT_AYUV:
      switch (alpha->out_format) {
        case GST_VIDEO_FORMAT_AYUV:
          if (in_sdtv == out_sdtv) {
            alpha->process = transform_ayuv_ayuv;
            alpha->matrix = NULL;
          } else {
            alpha->process = transform_ayuv_ayuv;
            alpha->matrix =
                out_sdtv ? cog_ycbcr_hdtv_to_ycbcr_sdtv_matrix_8bit :
                cog_ycbcr_sdtv_to_ycbcr_hdtv_matrix_8bit;
          }
          break;
        case GST_VIDEO_FORMAT_ARGB:
          alpha->process = transform_ayuv_argb;
          alpha->matrix =
              in_sdtv ? cog_ycbcr_to_rgb_matrix_8bit_sdtv :
              cog_ycbcr_to_rgb_matrix_8bit_hdtv;
          break;
        case GST_VIDEO_FORMAT_BGRA:
          alpha->process = transform_ayuv_bgra;
          alpha->matrix =
              in_sdtv ? cog_ycbcr_to_rgb_matrix_8bit_sdtv :
              cog_ycbcr_to_rgb_matrix_8bit_hdtv;
          break;
        case GST_VIDEO_FORMAT_ABGR:
          alpha->process = transform_ayuv_abgr;
          alpha->matrix =
              in_sdtv ? cog_ycbcr_to_rgb_matrix_8bit_sdtv :
              cog_ycbcr_to_rgb_matrix_8bit_hdtv;
          break;
        case GST_VIDEO_FORMAT_RGBA:
          alpha->process = transform_ayuv_rgba;
          alpha->matrix =
              in_sdtv ? cog_ycbcr_to_rgb_matrix_8bit_sdtv :
              cog_ycbcr_to_rgb_matrix_8bit_hdtv;
          break;
        default:
          alpha->process = NULL;
          alpha->matrix = NULL;
          break;
      }
      break;
    default:
      alpha->process = NULL;
      alpha->matrix = NULL;
      break;
  }

  if (in_format == out_format && in_sdtv == out_sdtv)
    gst_base_transform_set_passthrough (btrans, TRUE);
  else if (!alpha->process)
    return FALSE;

  return TRUE;
}

static GstFlowReturn
gst_alpha_color_transform_ip (GstBaseTransform * btrans, GstBuffer * inbuf)
{
  GstAlphaColor *alpha = GST_ALPHA_COLOR (btrans);

  if (G_UNLIKELY (GST_BUFFER_SIZE (inbuf) != 4 * alpha->width * alpha->height)) {
    GST_ERROR_OBJECT (alpha, "Invalid buffer size (was %u, expected %u)",
        GST_BUFFER_SIZE (inbuf), alpha->width * alpha->height);
    return GST_FLOW_ERROR;
  }

  if (gst_base_transform_is_passthrough (btrans))
    return GST_FLOW_OK;

  if (G_UNLIKELY (!alpha->process)) {
    GST_ERROR_OBJECT (alpha, "Not negotiated yet");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  /* Transform in place */
  alpha->process (GST_BUFFER_DATA (inbuf), GST_BUFFER_SIZE (inbuf),
      alpha->matrix);

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "alphacolor", GST_RANK_NONE,
      GST_TYPE_ALPHA_COLOR);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "alphacolor",
    "RGBA from/to AYUV colorspace conversion preserving the alpha channel",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
