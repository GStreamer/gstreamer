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
 */

/*
 * SECTION:element-bayer2rgb
 *
 * Decodes raw camera sensor images.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <string.h>
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

  uint8_t *tmpdata;
};

struct _GstBayer2RGBClass
{
  GstBaseTransformClass parent;
};

static const GstElementDetails element_details =
GST_ELEMENT_DETAILS ("RAW Camera sensor decoder",
    "Filter/Effect",
    "FIXME example filter",
    "FIXME <fixme@fixme.com>");

#define SRC_CAPS GST_VIDEO_CAPS_ARGB
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

  gst_element_class_set_details (element_class, &element_details);

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

static void
gst_bayer2rgb_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //GstBayer2RGB *filter = GST_BAYER2RGB (object);

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
  //GstBayer2RGB *filter = GST_BAYER2RGB (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_bayer2rgb_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstBayer2RGB *filter = GST_BAYER2RGB (base);
  GstStructure *structure;

  GST_ERROR ("in caps %" GST_PTR_FORMAT " out caps %" GST_PTR_FORMAT, incaps,
      outcaps);

  structure = gst_caps_get_structure (incaps, 0);

  gst_structure_get_int (structure, "width", &filter->width);
  gst_structure_get_int (structure, "height", &filter->height);
  filter->stride = GST_ROUND_UP_4 (filter->width);

  if (filter->tmpdata) {
    g_free (filter->tmpdata);
  }
  filter->tmpdata = g_malloc (filter->stride * (4 * 3 + 1));

  return TRUE;
}

static void
gst_bayer2rgb_reset (GstBayer2RGB * filter)
{
  filter->width = 0;
  filter->height = 0;
  filter->stride = 0;
  if (filter->tmpdata) {
    g_free (filter->tmpdata);
    filter->tmpdata = NULL;
  }
}

static GstCaps *
gst_bayer2rgb_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *newcaps;
  GstStructure *newstruct;

  GST_ERROR ("transforming caps %" GST_PTR_FORMAT, caps);

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
  gst_structure_set_value (newstruct, "pixel-aspect-ratio",
      gst_structure_get_value (structure, "pixel-aspect-ratio"));

  GST_ERROR ("into %" GST_PTR_FORMAT, newcaps);

  return newcaps;
}

static gboolean
gst_bayer2rgb_get_unit_size (GstBaseTransform * base, GstCaps * caps,
    guint * size)
{
  GstStructure *structure;
  int width;
  int height;
  const char *name;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  name = gst_structure_get_name (structure);
  if (strcmp (name, "video/x-raw-rgb")) {
    *size = GST_ROUND_UP_4 (width) * height;
  } else {
    *size = 4 * width * height;
  }

  return TRUE;
}

#define ARGB(a,r,g,b) ((b)<<24 | (g)<<16 | (r)<<8 | a)

static void
upsample_even (uint8_t * dest, uint8_t * src, int width)
{
  int i;

  for (i = 0; i < width - 2; i += 2) {
    dest[i] = src[i];
    dest[i + 1] = (src[i] + src[i + 2] + 1) / 2;
  }
  dest[i] = src[i];
  if (i + 1 < width) {
    dest[i + 1] = src[i];
  }
}

static void
upsample_odd (uint8_t * dest, uint8_t * src, int width)
{
  int i;

  dest[0] = src[1];
  for (i = 1; i < width - 2; i += 2) {
    dest[i] = src[i];
    dest[i + 1] = (src[i] + src[i + 2] + 1) / 2;
  }
  dest[i] = src[i];
  if (i + 1 < width) {
    dest[i + 1] = src[i];
  }
}

static void
interpolate (uint8_t * dest, uint8_t * src1, uint8_t * src2, int width)
{
  int i;

  for (i = 0; i < width; i++) {
    dest[i] = (src1[i] + src2[i] + 1) / 2;
  }
}

static void
merge (uint32_t * dest, uint8_t * r, uint8_t * g, uint8_t * b, int width)
{
  int i;

  for (i = 0; i < width; i++) {
    dest[i] = ARGB (0xff, r[i], g[i], b[i]);
  }
}

static GstFlowReturn
gst_bayer2rgb_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstBayer2RGB *filter = GST_BAYER2RGB (base);
  uint8_t *tmpdata;
  int j;

  GST_DEBUG ("got here");
  tmpdata = filter->tmpdata;

  /* This is a pretty lousy algorithm.  In particular, most higher
   * quality algorithms will apply some non-linear weighting factors
   * in red/blue interpolation based on the green components.  This
   * just does a linear interpolation between surrounding pixels.
   * For green, we only interpolate horizontally.  */

  for (j = 0; j < filter->height + 1; j++) {
    if (j < filter->height) {
      /* upsample horizontally */
      if ((j & 1) == 0) {
        upsample_even (tmpdata + (1 * 4 + (j & 3)) * filter->stride,
            (uint8_t *) GST_BUFFER_DATA (inbuf) + filter->stride * j,
            filter->width);
        upsample_odd (tmpdata + (0 * 4 + (j & 3)) * filter->stride,
            (uint8_t *) GST_BUFFER_DATA (inbuf) + filter->stride * j,
            filter->width);
      } else {
        upsample_even (tmpdata + (2 * 4 + (j & 3)) * filter->stride,
            (uint8_t *) GST_BUFFER_DATA (inbuf) + filter->stride * j,
            filter->width);
        upsample_odd (tmpdata + (1 * 4 + (j & 3)) * filter->stride,
            (uint8_t *) GST_BUFFER_DATA (inbuf) + filter->stride * j,
            filter->width);
      }
    }
    if (j - 1 >= 0 && j - 1 < filter->height) {
      int comp, j1, j2;

      if (((j - 1) & 1) == 0) {
        comp = 2;
      } else {
        comp = 0;
      }
      j1 = j - 2;
      if (j1 < 0)
        j1 += 2;
      j2 = j;
      if (j2 > filter->height - 1)
        j2 -= 2;
      interpolate (tmpdata + (comp * 4 + ((j - 1) & 3)) * filter->stride,
          tmpdata + (comp * 4 + (j1 & 3)) * filter->stride,
          tmpdata + (comp * 4 + (j2 & 3)) * filter->stride, filter->width);

      merge (
          (uint32_t *) ((uint8_t *) GST_BUFFER_DATA (outbuf) +
              4 * filter->width * (j - 1)),
          tmpdata + (0 * 4 + ((j - 1) & 3)) * filter->stride,
          tmpdata + (1 * 4 + ((j - 1) & 3)) * filter->stride,
          tmpdata + (2 * 4 + ((j - 1) & 3)) * filter->stride, filter->width);
    }
  }

  return GST_FLOW_OK;
}
