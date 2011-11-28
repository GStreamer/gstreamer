/* 
 * GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
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
 *
 * March 2008
 * Logic enhanced by William Brack <wbrack@mmm.com.hk>
 */

/**
 * SECTION:element-bayer2rgb
 *
 * Decodes raw camera bayer (fourcc BA81) to RGB.
 */

/*
 * In order to guard against my advancing maturity, some extra detailed
 * information about the logic of the decode is included here.  Much of
 * this was inspired by a technical paper from siliconimaging.com, which
 * in turn was based upon an article from IEEE,
 * T. Sakamoto, C. Nakanishi and T. Hase,
 * “Software pixel interpolation for digital still cameras suitable for
 *  a 32-bit MCU,”
 * IEEE Trans. Consumer Electronics, vol. 44, no. 4, November 1998.
 *
 * The code assumes a Bayer matrix of the type produced by the fourcc
 * BA81 (v4l2 format SBGGR8) of width w and height h which looks like:
 *       0 1 2 3  w-2 w-1
 *
 *   0   B G B G ....B G
 *   1   G R G R ....G R
 *   2   B G B G ....B G
 *       ...............
 * h-2   B G B G ....B G
 * h-1   G R G R ....G R
 *
 * We expand this matrix, producing a separate {r, g, b} triple for each
 * of the individual elements.  The algorithm for doing this expansion is
 * as follows.
 *
 * We are designing for speed of transformation, at a slight expense of code.
 * First, we calculate the appropriate triples for the four corners, the
 * remainder of the top and bottom rows, and the left and right columns.
 * The reason for this is that those elements are transformed slightly
 * differently than all of the remainder of the matrix. Finally, we transform
 * all of the remainder.
 *
 * The transformation into the "appropriate triples" is based upon the
 * "nearest neighbor" principal, with some additional complexity for the
 * calculation of the "green" element, where an "adaptive" pairing is used.
 *
 * For purposes of documentation and indentification, each element of the
 * original array can be put into one of four classes:
 *   R   A red element
 *   B   A blue element
 *   GR  A green element which is followed by a red one
 *   GB  A green element which is followed by a blue one
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <string.h>
#include <stdlib.h>
#include <_stdint.h>
#include "gstbayerorc.h"

#define GST_CAT_DEFAULT gst_bayer2rgb_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  GST_BAYER_2_RGB_FORMAT_BGGR = 0,
  GST_BAYER_2_RGB_FORMAT_GBRG,
  GST_BAYER_2_RGB_FORMAT_GRBG,
  GST_BAYER_2_RGB_FORMAT_RGGB
};


#define GST_TYPE_BAYER2RGB            (gst_bayer2rgb_get_type())
#define GST_BAYER2RGB(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BAYER2RGB,GstBayer2RGB))
#define GST_IS_BAYER2RGB(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BAYER2RGB))
#define GST_BAYER2RGB_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_BAYER2RGB,GstBayer2RGBClass))
#define GST_IS_BAYER2RGB_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_BAYER2RGB))
#define GST_BAYER2RGB_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_BAYER2RGB,GstBayer2RGBClass))
typedef struct _GstBayer2RGB GstBayer2RGB;
typedef struct _GstBayer2RGBClass GstBayer2RGBClass;

typedef void (*GstBayer2RGBProcessFunc) (GstBayer2RGB *, guint8 *, guint);

struct _GstBayer2RGB
{
  GstBaseTransform basetransform;

  /* < private > */
  int width;
  int height;
  int stride;
  int pixsize;                  /* bytes per pixel */
  int r_off;                    /* offset for red */
  int g_off;                    /* offset for green */
  int b_off;                    /* offset for blue */
  int format;
};

struct _GstBayer2RGBClass
{
  GstBaseTransformClass parent;
};

#define	SRC_CAPS                                 \
  GST_VIDEO_CAPS_RGBx ";"                        \
  GST_VIDEO_CAPS_xRGB ";"                        \
  GST_VIDEO_CAPS_BGRx ";"                        \
  GST_VIDEO_CAPS_xBGR ";"                        \
  GST_VIDEO_CAPS_RGBA ";"                        \
  GST_VIDEO_CAPS_ARGB ";"                        \
  GST_VIDEO_CAPS_BGRA ";"                        \
  GST_VIDEO_CAPS_ABGR

