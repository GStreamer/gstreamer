/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV is free software. We release this product under the terms of the
 * GNU General Public License version 2. The license is included in the file
 * COPYING.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 */
/*
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * From main.c of warp-1.1:
 *
 *      Simple DirectMedia Layer demo
 *      Realtime picture 'gooing'
 *      Released under GPL
 *      by sam lantinga slouken@devolution.com
 */

#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include "gsteffectv.h"

#ifndef M_PI
#define M_PI	3.14159265358979323846
#endif

#define GST_TYPE_WARPTV \
  (gst_warptv_get_type())
#define GST_WARPTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WARPTV,GstWarpTV))
#define GST_WARPTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstWarpTV))
#define GST_IS_WARPTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WARPTV))
#define GST_IS_WARPTV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WARPTV))

typedef struct _GstWarpTV GstWarpTV;
typedef struct _GstWarpTVClass GstWarpTVClass;

struct _GstWarpTV
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gint *offstable;
  gint32 *disttable;
  gint32 ctable[1024];
  gint32 sintable[1024+256];
  gint tval;
};

struct _GstWarpTVClass
{
  GstElementClass parent_class;
};

GstElementDetails gst_warptv_details = {
  "WarpTV",
  "Filter/Effect",
  "WarpTV does realtime goo'ing of the video input",
  VERSION,
  "Sam Lantinga <slouken@devolution.com>",
  "Wim Taymans <wim.taymans@chello.be>, "
  "(C) 2001 FUKUCHI Kentarou",
};


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};

static void 	gst_warptv_class_init 		(GstWarpTVClass * klass);
static void 	gst_warptv_init 		(GstWarpTV * filter);

static void 	gst_warptv_initialize 		(GstWarpTV *filter);

static void 	gst_warptv_set_property 	(GObject * object, guint prop_id,
					  	 const GValue * value, GParamSpec * pspec);
static void 	gst_warptv_get_property 	(GObject * object, guint prop_id,
					  	 GValue * value, GParamSpec * pspec);

static void 	gst_warptv_chain 		(GstPad * pad, GstBuffer * buf);

static GstElementClass *parent_class = NULL;
/*static guint gst_warptv_signals[LAST_SIGNAL] = { 0 }; */

GType gst_warptv_get_type (void)
{
  static GType warptv_type = 0;

  if (!warptv_type) {
    static const GTypeInfo warptv_info = {
      sizeof (GstWarpTVClass), NULL,
      NULL,
      (GClassInitFunc) gst_warptv_class_init,
      NULL,
      NULL,
      sizeof (GstWarpTV),
      0,
      (GInstanceInitFunc) gst_warptv_init,
    };

    warptv_type = g_type_register_static (GST_TYPE_ELEMENT, "GstWarpTV", &warptv_info, 0);
  }
  return warptv_type;
}

static void
gst_warptv_class_init (GstWarpTVClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_warptv_set_property;
  gobject_class->get_property = gst_warptv_get_property;
}

static GstPadConnectReturn
gst_warptv_sinkconnect (GstPad * pad, GstCaps * caps)
{
  GstWarpTV *filter;

  filter = GST_WARPTV (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;

  gst_caps_get_int (caps, "width", &filter->width);
  gst_caps_get_int (caps, "height", &filter->height);

  gst_warptv_initialize (filter);

  if (gst_pad_try_set_caps (filter->srcpad, caps)) {
    return GST_PAD_CONNECT_OK;
  }

  return GST_PAD_CONNECT_REFUSED;
}

static void
gst_warptv_init (GstWarpTV * filter)
{
  filter->sinkpad = gst_pad_new_from_template (gst_effectv_sink_factory (), "sink");
  gst_pad_set_chain_function (filter->sinkpad, gst_warptv_chain);
  gst_pad_set_connect_function (filter->sinkpad, gst_warptv_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_template (gst_effectv_src_factory (), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->tval = 0;
  filter->disttable = NULL;
  filter->offstable = NULL;
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

static void 
gst_warptv_initialize (GstWarpTV *filter) 
{
  g_free (filter->disttable);
  g_free (filter->offstable);

  filter->offstable = (guint32 *) g_malloc (filter->height * sizeof (guint32));      
  filter->disttable = g_malloc (filter->width * filter->height * sizeof (guint32));

  initSinTable (filter);
  initOffsTable (filter);
  initDistTable (filter);
}

static void
gst_warptv_chain (GstPad * pad, GstBuffer * buf)
{
  GstWarpTV *filter;
  guint32 *src, *dest;
  gint xw,yw,cw;
  GstBuffer *outbuf;
  gint32 c,i, x,y, dx,dy, maxx, maxy;
  gint32 width, height, skip, *ctptr, *distptr;
  gint32 *sintable, *ctable;

  filter = GST_WARPTV (gst_pad_get_parent (pad));

  src = (guint32 *) GST_BUFFER_DATA (buf);

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = (filter->width * filter->height * sizeof(guint32));
  dest = (guint32 *) GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  
  xw  = (gint) (sin ((filter->tval + 100) * M_PI / 128) * 30);
  yw  = (gint) (sin ((filter->tval) * M_PI / 256) * -35);
  cw  = (gint) (sin ((filter->tval - 70) * M_PI / 64) * 50);
  xw += (gint) (sin ((filter->tval - 10) * M_PI / 512) * 40);
  yw += (gint) (sin ((filter->tval + 30) * M_PI / 512) * 40);	  

  ctptr = filter->ctable;
  distptr = filter->disttable;
  width = filter->width;
  height = filter->height;
  sintable = filter->sintable;
  ctable = filter->ctable;

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
      *dest++ = src[filter->offstable[dy] + dx]; 
    }
    dest += skip;
  }

  filter->tval = (filter->tval + 1) & 511;

  gst_buffer_unref (buf);

  gst_pad_push (filter->srcpad, outbuf);
}

static void
gst_warptv_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstWarpTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_WARPTV (object));

  filter = GST_WARPTV (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_warptv_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstWarpTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_WARPTV (object));

  filter = GST_WARPTV (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
