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
static GstElementDetails videodrop_details =
GST_ELEMENT_DETAILS ("Video frame dropper",
    "Filter/Effect/Video",
    "Re-FPS'es video",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>");

/* GstVideodrop signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_SPEED
      /* FILL ME */
};

static GstStaticPadTemplate gst_videodrop_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ YUY2, I420, YV12, YUYV, UYVY }")
    )
    );

static GstStaticPadTemplate gst_videodrop_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ YUY2, I420, YV12, YUYV, UYVY }")
    )
    );

static void gst_videodrop_base_init (gpointer g_class);
static void gst_videodrop_class_init (GstVideodropClass * klass);
static void gst_videodrop_init (GstVideodrop * videodrop);
static void gst_videodrop_chain (GstPad * pad, GstData * _data);

static void gst_videodrop_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_videodrop_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstElementStateReturn gst_videodrop_change_state (GstElement * element);

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
        "GstVideodrop", &videodrop_info, 0);
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
gst_videodrop_class_init (GstVideodropClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  g_object_class_install_property (object_class, ARG_SPEED,
      g_param_spec_float ("speed", "Speed",
          "Output speed (relative to input)", 0.01, 100, 1, G_PARAM_READWRITE));

  object_class->set_property = gst_videodrop_set_property;
  object_class->get_property = gst_videodrop_get_property;

  element_class->change_state = gst_videodrop_change_state;
}

static GstCaps *
gst_videodrop_getcaps (GstPad * pad)
{
  GstVideodrop *videodrop;
  GstPad *otherpad;
  GstCaps *caps, *copy, *copy2 = NULL;
  int i;
  gdouble otherfps;
  GstStructure *structure;
  gboolean negotiated;

  videodrop = GST_VIDEODROP (gst_pad_get_parent (pad));

  otherpad = (pad == videodrop->srcpad) ? videodrop->sinkpad :
      videodrop->srcpad;
  negotiated = gst_pad_is_negotiated (otherpad);
  otherfps = (pad == videodrop->srcpad) ? videodrop->from_fps :
      videodrop->to_fps;

  caps = gst_pad_get_allowed_caps (otherpad);
  copy = gst_caps_copy (caps);
  if (negotiated) {
    copy2 = gst_caps_copy (caps);
  }
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    gst_structure_set (structure,
        "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE, NULL);
  }
  if (negotiated) {
    for (i = 0; i < gst_caps_get_size (caps); i++) {
      structure = gst_caps_get_structure (caps, i);

      gst_structure_set (structure,
          "framerate", G_TYPE_DOUBLE, otherfps * videodrop->speed, NULL);
    }
    gst_caps_append (copy2, copy);
    copy = copy2;
  }
  gst_caps_append (copy, caps);

  return copy;
}

static GstPadLinkReturn
gst_videodrop_link (GstPad * pad, const GstCaps * caps)
{
  GstVideodrop *videodrop;
  GstStructure *structure;
  gboolean ret;
  double fps;
  GstPad *otherpad;

  videodrop = GST_VIDEODROP (gst_pad_get_parent (pad));

  otherpad = (pad == videodrop->srcpad) ? videodrop->sinkpad :
      videodrop->srcpad;

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_double (structure, "framerate", &fps);
  if (!ret)
    return GST_PAD_LINK_REFUSED;
  if (pad == videodrop->srcpad) {
    videodrop->from_fps = fps;
  } else {
    videodrop->to_fps = fps;
  }

  if (gst_pad_is_negotiated (otherpad)) {
    gst_pad_renegotiate (otherpad);
  }

  return GST_PAD_LINK_OK;
}

