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

#include "gstsmoothwave.h"

static GstElementDetails gst_smoothwave_details =
GST_ELEMENT_DETAILS ("Smooth waveform",
    "Visualization",
    "Fading grayscale waveform display",
    "Erik Walthinsen <omega@cse.ogi.edu>");


/* SmoothWave signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_WIDGET
};

static void gst_smoothwave_base_init (gpointer g_class);
static void gst_smoothwave_class_init (GstSmoothWaveClass * klass);
static void gst_smoothwave_init (GstSmoothWave * smoothwave);

static void gst_smoothwave_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_smoothwave_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_smoothwave_chain (GstPad * pad, GstData * _data);

static GstElementClass *parent_class = NULL;

/*static guint gst_smoothwave_signals[LAST_SIGNAL] = { 0 }; */


GType
gst_smoothwave_get_type (void)
{
  static GType smoothwave_type = 0;

  if (!smoothwave_type) {
    static const GTypeInfo smoothwave_info = {
      sizeof (GstSmoothWaveClass),
      gst_smoothwave_base_init,
      NULL,
      (GClassInitFunc) gst_smoothwave_class_init,
      NULL,
      NULL,
      sizeof (GstSmoothWave),
      0,
      (GInstanceInitFunc) gst_smoothwave_init,
    };

    smoothwave_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstSmoothWave",
        &smoothwave_info, 0);
  }
  return smoothwave_type;
}

static void
gst_smoothwave_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_smoothwave_details);
}

static void
gst_smoothwave_class_init (GstSmoothWaveClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WIDTH, g_param_spec_int ("width", "width", "width", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));  /* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HEIGHT, g_param_spec_int ("height", "height", "height", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));      /* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WIDGET, g_param_spec_object ("widget", "widget", "widget", GTK_TYPE_WIDGET, G_PARAM_READABLE));  /* CHECKME! */

  gobject_class->set_property = gst_smoothwave_set_property;
  gobject_class->get_property = gst_smoothwave_get_property;
}

static void
gst_smoothwave_init (GstSmoothWave * smoothwave)
{
  int i;
  guint32 palette[256];

  smoothwave->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (smoothwave), smoothwave->sinkpad);
  gst_pad_set_chain_function (smoothwave->sinkpad, gst_smoothwave_chain);
  smoothwave->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (smoothwave), smoothwave->srcpad);

/*  smoothwave->meta = NULL; */
  smoothwave->width = 512;
  smoothwave->height = 256;

  gdk_rgb_init ();
/*  gtk_widget_set_default_colormap (gdk_rgb_get_cmap()); */
/*  gtk_widget_set_default_visual (gdk_rgb_get_visual()); */

/*  GST_DEBUG ("creating palette"); */
  for (i = 0; i < 256; i++)
    palette[i] = (i << 16) || (i << 8);
/*  GST_DEBUG ("creating cmap"); */
  smoothwave->cmap = gdk_rgb_cmap_new (palette, 256);
/*  GST_DEBUG ("created cmap"); */
/*  gtk_widget_set_default_colormap (smoothwave->cmap); */

  smoothwave->image = gtk_drawing_area_new ();
  gtk_drawing_area_size (GTK_DRAWING_AREA (smoothwave->image),
      smoothwave->width, smoothwave->height);
  gtk_widget_show (smoothwave->image);

  smoothwave->imagebuffer = g_malloc (smoothwave->width * smoothwave->height);
  memset (smoothwave->imagebuffer, 0, smoothwave->width * smoothwave->height);
}

static void
gst_smoothwave_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstSmoothWave *smoothwave;
  gint16 *samples;
  gint samplecount, i;
  register guint32 *ptr;
  gint qheight;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
/*  g_return_if_fail(GST_IS_BUFFER(buf)); */

  smoothwave = GST_SMOOTHWAVE (GST_OBJECT_PARENT (pad));

  /* first deal with audio metadata */
#if 0
  if (buf->meta) {
    if (smoothwave->meta != NULL) {
      /* FIXME: need to unref the old metadata so it goes away */
    }
    /* we just make a copy of the pointer */
    smoothwave->meta = (MetaAudioRaw *) (buf->meta);
    /* FIXME: now we have to ref the metadata so it doesn't go away */
  }
#endif

