/* -*- c-basic-offset: 2 -*-
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
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
#  include "config.h"
#endif

#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "gstspeed.h"

/* buffer size to make if no bufferpool is available, must be divisible by
 * sizeof(gfloat) */
#define SPEED_BUFSIZE 4096
/* number of buffers to allocate per chunk in sink buffer pool */
#define SPEED_NUMBUF 6

/* elementfactory information */
static GstElementDetails speed_details = GST_ELEMENT_DETAILS (
  "Speed",
  "Filter/Effect/Audio",
  "Set speed/pitch on audio/raw streams (resampler)",
  "Andy Wingo <apwingo@eos.ncsu.edu>"
);


/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SPEED
};

static GstPadTemplate*
speed_sink_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template) {
    template = gst_pad_template_new
      ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_append(gst_caps_new ("sink_int",  "audio/x-raw-int",
                                    GST_AUDIO_INT_PAD_TEMPLATE_PROPS),
                      gst_caps_new ("sink_float", "audio/x-raw-float",
                                    GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_PROPS)),
      NULL);
  }
  return template;
}

static GstPadTemplate*
speed_src_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template)
    template = gst_pad_template_new
      ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
       gst_caps_append (gst_caps_new ("src_float", "audio/x-raw-float",
                                      GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_PROPS),
                        gst_caps_new ("src_int", "audio/x-raw-int",
                                      GST_AUDIO_INT_PAD_TEMPLATE_PROPS)),
       NULL);

  return template;
}

static GstBufferPool*
speed_sink_get_bufferpool (GstPad *pad)
{
  GstSpeed *filter;

  filter = GST_SPEED (gst_pad_get_parent(pad));

  return filter->sinkpool;
}

static void		speed_base_init			(gpointer g_class);
static void		speed_class_init		(GstSpeedClass *klass);
static void		speed_init		(GstSpeed *filter);

