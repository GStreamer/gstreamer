/* gstsynaesthesia.c: implementation of synaesthesia drawing element
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

#include "synaescope.h"

#define GST_TYPE_SYNAESTHESIA (gst_synaesthesia_get_type())
#define GST_SYNAESTHESIA(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SYNAESTHESIA,GstSynaesthesia))
#define GST_SYNAESTHESIA_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SYNAESTHESIA,GstSynaesthesia))
#define GST_IS_SYNAESTHESIA(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SYNAESTHESIA))
#define GST_IS_SYNAESTHESIA_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SYNAESTHESIA))

typedef struct _GstSynaesthesia GstSynaesthesia;
typedef struct _GstSynaesthesiaClass GstSynaesthesiaClass;

struct _GstSynaesthesia {
  GstElement element;

  /* pads */
  GstPad *sinkpad,*srcpad;
  GstBufferPool *peerpool;

  /* the timestamp of the next frame */
  guint64 next_time;
  gint16 datain[2][512];

  /* video state */
  gint fps;
  gint width;
  gint height;
  gboolean first_buffer;
};

struct _GstSynaesthesiaClass {
  GstElementClass parent_class;
};

GType gst_synaesthesia_get_type(void);


/* elementfactory information */
static GstElementDetails gst_synaesthesia_details = {
  "Synaesthesia",
  "Filter/Visualization",
  "Creates video visualizations of audio input, using stereo and pitch information",
  VERSION,
  "Richard Boulton <richard@tartarus.org>",
  "(C) 2002",
};

/* signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_FPS,
  /* FILL ME */
};

GST_PADTEMPLATE_FACTORY (src_template,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "synaesthesiasrc",
    "video/raw",
      "format",		GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB ")),
      "bpp",		GST_PROPS_INT (32),
      "depth",		GST_PROPS_INT (32),
      "endianness", 	GST_PROPS_INT (G_BYTE_ORDER),
      "red_mask",   	GST_PROPS_INT (0xff0000),
      "green_mask", 	GST_PROPS_INT (0xff00),
      "blue_mask",  	GST_PROPS_INT (0xff),
      "width",		GST_PROPS_INT_RANGE (16, 4096),
      "height",		GST_PROPS_INT_RANGE (16, 4096)
  )
)

GST_PADTEMPLATE_FACTORY (sink_template,
  "sink",					/* the name of the pads */
  GST_PAD_SINK,				/* type of the pad */
  GST_PAD_ALWAYS,				/* ALWAYS/SOMETIMES */
  GST_CAPS_NEW (
    "synaesthesiasink",				/* the name of the caps */
    "audio/raw",				/* the mime type of the caps */
       /* Properties follow: */
      "format",     GST_PROPS_STRING ("int"),
      "law",        GST_PROPS_INT (0),
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "signed",     GST_PROPS_BOOLEAN (TRUE),
      "width",      GST_PROPS_INT (16),
      "depth",      GST_PROPS_INT (16),
      "rate",       GST_PROPS_INT_RANGE (8000, 96000),
      "channels",   GST_PROPS_INT (2)
  )
)


static void		gst_synaesthesia_class_init	(GstSynaesthesiaClass *klass);
static void		gst_synaesthesia_init		(GstSynaesthesia *synaesthesia);

static void		gst_synaesthesia_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void		gst_synaesthesia_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static void		gst_synaesthesia_chain		(GstPad *pad, GstBuffer *buf);

static GstPadConnectReturn 
			gst_synaesthesia_sinkconnect 	(GstPad *pad, GstCaps *caps);

static GstElementClass *parent_class = NULL;

GType
gst_synaesthesia_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof (GstSynaesthesiaClass),      
      NULL,      
      NULL,      
      (GClassInitFunc) gst_synaesthesia_class_init,
      NULL,
      NULL,
      sizeof (GstSynaesthesia),
      0,
      (GInstanceInitFunc) gst_synaesthesia_init,
    };
    type = g_type_register_static (GST_TYPE_ELEMENT, "GstSynaesthesia", &info, 0);
  }
  return type;
}

static void
gst_synaesthesia_class_init(GstSynaesthesiaClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WIDTH,
    g_param_spec_int ("width","Width","The Width",
                       1, 2048, 320, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HEIGHT,
    g_param_spec_int ("height","Height","The height",
                       1, 2048, 320, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FPS,
    g_param_spec_int ("fps","FPS","Frames per second",
                       1, 100, 25, G_PARAM_READWRITE));

  gobject_class->set_property = gst_synaesthesia_set_property;
  gobject_class->get_property = gst_synaesthesia_get_property;
}

static void
gst_synaesthesia_init (GstSynaesthesia *synaesthesia)
{
  /* create the sink and src pads */
  synaesthesia->sinkpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (sink_template ), "sink");
  synaesthesia->srcpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (src_template ), "src");
  gst_element_add_pad (GST_ELEMENT (synaesthesia), synaesthesia->sinkpad);
  gst_element_add_pad (GST_ELEMENT (synaesthesia), synaesthesia->srcpad);

  gst_pad_set_chain_function (synaesthesia->sinkpad, gst_synaesthesia_chain);
  gst_pad_set_connect_function (synaesthesia->sinkpad, gst_synaesthesia_sinkconnect);

  synaesthesia->next_time = 0;
  synaesthesia->peerpool = NULL;

  /* reset the initial video state */
  synaesthesia->first_buffer = TRUE;
  synaesthesia->width = 320;
  synaesthesia->height = 200;
  synaesthesia->fps = 25; /* desired frame rate */

}

