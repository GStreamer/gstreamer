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
  SIGNAL_FRAME_DISPLAYED,
  SIGNAL_HAVE_SIZE,
  LAST_SIGNAL
};


enum {
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_DITHER,
  ARG_FRAMES_DISPLAYED,
  ARG_FRAME_TIME,
};

GST_PAD_TEMPLATE_FACTORY (sink_template,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  gst_caps_new (
    "cacasink_caps",
    "video/x-raw-rgb",
    GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_24_32
  )
)

static void	gst_cacasink_base_init	(gpointer g_class);
static void	gst_cacasink_class_init	(GstCACASinkClass *klass);
static void	gst_cacasink_init		(GstCACASink *cacasink);

static void 	gst_cacasink_set_clock 	(GstElement *element, GstClock *clock);
static void	gst_cacasink_chain	(GstPad *pad, GstData *_data);

static void	gst_cacasink_set_property	(GObject *object, guint prop_id, 
					 const GValue *value, GParamSpec *pspec);
static void	gst_cacasink_get_property	(GObject *object, guint prop_id, 
					 GValue *value, GParamSpec *pspec);

static GstElementStateReturn gst_cacasink_change_state (GstElement *element);

static GstElementClass *parent_class = NULL;
static guint gst_cacasink_signals[LAST_SIGNAL] = { 0 };

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
      (GInstanceInitFunc)gst_cacasink_init,
    };
    cacasink_type = g_type_register_static(GST_TYPE_ELEMENT, "GstCACASink", &cacasink_info, 0);
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
    gchar caca_dithernames[] = {
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

  gst_element_class_add_pad_template (element_class,
		GST_PAD_TEMPLATE_GET (sink_template));
  gst_element_class_set_details (element_class, &gst_cacasink_details);
}

static void
gst_cacasink_class_init (GstCACASinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_WIDTH,
    g_param_spec_int("width","width","width",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HEIGHT,
    g_param_spec_int("height","height","height",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DITHER,
    g_param_spec_enum("dither","dither","dither",
                      GST_TYPE_CACADITHER,0,G_PARAM_READWRITE)); /* CHECKME! */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FRAMES_DISPLAYED,
    g_param_spec_int("frames_displayed","frames_displayed","frames_displayed",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FRAME_TIME,
    g_param_spec_int("frame_time","frame_time","frame_time",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); /* CHECKME */

  gobject_class->set_property = gst_cacasink_set_property;
  gobject_class->get_property = gst_cacasink_get_property;

  gst_cacasink_signals[SIGNAL_FRAME_DISPLAYED] =
    g_signal_new ("frame_displayed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstCACASinkClass, frame_displayed), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_cacasink_signals[SIGNAL_HAVE_SIZE] =
    g_signal_new ("have_size", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstCACASinkClass, have_size), NULL, NULL,
                   gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2,
                   G_TYPE_UINT, G_TYPE_UINT);

  gstelement_class->change_state = gst_cacasink_change_state;
  gstelement_class->set_clock    = gst_cacasink_set_clock;
}

static GstPadLinkReturn
gst_cacasink_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstCACASink *cacasink;

  cacasink = GST_CACASINK (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;
  
  gst_caps_get_int (caps, "width", &cacasink->image_width);
  gst_caps_get_int (caps, "height", &cacasink->image_height);
  gst_caps_get_int (caps, "bpp", &cacasink->bpp);
  gst_caps_get_int (caps, "red_mask", &cacasink->red_mask);
  gst_caps_get_int (caps, "green_mask", &cacasink->green_mask);
  gst_caps_get_int (caps, "blue_mask", &cacasink->blue_mask);

  g_signal_emit( G_OBJECT (cacasink), gst_cacasink_signals[SIGNAL_HAVE_SIZE], 0,
		 cacasink->image_width, cacasink->image_height);

  /*if (cacasink->bitmap != NULL) {
    caca_free_bitmap (cacasink->bitmap);
  }

  caca->bitmap = caca_create_bitmap (cacasink->bpp, cacasink->image_width, cacasink->image_height, cacasink->image_width * cacasink->bpp/8, cacasink->red_mask, cacasink->green_mask, cacasink->blue_mask);*/

  return GST_PAD_LINK_OK;
}

static void
gst_cacasink_set_clock (GstElement *element, GstClock *clock)
{
  GstCACASink *cacasink = GST_CACASINK (element);

  cacasink->clock = clock;
}

static void
gst_cacasink_init (GstCACASink *cacasink)
{
  cacasink->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (cacasink), cacasink->sinkpad);
  gst_pad_set_chain_function (cacasink->sinkpad, gst_cacasink_chain);
  gst_pad_set_link_function (cacasink->sinkpad, gst_cacasink_sinkconnect);

  cacasink->screen_width = -1;
  cacasink->screen_height = -1;
  cacasink->image_width = GST_CACA_DEFAULT_IMAGE_WIDTH;
  cacasink->image_height = GST_CACA_DEFAULT_IMAGE_HEIGHT;
  cacasink->bpp = GST_CACA_DEFAULT_BPP;
  cacasink->red_mask = GST_CACA_DEFAULT_RED_MASK;
  cacasink->green_mask = GST_CACA_DEFAULT_GREEN_MASK;
  cacasink->blue_mask = GST_CACA_DEFAULT_BLUE_MASK;

  cacasink->clock = NULL;
  cacasink->bitmap = NULL;

  GST_FLAG_SET(cacasink, GST_ELEMENT_THREAD_SUGGESTED);
}

static void
gst_cacasink_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstCACASink *cacasink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  cacasink = GST_CACASINK (gst_pad_get_parent (pad));

  GST_DEBUG ("videosink: clock wait: %" G_GUINT64_FORMAT, GST_BUFFER_TIMESTAMP(buf));

  if (cacasink->clock) {
    GstClockID id = gst_clock_new_single_shot_id (cacasink->clock, GST_BUFFER_TIMESTAMP(buf));
    gst_element_clock_wait (GST_ELEMENT (cacasink), id, NULL);
    gst_clock_id_free (id);
  }

  caca_draw_bitmap (0, 0, cacasink->screen_width-1, cacasink->screen_height-1, cacasink->bitmap, GST_BUFFER_DATA (buf));
  caca_refresh ();

  g_signal_emit(G_OBJECT(cacasink),gst_cacasink_signals[SIGNAL_FRAME_DISPLAYED], 0);

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
    case ARG_WIDTH: {
      g_value_set_int (value, cacasink->screen_width);
      break;
    }
    case ARG_HEIGHT: {
      g_value_set_int (value, cacasink->screen_height);
      break;
    }
    case ARG_DITHER: {
      g_value_set_enum (value, cacasink->dither);
      break;
    }
    case ARG_FRAMES_DISPLAYED: {
      g_value_set_int (value, cacasink->frames_displayed);
      break;
    }
    case ARG_FRAME_TIME: {
      g_value_set_int (value, cacasink->frame_time/1000000);
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

  screen_width = caca_get_width ();
  screen_height = caca_get_height ();
  caca_set_dithering (cacasink->dither);

  caca->bitmap = caca_create_bitmap (
			cacasink->bpp, 
			cacasink->image_width, 
			cacasink->image_height, 
			cacasink->image_width * cacasink->bpp/8, 
			cacasink->red_mask, 
			cacasink->green_mask, 
			cacasink->blue_mask);

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
  "GPL",
  GST_PACKAGE,
  GST_ORIGIN
)
