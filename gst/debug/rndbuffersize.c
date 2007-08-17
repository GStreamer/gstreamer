/* GStreamer
 * Copyright (C) 2007 Nokia Corporation (contact <stefan.kost@nokia.com>)
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
/**
 * SECTION:element-rndbuffersize
 *
 * This element pulls buffers with random sizes from the source.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC (gst_rnd_buffer_size_debug);
#define GST_CAT_DEFAULT gst_rnd_buffer_size_debug

#define GST_TYPE_RND_BUFFER_SIZE            (gst_rnd_buffer_size_get_type())
#define GST_RND_BUFFER_SIZE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RND_BUFFER_SIZE,GstRndBufferSize))
#define GST_RND_BUFFER_SIZE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RND_BUFFER_SIZE,GstRndBufferSizeClass))
#define GST_IS_RND_BUFFER_SIZE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RND_BUFFER_SIZE))
#define GST_IS_RND_BUFFER_SIZE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RND_BUFFER_SIZE))

typedef struct _GstRndBufferSize GstRndBufferSize;
typedef struct _GstRndBufferSizeClass GstRndBufferSizeClass;

struct _GstRndBufferSize
{
  GstElement parent;

  GRand *rand;
  gulong seed;
  glong min, max;

  /* < private > */
  GstPad *sinkpad, *srcpad;
  guint64 offset;
};

struct _GstRndBufferSizeClass
{
  GstElementClass parent_class;
};

enum
{
  ARG_SEED = 1,
  ARG_MINIMUM,
  ARG_MAXIMUM
};

GstStaticPadTemplate gst_rnd_buffer_size_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GstStaticPadTemplate gst_rnd_buffer_size_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_rnd_buffer_size_finalize (GObject * object);
static void gst_rnd_buffer_size_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rnd_buffer_size_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_rnd_buffer_size_activate (GstPad * pad);
static gboolean gst_rnd_buffer_size_activate_pull (GstPad * pad,
    gboolean active);
static void gst_rnd_buffer_size_loop (GstRndBufferSize * self);
static GstStateChangeReturn gst_rnd_buffer_size_change_state (GstElement *
    element, GstStateChange transition);


GST_BOILERPLATE (GstRndBufferSize, gst_rnd_buffer_size, GstElement,
    GST_TYPE_ELEMENT);


static void
gst_rnd_buffer_size_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  const GstElementDetails details = GST_ELEMENT_DETAILS ("Random buffer size",
      "Testing",
      "pull random sized buffers",
      "Nokia Corporation (contact <stefan.kost@nokia.com>)");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rnd_buffer_size_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rnd_buffer_size_src_template));

  gst_element_class_set_details (gstelement_class, &details);
}


static void
gst_rnd_buffer_size_class_init (GstRndBufferSizeClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_rnd_buffer_size_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_rnd_buffer_size_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_rnd_buffer_size_finalize);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rnd_buffer_size_change_state);

  g_object_class_install_property (gobject_class, ARG_SEED,
      g_param_spec_ulong ("seed", "random number seed",
          "seed for randomness (initialized when going from READY to PAUSED)",
          0, G_MAXULONG, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, ARG_MINIMUM,
      g_param_spec_long ("min", "mininum", "mininum buffer size",
          0, G_MAXLONG, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, ARG_MAXIMUM,
      g_param_spec_long ("max", "maximum", "maximum buffer size",
          0, G_MAXLONG, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

}


static void
gst_rnd_buffer_size_init (GstRndBufferSize * self,
    GstRndBufferSizeClass * g_class)
{
  self->sinkpad =
      gst_pad_new_from_static_template (&gst_rnd_buffer_size_sink_template,
      "sink");
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);
  gst_pad_set_activate_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rnd_buffer_size_activate));
  gst_pad_set_activatepull_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rnd_buffer_size_activate_pull));

  self->srcpad =
      gst_pad_new_from_static_template (&gst_rnd_buffer_size_src_template,
      "src");
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

}