static void		speed_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		speed_get_property        (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean		speed_parse_caps          (GstSpeed *filter, GstCaps *caps);

static void		speed_loop              (GstElement *element);

static GstElementClass *parent_class = NULL;
/*static guint gst_filter_signals[LAST_SIGNAL] = { 0 }; */

static GstPadLinkReturn
speed_link (GstPad *pad, GstCaps *caps)
{
  GstSpeed *filter;
  GstPad *otherpad;

  filter = GST_SPEED (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_SPEED (filter), GST_PAD_LINK_REFUSED);
  otherpad = (pad == filter->srcpad ? filter->sinkpad : filter->srcpad);

  if (GST_CAPS_IS_FIXED (caps)) {
    if (! speed_parse_caps (filter, caps)) return GST_PAD_LINK_REFUSED;

    return gst_pad_try_set_caps(otherpad, caps);
  }

  return GST_PAD_LINK_DELAYED;
}

static gboolean
speed_parse_caps (GstSpeed *filter, GstCaps *caps)
{
  const gchar *mimetype;

  g_return_val_if_fail(filter != NULL, FALSE);
  g_return_val_if_fail(caps != NULL, FALSE);

  gst_caps_get_int (caps, "rate", &filter->rate);
  gst_caps_get_int (caps, "channels", &filter->channels);
  gst_caps_get_int (caps, "width", &filter->width);
  gst_caps_get_int (caps, "endianness", &filter->endianness);
  gst_caps_get_int (caps, "buffer-frames", &filter->buffer_frames);

  mimetype = gst_caps_get_mime (caps);

  if (! strncmp(mimetype, "audio/x-raw-int", 15)) {
    filter->format = GST_SPEED_FORMAT_INT;
    gst_caps_get_int	 (caps, "depth", &filter->depth);
    gst_caps_get_boolean (caps, "signed", &filter->is_signed);
  } else if (! strncmp(mimetype, "audio/x-raw-float", 17)) {
    filter->format = GST_SPEED_FORMAT_FLOAT;
  } else  {
    return FALSE;
  }
  return TRUE;
}


GType
gst_speed_get_type(void) {
  static GType speed_type = 0;

  if (!speed_type) {
    static const GTypeInfo speed_info = {
      sizeof(GstSpeedClass),
      speed_base_init,
      NULL,
      (GClassInitFunc)speed_class_init,
      NULL,
      NULL,
      sizeof(GstSpeed),
      0,
      (GInstanceInitFunc)speed_init,
    };
    speed_type = g_type_register_static(GST_TYPE_ELEMENT, "GstSpeed", &speed_info, 0);
  }
  return speed_type;
}

static void
speed_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &speed_details);

  gst_element_class_add_pad_template (element_class, speed_src_factory ());
  gst_element_class_add_pad_template (element_class, speed_sink_factory ());
}
static void
speed_class_init (GstSpeedClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass*)klass;

  gobject_class->set_property = speed_set_property;
  gobject_class->get_property = speed_get_property;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SPEED,
    g_param_spec_float("speed","speed","speed",
                       0.1,40.0,1.0,G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
speed_init (GstSpeed *filter)
{
  filter->sinkpad = gst_pad_new_from_template(speed_sink_factory (), "sink");
  gst_pad_set_link_function(filter->sinkpad, speed_link);
  gst_element_add_pad(GST_ELEMENT(filter),filter->sinkpad);
  gst_pad_set_bufferpool_function (filter->sinkpad, speed_sink_get_bufferpool);

  filter->srcpad = gst_pad_new_from_template(speed_src_factory (), "src");
  gst_pad_set_link_function(filter->srcpad, speed_link);
  gst_element_add_pad(GST_ELEMENT(filter),filter->srcpad);

  gst_element_set_loop_function(GST_ELEMENT(filter),speed_loop);

  filter->sinkpool = gst_buffer_pool_get_default(SPEED_BUFSIZE, SPEED_NUMBUF);
}

static void
speed_loop (GstElement *element)
{
  GstSpeed *filter = GST_SPEED(element);
  GstBuffer *in, *out;
  guint i, j, nin, nout;
  gfloat interp, speed, lower, i_float;

  g_return_if_fail(filter != NULL);
  g_return_if_fail(GST_IS_SPEED(filter));

  filter->srcpool = gst_pad_get_bufferpool(filter->srcpad);

  i = j = 0;
  speed = filter->speed;
  
  in = GST_BUFFER (gst_pad_pull(filter->sinkpad));

  if (GST_IS_EVENT (in)) {
    gst_pad_event_default (filter->sinkpad, GST_EVENT (in));
    return;
  }

  while (GST_IS_EVENT (in)) {
    gst_pad_event_default (filter->srcpad, GST_EVENT (in));
    in = GST_BUFFER (gst_pad_pull (filter->sinkpad));
  }

  /* this is a bit nasty, but hey, it's what you've got to do to keep the same
   * algorithm and multiple data types in c. */
  if (filter->format==GST_SPEED_FORMAT_FLOAT) {
#define _FORMAT gfloat
#include "filter.func"
#undef _FORMAT
  } else if (filter->format==GST_SPEED_FORMAT_INT && filter->width==16) {
#define _FORMAT gint16
#include "filter.func"
#undef _FORMAT
  } else if (filter->format==GST_SPEED_FORMAT_INT && filter->width==8) {
#define _FORMAT gint8
#include "filter.func"
#undef _FORMAT
  } else {
    gst_element_error (element, "capsnego was never performed, bailing...");
    gst_element_yield (element);
  }
}

static void
speed_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSpeed *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SPEED(object));
  filter = GST_SPEED(object);

  switch (prop_id)
  {
    case ARG_SPEED:
      filter->speed = g_value_get_float (value);
      break;
    default:
      break;
  }
}

static void
speed_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSpeed *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SPEED(object));
  filter = GST_SPEED(object);

  switch (prop_id) {
    case ARG_SPEED:
      g_value_set_float (value, filter->speed);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register(plugin, "speed", GST_RANK_NONE, GST_TYPE_SPEED);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "speed",
  "Set speed/pitch on audio/raw streams (resampler)",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
