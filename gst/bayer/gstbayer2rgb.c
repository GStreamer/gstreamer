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
#include "_stdint.h"

#define GST_CAT_DEFAULT gst_bayer2rgb_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

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
};

struct _GstBayer2RGBClass
{
  GstBaseTransformClass parent;
};

//#define SRC_CAPS GST_VIDEO_CAPS_RGBx
#define	SRC_CAPS                                 \
  GST_VIDEO_CAPS_RGBx ";"                        \
  GST_VIDEO_CAPS_xRGB ";"                        \
  GST_VIDEO_CAPS_BGRx ";"                        \
  GST_VIDEO_CAPS_xBGR ";"                        \
  GST_VIDEO_CAPS_RGBA ";"                        \
  GST_VIDEO_CAPS_ARGB ";"                        \
  GST_VIDEO_CAPS_BGRA ";"                        \
  GST_VIDEO_CAPS_ABGR ";"                        \
  GST_VIDEO_CAPS_RGB ";"                         \
  GST_VIDEO_CAPS_BGR

#define SINK_CAPS "video/x-raw-bayer,width=(int)[1,MAX],height=(int)[1,MAX]"

enum
{
  PROP_0
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_bayer2rgb_debug, "bayer2rgb", 0, "bayer2rgb element");

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

  gst_element_class_set_details_simple (element_class,
      "Bayer to RGB decoder for cameras", "Filter/Converter/Video",
      "Converts video/x-raw-bayer to video/x-raw-rgb",
      "William Brack <wbrack@mmm.com.hk>");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_caps_from_string (SRC_CAPS)));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          gst_caps_from_string (SINK_CAPS)));
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
  GstBayer2RGB *filter = GST_BAYER2RGB (base);
  GstStructure *structure;
  int val, bpp;

  GST_DEBUG ("in caps %" GST_PTR_FORMAT " out caps %" GST_PTR_FORMAT, incaps,
      outcaps);

  structure = gst_caps_get_structure (incaps, 0);

  gst_structure_get_int (structure, "width", &filter->width);
  gst_structure_get_int (structure, "height", &filter->height);
  filter->stride = GST_ROUND_UP_4 (filter->width);

  /* To cater for different RGB formats, we need to set params for later */
  structure = gst_caps_get_structure (outcaps, 0);
  gst_structure_get_int (structure, "bpp", &bpp);
  filter->pixsize = bpp / 8;
  gst_structure_get_int (structure, "red_mask", &val);
  filter->r_off = get_pix_offset (val, bpp);
  gst_structure_get_int (structure, "green_mask", &val);
  filter->g_off = get_pix_offset (val, bpp);
  gst_structure_get_int (structure, "blue_mask", &val);
  filter->b_off = get_pix_offset (val, bpp);

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
    newcaps = gst_caps_new_simple ("video/x-raw-bayer", NULL);
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
      /* For bayer, we handle only BA81 (BGGR), which is BPP=24 */
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

/*
 * We define values for the colors, just to make the code more readable.
 */
#define	RED	0               /* Pure red element */
#define	GREENB	1               /* Green element which is on a blue line */
#define	BLUE	2               /* Pure blue element */
#define	GREENR	3               /* Green element which is on a red line */

/* Routine to generate the top and bottom edges (not including corners) */
static void
hborder (uint8_t * input, uint8_t * output, int bot_top,
    int typ, GstBayer2RGB * filter)
{
  uint8_t *op;                  /* output pointer */
  uint8_t *ip;                  /* input pointer */
  uint8_t *nx;                  /* next line pointer */
  int ix;                       /* loop index */

  op = output + (bot_top * filter->width * (filter->height - 1) + 1) *
      filter->pixsize;
  ip = input + bot_top * filter->stride * (filter->height - 1);
  /* calculate minus or plus one line, depending upon bot_top flag */
  nx = ip + (1 - 2 * bot_top) * filter->stride;
  /* Stepping horizontally */
  for (ix = 1; ix < filter->width - 1; ix++, op += filter->pixsize) {
    switch (typ) {
      case RED:
        op[filter->r_off] = ip[ix];
        op[filter->g_off] = (ip[ix + 1] + ip[ix - 1] + nx[ix] + 1) / 3;
        op[filter->b_off] = (nx[ix + 1] + nx[ix - 1] + 1) / 2;
        typ = GREENR;
        break;
      case GREENR:
        op[filter->r_off] = (ip[ix + 1] + ip[ix - 1] + 1) / 2;
        op[filter->g_off] = ip[ix];
        op[filter->b_off] = nx[ix];
        typ = RED;
        break;
      case GREENB:
        op[filter->r_off] = nx[ix];
        op[filter->g_off] = ip[ix];
        op[filter->b_off] = (ip[ix + 1] + ip[ix - 1] + 1) / 2;
        typ = BLUE;
        break;
      case BLUE:
        op[filter->r_off] = (nx[ix + 1] + nx[ix - 1] + 1) / 2;
        op[filter->g_off] = (ip[ix + 1] + ip[ix - 1] + nx[ix] + 1) / 3;
        op[filter->b_off] = ip[ix];
        typ = GREENB;
        break;
    }
  }
}

