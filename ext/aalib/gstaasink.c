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

#include <config.h>

#include <string.h>
#include <sys/time.h>

#include "gstaasink.h"

static GstElementDetails gst_aasink_details = {
  "Video sink",
  "Sink/Video",
  "An ASCII art videosink",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2001",
};

/* aasink signals and args */
enum {
  SIGNAL_FRAME_DISPLAYED,
  SIGNAL_HAVE_SIZE,
  LAST_SIGNAL
};


enum {
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_DRIVER,
  ARG_DITHER,
  ARG_BRIGHTNESS,
  ARG_CONTRAST,
  ARG_GAMMA,
  ARG_INVERSION,
  ARG_RANDOMVAL,
  ARG_FRAMES_DISPLAYED,
  ARG_FRAME_TIME,
};

GST_PADTEMPLATE_FACTORY (sink_template,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "aasink_caps",
    "video/raw",
      "format", 	GST_PROPS_FOURCC (GST_STR_FOURCC ("I420"))
  )
)
    
static void	gst_aasink_class_init	(GstAASinkClass *klass);
static void	gst_aasink_init		(GstAASink *aasink);

static void	gst_aasink_chain	(GstPad *pad, GstBuffer *buf);

static void	gst_aasink_set_property	(GObject *object, guint prop_id, 
					 const GValue *value, GParamSpec *pspec);
static void	gst_aasink_get_property	(GObject *object, guint prop_id, 
					 GValue *value, GParamSpec *pspec);

static GstElementStateReturn gst_aasink_change_state (GstElement *element);

static GstElementClass *parent_class = NULL;
static guint gst_aasink_signals[LAST_SIGNAL] = { 0 };

