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

#include <gstvideodrop.h>
#include <gst/video/video.h>

/* elementfactory information */
static GstElementDetails videodrop_details = GST_ELEMENT_DETAILS (
  "Video frame dropper",
  "Filter/Effect/Video",
  "Re-FPS'es video",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>"
);

/* GstVideodrop signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SPEED
  /* FILL ME */
};

static GstStaticPadTemplate gst_videodrop_src_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS(
    GST_VIDEO_YUV_PAD_TEMPLATE_CAPS("{ YUY2, I420, YV12, YUYV, UYVY }")
  )
);

static GstStaticPadTemplate gst_videodrop_sink_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS(
    GST_VIDEO_YUV_PAD_TEMPLATE_CAPS("{ YUY2, I420, YV12, YUYV, UYVY }")
  )
);

static void	gst_videodrop_base_init		(gpointer g_class);
static void	gst_videodrop_class_init	(GstVideodropClass *klass);
static void	gst_videodrop_init		(GstVideodrop *videodrop);
static void	gst_videodrop_chain		(GstPad *pad, GstData *_data);

static void	gst_videodrop_set_property	(GObject *object,
						 guint prop_id, 
		 				 const GValue *value,
						 GParamSpec *pspec);
static void	gst_videodrop_get_property	(GObject *object,
						 guint prop_id, 
						 GValue *value,
						 GParamSpec *pspec);

static GstElementStateReturn
		gst_videodrop_change_state	(GstElement *element);

static GstElementClass *parent_class = NULL;
/*static guint gst_videodrop_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_videodrop_get_type (void)
{
  static GType videodrop_type = 0;

  if (!videodrop_type) {
    static const GTypeInfo videodrop_info = {
      sizeof (GstVideodropClass),
      gst_videodrop_base_init,
      NULL,
      (GClassInitFunc) gst_videodrop_class_init,
      NULL,
      NULL,
      sizeof (GstVideodrop),
      0,
      (GInstanceInitFunc) gst_videodrop_init,
    };

    videodrop_type = g_type_register_static (GST_TYPE_ELEMENT,
					     "GstVideodrop",
					     &videodrop_info, 0);
  }

  return videodrop_type;
}

static void
gst_videodrop_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &videodrop_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_videodrop_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_videodrop_src_template));
}
static void
gst_videodrop_class_init (GstVideodropClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  g_object_class_install_property (object_class, ARG_SPEED,
    g_param_spec_float ("speed", "Speed",
			"Output speed (relative to input)",
                        0.01, 100, 1, G_PARAM_READWRITE));

  object_class->set_property = gst_videodrop_set_property;
  object_class->get_property = gst_videodrop_get_property;

  element_class->change_state = gst_videodrop_change_state;
}

#define gst_caps_get_float_range(caps, name, min, max) \
  gst_props_entry_get_float_range(gst_props_get_entry((caps)->properties, \
                                                    name), \
                                  min, max)

static GstPadLinkReturn
gst_videodrop_link (GstPad *pad, const GstCaps *caps)
{
  GstVideodrop *videodrop;
  GstStructure *structure;
  gboolean ret;
  double fps;

  videodrop = GST_VIDEODROP (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_double (structure, "framerate", &fps);

  if (!ret) return GST_PAD_LINK_REFUSED;

  if (pad == videodrop->srcpad) {
    videodrop->to_fps = fps;
  } else {
    videodrop->from_fps = fps;
  }

  return GST_PAD_LINK_OK;
}

static void
gst_videodrop_init (GstVideodrop *videodrop)
{
  GST_DEBUG ("gst_videodrop_init");
  videodrop->sinkpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_videodrop_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (videodrop), videodrop->sinkpad);
  gst_pad_set_chain_function (videodrop->sinkpad, gst_videodrop_chain);
  gst_pad_set_link_function (videodrop->sinkpad, gst_videodrop_link);

  videodrop->srcpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_videodrop_src_template), "src");
  gst_element_add_pad (GST_ELEMENT(videodrop), videodrop->srcpad);
  gst_pad_set_link_function (videodrop->srcpad, gst_videodrop_link);

  videodrop->inited = FALSE;
  videodrop->total = videodrop->pass = 0;
  videodrop->speed = 1.;
}

static void
gst_videodrop_chain (GstPad *pad, GstData *data)
{
  GstVideodrop *videodrop = GST_VIDEODROP (gst_pad_get_parent (pad));
  GstBuffer *buf;

  if (GST_IS_EVENT (data)) {
    gst_pad_event_default (videodrop->srcpad, GST_EVENT (data));
    return;
  }

  buf = GST_BUFFER (data);

  videodrop->total++;
  while (videodrop->to_fps / (videodrop->from_fps * videodrop->speed) >
	 (gfloat) videodrop->pass / videodrop->total) {
    videodrop->pass++;
    gst_buffer_ref (buf);
    GST_BUFFER_TIMESTAMP (buf) /= videodrop->speed;
    gst_pad_push (videodrop->srcpad, GST_DATA (buf));
  }

  gst_buffer_unref (buf);
}

static void
gst_videodrop_set_property (GObject      *object,
			    guint         prop_id, 
		 	    const GValue *value,
			    GParamSpec   *pspec)
{
  GstVideodrop *videodrop = GST_VIDEODROP (object);

  switch (prop_id) {
    case ARG_SPEED:
      videodrop->speed = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videodrop_get_property (GObject    *object,
			    guint       prop_id, 
			    GValue     *value,
			    GParamSpec *pspec)
{
  GstVideodrop *videodrop = GST_VIDEODROP (object);

  switch (prop_id) {
    case ARG_SPEED:
      g_value_set_float (value, videodrop->speed);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_videodrop_change_state (GstElement *element)
{
  GstVideodrop *videodrop = GST_VIDEODROP (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      videodrop->inited = FALSE;
      videodrop->total = videodrop->pass = 0;
      break;
    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "videodrop", GST_RANK_NONE, GST_TYPE_VIDEODROP);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videodrop",
  "Re-FPS'es video",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
