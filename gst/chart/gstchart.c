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

#include <config.h>
#include <gst/gst.h>

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
  GstBufferPool *peerpool;

  // the timestamp of the next frame
  guint64 next_time;

  // video state
  gint bpp;
  gint depth;
  gint width;
  gint height;

  gint samplerate;
  gint framerate; // desired frame rate
  gint samples_between_frames; // number of samples between start of successive frames
  gint samples_since_last_frame; // number of samples between start of successive frames
};

struct _GstChartClass {
  GstElementClass parent_class;
};

GType gst_chart_get_type(void);


/* elementfactory information */
static GstElementDetails gst_chart_details = {
  "chart drawer",
  "Filter/Visualization",
  "Takes frames of data and outputs video frames of a chart of data",
  VERSION,
  "Richard Boulton <richard@tartarus.org>",
  "(C) 2001",
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

static GstPadTemplate*
src_template_factory (void) 
{
  static GstPadTemplate *template = NULL;
  
  if (!template) {
    template = gst_padtemplate_new (
  	"src",
  	GST_PAD_SRC,
  	GST_PAD_ALWAYS,
  	gst_caps_new (
  	  "chartsrc",
    	  "video/raw",
	  /*gst_props_new (
    	    "format",	GST_PROPS_FOURCC (GST_MAKE_FOURCC ('R','G','B',' ')),
	    "bpp",	GST_PROPS_INT (32),
	    "depth",	GST_PROPS_INT (32),
	    "endianness", GST_PROPS_INT (G_BYTE_ORDER),
	    "red_mask",   GST_PROPS_INT (0xff0000),
	    "green_mask", GST_PROPS_INT (0xff00),
	    "blue_mask",  GST_PROPS_INT (0xff),
    	    "width",	GST_PROPS_INT_RANGE (16, 4096),
    	    "height",	GST_PROPS_INT_RANGE (16, 4096),
	    NULL),*/
	  gst_props_new (
    	    "format",	GST_PROPS_FOURCC (GST_MAKE_FOURCC ('R','G','B',' ')),
	    "bpp",	GST_PROPS_INT (16),
	    "depth",	GST_PROPS_INT (16),
	    "endianness", GST_PROPS_INT (G_BYTE_ORDER),
	    "red_mask",   GST_PROPS_INT (0xf800),
	    "green_mask", GST_PROPS_INT (0x07e0),
	    "blue_mask",  GST_PROPS_INT (0x001f),
    	    "width",	GST_PROPS_INT_RANGE (16, 4096),
    	    "height",	GST_PROPS_INT_RANGE (16, 4096),
	    NULL)
	),
	NULL);
  }
  return template;
}

static GstPadTemplate*
sink_template_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template) {
    template = gst_padtemplate_new (
  	"sink",					/* the name of the pads */
  	GST_PAD_SINK,				/* type of the pad */
  	GST_PAD_ALWAYS,				/* ALWAYS/SOMETIMES */
  	gst_caps_new (
     	  "chartsink",				/* the name of the caps */
     	  "audio/raw",				/* the mime type of the caps */
	  gst_props_new (
     	    /* Properties follow: */
            "format",       GST_PROPS_STRING ("int"),
              "law",        GST_PROPS_INT (0),
              "endianness", GST_PROPS_INT (G_BYTE_ORDER),
              "signed",     GST_PROPS_BOOLEAN (TRUE),
              "width",      GST_PROPS_INT (16),
	      "depth",      GST_PROPS_INT (16),
	      "rate",       GST_PROPS_INT_RANGE (8000, 96000),
     	      "channels",   GST_PROPS_INT (1),
	      NULL)
  	),
  	NULL);
  }

  return template;
}




static void	gst_chart_class_init	(GstChartClass *klass);
static void	gst_chart_init		(GstChart *chart);

static void	gst_chart_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_chart_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void	gst_chart_chain		(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;

static void gst_chart_newsinkcaps (GstPad *pad, GstCaps *caps);
static void gst_chart_newsrccaps (GstPad *pad, GstCaps *caps);

GType
gst_chart_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof(GstChartClass),      NULL,      NULL,      (GClassInitFunc)gst_chart_class_init,
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
  chart->sinkpad = gst_pad_new_from_template (sink_template_factory (), "sink");
  chart->srcpad = gst_pad_new_from_template (src_template_factory (), "src");
  gst_element_add_pad (GST_ELEMENT (chart), chart->sinkpad);
  gst_element_add_pad (GST_ELEMENT (chart), chart->srcpad);

  gst_pad_set_chain_function (chart->sinkpad, gst_chart_chain);
  gst_pad_set_newcaps_function (chart->sinkpad, gst_chart_newsinkcaps);
  gst_pad_set_newcaps_function (chart->srcpad, gst_chart_newsrccaps);


  chart->next_time = 0;
  chart->peerpool = NULL;

  // reset the initial video state
  chart->bpp = 16;
  chart->depth = 16;
  chart->width = -1;
  chart->height = -1;

  chart->samplerate = -1;
  chart->framerate = 25; // desired frame rate
  chart->samples_between_frames = 0; // number of samples between start of successive frames
  chart->samples_since_last_frame = 0;
}