static GstPadConnectReturn
gst_synaesthesia_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstSynaesthesia *synaesthesia;
  synaesthesia = GST_SYNAESTHESIA (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_CONNECT_DELAYED;
  }

  return GST_PAD_CONNECT_OK;
}

static void
gst_synaesthesia_chain (GstPad *pad, GstBuffer *bufin)
{
  GstSynaesthesia *synaesthesia;
  GstBuffer *bufout;
  guint32 samples_in;
  gint16 *data;
  gint i;

  synaesthesia = GST_SYNAESTHESIA (gst_pad_get_parent (pad));

  GST_DEBUG (0, "Synaesthesia: chainfunc called\n");

  samples_in = GST_BUFFER_SIZE (bufin) / sizeof (gint16);

  GST_DEBUG (0, "input buffer has %d samples\n", samples_in);

  /* FIXME: should really select the first 1024 samples after the timestamp. */
  if (GST_BUFFER_TIMESTAMP (bufin) < synaesthesia->next_time || samples_in < 1024) {
    GST_DEBUG (0, "timestamp is %llu: want >= %llu\n", GST_BUFFER_TIMESTAMP (bufin), synaesthesia->next_time);
    gst_buffer_unref (bufin);
    return;
  }

  data = (gint16 *) GST_BUFFER_DATA (bufin);
  for (i=0; i < 512; i++) {
    synaesthesia->datain[0][i] = *data++;
    synaesthesia->datain[1][i] = *data++;
  }

  if (synaesthesia->first_buffer) {
    GstCaps *caps;

    synaesthesia_init (synaesthesia->width, synaesthesia->height);
	
    GST_DEBUG (0, "making new pad\n");

    caps = GST_CAPS_NEW (
		     "synaesthesiasrc",
		     "video/raw",
		       "format", 	GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB ")), 
		       "bpp", 		GST_PROPS_INT (32), 
		       "depth", 	GST_PROPS_INT (32), 
		       "endianness", 	GST_PROPS_INT (G_BYTE_ORDER), 
		       "red_mask", 	GST_PROPS_INT (0xff0000), 
		       "green_mask", 	GST_PROPS_INT (0x00ff00), 
		       "blue_mask", 	GST_PROPS_INT (0x0000ff), 
		       "width", 	GST_PROPS_INT (synaesthesia->width), 
		       "height", 	GST_PROPS_INT (synaesthesia->height)
		   );

    if (!gst_pad_try_set_caps (synaesthesia->srcpad, caps)) {
      gst_element_error (GST_ELEMENT (synaesthesia), "could not set caps");
      return;
    }
    synaesthesia->first_buffer = FALSE;
  }

  bufout = gst_buffer_new ();
  GST_BUFFER_SIZE (bufout) = synaesthesia->width * synaesthesia->height * 4;
  GST_BUFFER_DATA (bufout) = (guchar *) synaesthesia_update (synaesthesia->datain);
  GST_BUFFER_TIMESTAMP (bufout) = synaesthesia->next_time;
  GST_BUFFER_FLAG_SET (bufout, GST_BUFFER_DONTFREE);

  synaesthesia->next_time += 1000000LL / synaesthesia->fps;

  gst_pad_push (synaesthesia->srcpad, bufout);

  gst_buffer_unref (bufin);

  GST_DEBUG (0, "Synaesthesia: exiting chainfunc\n");

}

static void
gst_synaesthesia_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSynaesthesia *synaesthesia;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SYNAESTHESIA (object));
  synaesthesia = GST_SYNAESTHESIA (object);

  switch (prop_id) {
    case ARG_WIDTH:
      synaesthesia->width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      synaesthesia->height = g_value_get_int (value);
      break;
    case ARG_FPS:
      synaesthesia->fps = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_synaesthesia_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSynaesthesia *synaesthesia;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SYNAESTHESIA (object));
  synaesthesia = GST_SYNAESTHESIA (object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int (value, synaesthesia->width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, synaesthesia->height);
      break;
    case ARG_FPS:
      g_value_set_int (value, synaesthesia->fps);
      break;
    default:
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the synaesthesia element */
  factory = gst_elementfactory_new("synaesthesia",GST_TYPE_SYNAESTHESIA,
                                   &gst_synaesthesia_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (src_template));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (sink_template));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "synaesthesia",
  plugin_init
};