/* Routine to generate the left and right edges, not including corners */
static void
vborder (uint8_t * input, uint8_t * output, int right_left,
    int typ, GstBayer2RGB * filter)
{
  uint8_t *op;                  /* output pointer */
  uint8_t *ip;                  /* input pointer */
  uint8_t *la;                  /* line above pointer */
  uint8_t *lb;                  /* line below pointer */
  int ix;                       /* loop index */
  int lr;                       /* 'left-right' flag - +1 is right, -1 is left */

  lr = (1 - 2 * right_left);
  /* stepping vertically */
  for (ix = 1; ix < filter->height - 1; ix++) {
    ip = input + right_left * (filter->width - 1) + ix * filter->stride;
    op = output + (right_left * (filter->width - 1) + ix * filter->width) *
        filter->pixsize;
    la = ip + filter->stride;
    lb = ip - filter->stride;
    switch (typ) {
      case RED:
        op[filter->r_off] = ip[0];
        op[filter->g_off] = (la[0] + ip[lr] + lb[0] + 1) / 3;
        op[filter->b_off] = (la[lr] + lb[lr] + 1) / 2;
        typ = GREENB;
        break;
      case GREENR:
        op[filter->r_off] = ip[lr];
        op[filter->g_off] = ip[0];
        op[filter->b_off] = (la[lr] + lb[lr] + 1) / 2;
        typ = BLUE;
        break;
      case GREENB:
        op[filter->r_off] = (la[lr] + lb[lr] + 1) / 2;
        op[filter->g_off] = ip[0];
        op[filter->b_off] = ip[lr];
        typ = RED;
        break;
      case BLUE:
        op[filter->r_off] = (la[lr] + lb[lr] + 1) / 2;
        op[filter->g_off] = (la[0] + ip[lr] + lb[0] + 1) / 3;
        op[filter->b_off] = ip[0];
        typ = GREENR;
        break;
    }
  }
}

/* Produce the four (top, bottom, left, right) edges */
static void
do_row0_col0 (uint8_t * input, uint8_t * output, GstBayer2RGB * filter)
{
  int type;

  /* Horizontal edges */
  hborder (input, output, 0, GREENB, filter);
  if (filter->height & 1)
    type = GREENB;              /* odd # rows, "bottom" edge same as top */
  else
    type = RED;                 /* even #, bottom side different */
  hborder (input, output, 1, type, filter);

  /* Vertical edges */
  vborder (input, output, 0, GREENR, filter);
  if (filter->width & 1)
    type = GREENR;              /* odd # cols, "right" edge same as left */
  else
    type = RED;                 /* even #, right side different */
  vborder (input, output, 1, type, filter);
}

static void
corner (uint8_t * input, uint8_t * output, int x, int y,
    int xd, int yd, int typ, GstBayer2RGB * filter)
{
  uint8_t *ip;                  /* input pointer */
  uint8_t *op;                  /* output pointer */
  uint8_t *nx;                  /* adjacent line */

  op = output + y * filter->width * filter->pixsize + x * filter->pixsize;
  ip = input + y * filter->stride + x;
  nx = ip + yd * filter->stride;
  switch (typ) {
    case RED:
      op[filter->r_off] = ip[0];
      op[filter->g_off] = (nx[0] + ip[xd] + 1) / 2;
      op[filter->b_off] = nx[xd];
      break;
    case GREENR:
      op[filter->r_off] = ip[xd];
      op[filter->g_off] = ip[0];
      op[filter->b_off] = nx[0];
      break;
    case GREENB:
      op[filter->r_off] = nx[0];
      op[filter->g_off] = ip[0];
      op[filter->b_off] = ip[xd];
      break;
    case BLUE:
      op[filter->r_off] = nx[xd];
      op[filter->g_off] = (nx[0] + ip[xd] + 1) / 2;
      op[filter->b_off] = ip[0];
      break;
  }
}

