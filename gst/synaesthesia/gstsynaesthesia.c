/* Gnome-Streamer
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


#include "config.h"

#include "gstsynaesthesia.h"
#include "core.h"

static gboolean gst_synaesthesia_start(GstElement *element);

GstElementDetails gst_synaesthesia_details = {
  "Synaesthesia display",
  "Sink/Visualization",
  "Cool color display based on stereo info",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};

static GstElementClass *parent_class = NULL;
//static guint gst_synaesthesia_signals[LAST_SIGNAL] = { 0 };

/* Synaesthesia signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_WIDGET,
};

static GstPadTemplate*
sink_factory (void) 
{
  return 
   gst_padtemplate_new (
    	"sink",                                       /* the name of the pads */
  	GST_PAD_SINK,                         /* type of the pad */
  	GST_PAD_ALWAYS,                       /* ALWAYS/SOMETIMES */
  	gst_caps_new (
    	  "synaesthesia_sink16",                      /* the name of the caps */
    	  "audio/raw",                                /* the mime type of the caps */
	  gst_props_new (
    	    /* Properties follow: */
    	    "format",   GST_PROPS_INT (16),
    	    "depth",    GST_PROPS_INT (16),
	    NULL)),
	NULL);
    // These properties commented out so that autoplugging works for now:
    // the autoplugging needs to be fixed (caps negotiation needed)
    //,"rate",     GST_PROPS_INT (44100)
    //,"channels", GST_PROPS_INT (2)
}

static void gst_synaesthesia_class_init(GstSynaesthesiaClass *klass);
static void gst_synaesthesia_init(GstSynaesthesia *synaesthesia);

static void gst_synaesthesia_chain(GstPad *pad,GstBuffer *buf);

static void gst_synaesthesia_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_synaesthesia_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);


static GstPadTemplate *sink_template;

GType
gst_synaesthesia_get_type(void) {
  static GType synaesthesia_type = 0;

  if (!synaesthesia_type) {
    static const GTypeInfo synaesthesia_info = {
      sizeof(GstSynaesthesiaClass),      NULL,
      NULL,
      (GClassInitFunc)gst_synaesthesia_class_init,
      NULL,
      NULL,
      sizeof(GstSynaesthesia),
      0,
      (GInstanceInitFunc)gst_synaesthesia_init,
    };
    synaesthesia_type = g_type_register_static(GST_TYPE_ELEMENT, "GstSynaesthesia", &synaesthesia_info, 0);
  }
  return synaesthesia_type;
}

static void
gst_synaesthesia_class_init(GstSynaesthesiaClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_WIDTH,
    g_param_spec_int("width","width","width",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HEIGHT,
    g_param_spec_int("height","height","height",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); // CHECKME

  gobject_class->set_property = gst_synaesthesia_set_property;
  gobject_class->get_property = gst_synaesthesia_get_property;
}

static void
gst_synaesthesia_init(GstSynaesthesia *synaesthesia)
{
  synaesthesia->sinkpad = gst_pad_new_from_template (sink_template, "sink");
  gst_element_add_pad(GST_ELEMENT(synaesthesia), synaesthesia->sinkpad);
  gst_pad_set_chain_function(synaesthesia->sinkpad, gst_synaesthesia_chain);

  gst_synaesthesia_start(GST_ELEMENT(synaesthesia));
}

static void gst_synaesthesia_chain(GstPad *pad,GstBuffer *buf) {
  GstSynaesthesia *syna;
  gint samplecount;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  syna = GST_SYNAESTHESIA(GST_OBJECT_PARENT (pad));
  g_return_if_fail(syna != NULL);
  g_return_if_fail(GST_IS_SYNAESTHESIA(syna));

  samplecount = GST_BUFFER_SIZE(buf) /
                (2 * sizeof(gint16));

//  GST_DEBUG (0,"fading\n");
//  fade(&syna->sp);
  GST_DEBUG (0,"doing effect\n");
  coreGo(&syna->sp,GST_BUFFER_DATA(buf),samplecount);

//  GST_DEBUG (0,"drawing\n");
/*  GST_DEBUG (0,"gdk_draw_indexed_image(%p,%p,%d,%d,%d,%d,%s,%p,%d,%p);\n",
        syna->image->window,
	syna->image->style->fg_gc[GTK_STATE_NORMAL],
	0,0,syna->width,syna->height,
	"GDK_RGB_DITHER_NORMAL",
	syna->sp.output,syna->width,
	&syna->cmap);*/
/*  gdk_draw_indexed_image(syna->image->window,
	syna->image->style->fg_gc[GTK_STATE_NORMAL],
	0,0,syna->width,syna->height,
	GDK_RGB_DITHER_NORMAL,
	syna->sp.output,syna->width,
	&syna->cmap);*/
  gdk_draw_gray_image(syna->image->window,
	syna->image->style->fg_gc[GTK_STATE_NORMAL],
	0,0,syna->width,syna->height,
	GDK_RGB_DITHER_NORMAL,
	syna->sp.output,syna->width);

  gst_trace_add_entry(NULL,0,buf,"synaesthesia: calculated syna");

  gst_buffer_unref(buf);
}

static void gst_synaesthesia_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
  GstSynaesthesia *synaesthesia;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SYNAESTHESIA(object));
  synaesthesia = GST_SYNAESTHESIA(object);

  switch (prop_id) {
    case ARG_WIDTH:
      synaesthesia->width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      synaesthesia->height = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_synaesthesia_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSynaesthesia *synaesthesia;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SYNAESTHESIA(object));
  synaesthesia = GST_SYNAESTHESIA(object);

  GST_DEBUG (0,"have synaesthesia get_property(%d), wanting %d\n",id,ARG_WIDGET);

  switch (prop_id) {
    case ARG_WIDTH: {
      g_value_set_int (value, synaesthesia->width);
      GST_DEBUG (0,"returning width value %d\n",g_value_get_int (value));
      break;
    }
    case ARG_HEIGHT: {
      g_value_set_int (value, synaesthesia->height);
      GST_DEBUG (0,"returning height value %d\n",g_value_get_int (value));
      break;
    }
    case ARG_WIDGET: {
      g_value_set_object (value, G_OBJECT(synaesthesia->image));
      GST_DEBUG (0,"returning widget value %p\n",g_value_get_object (value));
      break;
    }
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      GST_DEBUG (0,"returning invalid type\n");
      break;
    }
  }
}



static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_elementfactory_new("synaesthesia", GST_TYPE_SYNAESTHESIA,
                                   &gst_synaesthesia_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  sink_template = sink_factory ();
  gst_elementfactory_add_padtemplate(factory, sink_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "synaesthesia",
  plugin_init
};

static gboolean
gst_synaesthesia_start(GstElement *element)
{
  GstSynaesthesia *syna;

  g_return_val_if_fail(GST_IS_SYNAESTHESIA(element), FALSE);
  syna = GST_SYNAESTHESIA(element);

  syna->width = 255;
  syna->height = 255;
  syna->starsize = 2;

  coreInit(&syna->sp, syna->width, syna->height);
  setStarSize(&syna->sp, syna->starsize);

  setupPalette(&syna->sp, syna->cmap.colors);

  gdk_rgb_init();
  syna->image = gtk_drawing_area_new();
  GST_DEBUG (0,"image is %p\n",syna->image);
  gtk_drawing_area_size(GTK_DRAWING_AREA(syna->image),
			syna->width,
                        syna->height);
  gtk_widget_show(syna->image);

  GST_DEBUG (0,"started synaesthesia\n");
  return TRUE;
}

