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

#include <gst/gst.h>
#include <gstvideofilter.h>
#include <string.h>
#include <math.h>
#include "gsteffectv.h"

#ifndef M_PI
#define M_PI	3.14159265358979323846
#endif


#define GST_TYPE_WARPTV \
  (gst_warptv_get_type())
#define GST_WARPTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WARPTV,GstWarpTV))
#define GST_WARPTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WARPTV,GstWarpTVClass))
#define GST_IS_WARPTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WARPTV))
#define GST_IS_WARPTV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WARPTV))

typedef struct _GstWarpTV GstWarpTV;
typedef struct _GstWarpTVClass GstWarpTVClass;

struct _GstWarpTV {
  GstVideofilter videofilter;

  gint width, height;
  gint *offstable;
  gint32 *disttable;
  gint32 ctable[1024];
  gint32 sintable[1024+256];
  gint tval;
};

struct _GstWarpTVClass {
  GstVideofilterClass parent_class;
};


/* GstWarpTV signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void	gst_warptv_base_init	(gpointer g_class);
static void	gst_warptv_class_init	(gpointer g_class, gpointer class_data);
static void	gst_warptv_init		(GTypeInstance *instance, gpointer g_class);

static void	gst_warptv_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_warptv_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void gst_warptv_setup(GstVideofilter *videofilter);
static void initSinTable (GstWarpTV *filter);
static void initOffsTable (GstWarpTV *filter);
static void initDistTable (GstWarpTV *filter);
static void gst_warptv_rgb32 (GstVideofilter *videofilter, void *d, void *s);

GType
gst_warptv_get_type (void)
{
  static GType warptv_type = 0;

  if (!warptv_type) {
    static const GTypeInfo warptv_info = {
      sizeof(GstWarpTVClass),
      gst_warptv_base_init,
      NULL,
      gst_warptv_class_init,
      NULL,
      NULL,
      sizeof(GstWarpTV),
      0,
      gst_warptv_init,
    };
    warptv_type = g_type_register_static(GST_TYPE_VIDEOFILTER,
        "GstWarpTV", &warptv_info, 0);
  }
  return warptv_type;
}

static GstVideofilterFormat gst_warptv_formats[] = {
  { "RGB ", 32, gst_warptv_rgb32, 24, G_BIG_ENDIAN, 0x00ff0000, 0x0000ff00, 0x000000ff },
  { "RGB ", 32, gst_warptv_rgb32, 24, G_BIG_ENDIAN, 0xff000000, 0x00ff0000, 0x0000ff00 },
  { "RGB ", 32, gst_warptv_rgb32, 24, G_BIG_ENDIAN, 0x000000ff, 0x0000ff00, 0x00ff0000 },
  { "RGB ", 32, gst_warptv_rgb32, 24, G_BIG_ENDIAN, 0x0000ff00, 0x00ff0000, 0xff000000 },
};

static void
gst_warptv_base_init (gpointer g_class)
{
  static GstElementDetails warptv_details = GST_ELEMENT_DETAILS (
    "WarpTV",
    "Filter/Effect/Video",
    "WarpTV does realtime goo'ing of the video input",
    "Sam Lantinga <slouken@devolution.com>"
  );
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);
  int i;
  
  gst_element_class_set_details (element_class, &warptv_details);

  for(i=0;i<G_N_ELEMENTS(gst_warptv_formats);i++){
    gst_videofilter_class_add_format(videofilter_class,
	gst_warptv_formats + i);
  }

  gst_videofilter_class_add_pad_templates (GST_VIDEOFILTER_CLASS (g_class));
}

static void
gst_warptv_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstVideofilterClass *videofilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  videofilter_class = GST_VIDEOFILTER_CLASS (g_class);

#if 0
  g_object_class_install_property(gobject_class, ARG_METHOD,
      g_param_spec_enum("method","method","method",
      GST_TYPE_WARPTV_METHOD, GST_WARPTV_METHOD_1,
      G_PARAM_READWRITE));
#endif

  gobject_class->set_property = gst_warptv_set_property;
  gobject_class->get_property = gst_warptv_get_property;

  videofilter_class->setup = gst_warptv_setup;
}

static void
gst_warptv_init (GTypeInstance *instance, gpointer g_class)
{
  GstWarpTV *warptv = GST_WARPTV (instance);
  GstVideofilter *videofilter;

  GST_DEBUG("gst_warptv_init");

  videofilter = GST_VIDEOFILTER(warptv);

  /* do stuff */
}

static void
gst_warptv_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstWarpTV *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_WARPTV(object));
  src = GST_WARPTV(object);

  GST_DEBUG("gst_warptv_set_property");
  switch (prop_id) {
#if 0
    case ARG_METHOD:
      src->method = g_value_get_enum (value);
      break;
#endif
    default:
      break;
  }
}

