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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/gstvideofilter.h>

#include <string.h>
#include <math.h>

#include <gst/video/video.h>

#ifndef M_PI
#define M_PI    3.14159265358979323846
#endif

#define GST_TYPE_WARPTV \
  (gst_warptv_get_type())
#define GST_WARPTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WARPTV,GstWarpTV))
#define GST_WARPTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WARPTV,GstWarpTVClass))
#define GST_IS_WARPTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WARPTV))
#define GST_IS_WARPTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WARPTV))

typedef struct _GstWarpTV GstWarpTV;
typedef struct _GstWarpTVClass GstWarpTVClass;

struct _GstWarpTV
{
  GstVideoFilter videofilter;

  gint width, height;
  gint *offstable;
  gint32 *disttable;
  gint32 ctable[1024];
  gint32 sintable[1024 + 256];
  gint tval;
};

struct _GstWarpTVClass
{
  GstVideoFilterClass parent_class;
};

GType gst_warptv_get_type (void);

static void initSinTable (GstWarpTV * filter);
static void initOffsTable (GstWarpTV * filter);
static void initDistTable (GstWarpTV * filter);

static const GstElementDetails warptv_details =
GST_ELEMENT_DETAILS ("WarpTV effect",
    "Filter/Effect/Video",
    "WarpTV does realtime goo'ing of the video input",
    "Sam Lantinga <slouken@devolution.com>");

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

static GstVideoFilterClass *parent_class = NULL;

static gboolean
gst_warptv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstWarpTV *filter = GST_WARPTV (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (incaps, 0);

  if (gst_structure_get_int (structure, "width", &filter->width) &&
      gst_structure_get_int (structure, "height", &filter->height)) {
    g_free (filter->disttable);
    g_free (filter->offstable);

    filter->offstable = g_malloc (filter->height * sizeof (guint32));
    filter->disttable =
        g_malloc (filter->width * filter->height * sizeof (guint32));

    initSinTable (filter);
    initOffsTable (filter);
    initDistTable (filter);
    ret = TRUE;
  }

  return ret;
}

static gboolean
gst_warptv_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    guint * size)
{
  GstWarpTV *filter;
  GstStructure *structure;
  gboolean ret = FALSE;
  gint width, height;

  filter = GST_WARPTV (btrans);

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height)) {
    *size = width * height * 32 / 8;
    ret = TRUE;
    GST_DEBUG_OBJECT (filter, "our frame size is %d bytes (%dx%d)", *size,
        width, height);
  }

  return ret;
}

static void
initSinTable (GstWarpTV * filter)
{
  gint32 *tptr, *tsinptr;
  double i;

  tsinptr = tptr = filter->sintable;

  for (i = 0; i < 1024; i++)
    *tptr++ = (int) (sin (i * M_PI / 512) * 32767);

  for (i = 0; i < 256; i++)
    *tptr++ = *tsinptr++;
}

static void
initOffsTable (GstWarpTV * filter)
{
  int y;

  for (y = 0; y < filter->height; y++) {
    filter->offstable[y] = y * filter->width;
  }
}

static void
initDistTable (GstWarpTV * filter)
{
  gint32 halfw, halfh, *distptr;

#ifdef PS2
  float x, y, m;
#else
  double x, y, m;
#endif

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
  int width = warptv->width;
  int height = warptv->height;
  guint32 *src = (guint32 *) GST_BUFFER_DATA (in);
  guint32 *dest = (guint32 *) GST_BUFFER_DATA (out);
  gint xw, yw, cw;
  gint32 c, i, x, y, dx, dy, maxx, maxy;
  gint32 skip, *ctptr, *distptr;
  gint32 *sintable, *ctable;
  GstFlowReturn ret = GST_FLOW_OK;

  gst_buffer_copy_metadata (out, in, GST_BUFFER_COPY_TIMESTAMPS);

  xw = (gint) (sin ((warptv->tval + 100) * M_PI / 128) * 30);
  yw = (gint) (sin ((warptv->tval) * M_PI / 256) * -35);
  cw = (gint) (sin ((warptv->tval - 70) * M_PI / 64) * 50);
  xw += (gint) (sin ((warptv->tval - 10) * M_PI / 512) * 40);
  yw += (gint) (sin ((warptv->tval + 30) * M_PI / 512) * 40);

  ctptr = warptv->ctable;
  distptr = warptv->disttable;
  sintable = warptv->sintable;
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

  return ret;
}

static void
gst_warptv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &warptv_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_warptv_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_warptv_src_template));
}

static void
gst_warptv_class_init (gpointer klass, gpointer class_data)
{
  GstBaseTransformClass *trans_class;

  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_warptv_set_caps);
  trans_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_warptv_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_warptv_transform);
}

static void
gst_warptv_init (GTypeInstance * instance, gpointer g_class)
{
}

GType
gst_warptv_get_type (void)
{
  static GType warptv_type = 0;

  if (!warptv_type) {
    static const GTypeInfo warptv_info = {
      sizeof (GstWarpTVClass),
      gst_warptv_base_init,
      NULL,
      gst_warptv_class_init,
      NULL,
      NULL,
      sizeof (GstWarpTV),
      0,
      gst_warptv_init,
    };

    warptv_type = g_type_register_static (GST_TYPE_VIDEO_FILTER,
        "GstWarpTV", &warptv_info, 0);
  }
  return warptv_type;
}