/*  g_return_if_fail(smoothwave->meta != NULL); */

  samples = (gint16 *) GST_BUFFER_DATA (buf);
/*  samplecount = buf->datasize / (smoothwave->meta->channels * sizeof(gint16)); */
  samplecount = GST_BUFFER_SIZE (buf) / (2 * sizeof (gint16));

  qheight = smoothwave->height / 4;

/*  GST_DEBUG ("traversing %d",smoothwave->width); */
  for (i = 0; i < MAX (smoothwave->width, samplecount); i++) {
    gint16 y1 = (gint32) (samples[i * 2] * qheight) / 32768 + qheight;
    gint16 y2 = (gint32) (samples[(i * 2) + 1] * qheight) / 32768 +
        (qheight * 3);
    smoothwave->imagebuffer[y1 * smoothwave->width + i] = 0xff;
    smoothwave->imagebuffer[y2 * smoothwave->width + i] = 0xff;
/*    smoothwave->imagebuffer[i+(smoothwave->width*5)] = i; */
  }

  ptr = (guint32 *) smoothwave->imagebuffer;
  for (i = 0; i < (smoothwave->width * smoothwave->height) / 4; i++) {
    if (*ptr) {
      *ptr -= ((*ptr & 0xf0f0f0f0ul) >> 4) + ((*ptr & 0xe0e0e0e0ul) >> 5);
      ptr++;
    } else {
      ptr++;
    }
  }

/*  GST_DEBUG ("drawing"); */
/*  GST_DEBUG ("gdk_draw_indexed_image(%p,%p,%d,%d,%d,%d,%s,%p,%d,%p);",
        smoothwave->image->window,
	smoothwave->image->style->fg_gc[GTK_STATE_NORMAL],
	0,0,smoothwave->width,smoothwave->height,
	"GDK_RGB_DITHER_NORMAL",
	smoothwave->imagebuffer,smoothwave->width,
	smoothwave->cmap);*/
/*  gdk_draw_indexed_image(smoothwave->image->window,
	smoothwave->image->style->fg_gc[GTK_STATE_NORMAL],
	0,0,smoothwave->width,smoothwave->height,
	GDK_RGB_DITHER_NONE,
	smoothwave->imagebuffer,smoothwave->width,
	smoothwave->cmap);*/
  gdk_draw_gray_image (smoothwave->image->window,
      smoothwave->image->style->fg_gc[GTK_STATE_NORMAL],
      0, 0, smoothwave->width, smoothwave->height,
      GDK_RGB_DITHER_NORMAL, smoothwave->imagebuffer, smoothwave->width);

/*  gst_trace_add_entry(NULL,0,buf,"smoothwave: calculated smoothwave"); */

  gst_buffer_unref (buf);
}

static void
gst_smoothwave_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSmoothWave *smoothwave;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SMOOTHWAVE (object));
  smoothwave = GST_SMOOTHWAVE (object);

  switch (prop_id) {
    case ARG_WIDTH:
      smoothwave->width = g_value_get_int (value);
      gtk_drawing_area_size (GTK_DRAWING_AREA (smoothwave->image),
          smoothwave->width, smoothwave->height);
      gtk_widget_set_usize (GTK_WIDGET (smoothwave->image),
          smoothwave->width, smoothwave->height);
      break;
    case ARG_HEIGHT:
      smoothwave->height = g_value_get_int (value);
      gtk_drawing_area_size (GTK_DRAWING_AREA (smoothwave->image),
          smoothwave->width, smoothwave->height);
      gtk_widget_set_usize (GTK_WIDGET (smoothwave->image),
          smoothwave->width, smoothwave->height);
      break;
    default:
      break;
  }
}

static void
gst_smoothwave_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSmoothWave *smoothwave;

  /* it's not null if we got it, but it might not be ours */
  smoothwave = GST_SMOOTHWAVE (object);

  switch (prop_id) {
    case ARG_WIDTH:{
      g_value_set_int (value, smoothwave->width);
      break;
    }
    case ARG_HEIGHT:{
      g_value_set_int (value, smoothwave->height);
      break;
    }
    case ARG_WIDGET:{
      g_value_set_object (value, smoothwave->image);
      break;
    }
    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}



static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "smoothwave", GST_RANK_NONE,
          GST_TYPE_SMOOTHWAVE))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "smoothwave",
    "Fading greyscale waveform display",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
