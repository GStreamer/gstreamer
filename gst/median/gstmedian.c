/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <string.h>
#include <gstmedian.h>


static GstElementDetails median_details = {
  "Median effect",
  "Filter/Effect",
  "apply a median filter to an image",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};

GST_PAD_TEMPLATE_FACTORY (median_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
   "median_src",
   "video/raw",
     "format",   GST_PROPS_FOURCC (GST_STR_FOURCC ("I420"))
  )
)

GST_PAD_TEMPLATE_FACTORY (median_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
   "median_src",
   "video/raw",
     "format",   GST_PROPS_FOURCC (GST_STR_FOURCC ("I420"))
  )
)


/* Median signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_ACTIVE,
  ARG_FILTERSIZE,
  ARG_LUM_ONLY
};

static GType 	gst_median_get_type 	(void);
static void	gst_median_class_init	(GstMedianClass *klass);
static void	gst_median_init		(GstMedian *median);

static void	median_5		(unsigned char *src, unsigned char *dest, int height, int width);
static void	median_9		(unsigned char *src, unsigned char *dest, int height, int width);
static void	gst_median_chain	(GstPad *pad, GstBuffer *buf);

static void	gst_median_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_median_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstElementClass *parent_class = NULL;
/*static guint gst_median_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_median_get_type (void)
{
  static GType median_type = 0;

  if (!median_type) {
    static const GTypeInfo median_info = {
      sizeof(GstMedianClass),      NULL,      NULL,      (GClassInitFunc)gst_median_class_init,
      NULL,
      NULL,
      sizeof(GstMedian),
      0,
      (GInstanceInitFunc)gst_median_init,
    };
    median_type = g_type_register_static(GST_TYPE_ELEMENT, "GstMedian", &median_info, 0);
  }
  return median_type;
}

static void
gst_median_class_init (GstMedianClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ACTIVE,
    g_param_spec_boolean("active","active","active",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FILTERSIZE,
    g_param_spec_int("filtersize","filtersize","filtersize",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_LUM_ONLY,
    g_param_spec_boolean("lum_only","lum_only","lum_only",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */

  gobject_class->set_property = gst_median_set_property;
  gobject_class->get_property = gst_median_get_property;
}

static gboolean
gst_median_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstMedian *filter;

  filter = GST_MEDIAN (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;

  gst_caps_get_int (caps, "width", &filter->width);
  gst_caps_get_int (caps, "height", &filter->height);

  return GST_PAD_CONNECT_OK;
}

void gst_median_init (GstMedian *median)
{
  median->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (median_sink_factory), "sink");
  gst_pad_set_connect_function (median->sinkpad, gst_median_sinkconnect);
  gst_pad_set_chain_function (median->sinkpad, gst_median_chain);
  gst_element_add_pad (GST_ELEMENT (median), median->sinkpad);

  median->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (median_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (median), median->srcpad);

  median->filtersize = 5;
  median->lum_only = TRUE;
  median->active = TRUE;
}

#define PIX_SORT(a,b) { if ((a)>(b)) PIX_SWAP((a),(b)); }
#define PIX_SWAP(a,b) { unsigned char temp=(a);(a)=(b);(b)=temp; }

static void
median_5 (unsigned char *src, unsigned char *dest, int width, int height)
{
  int nLastRow;
  int nLastCol;
  unsigned char p[9];
  int i, j, k;

  nLastCol = width - 1;
  nLastRow = height - 1;

  /*copy the top and bottom rows into the result array */
  for (i=0; i<width; i++) {
    dest[i] = src[i];
    dest[nLastRow * width + i] = src[nLastRow * width + i];
  }
  dest[i] = src[i];

  nLastCol--;
  nLastRow--;

  /* process the interior pixels */
  i = width + 1;
  for (k=0; k < nLastRow; k++) {
    for (j=0; j < nLastCol; j++, i++) {
      p[0] = src[i-width];
      p[1] = src[i-1];
      p[2] = src[i];
      p[3] = src[i+1];
      p[4] = src[i+width];
      PIX_SORT(p[0],p[1]) ; PIX_SORT(p[3],p[4]) ; PIX_SORT(p[0],p[3]) ;
      PIX_SORT(p[1],p[4]) ; PIX_SORT(p[1],p[2]) ; PIX_SORT(p[2],p[3]) ;
      PIX_SORT(p[1],p[2]) ;
      dest[i] = p[2];
    }
    dest[i] = src[i];
    i++;
    dest[i] = src[i];
    i++;
  }
  dest[i] = src[i];
  i++;
}