static void
gst_warptv_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstWarpTV *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_WARPTV(object));
  src = GST_WARPTV(object);

  switch (prop_id) {
#if 0
    case ARG_METHOD:
      g_value_set_enum (value, src->method);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void gst_warptv_setup(GstVideofilter *videofilter)
{
  GstWarpTV *warptv;
  int width = gst_videofilter_get_input_width(videofilter);
  int height = gst_videofilter_get_input_height(videofilter);

  g_return_if_fail(GST_IS_WARPTV(videofilter));
  warptv = GST_WARPTV(videofilter);

  /* if any setup needs to be done, do it here */

  warptv->width = width;
  warptv->height = height;
#if 0
  /* FIXME this should be reset in PAUSE->READY, not here */
  warptv->tval = 0;
#endif

  g_free (warptv->disttable);
  g_free (warptv->offstable);

  warptv->offstable = (guint32 *) g_malloc (height * sizeof (guint32));      
  warptv->disttable = g_malloc (width * height * sizeof (guint32));

  initSinTable (warptv);
  initOffsTable (warptv);
  initDistTable (warptv);
}

static void 
initSinTable (GstWarpTV *filter) 
{
  gint32	*tptr, *tsinptr;
  double	i;

  tsinptr = tptr = filter->sintable;

  for (i = 0; i < 1024; i++)
    *tptr++ = (int) (sin (i * M_PI / 512) * 32767);

  for (i = 0; i < 256; i++)
    *tptr++ = *tsinptr++;
}

static void 
initOffsTable (GstWarpTV *filter) 
{
  int y;
	
  for (y = 0; y < filter->height; y++) {
    filter->offstable[y] = y * filter->width;
  }
}
      
static void 
initDistTable (GstWarpTV *filter) 
{
  gint32 halfw, halfh, *distptr;
#ifdef PS2
  float x,y,m;
#else
  double x,y,m;
#endif

  halfw = filter->width>> 1;
  halfh = filter->height >> 1;

  distptr = filter->disttable;

  m = sqrt ((double)(halfw * halfw + halfh * halfh));

  for (y = -halfh; y < halfh; y++)
    for (x= -halfw; x < halfw; x++)
#ifdef PS2
      *distptr++ = ((int) ((sqrtf (x * x + y * y) * 511.9999) / m)) << 1;
#else
      *distptr++ = ((int) ((sqrt (x * x + y * y) * 511.9999) / m)) << 1;
#endif
}

static void gst_warptv_rgb32 (GstVideofilter *videofilter,
    void *d, void *s)
{
  GstWarpTV *warptv;
  int width = gst_videofilter_get_input_width(videofilter);
  int height = gst_videofilter_get_input_height(videofilter);
  guint32 *src = s;
  guint32 *dest = d;
  gint xw,yw,cw;
  gint32 c,i, x,y, dx,dy, maxx, maxy;
  gint32 skip, *ctptr, *distptr;
  gint32 *sintable, *ctable;

  g_return_if_fail(GST_IS_WARPTV(videofilter));
  warptv = GST_WARPTV(videofilter);

  xw  = (gint) (sin ((warptv->tval + 100) * M_PI / 128) * 30);
  yw  = (gint) (sin ((warptv->tval) * M_PI / 256) * -35);
  cw  = (gint) (sin ((warptv->tval - 70) * M_PI / 64) * 50);
  xw += (gint) (sin ((warptv->tval - 10) * M_PI / 512) * 40);
  yw += (gint) (sin ((warptv->tval + 30) * M_PI / 512) * 40);	  

  ctptr = warptv->ctable;
  distptr = warptv->disttable;
  sintable = warptv->sintable;
  ctable = warptv->ctable;

  skip = 0 ; /* video_width*sizeof(RGB32)/4 - video_width;; */
  c = 0;

  for (x = 0; x < 512; x++) {
    i = (c >> 3) & 0x3FE;
    *ctptr++ = ((sintable[i] * yw) >> 15);
    *ctptr++ = ((sintable[i + 256] * xw) >> 15);
    c += cw;
  }
  maxx = width - 2; maxy = height - 2;

  for (y = 0; y < height - 1; y++) {
    for (x = 0; x < width; x++) {
      i = *distptr++; 
      dx = ctable [i + 1] + x; 
      dy = ctable [i] + y;	 

      if (dx < 0) dx = 0; 
      else if (dx > maxx) dx = maxx; 
   
      if (dy < 0) dy = 0; 
      else if (dy > maxy) dy = maxy; 
      *dest++ = src[warptv->offstable[dy] + dx]; 
    }
    dest += skip;
  }

  warptv->tval = (warptv->tval + 1) & 511;
}

