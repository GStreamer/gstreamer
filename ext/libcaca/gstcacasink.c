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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <sys/time.h>
#include <gst/navigation/navigation.h>

#include "gstcacasink.h"

/* elementfactory information */
static GstElementDetails gst_cacasink_details = {
  "CACA sink",
  "Sink/Video",
  "A colored ASCII art videosink",
  "Zeeshan Ali <zak147@yahoo.com>"
};

/* cacasink signals and args */
enum {
  LAST_SIGNAL
};


enum {
  ARG_0,
  ARG_SCREEN_WIDTH,
  ARG_SCREEN_HEIGHT,
  ARG_DITHER,
};

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB));

static void	gst_cacasink_base_init	(gpointer g_class);
static void	gst_cacasink_class_init	(GstCACASinkClass *klass);
static void	gst_cacasink_init		(GstCACASink *cacasink);
static void	gst_cacasink_interface_init	(GstImplementsInterfaceClass *klass);
static gboolean	gst_cacasink_interface_supported (GstImplementsInterface *iface, GType type);
static void     gst_cacasink_navigation_init  (GstNavigationInterface *iface);
static void     gst_cacasink_navigation_send_event (GstNavigation *navigation, GstStructure *structure);

static void	gst_cacasink_chain	(GstPad *pad, GstData *_data);

static void	gst_cacasink_set_property	(GObject *object, guint prop_id, 
					 const GValue *value, GParamSpec *pspec);
static void	gst_cacasink_get_property	(GObject *object, guint prop_id, 
					 GValue *value, GParamSpec *pspec);

static GstElementStateReturn gst_cacasink_change_state (GstElement *element);

static GstElementClass *parent_class = NULL;

GType
gst_cacasink_get_type (void)
{
  static GType cacasink_type = 0;

  if (!cacasink_type) {
    static const GTypeInfo cacasink_info = {
      sizeof(GstCACASinkClass),
      gst_cacasink_base_init,
      NULL,
      (GClassInitFunc) gst_cacasink_class_init,
      NULL,
      NULL,
      sizeof(GstCACASink),
      0,
      (GInstanceInitFunc) gst_cacasink_init,
    };
    
    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) gst_cacasink_interface_init,
      NULL,
      NULL,
    };

    static const GInterfaceInfo navigation_info = {
      (GInterfaceInitFunc) gst_cacasink_navigation_init,
      NULL,
      NULL,
    };

    cacasink_type = g_type_register_static (GST_TYPE_VIDEOSINK, "GstCACASink", &cacasink_info, 0);
    
    g_type_add_interface_static (cacasink_type, GST_TYPE_IMPLEMENTS_INTERFACE,
        &iface_info);
    g_type_add_interface_static (cacasink_type, GST_TYPE_NAVIGATION,
        &navigation_info);
  }
  return cacasink_type;
}

#define GST_TYPE_CACADITHER (gst_cacasink_dither_get_type())
static GType
gst_cacasink_dither_get_type (void)
{
  static GType dither_type = 0;
  if (!dither_type) {
    GEnumValue *dithers;
    gint n_dithers;
    gint i;
    gchar *caca_dithernames[] = {
	"NONE", "ORDERED2", "ORDERED4", "ORDERED8", "RANDOM", NULL};

    n_dithers = 5;
    
    dithers = g_new0(GEnumValue, n_dithers + 1);

    for (i = 0; i < n_dithers; i++){
      dithers[i].value = i;
      dithers[i].value_name = g_strdup (caca_dithernames[i]);
      dithers[i].value_nick = g_strdup (caca_dithernames[i]);
    }
    dithers[i].value = 0;
    dithers[i].value_name = NULL;
    dithers[i].value_nick = NULL;

    dither_type = g_enum_register_static ("GstCACASinkDithers", dithers);
  }
  return dither_type;
}

static void
gst_cacasink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_cacasink_details);
  gst_element_class_add_pad_template (element_class, 
    gst_static_pad_template_get (&sink_template));
}