static void
median_9 (unsigned char *src, unsigned char *dest, int width, int height)
{
  int nLastRow;
  int nLastCol;
  unsigned char p[9];
  int i, j, k;

  nLastCol = width - 1;
  nLastRow = height - 1;

  /*copy the top and bottom rows into the result array */
  for (i=0; i<width; i++) {
    dest[i] = src[i];
    dest[nLastRow * width + i] = src[nLastRow * width + i];
  }
  dest[i] = src[i];

  nLastCol--;
  nLastRow--;

  /* process the interior pixels */
  i = width + 1;
  for (k=0; k < nLastRow; k++) {
    for (j=0; j < nLastCol; j++, i++) {
      p[0] = src[i-width-1];
      p[1] = src[i-width];
      p[2] = src[i-width+1];
      p[3] = src[i-1];
      p[4] = src[i];
      p[5] = src[i+1];
      p[6] = src[i+width-1];
      p[7] = src[i+width];
      p[8] = src[i+width+1];
      PIX_SORT(p[1], p[2]) ; PIX_SORT(p[4], p[5]) ; PIX_SORT(p[7], p[8]) ; 
      PIX_SORT(p[0], p[1]) ; PIX_SORT(p[3], p[4]) ; PIX_SORT(p[6], p[7]) ; 
      PIX_SORT(p[1], p[2]) ; PIX_SORT(p[4], p[5]) ; PIX_SORT(p[7], p[8]) ; 
      PIX_SORT(p[0], p[3]) ; PIX_SORT(p[5], p[8]) ; PIX_SORT(p[4], p[7]) ; 
      PIX_SORT(p[3], p[6]) ; PIX_SORT(p[1], p[4]) ; PIX_SORT(p[2], p[5]) ; 
      PIX_SORT(p[4], p[7]) ; PIX_SORT(p[4], p[2]) ; PIX_SORT(p[6], p[4]) ; 
      PIX_SORT(p[4], p[2]) ;
      dest[i] = p[4];
    }
    dest[i] = src[i];
    i++;
    dest[i] = src[i];
    i++;
  }
  dest[i] = src[i];
  i++;
}

static void
gst_median_chain (GstPad *pad, GstBuffer *buf)
{
  GstMedian *median;
  guchar *data;
  gulong size;
  GstBuffer *outbuf;
/*  GstMeta *meta; */
  int lumsize, chromsize;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  median = GST_MEDIAN (GST_OBJECT_PARENT (pad));

  if (!median->active) {
    gst_pad_push(median->srcpad,buf);
    return;
  }

  data = GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  GST_DEBUG (0,"median: have buffer of %d", GST_BUFFER_SIZE(buf));

  outbuf = gst_buffer_new();
  GST_BUFFER_DATA(outbuf) = g_malloc(GST_BUFFER_SIZE(buf));
  GST_BUFFER_SIZE(outbuf) = GST_BUFFER_SIZE(buf);

  lumsize = median->width * median->height;
  chromsize = lumsize/4;

  if (median->filtersize == 5) {
    median_5(data, GST_BUFFER_DATA(outbuf), median->width, median->height);
    if (!median->lum_only) {
      median_5(data+lumsize, GST_BUFFER_DATA(outbuf)+lumsize, median->width/2, median->height/2);
      median_5(data+lumsize+chromsize, GST_BUFFER_DATA(outbuf)+lumsize+chromsize, median->width/2, median->height/2);
    }
    else {
      memcpy (GST_BUFFER_DATA (outbuf)+lumsize, data+lumsize, chromsize*2);
    }
  }
  else {
    median_9(data, GST_BUFFER_DATA(outbuf), median->width, median->height);
    if (!median->lum_only) {
      median_9(data+lumsize, GST_BUFFER_DATA(outbuf)+lumsize, median->width/2, median->height/2);
      median_9(data+lumsize+chromsize, GST_BUFFER_DATA(outbuf)+lumsize+chromsize, median->width/2, median->height/2);
    }
    else {
      memcpy (GST_BUFFER_DATA (outbuf)+lumsize, data+lumsize, chromsize*2);
    }
  }
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

  gst_buffer_unref(buf);

  gst_pad_push(median->srcpad,outbuf);
}

static void
gst_median_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstMedian *median;
  gint argvalue;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MEDIAN(object));
  median = GST_MEDIAN(object);

  switch (prop_id) {
    case ARG_FILTERSIZE:
      argvalue = g_value_get_int (value);
      if (argvalue != 5 && argvalue != 9) {
	g_warning ("median: invalid filtersize (%d), must be 5 or 9\n", argvalue);
      }
      else {
	median->filtersize = argvalue;
      }
      break;
    case ARG_ACTIVE:
      median->active = g_value_get_boolean (value);
      break;
    case ARG_LUM_ONLY:
      median->lum_only = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void
gst_median_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstMedian *median;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MEDIAN(object));
  median = GST_MEDIAN(object);

  switch (prop_id) {
    case ARG_FILTERSIZE:
      g_value_set_int (value, median->filtersize);
      break;
    case ARG_ACTIVE:
      g_value_set_boolean (value, median->active);
      break;
    case ARG_LUM_ONLY:
      g_value_set_boolean (value, median->lum_only);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new("median",GST_TYPE_MEDIAN,
                                   &median_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (median_sink_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (median_src_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "median",
  plugin_init
};

