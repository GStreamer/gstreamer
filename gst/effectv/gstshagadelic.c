/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * Inspired by Adrian Likin's script for the GIMP.
 * EffecTV is free software.  This library is free software;
 * you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
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
 * SECTION:element-shagadelictv
 *
 * Oh behave, ShagedelicTV makes images shagadelic!
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! shagadelictv ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline shows the effect of shagadelictv on a test stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

#include "gstshagadelic.h"
#include "gsteffectv.h"

#include <gst/video/video.h>

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif

GST_BOILERPLATE (GstShagadelicTV, gst_shagadelictv, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER);

static void gst_shagadelic_initialize (GstShagadelicTV * filter);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define CAPS_STR GST_VIDEO_CAPS_BGRx
#else
#define CAPS_STR GST_VIDEO_CAPS_xRGB
#endif

static GstStaticPadTemplate gst_shagadelictv_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

static GstStaticPadTemplate gst_shagadelictv_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

static gboolean
gst_shagadelictv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstShagadelicTV *filter = GST_SHAGADELICTV (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (incaps, 0);

  GST_OBJECT_LOCK (filter);
  if (gst_structure_get_int (structure, "width", &filter->width) &&
      gst_structure_get_int (structure, "height", &filter->height)) {
    gint area = filter->width * filter->height;

    g_free (filter->ripple);
    g_free (filter->spiral);

    filter->ripple = (guint8 *) g_malloc (area * 4);
    filter->spiral = (guint8 *) g_malloc (area);

    gst_shagadelic_initialize (filter);
    ret = TRUE;
  }
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

static void
gst_shagadelic_initialize (GstShagadelicTV * filter)
{
  int i, x, y;

#ifdef PS2
  float xx, yy;
#else
  double xx, yy;
#endif

  i = 0;
  for (y = 0; y < filter->height * 2; y++) {
    yy = y - filter->height;
    yy *= yy;

    for (x = 0; x < filter->width * 2; x++) {
      xx = x - filter->width;
#ifdef PS2
      filter->ripple[i++] = ((unsigned int) (sqrtf (xx * xx + yy) * 8)) & 255;
#else
      filter->ripple[i++] = ((unsigned int) (sqrt (xx * xx + yy) * 8)) & 255;
#endif
    }
  }

  i = 0;
  for (y = 0; y < filter->height; y++) {
    yy = y - filter->height / 2;

    for (x = 0; x < filter->width; x++) {
      xx = x - filter->width / 2;
#ifdef PS2
      filter->spiral[i++] = ((unsigned int)
          ((atan2f (xx,
                      yy) / ((float) M_PI) * 256 * 9) + (sqrtf (xx * xx +
                      yy * yy) * 5))) & 255;
#else
      filter->spiral[i++] = ((unsigned int)
          ((atan2 (xx, yy) / M_PI * 256 * 9) + (sqrt (xx * xx +
                      yy * yy) * 5))) & 255;
#endif
/* Here is another Swinger!
 * ((atan2(xx, yy)/M_PI*256) + (sqrt(xx*xx+yy*yy)*10))&255;
 */
    }
  }
  filter->rx = fastrand () % filter->width;
  filter->ry = fastrand () % filter->height;
  filter->bx = fastrand () % filter->width;
  filter->by = fastrand () % filter->height;
  filter->rvx = -2;
  filter->rvy = -2;
  filter->bvx = 2;
  filter->bvy = 2;
  filter->phase = 0;
}

static GstFlowReturn
gst_shagadelictv_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstShagadelicTV *filter = GST_SHAGADELICTV (trans);
  guint32 *src, *dest;
  gint x, y;
  guint32 v;
  guint8 r, g, b;
  gint width, height;
  GstFlowReturn ret = GST_FLOW_OK;

  src = (guint32 *) GST_BUFFER_DATA (in);
  dest = (guint32 *) GST_BUFFER_DATA (out);

  GST_OBJECT_LOCK (filter);
  width = filter->width;
  height = filter->height;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      v = *src++ | 0x1010100;
      v = (v - 0x707060) & 0x1010100;
      v -= v >> 8;
/* Try another Babe! 
 * v = *src++;
 * *dest++ = v & ((r<<16)|(g<<8)|b);
 */
      r = ((gint8) (filter->ripple[(filter->ry + y) * width * 2 + filter->rx +
                  x] + filter->phase * 2)) >> 7;
      g = ((gint8) (filter->spiral[y * width + x] + filter->phase * 3)) >> 7;
      b = ((gint8) (filter->ripple[(filter->by + y) * width * 2 + filter->bx +
                  x] - filter->phase)) >> 7;
      *dest++ = v & ((r << 16) | (g << 8) | b);
    }
  }

  filter->phase -= 8;
  if ((filter->rx + filter->rvx) < 0 || (filter->rx + filter->rvx) >= width)
    filter->rvx = -filter->rvx;
  if ((filter->ry + filter->rvy) < 0 || (filter->ry + filter->rvy) >= height)
    filter->rvy = -filter->rvy;
  if ((filter->bx + filter->bvx) < 0 || (filter->bx + filter->bvx) >= width)
    filter->bvx = -filter->bvx;
  if ((filter->by + filter->bvy) < 0 || (filter->by + filter->bvy) >= height)
    filter->bvy = -filter->bvy;
  filter->rx += filter->rvx;
  filter->ry += filter->rvy;
  filter->bx += filter->bvx;
  filter->by += filter->bvy;
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

static void
gst_shagadelictv_finalize (GObject * object)
{
  GstShagadelicTV *filter = GST_SHAGADELICTV (object);

  if (filter->ripple)
    g_free (filter->ripple);
  filter->ripple = NULL;

  if (filter->spiral)
    g_free (filter->spiral);
  filter->spiral = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_shagadelictv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "ShagadelicTV",
      "Filter/Effect/Video",
      "Oh behave, ShagedelicTV makes images shagadelic!",
      "Wim Taymans <wim.taymans@chello.be>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_shagadelictv_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_shagadelictv_src_template);
}

static void
gst_shagadelictv_class_init (GstShagadelicTVClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->finalize = gst_shagadelictv_finalize;

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_shagadelictv_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_shagadelictv_transform);
}

static void
gst_shagadelictv_init (GstShagadelicTV * filter, GstShagadelicTVClass * klass)
{
  filter->ripple = NULL;
  filter->spiral = NULL;
}