#define SINK_CAPS "video/x-raw-bayer,format=(string){bggr,grbg,gbrg,rggb}," \
  "width=(int)[1,MAX],height=(int)[1,MAX],framerate=(fraction)[0/1,MAX]"

enum
{
  PROP_0
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_bayer2rgb_debug, "bayer2rgb", 0, "bayer2rgb element");

GType gst_bayer2rgb_get_type (void);
GST_BOILERPLATE_FULL (GstBayer2RGB, gst_bayer2rgb, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_bayer2rgb_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_bayer2rgb_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_bayer2rgb_set_caps (GstBaseTransform * filter,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_bayer2rgb_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf);
static void gst_bayer2rgb_reset (GstBayer2RGB * filter);
static GstCaps *gst_bayer2rgb_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_bayer2rgb_get_unit_size (GstBaseTransform * base,
    GstCaps * caps, guint * size);


static void
gst_bayer2rgb_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *pad_template;

  gst_element_class_set_details_simple (element_class,
      "Bayer to RGB decoder for cameras", "Filter/Converter/Video",
      "Converts video/x-raw-bayer to video/x-raw-rgb",
      "William Brack <wbrack@mmm.com.hk>");

  pad_template =
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_caps_from_string (SRC_CAPS));
  gst_element_class_add_pad_template (element_class, pad_template);
  gst_object_unref (pad_template);
  pad_template =
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_from_string (SINK_CAPS));
  gst_element_class_add_pad_template (element_class, pad_template);
  gst_object_unref (pad_template);
}

static void
gst_bayer2rgb_class_init (GstBayer2RGBClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_bayer2rgb_set_property;
  gobject_class->get_property = gst_bayer2rgb_get_property;

  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      GST_DEBUG_FUNCPTR (gst_bayer2rgb_transform_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_bayer2rgb_get_unit_size);
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps =
      GST_DEBUG_FUNCPTR (gst_bayer2rgb_set_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->transform =
      GST_DEBUG_FUNCPTR (gst_bayer2rgb_transform);
}

static void
gst_bayer2rgb_init (GstBayer2RGB * filter, GstBayer2RGBClass * klass)
{
  gst_bayer2rgb_reset (filter);
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), TRUE);
}

/* No properties are implemented, so only a warning is produced */
static void
gst_bayer2rgb_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_bayer2rgb_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Routine to convert colormask value into relative byte offset */
static int
get_pix_offset (int mask, int bpp)
{
  int bpp32 = (bpp / 8) - 3;

  switch (mask) {
    case 255:
      return 2 + bpp32;
    case 65280:
      return 1 + bpp32;
    case 16711680:
      return 0 + bpp32;
    case -16777216:
      return 0;
    default:
      GST_ERROR ("Invalid color mask 0x%08x", mask);
      return -1;
  }
}

static gboolean
gst_bayer2rgb_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstBayer2RGB *bayer2rgb = GST_BAYER2RGB (base);
  GstStructure *structure;
  int val, bpp;
  const char *format;

  GST_DEBUG ("in caps %" GST_PTR_FORMAT " out caps %" GST_PTR_FORMAT, incaps,
      outcaps);

  structure = gst_caps_get_structure (incaps, 0);

  gst_structure_get_int (structure, "width", &bayer2rgb->width);
  gst_structure_get_int (structure, "height", &bayer2rgb->height);
  bayer2rgb->stride = GST_ROUND_UP_4 (bayer2rgb->width);

  format = gst_structure_get_string (structure, "format");
  if (g_str_equal (format, "bggr")) {
    bayer2rgb->format = GST_BAYER_2_RGB_FORMAT_BGGR;
  } else if (g_str_equal (format, "gbrg")) {
    bayer2rgb->format = GST_BAYER_2_RGB_FORMAT_GBRG;
  } else if (g_str_equal (format, "grbg")) {
    bayer2rgb->format = GST_BAYER_2_RGB_FORMAT_GRBG;
  } else if (g_str_equal (format, "rggb")) {
    bayer2rgb->format = GST_BAYER_2_RGB_FORMAT_RGGB;
  } else {
    return FALSE;
  }

  /* To cater for different RGB formats, we need to set params for later */
  structure = gst_caps_get_structure (outcaps, 0);
  gst_structure_get_int (structure, "bpp", &bpp);
  bayer2rgb->pixsize = bpp / 8;
  gst_structure_get_int (structure, "red_mask", &val);
  bayer2rgb->r_off = get_pix_offset (val, bpp);
  gst_structure_get_int (structure, "green_mask", &val);
  bayer2rgb->g_off = get_pix_offset (val, bpp);
  gst_structure_get_int (structure, "blue_mask", &val);
  bayer2rgb->b_off = get_pix_offset (val, bpp);

  return TRUE;
}