static void
do_corners (uint8_t * input, uint8_t * output, GstBayer2RGB * filter)
{
  int typ;

  /* Top left */
  corner (input, output, 0, 0, 1, 1, BLUE, filter);
  /* Bottom left */
  corner (input, output, 0, filter->height - 1, 1, -1,
      (filter->height & 1) ? BLUE : GREENR, filter);
  /* Top right */
  corner (input, output, filter->width - 1, 0, -1, 0,
      (filter->width & 1) ? BLUE : GREENB, filter);
  /* Bottom right */
  if (filter->width & 1)        /* if odd  # cols, B or GB */
    typ = BLUE;
  else
    typ = GREENB;               /* if even # cols, B or GR */
  typ |= (filter->height & 1);  /* if odd  # rows, GB or GR */
  corner (input, output, filter->width - 1, filter->height - 1, -1, -1,
      typ, filter);
}

static void
do_body (uint8_t * input, uint8_t * output, GstBayer2RGB * filter)
{
  int ip, op;                   /* input and output pointers */
  int w, h;                     /* loop indices */
  int type;                     /* calculated colour of current element */
  int a1, a2;
  int v1, v2, h1, h2;

  /*
   * We are processing row (line) by row, starting with the second
   * row and continuing through the next to last.  Each row is processed
   * column by column, starting with the second and continuing through
   * to the next to last.
   */
  for (h = 1; h < filter->height - 1; h++) {
    /*
     * Remember we are processing "row by row". For each row, we need
     * to set the type of the first element to be processed.  Since we
     * have already processed the edges, the "first element" will be
     * the pixel at position (1,1).  Assuming BG format, this should
     * be RED for odd-numbered rows and GREENB for even rows.
     */
    if (h & 1)
      type = RED;
    else
      type = GREENB;
    /* Calculate the starting position for the row */
    op = h * filter->width * filter->pixsize;   /* output (converted) pos */
    ip = h * filter->stride;    /* input (bayer data) pos */
    for (w = 1; w < filter->width - 1; w++) {
      op += filter->pixsize;    /* we are processing "horizontally" */
      ip++;
      switch (type) {
        case RED:
          output[op + filter->r_off] = input[ip];
          output[op + filter->b_off] = (input[ip - filter->stride - 1] +
              input[ip - filter->stride + 1] +
              input[ip + filter->stride - 1] +
              input[ip + filter->stride + 1] + 2) / 4;
          v1 = input[ip + filter->stride];
          v2 = input[ip - filter->stride];
          h1 = input[ip + 1];
          h2 = input[ip - 1];
          a1 = abs (v1 - v2);
          a2 = abs (h1 - h2);
          if (a1 < a2)
            output[op + filter->g_off] = (v1 + v2 + 1) / 2;
          else if (a1 > a2)
            output[op + filter->g_off] = (h1 + h2 + 1) / 2;
          else
            output[op + filter->g_off] = (v1 + h1 + v2 + h2 + 2) / 4;
          type = GREENR;
          break;
        case GREENR:
          output[op + filter->r_off] = (input[ip + 1] + input[ip - 1] + 1) / 2;
          output[op + filter->g_off] = input[ip];
          output[op + filter->b_off] = (input[ip - filter->stride] +
              input[ip + filter->stride] + 1) / 2;
          type = RED;
          break;
        case GREENB:
          output[op + filter->r_off] = (input[ip - filter->stride] +
              input[ip + filter->stride] + 1) / 2;
          output[op + filter->g_off] = input[ip];
          output[op + filter->b_off] = (input[ip + 1] + input[ip - 1] + 1) / 2;
          type = BLUE;
          break;
        case BLUE:
          output[op + filter->r_off] = (input[ip - filter->stride - 1] +
              input[ip - filter->stride + 1] +
              input[ip + filter->stride - 1] +
              input[ip + filter->stride + 1] + 2) / 4;
          output[op + filter->b_off] = input[ip];
          v1 = input[ip + filter->stride];
          v2 = input[ip - filter->stride];
          h1 = input[ip + 1];
          h2 = input[ip - 1];
          a1 = abs (v1 - v2);
          a2 = abs (h1 - h2);
          if (a1 < a2)
            output[op + filter->g_off] = (v1 + v2 + 1) / 2;
          else if (a1 > a2)
            output[op + filter->g_off] = (h1 + h2 + 1) / 2;
          else
            output[op + filter->g_off] = (v1 + h1 + v2 + h2 + 2) / 4;
          type = GREENB;
          break;
      }
    }
  }
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
  do_corners (input, output, filter);
  do_row0_col0 (input, output, filter);
  do_body (input, output, filter);

  GST_OBJECT_UNLOCK (filter);
  return GST_FLOW_OK;
}
