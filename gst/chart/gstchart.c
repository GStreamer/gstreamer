/* gstchart.c: implementation of chart drawing element
 * Copyright (C) <2001> Richard Boulton <richard@tartarus.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#define GST_TYPE_CHART (gst_chart_get_type())
#define GST_CHART(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CHART,GstChart))
#define GST_CHART_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CHART,GstChart))
#define GST_IS_CHART(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CHART))
#define GST_IS_CHART_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CHART))

typedef struct _GstChart GstChart;
typedef struct _GstChartClass GstChartClass;

struct _GstChart {
  GstElement element;

  /* pads */
  GstPad *sinkpad,*srcpad;

  /* the timestamp of the next frame */
  guint64 next_time;

  /* video state */
  gint bpp;
  gint depth;
  gint width;
  gint height;

  gint samplerate;
  gdouble framerate; /* desired frame rate */
  gint samples_between_frames; /* number of samples between start of successive frames */
  gint samples_since_last_frame; /* number of samples between start of successive frames */
};

struct _GstChartClass {
  GstElementClass parent_class;
};

GType gst_chart_get_type(void);


/* elementfactory information */
static GstElementDetails gst_chart_details = {
  "chart drawer",
  "Visualization",
  "Takes frames of data and outputs video frames of a chart of data",
  "Richard Boulton <richard@tartarus.org>",
};

/* signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ( GST_VIDEO_CAPS_RGB_16)
);

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("audio/x-raw-int, "
    "endianness = (int) BYTE_ORDER, "
    "signed = (boolean) TRUE, "
    "width = (int) 16, "
    "depth = (int) 16, "
    "rate = (int) [ 8000, 96000 ], "
    "channels = (int) 1")
);

static void     gst_chart_base_init     (gpointer g_class);
static void	gst_chart_class_init	(GstChartClass *klass);
static void	gst_chart_init		(GstChart *chart);

static void	gst_chart_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_chart_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void	gst_chart_chain		(GstPad *pad, GstData *_data);

static GstPadLinkReturn 
		gst_chart_sinkconnect 	(GstPad *pad, const GstCaps *caps);
static GstPadLinkReturn 
		gst_chart_srcconnect 	(GstPad *pad, const GstCaps *caps);

static GstElementClass *parent_class = NULL;

GType
gst_chart_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof(GstChartClass),
      gst_chart_base_init,
      NULL,
      (GClassInitFunc)gst_chart_class_init,
      NULL,
      NULL,
      sizeof(GstChart),
      0,
      (GInstanceInitFunc)gst_chart_init,
    };
    type = g_type_register_static(GST_TYPE_ELEMENT, "GstChart", &info, 0);
  }
  return type;
}

static void
gst_chart_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &gst_chart_details);
}

static void
gst_chart_class_init(GstChartClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_chart_set_property;
  gobject_class->get_property = gst_chart_get_property;
}

static void
gst_chart_init (GstChart *chart)
{
  /* create the sink and src pads */
  chart->sinkpad = gst_pad_new_from_template (
			gst_static_pad_template_get (&sink_factory),
			"sink");
  chart->srcpad = gst_pad_new_from_template (
			gst_static_pad_template_get (&src_factory),
			"src");
  gst_element_add_pad (GST_ELEMENT (chart), chart->sinkpad);
  gst_element_add_pad (GST_ELEMENT (chart), chart->srcpad);

  gst_pad_set_chain_function (chart->sinkpad, gst_chart_chain);
  gst_pad_set_link_function (chart->sinkpad, gst_chart_sinkconnect);
  gst_pad_set_link_function (chart->sinkpad, gst_chart_srcconnect);

  chart->next_time = 0;

  /* reset the initial video state */
  chart->bpp = 16;
  chart->depth = 16;
  chart->width = 256;
  chart->height = 128;

  chart->samplerate = -1;
  chart->framerate = 25; /* desired frame rate */
  chart->samples_between_frames = 0; /* number of samples between start of successive frames */
  chart->samples_since_last_frame = 0;
}

