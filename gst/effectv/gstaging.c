/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * EffecTV is free software. * This library is free software;
 * you can redistribute it and/or
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "gsteffectv.h"

#define GST_TYPE_AGINGTV \
  (gst_agingtv_get_type())
#define GST_AGINGTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AGINGTV,GstAgingTV))
#define GST_AGINGTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstAgingTV))
#define GST_IS_AGINGTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AGINGTV))
#define GST_IS_AGINGTV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AGINGTV))

#define SCRATCH_MAX 20
typedef struct _scratch
{
  gint life;
  gint x;
  gint dx;
  gint init;
} scratch;

static int dx[8] = { 1, 1, 0, -1, -1, -1,  0, 1};
static int dy[8] = { 0, -1, -1, -1, 0, 1, 1, 1};


typedef struct _GstAgingTV GstAgingTV;
typedef struct _GstAgingTVClass GstAgingTVClass;

struct _GstAgingTV
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gint video_size;
  gint area_scale;
  gint aging_mode;

  scratch scratches[SCRATCH_MAX];
  gint scratch_lines;

  gint dust_interval;
  gint pits_interval;
};

struct _GstAgingTVClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_agingtv_details = GST_ELEMENT_DETAILS (
  "AgingTV",
  "Filter/Effect/Video",
  "Apply aging effect on video",
  "Wim Taymans <wim.taymans@chello.be>"
);


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

static void 	gst_agingtv_base_init 		(gpointer g_class);
static void 	gst_agingtv_class_init 		(GstAgingTVClass * klass);
static void 	gst_agingtv_init 		(GstAgingTV * filter);

static void 	aging_mode_switch 		(GstAgingTV *filter);

static void 	gst_agingtv_set_property 	(GObject * object, guint prop_id,
					  	 const GValue * value, GParamSpec * pspec);
static void 	gst_agingtv_get_property 	(GObject * object, guint prop_id,
					  	 GValue * value, GParamSpec * pspec);

static void 	gst_agingtv_chain 		(GstPad * pad, GstData *_data);

static GstElementClass *parent_class = NULL;
/*static guint gst_agingtv_signals[LAST_SIGNAL] = { 0 }; */

GType gst_agingtv_get_type (void)
{
  static GType agingtv_type = 0;

  if (!agingtv_type) {
    static const GTypeInfo agingtv_info = {
      sizeof (GstAgingTVClass),
      gst_agingtv_base_init,
      NULL,
      (GClassInitFunc) gst_agingtv_class_init,
      NULL,
      NULL,
      sizeof (GstAgingTV),
      0,
      (GInstanceInitFunc) gst_agingtv_init,
    };

    agingtv_type = g_type_register_static (GST_TYPE_ELEMENT, "GstAgingTV", &agingtv_info, 0);
  }
  return agingtv_type;
}

static void
gst_agingtv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get(&gst_effectv_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get(&gst_effectv_sink_template));
 
  gst_element_class_set_details (element_class, &gst_agingtv_details);
}

static void
gst_agingtv_class_init (GstAgingTVClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_agingtv_set_property;
  gobject_class->get_property = gst_agingtv_get_property;
}

static GstPadLinkReturn
gst_agingtv_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  GstAgingTV *filter;
  GstStructure *structure;

  filter = GST_AGINGTV (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &filter->width);
  gst_structure_get_int (structure, "height", &filter->height);

  filter->video_size = filter->width * filter->height;
  filter->aging_mode = 0;
  aging_mode_switch (filter);

  return gst_pad_try_set_caps (filter->srcpad, caps);
}

