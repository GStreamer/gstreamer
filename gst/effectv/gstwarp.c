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

#ifndef M_PI
#define M_PI    3.14159265358979323846
#endif

#define gst_warptv_parent_class parent_class
G_DEFINE_TYPE (GstWarpTV, gst_warptv, GST_TYPE_VIDEO_FILTER);

static void initSinTable ();
static void initOffsTable (GstWarpTV * filter, gint width, gint height);
static void initDistTable (GstWarpTV * filter, gint width, gint height);

static GstStaticPadTemplate gst_warptv_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGBx, xRGB, BGRx, xBGR }"))
    );

static GstStaticPadTemplate gst_warptv_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGBx, xRGB, BGRx, xBGR }"))
    );

static gboolean
gst_warptv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstWarpTV *filter = GST_WARPTV (btrans);
  GstVideoInfo info;
  gint width, height;

  if (!gst_video_info_from_caps (&info, incaps))
    goto invalid_caps;

  filter->info = info;

  width = GST_VIDEO_INFO_WIDTH (&info);
  height = GST_VIDEO_INFO_HEIGHT (&info);

  g_free (filter->disttable);
  g_free (filter->offstable);
  filter->offstable = g_malloc (height * sizeof (guint32));
  filter->disttable = g_malloc (width * height * sizeof (guint32));
  initOffsTable (filter, width, height);
  initDistTable (filter, width, height);

  return TRUE;

  /* ERRORS */
invalid_caps:
  {
    GST_DEBUG_OBJECT (filter, "invalid caps received");
    return FALSE;
  }
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
initOffsTable (GstWarpTV * filter, gint width, gint height)
{
  gint y;

  for (y = 0; y < height; y++) {
    filter->offstable[y] = y * width;
  }
}

static void
initDistTable (GstWarpTV * filter, gint width, gint height)
{
  gint32 halfw, halfh, *distptr;
  gint x, y;
#ifdef PS2
  float m;
#else
  float m;
#endif

  halfw = width >> 1;
  halfh = height >> 1;

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
  gint xw, yw, cw;
  gint32 c, i, x, y, dx, dy, maxx, maxy;
  gint32 skip, *ctptr, *distptr;
  gint32 *ctable;
  guint32 *src, *dest;
  GstVideoFrame in_frame, out_frame;

  gst_video_frame_map (&in_frame, &warptv->info, in, GST_MAP_READ);
  gst_video_frame_map (&out_frame, &warptv->info, out, GST_MAP_WRITE);

  src = GST_VIDEO_FRAME_PLANE_DATA (&in_frame, 0);
  dest = GST_VIDEO_FRAME_PLANE_DATA (&out_frame, 0);

  width = GST_VIDEO_FRAME_WIDTH (&in_frame);
  height = GST_VIDEO_FRAME_HEIGHT (&in_frame);

  GST_OBJECT_LOCK (warptv);
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

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  return GST_FLOW_OK;
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
gst_warptv_class_init (GstWarpTVClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->finalize = gst_warptv_finalize;

  gst_element_class_set_details_simple (gstelement_class, "WarpTV effect",
      "Filter/Effect/Video",
      "WarpTV does realtime goo'ing of the video input",
      "Sam Lantinga <slouken@devolution.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_warptv_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_warptv_src_template));

  trans_class->start = GST_DEBUG_FUNCPTR (gst_warptv_start);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_warptv_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_warptv_transform);

  initSinTable ();
}

static void
gst_warptv_init (GstWarpTV * warptv)
{
  gst_pad_use_fixed_caps (GST_BASE_TRANSFORM_SRC_PAD (warptv));
  gst_pad_use_fixed_caps (GST_BASE_TRANSFORM_SINK_PAD (warptv));
}