static void
gst_rnd_buffer_size_finalize (GObject * object)
{
  GstRndBufferSize *self = GST_RND_BUFFER_SIZE (object);

  if (self->rand) {
    g_rand_free (self->rand);
    self->rand = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
gst_rnd_buffer_size_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRndBufferSize *self = GST_RND_BUFFER_SIZE (object);

  switch (prop_id) {
    case ARG_SEED:
      self->seed = g_value_get_ulong (value);
      break;
    case ARG_MINIMUM:
      self->min = g_value_get_long (value);
      break;
    case ARG_MAXIMUM:
      self->max = g_value_get_long (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_rnd_buffer_size_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRndBufferSize *self = GST_RND_BUFFER_SIZE (object);

  switch (prop_id) {
    case ARG_SEED:
      g_value_set_ulong (value, self->seed);
      break;
    case ARG_MINIMUM:
      g_value_set_long (value, self->min);
      break;
    case ARG_MAXIMUM:
      g_value_set_long (value, self->max);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
gst_rnd_buffer_size_activate (GstPad * pad)
{
  if (gst_pad_check_pull_range (pad)) {
    return gst_pad_activate_pull (pad, TRUE);
  } else {
    GST_INFO_OBJECT (GST_RND_BUFFER_SIZE (GST_OBJECT_PARENT (pad)),
        "push mode not supported");
    return FALSE;
  }
}


static gboolean
gst_rnd_buffer_size_activate_pull (GstPad * pad, gboolean active)
{
  GstRndBufferSize *self = GST_RND_BUFFER_SIZE (GST_OBJECT_PARENT (pad));

  if (active) {
    GST_INFO_OBJECT (self, "starting pull");
    return gst_pad_start_task (pad, (GstTaskFunction) gst_rnd_buffer_size_loop,
        self);
  } else {
    GST_INFO_OBJECT (self, "stopping pull");
    return gst_pad_stop_task (pad);
  }
}


static void
gst_rnd_buffer_size_loop (GstRndBufferSize * self)
{
  GstBuffer *buf = NULL;
  GstFlowReturn ret;
  gulong num_bytes = g_rand_int_range (self->rand, self->min, self->max);

  GST_INFO_OBJECT (self, "pull_range from %" G_GUINT64_FORMAT " of %lu bytes",
      self->offset, num_bytes);
  ret = gst_pad_pull_range (self->sinkpad, self->offset, num_bytes, &buf);
  if (ret == GST_FLOW_OK) {
    if (GST_BUFFER_SIZE (buf) < num_bytes) {
      self->offset += GST_BUFFER_SIZE (buf);
      GST_WARNING_OBJECT (self, "short buffer : %u < %lu",
          GST_BUFFER_SIZE (buf), num_bytes);
    } else {
      self->offset += num_bytes;
    }

    gst_pad_push (self->srcpad, buf);
  } else {
    GST_WARNING_OBJECT (self, "pull_range read failed: %s",
        gst_flow_get_name (ret));
    gst_pad_pause_task (self->sinkpad);
    if (ret == GST_FLOW_UNEXPECTED) {
      gst_pad_push_event (self->srcpad, gst_event_new_eos ());
    }
  }
}

static GstStateChangeReturn
gst_rnd_buffer_size_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRndBufferSize *self = GST_RND_BUFFER_SIZE (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->offset = 0;
      if (!self->rand) {
        self->rand = g_rand_new_with_seed (self->seed);
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (self->rand) {
        g_rand_free (self->rand);
        self->rand = NULL;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}


gboolean
gst_rnd_buffer_size_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "rndbuffersize", GST_RANK_NONE,
          GST_TYPE_RND_BUFFER_SIZE))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_rnd_buffer_size_debug, "rndbuffersize", 0,
      "debugging category for rndbuffersize element");

  return TRUE;
}