static void
gst_bayer2rgb_reset (GstBayer2RGB * filter)
{
  filter->width = 0;
  filter->height = 0;
  filter->stride = 0;
  filter->pixsize = 0;
  filter->r_off = 0;
  filter->g_off = 0;
  filter->b_off = 0;
}

static GstCaps *
gst_bayer2rgb_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *newcaps;
  GstStructure *newstruct;

  GST_DEBUG_OBJECT (caps, "transforming caps (from)");

  structure = gst_caps_get_structure (caps, 0);

  if (direction == GST_PAD_SRC) {
    newcaps = gst_caps_from_string ("video/x-raw-bayer,"
        "format=(string){bggr,grbg,gbrg,rggb}");
  } else {
    newcaps = gst_caps_new_simple ("video/x-raw-rgb", NULL);
  }
  newstruct = gst_caps_get_structure (newcaps, 0);

  gst_structure_set_value (newstruct, "width",
      gst_structure_get_value (structure, "width"));
  gst_structure_set_value (newstruct, "height",
      gst_structure_get_value (structure, "height"));
  gst_structure_set_value (newstruct, "framerate",
      gst_structure_get_value (structure, "framerate"));

  GST_DEBUG_OBJECT (newcaps, "transforming caps (into)");

  return newcaps;
}

static gboolean
gst_bayer2rgb_get_unit_size (GstBaseTransform * base, GstCaps * caps,
    guint * size)
{
  GstStructure *structure;
  int width;
  int height;
  int pixsize;
  const char *name;

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height)) {
    name = gst_structure_get_name (structure);
    /* Our name must be either video/x-raw-bayer video/x-raw-rgb */
    if (strcmp (name, "video/x-raw-rgb")) {
      *size = GST_ROUND_UP_4 (width) * height;
      return TRUE;
    } else {
      /* For output, calculate according to format */
      if (gst_structure_get_int (structure, "bpp", &pixsize)) {
        *size = width * height * (pixsize / 8);
        return TRUE;
      }
    }

  }
  GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
      ("Incomplete caps, some required field missing"));
  return FALSE;
}

static void
gst_bayer2rgb_split_and_upsample_horiz (guint8 * dest0, guint8 * dest1,
    const guint8 * src, int n)
{
  int i;

  dest0[0] = src[0];
  dest1[0] = src[1];
  dest0[1] = (src[0] + src[2] + 1) >> 1;
  dest1[1] = src[1];

#if defined(__i386__) || defined(__amd64__)
  gst_bayer_horiz_upsample_unaligned (dest0 + 2, dest1 + 2, src + 1,
      (n - 4) >> 1);
#else
  gst_bayer_horiz_upsample (dest0 + 2, dest1 + 2, src + 2, (n - 4) >> 1);
#endif

  for (i = n - 2; i < n; i++) {
    if ((i & 1) == 0) {
      dest0[i] = src[i];
      dest1[i] = src[i - 1];
    } else {
      dest0[i] = src[i - 1];
      dest1[i] = src[i];
    }
  }
}

typedef void (*process_func) (guint8 * d0, const guint8 * s0, const guint8 * s1,
    const guint8 * s2, const guint8 * s3, const guint8 * s4, const guint8 * s5,
    int n);