static void
gst_videodrop_init (GstVideodrop * videodrop)
{
  GST_FLAG_SET (videodrop, GST_ELEMENT_EVENT_AWARE);

  GST_DEBUG ("gst_videodrop_init");
  videodrop->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_videodrop_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (videodrop), videodrop->sinkpad);
  gst_pad_set_chain_function (videodrop->sinkpad, gst_videodrop_chain);
  gst_pad_set_getcaps_function (videodrop->sinkpad, gst_videodrop_getcaps);
  gst_pad_set_link_function (videodrop->sinkpad, gst_videodrop_link);

  videodrop->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_videodrop_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (videodrop), videodrop->srcpad);
  gst_pad_set_getcaps_function (videodrop->srcpad, gst_videodrop_getcaps);
  gst_pad_set_link_function (videodrop->srcpad, gst_videodrop_link);

  videodrop->inited = FALSE;
  videodrop->total = videodrop->pass = 0;
  videodrop->speed = 1.;
  videodrop->time_adjust = 0;
}

static void
gst_videodrop_chain (GstPad * pad, GstData * data)
{
  GstVideodrop *videodrop = GST_VIDEODROP (gst_pad_get_parent (pad));
  GstBuffer *buf;

  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    if (GST_EVENT_TYPE (event) == GST_EVENT_DISCONTINUOUS) {
      /* since we rely on timestamps of the source, we need to handle
       * changes in time carefully. */
      gint64 time;

      if (gst_event_discont_get_value (event, GST_FORMAT_TIME, &time)) {
        videodrop->total = videodrop->pass = 0;
        videodrop->time_adjust = time;
      } else {
        GST_ELEMENT_ERROR (videodrop, STREAM, TOO_LAZY, (NULL),
            ("Received discont, but no time information"));
        gst_event_unref (event);
        return;
      }
      /* FIXME: increase timestamp / speed */
    }

    gst_pad_event_default (pad, event);
    return;
  }

  buf = GST_BUFFER (data);
  videodrop->total++;
  GST_DEBUG ("Received buffer at %u:%02u:%02u:%09u, fps=%lf, pass=%"
      G_GUINT64_FORMAT " of " G_GUINT64_FORMAT ", speed=%lf",
      (guint) (GST_BUFFER_TIMESTAMP (buf) / (GST_SECOND * 60 * 60)),
      (guint) ((GST_BUFFER_TIMESTAMP (buf) / (GST_SECOND * 60)) % 60),
      (guint) ((GST_BUFFER_TIMESTAMP (buf) / GST_SECOND) % 60),
      (guint) (GST_BUFFER_TIMESTAMP (buf) % GST_SECOND),
      videodrop->to_fps, videodrop->total, videodrop->pass, videodrop->speed);
  while (((GST_BUFFER_TIMESTAMP (buf) - videodrop->time_adjust) /
          videodrop->speed * videodrop->to_fps / GST_SECOND) >=
      videodrop->pass) {
    /* since we write to the struct (time/duration), we need a new struct,
     * but we don't want to copy around data - a subbuffer is the easiest
     * way to accomplish that... */
    GstBuffer *copy = gst_buffer_create_sub (buf, 0, GST_BUFFER_SIZE (buf));

    /* adjust timestamp/duration and push forward */
    GST_BUFFER_TIMESTAMP (copy) = (videodrop->time_adjust / videodrop->speed) +
        GST_SECOND * videodrop->pass / videodrop->to_fps;
    GST_BUFFER_DURATION (copy) = GST_SECOND / videodrop->to_fps;
    GST_DEBUG ("Sending out buffer from out %u:%02u:%02u:%09u",
        (guint) (GST_BUFFER_TIMESTAMP (copy) / (GST_SECOND * 60 * 60)),
        (guint) ((GST_BUFFER_TIMESTAMP (copy) / (GST_SECOND * 60)) % 60),
        (guint) ((GST_BUFFER_TIMESTAMP (copy) / GST_SECOND) % 60),
        (guint) (GST_BUFFER_TIMESTAMP (copy) % GST_SECOND));
    gst_pad_push (videodrop->srcpad, GST_DATA (copy));

    videodrop->pass++;
  }

  gst_buffer_unref (buf);
}

static void
gst_videodrop_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
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
gst_videodrop_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
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
gst_videodrop_change_state (GstElement * element)
{
  GstVideodrop *videodrop = GST_VIDEODROP (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      videodrop->inited = FALSE;
      videodrop->time_adjust = 0;
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
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "videodrop", GST_RANK_NONE,
      GST_TYPE_VIDEODROP);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videodrop",
    "Re-FPS'es video",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