static void
gst_agingtv_init (GstAgingTV * filter)
{
  filter->sinkpad = gst_pad_new_from_template (
      gst_static_pad_template_get(&gst_effectv_sink_template), "sink");
  gst_pad_set_chain_function (filter->sinkpad, gst_agingtv_chain);
  gst_pad_set_link_function (filter->sinkpad, gst_agingtv_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_template (
      gst_static_pad_template_get(&gst_effectv_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
}


static unsigned int 
fastrand (void)
{
  static unsigned int fastrand_val;

  return (fastrand_val = fastrand_val * 1103515245 + 12345);
}


static void 
coloraging (guint32 *src, guint32 *dest, gint video_area)
{
  guint32 a, b;
  gint i;

  for (i = video_area; i; i--) {
    a = *src++;
    b = (a & 0xfcfcfc) >> 2;
    *dest++ = a - b + 0x181818 + ((fastrand () >> 8) & 0x101010);
  }
}


static void 
scratching (scratch *scratches, gint scratch_lines, guint32 *dest, gint width, gint height)
{
  gint i, y, y1, y2;
  guint32 *p, a, b;
  scratch *scratch;

  for (i = 0; i < scratch_lines; i++) {
    scratch = &scratches[i];

    if (scratch->life) {
      scratch->x = scratch->x + scratch->dx;
      
      if (scratch->x < 0 || scratch->x > width * 256) {
	scratch->life = 0;
	break;
      }
      p = dest + (scratch->x >> 8);
      if (scratch->init) {
	y1 = scratch->init;
	scratch->init = 0;
      } else {
	y1 = 0;
      }
      scratch->life--;
      if (scratch->life) {
	y2 = height;
      } else {
	y2 = fastrand () % height;
      }
      for (y = y1; y < y2; y++) {
	a = *p & 0xfefeff;
	a += 0x202020;
	b = a & 0x1010100;
	*p = a | (b - (b >> 8));
	p += width;
      }
    } else {
      if ((fastrand () & 0xf0000000) == 0) {
	scratch->life = 2 + (fastrand () >> 27);
	scratch->x = fastrand () % (width * 256);
	scratch->dx = ((int) fastrand ()) >> 23;
	scratch->init = (fastrand () % (height - 1)) + 1;
      }
    }
  }
}

static void 
dusts (guint32 *dest, gint width, gint height, gint dust_interval, gint area_scale)
{
  int i, j;
  int dnum;
  int d, len;
  guint x, y;

  if (dust_interval == 0) {
    if ((fastrand () & 0xf0000000) == 0) {
      dust_interval = fastrand () >> 29;
    }
    return;
  }
  dnum = area_scale * 4 + (fastrand() >> 27);
  
  for (i = 0; i < dnum; i++) {
    x = fastrand () % width;
    y = fastrand () % height;
    d = fastrand () >> 29;
    len = fastrand () % area_scale + 5;
    for (j = 0; j < len; j++) {
      dest[y * width + x] = 0x101010;
      y += dy[d];
      x += dx[d];

      if (y >= height || x >= width) break;

      d = (d + fastrand () % 3 - 1) & 7;
    }
  }
  dust_interval--;
}

static void 
pits (guint32 *dest, gint width, gint height, gint area_scale, gint pits_interval)
{
  int i, j;
  int pnum, size, pnumscale;
  guint x, y;

  pnumscale = area_scale * 2;
  if (pits_interval) {
    pnum = pnumscale + (fastrand () % pnumscale);

    pits_interval--;
  } else {
    pnum = fastrand () % pnumscale;

    if ((fastrand () & 0xf8000000) == 0) {
      pits_interval = (fastrand () >> 28) + 20;
    }
  }
  for (i = 0; i < pnum; i++) {
    x = fastrand () % (width - 1);
    y = fastrand () % (height - 1);

    size = fastrand () >> 28;

    for (j = 0; j < size; j++) {
      x = x + fastrand () % 3 - 1;
      y = y + fastrand () % 3 - 1;

      if (y >= height || x >= width) break;

      dest[y * width + x] = 0xc0c0c0;
    }
  }
}

static void 
aging_mode_switch (GstAgingTV *filter)
{
  switch (filter->aging_mode) {
    default:
    case 0:
      filter->scratch_lines = 7;
	/* Most of the parameters are tuned for 640x480 mode */
	/* area_scale is set to 10 when screen size is 640x480. */
      filter->area_scale = filter->width * filter->height / 64 / 480;
  }
  if (filter->area_scale <= 0)
    filter->area_scale = 1;
}

static void
gst_agingtv_chain (GstPad * pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstAgingTV *filter;
  guint32 *src, *dest;
  GstBuffer *outbuf;

  filter = GST_AGINGTV (gst_pad_get_parent (pad));

  src = (guint32 *) GST_BUFFER_DATA (buf);

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = (filter->video_size * sizeof (guint32));
  dest = (guint32 *) GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

  coloraging (src, dest, filter->video_size);
  scratching (filter->scratches, filter->scratch_lines, dest, filter->width, filter->height);
  pits (dest, filter->width, filter->height, filter->area_scale, filter->pits_interval);
  if(filter->area_scale > 1)
    dusts (dest, filter->width, filter->height, filter->dust_interval, filter->area_scale);
  
  gst_buffer_unref (buf);

  gst_pad_push (filter->srcpad, GST_DATA (outbuf));
}

static void
gst_agingtv_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstAgingTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AGINGTV (object));

  filter = GST_AGINGTV (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_agingtv_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstAgingTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AGINGTV (object));

  filter = GST_AGINGTV (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
