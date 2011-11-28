/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 *
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001 FUKUCHI Kentarou
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
 * This file was (probably) generated from gstvideotemplate.c,
 * gstvideotemplate.c,v 1.11 2004/01/07 08:56:45 ds Exp 
 */

/* From main.c of warp-1.1:
 *
 *      Simple DirectMedia Layer demo
 *      Realtime picture 'gooing'
 *      by sam lantinga slouken@devolution.com
 */

/**
 * SECTION:element-warptv
 *
 * WarpTV does realtime goo'ing of the video input.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! warptv ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline shows the effect of warptv on a test stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <math.h>

#include "gstwarp.h"

#include <gst/video/video.h>

#ifndef M_PI
#define M_PI    3.14159265358979323846
#endif

GST_BOILERPLATE (GstWarpTV, gst_warptv, GstVideoFilter, GST_TYPE_VIDEO_FILTER);

static void initSinTable ();
static void initOffsTable (GstWarpTV * filter);
static void initDistTable (GstWarpTV * filter);

static GstStaticPadTemplate gst_warptv_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_xBGR)
    );

static GstStaticPadTemplate gst_warptv_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_xBGR)
    );

static gboolean
gst_warptv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstWarpTV *filter = GST_WARPTV (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (incaps, 0);

  GST_OBJECT_LOCK (filter);
  if (gst_structure_get_int (structure, "width", &filter->width) &&
      gst_structure_get_int (structure, "height", &filter->height)) {
    g_free (filter->disttable);
    g_free (filter->offstable);

    filter->offstable = g_malloc (filter->height * sizeof (guint32));
    filter->disttable =
        g_malloc (filter->width * filter->height * sizeof (guint32));

    initOffsTable (filter);
    initDistTable (filter);
    ret = TRUE;
  }
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

static gint32 sintable[1024 + 256];

static void
initSinTable (void)
{
  gint32 *tptr, *tsinptr;
  gint i;

  tsinptr = tptr = sintable;

  for (i = 0; i < 1024; i++)
    *tptr++ = (int) (sin (i * M_PI / 512) * 32767);

  for (i = 0; i < 256; i++)
    *tptr++ = *tsinptr++;
}

static void
initOffsTable (GstWarpTV * filter)
{
  gint y;

  for (y = 0; y < filter->height; y++) {
    filter->offstable[y] = y * filter->width;
  }
}

static void
initDistTable (GstWarpTV * filter)
{
  gint32 halfw, halfh, *distptr;
  gint x, y;
  float m;

  halfw = filter->width >> 1;
  halfh = filter->height >> 1;

  distptr = filter->disttable;

  m = sqrt ((double) (halfw * halfw + halfh * halfh));

  for (y = -halfh; y < halfh; y++)
    for (x = -halfw; x < halfw; x++)
#ifdef PS2
      *distptr++ = ((int) ((sqrtf (x * x + y * y) * 511.9999) / m)) << 1;
#else
      *distptr++ = ((int) ((sqrt (x * x + y * y) * 511.9999) / m)) << 1;
#endif
}

static GstFlowReturn
gst_warptv_transform (GstBaseTransform * trans, GstBuffer * in, GstBuffer * out)
{
  GstWarpTV *warptv = GST_WARPTV (trans);
  gint width, height;
  guint32 *src = (guint32 *) GST_BUFFER_DATA (in);
  guint32 *dest = (guint32 *) GST_BUFFER_DATA (out);
  gint xw, yw, cw;
  gint32 c, i, x, y, dx, dy, maxx, maxy;
  gint32 skip, *ctptr, *distptr;
  gint32 *ctable;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_OBJECT_LOCK (warptv);
  width = warptv->width;
  height = warptv->height;

  xw = (gint) (sin ((warptv->tval + 100) * M_PI / 128) * 30);
  yw = (gint) (sin ((warptv->tval) * M_PI / 256) * -35);
  cw = (gint) (sin ((warptv->tval - 70) * M_PI / 64) * 50);
  xw += (gint) (sin ((warptv->tval - 10) * M_PI / 512) * 40);
  yw += (gint) (sin ((warptv->tval + 30) * M_PI / 512) * 40);

  ctptr = warptv->ctable;
  distptr = warptv->disttable;
  ctable = warptv->ctable;

  skip = 0;                     /* video_width*sizeof(RGB32)/4 - video_width;; */
  c = 0;

  for (x = 0; x < 512; x++) {
    i = (c >> 3) & 0x3FE;
    *ctptr++ = ((sintable[i] * yw) >> 15);
    *ctptr++ = ((sintable[i + 256] * xw) >> 15);
    c += cw;
  }
  maxx = width - 2;
  maxy = height - 2;

  for (y = 0; y < height - 1; y++) {
    for (x = 0; x < width; x++) {
      i = *distptr++;
      dx = ctable[i + 1] + x;
      dy = ctable[i] + y;

      if (dx < 0)
        dx = 0;
      else if (dx > maxx)
        dx = maxx;

      if (dy < 0)
        dy = 0;
      else if (dy > maxy)
        dy = maxy;
      *dest++ = src[warptv->offstable[dy] + dx];
    }
    dest += skip;
  }

  warptv->tval = (warptv->tval + 1) & 511;
  GST_OBJECT_UNLOCK (warptv);

  return ret;
}

static gboolean
gst_warptv_start (GstBaseTransform * trans)
{
  GstWarpTV *warptv = GST_WARPTV (trans);

  warptv->tval = 0;

  return TRUE;
}

static void
gst_warptv_finalize (GObject * object)
{
  GstWarpTV *warptv = GST_WARPTV (object);

  g_free (warptv->offstable);
  warptv->offstable = NULL;
  g_free (warptv->disttable);
  warptv->disttable = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_warptv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "WarpTV effect",
      "Filter/Effect/Video",
      "WarpTV does realtime goo'ing of the video input",
      "Sam Lantinga <slouken@devolution.com>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_warptv_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_warptv_src_template);
}

static void
gst_warptv_class_init (GstWarpTVClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->finalize = gst_warptv_finalize;

  trans_class->start = GST_DEBUG_FUNCPTR (gst_warptv_start);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_warptv_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_warptv_transform);

  initSinTable ();
}

static void
gst_warptv_init (GstWarpTV * warptv, GstWarpTVClass * klass)
{
  /* nothing to do */
}