static void
gst_cacasink_class_init (GstCACASinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideoSinkClass *gstvs_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;
  gstvs_class = (GstVideoSinkClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SCREEN_WIDTH,
    g_param_spec_int("screen_width","screen_width","screen_width",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SCREEN_HEIGHT,
    g_param_spec_int("screen_height","screen_height","screen_height",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DITHER,
    g_param_spec_enum("dither","dither","dither",
                      GST_TYPE_CACADITHER, 0, G_PARAM_READWRITE));

  gobject_class->set_property = gst_cacasink_set_property;
  gobject_class->get_property = gst_cacasink_get_property;

  gstelement_class->change_state = gst_cacasink_change_state;
}

static void
gst_cacasink_interface_init (GstImplementsInterfaceClass *klass)
{
  klass->supported = gst_cacasink_interface_supported;
}

static gboolean
gst_cacasink_interface_supported (GstImplementsInterface *iface, GType type)
{
  g_assert (type == GST_TYPE_NAVIGATION);

  return (GST_STATE (iface) != GST_STATE_NULL);
}

static void
gst_cacasink_navigation_init (GstNavigationInterface *iface)
{
  iface->send_event = gst_cacasink_navigation_send_event;
}

static void
gst_cacasink_navigation_send_event (GstNavigation *navigation,
    GstStructure *structure)
{
  GstCACASink *cacasink = GST_CACASINK (navigation);
  GstEvent *event;

  event = gst_event_new (GST_EVENT_NAVIGATION);
  /*GST_EVENT_TIMESTAMP (event) = 0;*/
  event->event_data.structure.structure = structure;

  /* FIXME 
   * Obviously, the pointer x,y coordinates need to be adjusted by the
   * window size and relation to the bounding window. */

  gst_pad_send_event (gst_pad_get_peer (GST_VIDEOSINK_PAD(cacasink)),
      event);
}
static GstPadLinkReturn
gst_cacasink_sinkconnect (GstPad *pad, const GstCaps *caps)
{
  GstCACASink *cacasink;
  GstStructure *structure;

  cacasink = GST_CACASINK (gst_pad_get_parent (pad));

  /*if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;*/
  
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width",
                    &(GST_VIDEOSINK_WIDTH (cacasink)));
  gst_structure_get_int (structure, "height",
                    &(GST_VIDEOSINK_HEIGHT (cacasink)));
  gst_structure_get_int (structure, "bpp", &cacasink->bpp);
  gst_structure_get_int (structure, "red_mask", &cacasink->red_mask);
  gst_structure_get_int (structure, "green_mask", &cacasink->green_mask);
  gst_structure_get_int (structure, "blue_mask", &cacasink->blue_mask);

  gst_video_sink_got_video_size (GST_VIDEOSINK (cacasink), GST_VIDEOSINK_WIDTH (cacasink), GST_VIDEOSINK_HEIGHT (cacasink));

  /*if (cacasink->bitmap != NULL) {
    caca_free_bitmap (cacasink->bitmap);
  }

  caca->bitmap = caca_create_bitmap (cacasink->bpp, cacasink->image_width, cacasink->image_height, cacasink->image_width * cacasink->bpp/8, cacasink->red_mask, cacasink->green_mask, cacasink->blue_mask);*/

  return GST_PAD_LINK_OK;
}

static void
gst_cacasink_init (GstCACASink *cacasink)
{
  GST_VIDEOSINK_PAD (cacasink) = gst_pad_new_from_template (
		  gst_static_pad_template_get (&sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (cacasink), GST_VIDEOSINK_PAD (cacasink));
  gst_pad_set_chain_function (GST_VIDEOSINK_PAD (cacasink), 
			      gst_cacasink_chain);
  gst_pad_set_link_function (GST_VIDEOSINK_PAD (cacasink), 
			     gst_cacasink_sinkconnect);

  cacasink->screen_width = GST_CACA_DEFAULT_SCREEN_WIDTH;
  cacasink->screen_height = GST_CACA_DEFAULT_SCREEN_HEIGHT;
  cacasink->bpp = GST_CACA_DEFAULT_BPP;
  cacasink->red_mask = GST_CACA_DEFAULT_RED_MASK;
  cacasink->green_mask = GST_CACA_DEFAULT_GREEN_MASK;
  cacasink->blue_mask = GST_CACA_DEFAULT_BLUE_MASK;

  cacasink->bitmap = NULL;

  GST_FLAG_SET(cacasink, GST_ELEMENT_THREAD_SUGGESTED);
}

static void
gst_cacasink_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstCACASink *cacasink;
  GstClockTime time = GST_BUFFER_TIMESTAMP (buf);
  gint64 jitter;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  cacasink = GST_CACASINK (gst_pad_get_parent (pad));

  if (GST_VIDEOSINK_CLOCK (cacasink) && time != -1) {
    GstClockReturn ret;

    cacasink->id = gst_clock_new_single_shot_id (
                       GST_VIDEOSINK_CLOCK (cacasink), time);

    GST_DEBUG ("videosink: clock %s wait: %" G_GUINT64_FORMAT " %u", 
               GST_OBJECT_NAME (GST_VIDEOSINK_CLOCK (cacasink)),
               time, GST_BUFFER_SIZE (buf));

    ret = gst_clock_id_wait (cacasink->id, &jitter);
    gst_clock_id_free (cacasink->id);
    cacasink->id = NULL;
  }

  //caca_clear ();
  caca_draw_bitmap (0, 0, cacasink->screen_width-1, cacasink->screen_height-1, cacasink->bitmap, GST_BUFFER_DATA (buf));
  caca_refresh ();

  if (GST_VIDEOSINK_CLOCK (cacasink)) {
    jitter = gst_clock_get_time (GST_VIDEOSINK_CLOCK (cacasink)) - time;

    cacasink->correction = (cacasink->correction + jitter) >> 1;
    cacasink->correction = 0;
  }


  gst_buffer_unref(buf);
}


static void
gst_cacasink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstCACASink *cacasink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_CACASINK (object));

  cacasink = GST_CACASINK (object);

  switch (prop_id) {
    case ARG_DITHER: {
      cacasink->dither = g_value_get_enum (value);
      break;
    }
    default:
      break;
  }
}