static GstPadLinkReturn
gst_chart_sinkconnect (GstPad *pad, const GstCaps *caps)
{
  GstChart *chart;
  GstStructure *structure;

  chart = GST_CHART (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "rate", &chart->samplerate);
  chart->samples_between_frames = chart->samplerate / chart->framerate;

  GST_DEBUG ("CHART: new sink caps: rate %d",
	     chart->samplerate);
  /*gst_chart_sync_parms (chart); */
  /* */
  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_chart_srcconnect (GstPad *pad, const GstCaps*caps)
{
  GstChart *chart;
  GstStructure *structure;

  chart = GST_CHART (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_double (structure, "framerate", &chart->framerate)) {
    chart->samples_between_frames = chart->samplerate / chart->framerate;
  }

  gst_structure_get_int (structure, "width", &chart->width);
  gst_structure_get_int (structure, "height", &chart->height);

  GST_DEBUG ("CHART: new src caps: framerate %f, %dx%d",
	     chart->framerate, chart->width, chart->height);

  return GST_PAD_LINK_OK;
}

static void
draw_chart_16bpp(guchar * output, gint width, gint height,
		 gint16 * src_data, gint src_size)
{
    gint i;
    guint16 *colstart;
    gint16 * in;

    GST_DEBUG ("CHART: drawing frame to %p, width = %d, height = %d, src_data = %p, src_size = %d",
	       output, width, height, src_data, src_size);
    
    for (colstart = (guint16 *)output, in = (gint16 *)src_data, i = 0;
	 i < width;
	 colstart++, in++, i++) {
	guint16 * pos = colstart;
	gint h1;

	h1 = (((gint)(*in)) * height / (1 << 16)) + height / 2;
	if (h1 >= height) h1 = height;

	if (h1 < height / 2) {
	    while (pos < colstart + h1 * width) {
		*pos = 0x0000;
		pos += width;
	    }
	    while (pos < colstart + height / 2 * width) {
		*pos = 0x07e0;
		pos += width;
	    }
	    while (pos < colstart + height * width) {
		*pos = 0x0000;
		pos += width;
	    }
	} else {
	    while (pos < colstart + height / 2 * width) {
		*pos = 0x0000;
		pos += width;
	    }
	    while (pos < colstart + h1 * width) {
		*pos = 0x07e0;
		pos += width;
	    }
	    while (pos < colstart + height * width) {
		*pos = 0x0000;
		pos += width;
	    }
	}
    }
}

static void
gst_chart_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *bufin = GST_BUFFER (_data);
  GstChart *chart;
  GstBuffer *bufout;
  guint32 samples_in;
  guint32 sizeout;
  gint16 *datain;
  guchar *dataout;

  g_return_if_fail (bufin != NULL);
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD(pad));
  g_return_if_fail (GST_IS_CHART(GST_OBJECT_PARENT(pad)));
  chart = GST_CHART(GST_OBJECT_PARENT (pad));
  g_return_if_fail (chart != NULL);

  GST_DEBUG ("CHART: chainfunc called");

  samples_in = GST_BUFFER_SIZE (bufin) / sizeof(gint16);
  datain = (gint16 *) (GST_BUFFER_DATA (bufin));
  GST_DEBUG ("input buffer has %d samples", samples_in);
  if (chart->next_time <= GST_BUFFER_TIMESTAMP (bufin)) {
    chart->next_time = GST_BUFFER_TIMESTAMP (bufin);
    GST_DEBUG ("in:  %" G_GINT64_FORMAT, GST_BUFFER_TIMESTAMP (bufin));
  }

  chart->samples_since_last_frame += samples_in;
  if (chart->samples_between_frames <= chart->samples_since_last_frame) {
      chart->samples_since_last_frame = 0;

      /* get data to draw into buffer */
      if (samples_in >= chart->width) {
	  /* make a new buffer for the output */
	  bufout = gst_buffer_new ();
	  sizeout = chart->bpp / 8 * chart->width * chart->height;
	  dataout = g_malloc (sizeout);
	  GST_BUFFER_SIZE(bufout) = sizeout;
	  GST_BUFFER_DATA(bufout) = dataout;
	  GST_DEBUG ("CHART: made new buffer: size %d, width %d, height %d",
		     sizeout, chart->width, chart->height);

	  /* take data and draw to new buffer */
	  /* FIXME: call different routines for different properties */
	  draw_chart_16bpp(dataout, chart->width, chart->height, (gint16 *)datain, samples_in);

          gst_buffer_unref(bufin);

	  /* set timestamp */
	  GST_BUFFER_TIMESTAMP (bufout) = chart->next_time;

	  GST_DEBUG ("CHART: outputting buffer");
	  /* output buffer */
	  GST_BUFFER_FLAG_SET (bufout, GST_BUFFER_READONLY);
	  gst_pad_push (chart->srcpad, GST_DATA (bufout));
      }
  } else {
      GST_DEBUG ("CHART: skipping buffer");
      gst_buffer_unref(bufin);
  }

  GST_DEBUG ("CHART: exiting chainfunc");
}

static void
gst_chart_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstChart *chart;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_CHART (object));
  chart = GST_CHART (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_chart_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstChart *chart;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_CHART (object));
  chart = GST_CHART (object);

  switch (prop_id) {
    default:
      break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "chart", GST_RANK_NONE, GST_TYPE_CHART))
    return FALSE;
  
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "chart",
  "Takes frames of data and outputs video frames of a chart of data",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN)