static void
gst_bayer2rgb_process (GstBayer2RGB * bayer2rgb, uint8_t * dest,
    int dest_stride, uint8_t * src, int src_stride)
{
  int j;
  guint8 *tmp;
  process_func merge[2] = { NULL, NULL };
  int r_off, g_off, b_off;

  /* We exploit some symmetry in the functions here.  The base functions
   * are all named for the BGGR arrangement.  For RGGB, we swap the
   * red offset and blue offset in the output.  For GRBG, we swap the
   * order of the merge functions.  For GBRG, do both. */
  r_off = bayer2rgb->r_off;
  g_off = bayer2rgb->g_off;
  b_off = bayer2rgb->b_off;
  if (bayer2rgb->format == GST_BAYER_2_RGB_FORMAT_RGGB ||
      bayer2rgb->format == GST_BAYER_2_RGB_FORMAT_GBRG) {
    r_off = bayer2rgb->b_off;
    b_off = bayer2rgb->r_off;
  }

  if (r_off == 2 && g_off == 1 && b_off == 0) {
    merge[0] = gst_bayer_merge_bg_bgra;
    merge[1] = gst_bayer_merge_gr_bgra;
  } else if (r_off == 3 && g_off == 2 && b_off == 1) {
    merge[0] = gst_bayer_merge_bg_abgr;
    merge[1] = gst_bayer_merge_gr_abgr;
  } else if (r_off == 1 && g_off == 2 && b_off == 3) {
    merge[0] = gst_bayer_merge_bg_argb;
    merge[1] = gst_bayer_merge_gr_argb;
  } else if (r_off == 0 && g_off == 1 && b_off == 2) {
    merge[0] = gst_bayer_merge_bg_rgba;
    merge[1] = gst_bayer_merge_gr_rgba;
  }
  if (bayer2rgb->format == GST_BAYER_2_RGB_FORMAT_GRBG ||
      bayer2rgb->format == GST_BAYER_2_RGB_FORMAT_GBRG) {
    process_func tmp = merge[0];
    merge[0] = merge[1];
    merge[1] = tmp;
  }

  tmp = g_malloc (2 * 4 * bayer2rgb->width);
#define LINE(x) (tmp + ((x)&7) * bayer2rgb->width)

  gst_bayer2rgb_split_and_upsample_horiz (LINE (3 * 2 + 0), LINE (3 * 2 + 1),
      src + 1 * src_stride, bayer2rgb->width);
  j = 0;
  gst_bayer2rgb_split_and_upsample_horiz (LINE (j * 2 + 0), LINE (j * 2 + 1),
      src + j * src_stride, bayer2rgb->width);

  for (j = 0; j < bayer2rgb->height; j++) {
    if (j < bayer2rgb->height - 1) {
      gst_bayer2rgb_split_and_upsample_horiz (LINE ((j + 1) * 2 + 0),
          LINE ((j + 1) * 2 + 1), src + (j + 1) * src_stride, bayer2rgb->width);
    }

    merge[j & 1] (dest + j * dest_stride,
        LINE (j * 2 - 2), LINE (j * 2 - 1),
        LINE (j * 2 + 0), LINE (j * 2 + 1),
        LINE (j * 2 + 2), LINE (j * 2 + 3), bayer2rgb->width >> 1);
  }

  g_free (tmp);
}




static GstFlowReturn
gst_bayer2rgb_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstBayer2RGB *filter = GST_BAYER2RGB (base);
  uint8_t *input, *output;

  /*
   * We need to lock our filter params to prevent changing
   * caps in the middle of a transformation (nice way to get
   * segfaults)
   */
  GST_OBJECT_LOCK (filter);

  GST_DEBUG ("transforming buffer");
  input = (uint8_t *) GST_BUFFER_DATA (inbuf);
  output = (uint8_t *) GST_BUFFER_DATA (outbuf);
  gst_bayer2rgb_process (filter, output, filter->width * 4,
      input, filter->width);

  GST_OBJECT_UNLOCK (filter);
  return GST_FLOW_OK;
}