static void
gst_cacasink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstCACASink *cacasink;

  /* it's not null if we got it, but it might not be ours */
  cacasink = GST_CACASINK(object);

  switch (prop_id) {
    case ARG_SCREEN_WIDTH: {
      g_value_set_int (value, cacasink->screen_width);
      break;
    }
    case ARG_SCREEN_HEIGHT: {
      g_value_set_int (value, cacasink->screen_height);
      break;
    }
    case ARG_DITHER: {
      g_value_set_enum (value, cacasink->dither);
      break;
    }
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}

static gboolean
gst_cacasink_open (GstCACASink *cacasink)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (cacasink ,GST_CACASINK_OPEN), FALSE);

  caca_init ();

  cacasink->screen_width = caca_get_width ();
  cacasink->screen_height = caca_get_height ();
  caca_set_dithering (cacasink->dither + CACA_DITHERING_NONE);

  cacasink->bitmap = caca_create_bitmap (
			cacasink->bpp, 
			GST_VIDEOSINK_WIDTH (cacasink), 
			GST_VIDEOSINK_HEIGHT (cacasink), 
			GST_VIDEOSINK_WIDTH (cacasink) * cacasink->bpp/8, 
			cacasink->red_mask, 
			cacasink->green_mask, 
			cacasink->blue_mask,
			0);

  GST_FLAG_SET (cacasink, GST_CACASINK_OPEN);

  return TRUE;
}

static void
gst_cacasink_close (GstCACASink *cacasink)
{
  g_return_if_fail (GST_FLAG_IS_SET (cacasink ,GST_CACASINK_OPEN));

  caca_free_bitmap (cacasink->bitmap);
  cacasink->bitmap = NULL;
  caca_end ();

  GST_FLAG_UNSET (cacasink, GST_CACASINK_OPEN);
}

static GstElementStateReturn
gst_cacasink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_CACASINK (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_CACASINK_OPEN))
      gst_cacasink_close (GST_CACASINK (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_CACASINK_OPEN)) {
      if (!gst_cacasink_open (GST_CACASINK (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  /* Loading the library containing GstVideoSink, our parent object */
  if (!gst_library_load ("gstvideo"))
    return FALSE;
  
  if (!gst_element_register (plugin, "cacasink", GST_RANK_NONE, GST_TYPE_CACASINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "cacasink",
  "Colored ASCII Art video sink",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