static void
gst_chart_newsinkcaps (GstPad *pad, GstCaps *caps)
{
  GstChart *chart;
  chart = GST_CHART (gst_pad_get_parent (pad));

  chart->samplerate = gst_caps_get_int (caps, "rate");
  chart->samples_between_frames = chart->samplerate / chart->framerate;

  GST_DEBUG (0, "CHART: new sink caps: rate %d\n",
	     chart->samplerate);
  //gst_chart_sync_parms (chart);
}

static void
gst_chart_newsrccaps (GstPad *pad, GstCaps *caps)
{
  GstChart *chart;
  chart = GST_CHART (gst_pad_get_parent (pad));

  chart->bpp = gst_caps_get_int (caps, "bpp");
  chart->depth = gst_caps_get_int (caps, "depth");
  chart->width = gst_caps_get_int (caps, "width");
  chart->height = gst_caps_get_int (caps, "height");

  GST_DEBUG (0, "CHART: new src caps: bpp %d, depth %d, width %d, height %d\n",
	     chart->bpp, chart->depth, chart->width, chart->height);
  //gst_chart_sync_parms (chart);
}

static void
gst_chart_free (GstChart *chart)
{
  g_free (chart);
}

static void
draw_chart_16bpp(guchar * output, gint width, gint height,
		 gint16 * src_data, gint src_size)
{
    gint i;
    guint16 *colstart;
    gint16 * in;

    GST_DEBUG (0, "CHART: drawing frame to %p, width = %d, height = %d, src_data = %p, src_size = %d\n",
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
gst_chart_chain (GstPad *pad, GstBuffer *bufin)
{
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

  GST_DEBUG (0, "CHART: chainfunc called\n");

  samples_in = GST_BUFFER_SIZE (bufin) / sizeof(gint16);
  datain = (gint16 *) (GST_BUFFER_DATA (bufin));
  GST_DEBUG (0, "input buffer has %d samples\n", samples_in);
  if (chart->next_time <= GST_BUFFER_TIMESTAMP (bufin)) {
    chart->next_time = GST_BUFFER_TIMESTAMP (bufin);
    GST_DEBUG (0, "in:  %lld\n", GST_BUFFER_TIMESTAMP (bufin));
  }

  chart->samples_since_last_frame += samples_in;
  if (chart->samples_between_frames <= chart->samples_since_last_frame) {
      chart->samples_since_last_frame = 0;

      // Check if we need to renegotiate size.
      if (chart->width == -1 || chart->height == -1) {
	  chart->width = 256;
	  chart->height = 128;
	  GST_DEBUG (0, "making new pad\n");
	  gst_pad_set_caps (chart->srcpad,
			    gst_caps_new (
					  "chartsrc",
					  "video/raw",
					  gst_props_new (
							 "format",	GST_PROPS_FOURCC (GST_MAKE_FOURCC ('R','G','B',' ')),
							 "bpp",		GST_PROPS_INT (chart->bpp),
							 "depth",	GST_PROPS_INT (chart->depth),
							 "endianness",	GST_PROPS_INT (G_BYTE_ORDER),
							 "red_mask",	GST_PROPS_INT (0xf800),
							 "green_mask",	GST_PROPS_INT (0x07e0),
							 "blue_mask",	GST_PROPS_INT (0x001f),
							 "width",	GST_PROPS_INT (chart->width),
							 "height",	GST_PROPS_INT (chart->height),
							 NULL)));
      }

      // get data to draw into buffer
      if (samples_in >= chart->width) {
	  // make a new buffer for the output
	  bufout = gst_buffer_new ();
	  sizeout = chart->bpp / 8 * chart->width * chart->height;
	  dataout = g_malloc (sizeout);
	  GST_BUFFER_SIZE(bufout) = sizeout;
	  GST_BUFFER_DATA(bufout) = dataout;
	  GST_DEBUG (0, "CHART: made new buffer: size %d, width %d, height %d\n",
		     sizeout, chart->width, chart->height);

	  // take data and draw to new buffer
	  // FIXME: call different routines for different properties
	  draw_chart_16bpp(dataout, chart->width, chart->height, (gint16 *)datain, samples_in);

	  // set timestamp
	  GST_BUFFER_TIMESTAMP (bufout) = chart->next_time;

	  GST_DEBUG (0, "CHART: outputting buffer\n");
	  // output buffer
	  GST_BUFFER_FLAG_SET (bufout, GST_BUFFER_READONLY);
	  gst_pad_push (chart->srcpad, bufout);
      }
  } else {
      GST_DEBUG (0, "CHART: skipping buffer\n");
  }

  gst_buffer_unref(bufin);
  GST_DEBUG (0, "CHART: exiting chainfunc\n");
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
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the chart element */
  factory = gst_elementfactory_new("chart",GST_TYPE_CHART,
                                   &gst_chart_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, src_template_factory ());
  gst_elementfactory_add_padtemplate (factory, sink_template_factory ());

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "chart",
  plugin_init
};