GType
gst_aasink_get_type (void)
{
  static GType aasink_type = 0;

  if (!aasink_type) {
    static const GTypeInfo aasink_info = {
      sizeof(GstAASinkClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_aasink_class_init,
      NULL,
      NULL,
      sizeof(GstAASink),
      0,
      (GInstanceInitFunc)gst_aasink_init,
    };
    aasink_type = g_type_register_static(GST_TYPE_ELEMENT, "GstAASink", &aasink_info, 0);
  }
  return aasink_type;
}

#define GST_TYPE_AADRIVERS (gst_aasink_drivers_get_type())
static GType
gst_aasink_drivers_get_type (void)
{
  static GType driver_type = 0;
  if (!driver_type) {
    GEnumValue *drivers;
    const struct aa_driver *driver;
    gint i = 0;

    driver = aa_drivers[i++];
    while (driver) {
      driver = aa_drivers[i++];
    }
    
    drivers = g_new0(GEnumValue, i);

    i = 0;
    driver = aa_drivers[i];
    while (driver) {
      drivers[i].value = i;
      drivers[i].value_name = g_strdup (driver->shortname);
      drivers[i].value_nick = g_strdup (driver->name);
      i++;
      driver = aa_drivers[i];
    }
    drivers[i].value = 0;
    drivers[i].value_name = NULL;
    drivers[i].value_nick = NULL;

    driver_type = g_enum_register_static ("GstAASinkDrivers", drivers);
  }
  return driver_type;
}

#define GST_TYPE_AADITHER (gst_aasink_dither_get_type())
static GType
gst_aasink_dither_get_type (void)
{
  static GType dither_type = 0;
  if (!dither_type) {
    GEnumValue *ditherers;
    gint i = 0;

    while (aa_dithernames[i]) {
      i++;
    }
    
    ditherers = g_new0(GEnumValue, i);

    i = 0;
    while (aa_dithernames[i]) {
      ditherers[i].value = i;
      ditherers[i].value_name = g_strdup (aa_dithernames[i]);
      ditherers[i].value_nick = g_strdup (aa_dithernames[i]);
      i++;
    }
    ditherers[i].value = 0;
    ditherers[i].value_name = NULL;
    ditherers[i].value_nick = NULL;

    dither_type = g_enum_register_static ("GstAASinkDitherers", ditherers);
  }
  return dither_type;
}

static void
gst_aasink_class_init (GstAASinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_WIDTH,
    g_param_spec_int("width","width","width",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HEIGHT,
    g_param_spec_int("height","height","height",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DRIVER,
    g_param_spec_enum("driver","driver","driver",
                      GST_TYPE_AADRIVERS,0,G_PARAM_READWRITE)); /* CHECKME! */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DITHER,
    g_param_spec_enum("dither","dither","dither",
                      GST_TYPE_AADITHER,0,G_PARAM_READWRITE)); /* CHECKME! */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BRIGHTNESS,
    g_param_spec_int("brightness","brightness","brightness",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CONTRAST,
    g_param_spec_int("contrast","contrast","contrast",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_GAMMA,
    g_param_spec_float("gamma","gamma","gamma",
                       0.0,5.0,1.0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_INVERSION,
    g_param_spec_boolean("inversion","inversion","inversion",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_RANDOMVAL,
    g_param_spec_int("randomval","randomval","randomval",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FRAMES_DISPLAYED,
    g_param_spec_int("frames_displayed","frames_displayed","frames_displayed",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FRAME_TIME,
    g_param_spec_int("frame_time","frame_time","frame_time",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); /* CHECKME */

  gobject_class->set_property = gst_aasink_set_property;
  gobject_class->get_property = gst_aasink_get_property;

  gst_aasink_signals[SIGNAL_FRAME_DISPLAYED] =
    g_signal_new ("frame_displayed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstAASinkClass, frame_displayed), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_aasink_signals[SIGNAL_HAVE_SIZE] =
    g_signal_new ("have_size", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstAASinkClass, have_size), NULL, NULL,
                   gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2,
                   G_TYPE_UINT, G_TYPE_UINT);

  gstelement_class->change_state = gst_aasink_change_state;
}

static GstPadConnectReturn
gst_aasink_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstAASink *aasink;
  gulong print_format;

  aasink = GST_AASINK (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;
  
  aasink->width =  gst_caps_get_int (caps, "width");
  aasink->height =  gst_caps_get_int (caps, "height");

  print_format = GULONG_FROM_LE (aasink->format);

  GST_DEBUG (0, "aasink: setting %08lx (%4.4s)\n", aasink->format, (gchar*)&print_format);
  
  g_signal_emit( G_OBJECT (aasink), gst_aasink_signals[SIGNAL_HAVE_SIZE], 0,
		 aasink->width, aasink->height);

  return GST_PAD_CONNECT_OK;
}

static void
gst_aasink_set_clock (GstElement *element, GstClock *clock)
{
  GstAASink *aasink = GST_AASINK (element);

  aasink->clock = clock;
}

static void
gst_aasink_init (GstAASink *aasink)
{
  aasink->sinkpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (aasink), aasink->sinkpad);
  gst_pad_set_chain_function (aasink->sinkpad, gst_aasink_chain);
  gst_pad_set_connect_function (aasink->sinkpad, gst_aasink_sinkconnect);

  memcpy(&aasink->ascii_surf, &aa_defparams, sizeof (struct aa_hardware_params));
  aasink->ascii_parms.bright = 0;
  aasink->ascii_parms.contrast = 16;
  aasink->ascii_parms.gamma = 1.0;
  aasink->ascii_parms.dither = 0;
  aasink->ascii_parms.inversion = 0;
  aasink->ascii_parms.randomval = 0;
  aasink->aa_driver = 0;

  aasink->width = -1;
  aasink->height = -1;

  aasink->clock = NULL;
  GST_ELEMENT (aasink)->setclockfunc    = gst_aasink_set_clock;

  GST_FLAG_SET(aasink, GST_ELEMENT_THREAD_SUGGESTED);
}

static void
gst_aasink_scale (GstAASink *aasink, gchar *src, gchar *dest,
		  gint sw, gint sh, 
		  gint dw, gint dh)
{
  gint ypos, yinc, y;
  gint xpos, xinc, x;

  g_return_if_fail ((dw != 0) && (dh != 0));

  ypos = 0x10000;
  yinc = (sh << 16) / dh;
  xinc = (sw << 16) / dw;

  for (y = dh; y; y--) {
    while (ypos > 0x10000) {
      ypos -= 0x10000;
      src  += sw;
    }
    xpos = 0x10000;
    {
      guchar *destp = dest;
      guchar *srcp = src;

      for ( x=dw; x; x-- ) {
        while (xpos >= 0x10000L) {
          srcp++;
          xpos -= 0x10000L;
        }
        *destp++ = *srcp;
        xpos     += xinc;
      }
    }
    dest += dw;
    ypos += yinc;
  }
}

static void
gst_aasink_chain (GstPad *pad, GstBuffer *buf)
{
  GstAASink *aasink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  aasink = GST_AASINK (gst_pad_get_parent (pad));

  gst_aasink_scale (aasink,
		    GST_BUFFER_DATA (buf),		/* src */
  		    aa_image (aasink->context),		/* dest */
  		    aasink->width,			/* sw */
  		    aasink->height,			/* sh */
  		    aa_imgwidth (aasink->context),	/* dw */
  		    aa_imgheight (aasink->context));	/* dh */

  GST_DEBUG (0,"videosink: clock wait: %llu\n", GST_BUFFER_TIMESTAMP(buf));

  if (aasink->clock) {
    gst_element_clock_wait (GST_ELEMENT (aasink), aasink->clock, GST_BUFFER_TIMESTAMP(buf));
  }

  aa_render (aasink->context, &aasink->ascii_parms, 
		  0, 0, aa_imgwidth (aasink->context), aa_imgheight (aasink->context));
  aa_flush (aasink->context);
  aa_getevent (aasink->context, FALSE);

  g_signal_emit(G_OBJECT(aasink),gst_aasink_signals[SIGNAL_FRAME_DISPLAYED], 0);

  gst_buffer_unref(buf);
}


static void
gst_aasink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstAASink *aasink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AASINK (object));

  aasink = GST_AASINK (object);

  switch (prop_id) {
    case ARG_WIDTH:
      aasink->ascii_surf.width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      aasink->ascii_surf.height = g_value_get_int (value);
      break;
    case ARG_DRIVER: {
      aasink->aa_driver = g_value_get_int (value);
      break;
    }
    case ARG_DITHER: {
      aasink->ascii_parms.dither = g_value_get_int (value);
      break;
    }
    case ARG_BRIGHTNESS: {
      aasink->ascii_parms.bright = g_value_get_int (value);
      break;
    }
    case ARG_CONTRAST: {
      aasink->ascii_parms.contrast = g_value_get_int (value);
      break;
    }
    case ARG_GAMMA: {
      aasink->ascii_parms.gamma = g_value_get_float (value);
      break;
    }
    case ARG_INVERSION: {
      aasink->ascii_parms.inversion = g_value_get_boolean (value);
      break;
    }
    case ARG_RANDOMVAL: {
      aasink->ascii_parms.randomval = g_value_get_int (value);
      break;
    }
    default:
      break;
  }
}

static void
gst_aasink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAASink *aasink;

  /* it's not null if we got it, but it might not be ours */
  aasink = GST_AASINK(object);

  switch (prop_id) {
    case ARG_WIDTH: {
      g_value_set_int (value, aasink->ascii_surf.width);
      break;
    }
    case ARG_HEIGHT: {
      g_value_set_int (value, aasink->ascii_surf.height);
      break;
    }
    case ARG_DRIVER: {
      g_value_set_int (value, aasink->aa_driver);
      break;
    }
    case ARG_DITHER: {
      g_value_set_int (value, aasink->ascii_parms.dither);
      break;
    }
    case ARG_BRIGHTNESS: {
      g_value_set_int (value, aasink->ascii_parms.bright);
      break;
    }
    case ARG_CONTRAST: {
      g_value_set_int (value, aasink->ascii_parms.contrast);
      break;
    }
    case ARG_GAMMA: {
      g_value_set_float (value, aasink->ascii_parms.gamma);
      break;
    }
    case ARG_INVERSION: {
      g_value_set_int (value, aasink->ascii_parms.inversion);
      break;
    }
    case ARG_RANDOMVAL: {
      g_value_set_boolean (value, aasink->ascii_parms.randomval);
      break;
    }
    case ARG_FRAMES_DISPLAYED: {
      g_value_set_int (value, aasink->frames_displayed);
      break;
    }
    case ARG_FRAME_TIME: {
      g_value_set_int (value, aasink->frame_time/1000000);
      break;
    }
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}

static gboolean
gst_aasink_open (GstAASink *aasink)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (aasink ,GST_AASINK_OPEN), FALSE);

  aa_recommendhidisplay(aa_drivers[aasink->aa_driver]->shortname);

  aasink->context = aa_autoinit (&aasink->ascii_surf);
  if (aasink->context == NULL) {
    gst_element_error (GST_ELEMENT (aasink), 
		    g_strdup("opening aalib context"));
    return FALSE;
  }
  aa_autoinitkbd(aasink->context, 0);
  aa_resizehandler(aasink->context, (void *)aa_resize);

  GST_FLAG_SET (aasink, GST_AASINK_OPEN);

  return TRUE;
}

static void
gst_aasink_close (GstAASink *aasink)
{
  g_return_if_fail (GST_FLAG_IS_SET (aasink ,GST_AASINK_OPEN));

  aa_close (aasink->context);

  GST_FLAG_UNSET (aasink, GST_AASINK_OPEN);
}

static GstElementStateReturn
gst_aasink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_AASINK (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_AASINK_OPEN))
      gst_aasink_close (GST_AASINK (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_AASINK_OPEN)) {
      if (!gst_aasink_open (GST_AASINK (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the aasink element */
  factory = gst_elementfactory_new("aasink",GST_TYPE_AASINK,
                                   &gst_aasink_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, 
		  GST_PADTEMPLATE_GET (sink_template));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "aasink",
  plugin_init
};
