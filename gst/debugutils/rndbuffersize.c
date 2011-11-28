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

  /*< private > */
  GRand *rand;
  gulong seed;
  glong min, max;

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

#define DEFAULT_SEED 0
#define DEFAULT_MIN  1
#define DEFAULT_MAX  (8*1024)

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
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

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_rnd_buffer_size_debug, "rndbuffersize", 0, \
      "rndbuffersize element");

GType gst_rnd_buffer_size_get_type (void);
GST_BOILERPLATE_FULL (GstRndBufferSize, gst_rnd_buffer_size, GstElement,
    GST_TYPE_ELEMENT, DEBUG_INIT);


static void
gst_rnd_buffer_size_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (gstelement_class,
      &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &src_template);

  gst_element_class_set_details_simple (gstelement_class, "Random buffer size",
      "Testing", "pull random sized buffers",
      "Stefan Kost <stefan.kost@nokia.com>");
}


static void
gst_rnd_buffer_size_class_init (GstRndBufferSizeClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_rnd_buffer_size_set_property;
  gobject_class->get_property = gst_rnd_buffer_size_get_property;
  gobject_class->finalize = gst_rnd_buffer_size_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rnd_buffer_size_change_state);

  /* FIXME 0.11: these should all be int instead of long, to avoid bugs
   * when passing these as varargs with g_object_set(), and there was no
   * reason to use long in the first place here */
  g_object_class_install_property (gobject_class, ARG_SEED,
      g_param_spec_ulong ("seed", "random number seed",
          "seed for randomness (initialized when going from READY to PAUSED)",
          0, G_MAXUINT32, DEFAULT_SEED,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_MINIMUM,
      g_param_spec_long ("min", "mininum", "mininum buffer size",
          0, G_MAXINT32, DEFAULT_MIN,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_MAXIMUM,
      g_param_spec_long ("max", "maximum", "maximum buffer size",
          1, G_MAXINT32, DEFAULT_MAX,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

static void
gst_rnd_buffer_size_init (GstRndBufferSize * self,
    GstRndBufferSizeClass * g_class)
{
  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_activate_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rnd_buffer_size_activate));
  gst_pad_set_activatepull_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rnd_buffer_size_activate_pull));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&src_template, "src");
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
    GST_INFO_OBJECT (pad, "push mode not supported");
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
  guint num_bytes;

  if (G_UNLIKELY (self->min > self->max))
    goto bogus_minmax;

  if (G_UNLIKELY (self->min != self->max)) {
    num_bytes = g_rand_int_range (self->rand, self->min, self->max);
  } else {
    num_bytes = self->min;
  }

  GST_LOG_OBJECT (self, "pulling %u bytes at offset %" G_GUINT64_FORMAT,
      num_bytes, self->offset);

  ret = gst_pad_pull_range (self->sinkpad, self->offset, num_bytes, &buf);

  if (ret != GST_FLOW_OK)
    goto pull_failed;

  if (GST_BUFFER_SIZE (buf) < num_bytes) {
    GST_WARNING_OBJECT (self, "short buffer: %u bytes", GST_BUFFER_SIZE (buf));
  }

  self->offset += GST_BUFFER_SIZE (buf);

  ret = gst_pad_push (self->srcpad, buf);

  if (ret != GST_FLOW_OK)
    goto push_failed;

  return;

pause_task:
  {
    GST_DEBUG_OBJECT (self, "pausing task");
    gst_pad_pause_task (self->sinkpad);
    return;
  }

pull_failed:
  {
    if (ret == GST_FLOW_UNEXPECTED) {
      GST_DEBUG_OBJECT (self, "eos");
      gst_pad_push_event (self->srcpad, gst_event_new_eos ());
    } else {
      GST_WARNING_OBJECT (self, "pull_range flow: %s", gst_flow_get_name (ret));
    }
    goto pause_task;
  }

push_failed:
  {
    GST_DEBUG_OBJECT (self, "push flow: %s", gst_flow_get_name (ret));
    if (ret == GST_FLOW_UNEXPECTED) {
      GST_DEBUG_OBJECT (self, "eos");
      gst_pad_push_event (self->srcpad, gst_event_new_eos ());
    } else if (ret < GST_FLOW_UNEXPECTED || ret == GST_FLOW_NOT_LINKED) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED,
          ("Internal data stream error."),
          ("streaming stopped, reason: %s", gst_flow_get_name (ret)));
    }
    goto pause_task;
  }

bogus_minmax:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS,
        ("The minimum buffer size is smaller than the maximum buffer size."),
        ("buffer sizes: max=%ld, min=%ld", self->min, self->max));
    goto pause_task;
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
