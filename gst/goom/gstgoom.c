/* gstgoom.c: implementation of goom drawing element
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

#include "goom_core.h"

#define GST_TYPE_GOOM (gst_goom_get_type())
#define GST_GOOM(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GOOM,GstGOOM))
#define GST_GOOM_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GOOM,GstGOOM))
#define GST_IS_GOOM(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GOOM))
#define GST_IS_GOOM_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GOOM))

typedef struct _GstGOOM GstGOOM;
typedef struct _GstGOOMClass GstGOOMClass;

struct _GstGOOM {
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

struct _GstGOOMClass {
  GstElementClass parent_class;
};

GType gst_goom_get_type(void);


/* elementfactory information */
static GstElementDetails gst_goom_details = {
  "GOOM: what a GOOM!",
  "Filter/Visualization",
  "Takes frames of data and outputs video frames using the GOOM filter",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
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

GST_PAD_TEMPLATE_FACTORY (src_template,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "goomsrc",
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

GST_PAD_TEMPLATE_FACTORY (sink_template,
  "sink",					/* the name of the pads */
  GST_PAD_SINK,				/* type of the pad */
  GST_PAD_ALWAYS,				/* ALWAYS/SOMETIMES */
  GST_CAPS_NEW (
    "goomsink",				/* the name of the caps */
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


static void		gst_goom_class_init	(GstGOOMClass *klass);
static void		gst_goom_init		(GstGOOM *goom);

static void		gst_goom_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void		gst_goom_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static void		gst_goom_chain		(GstPad *pad, GstBuffer *buf);

static GstPadConnectReturn 
			gst_goom_sinkconnect 	(GstPad *pad, GstCaps *caps);

static GstElementClass *parent_class = NULL;

GType
gst_goom_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof (GstGOOMClass),      
      NULL,      
      NULL,      
      (GClassInitFunc) gst_goom_class_init,
      NULL,
      NULL,
      sizeof (GstGOOM),
      0,
      (GInstanceInitFunc) gst_goom_init,
    };
    type = g_type_register_static (GST_TYPE_ELEMENT, "GstGOOM", &info, 0);
  }
  return type;
}

static void
gst_goom_class_init(GstGOOMClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WIDTH,
    g_param_spec_int ("width","Width","The Width",
                       0, 2048, 320, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HEIGHT,
    g_param_spec_int ("height","Height","The height",
                       0, 2048, 320, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FPS,
    g_param_spec_int ("fps","FPS","Frames per second",
                       1, 100, 25, G_PARAM_READWRITE));

  gobject_class->set_property = gst_goom_set_property;
  gobject_class->get_property = gst_goom_get_property;
}

static void
gst_goom_init (GstGOOM *goom)
{
  /* create the sink and src pads */
  goom->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_template ), "sink");
  goom->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (src_template ), "src");
  gst_element_add_pad (GST_ELEMENT (goom), goom->sinkpad);
  gst_element_add_pad (GST_ELEMENT (goom), goom->srcpad);

  gst_pad_set_chain_function (goom->sinkpad, gst_goom_chain);
  gst_pad_set_connect_function (goom->sinkpad, gst_goom_sinkconnect);

  goom->next_time = 0;
  goom->peerpool = NULL;

  /* reset the initial video state */
  goom->first_buffer = TRUE;
  goom->width = 320;
  goom->height = 200;
  goom->fps = 25; /* desired frame rate */

}

static GstPadConnectReturn
gst_goom_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstGOOM *goom;
  goom = GST_GOOM (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_CONNECT_DELAYED;
  }

  return GST_PAD_CONNECT_OK;
}

static void
gst_goom_chain (GstPad *pad, GstBuffer *bufin)
{
  GstGOOM *goom;
  GstBuffer *bufout;
  guint32 samples_in;
  gint16 *data;
  gint i;

  goom = GST_GOOM (gst_pad_get_parent (pad));

  GST_DEBUG (0, "GOOM: chainfunc called");

  samples_in = GST_BUFFER_SIZE (bufin) / sizeof (gint16);

  GST_DEBUG (0, "input buffer has %d samples", samples_in);

  if (GST_BUFFER_TIMESTAMP (bufin) < goom->next_time || samples_in < 1024) {
    gst_buffer_unref (bufin);
    return;
  }

  data = (gint16 *) GST_BUFFER_DATA (bufin);
  for (i=0; i < 512; i++) {
    goom->datain[0][i] = *data++;
    goom->datain[1][i] = *data++;
  }

  if (goom->first_buffer) {
    GstCaps *caps;

    goom_init (goom->width, goom->height);
	
    GST_DEBUG (0, "making new pad");

    caps = GST_CAPS_NEW (
		     "goomsrc",
		     "video/raw",
		       "format", 	GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB ")), 
		       "bpp", 		GST_PROPS_INT (32), 
		       "depth", 	GST_PROPS_INT (32), 
		       "endianness", 	GST_PROPS_INT (G_BYTE_ORDER), 
		       "red_mask", 	GST_PROPS_INT (0xff0000), 
		       "green_mask", 	GST_PROPS_INT (0x00ff00), 
		       "blue_mask", 	GST_PROPS_INT (0x0000ff), 
		       "width", 	GST_PROPS_INT (goom->width), 
		       "height", 	GST_PROPS_INT (goom->height)
		   );

    if (!gst_pad_try_set_caps (goom->srcpad, caps)) {
      gst_element_error (GST_ELEMENT (goom), "could not set caps");
      return;
    }
    goom->first_buffer = FALSE;
  }

  bufout = gst_buffer_new ();
  GST_BUFFER_SIZE (bufout) = goom->width * goom->height * 4;
  GST_BUFFER_DATA (bufout) = (guchar *) goom_update (goom->datain);
  GST_BUFFER_TIMESTAMP (bufout) = goom->next_time;
  GST_BUFFER_FLAG_SET (bufout, GST_BUFFER_DONTFREE);

  goom->next_time += 1000000LL / goom->fps;

  gst_pad_push (goom->srcpad, bufout);

  gst_buffer_unref (bufin);

  GST_DEBUG (0, "GOOM: exiting chainfunc");

}

static void
gst_goom_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstGOOM *goom;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_GOOM (object));
  goom = GST_GOOM (object);

  switch (prop_id) {
    case ARG_WIDTH:
      goom->width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      goom->height = g_value_get_int (value);
      break;
    case ARG_FPS:
      goom->fps = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_goom_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstGOOM *goom;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_GOOM (object));
  goom = GST_GOOM (object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int (value, goom->width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, goom->height);
      break;
    case ARG_FPS:
      g_value_set_int (value, goom->fps);
      break;
    default:
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the goom element */
  factory = gst_element_factory_new("goom",GST_TYPE_GOOM,
                                   &gst_goom_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_template));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_template));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "goom",
  plugin_init
};
