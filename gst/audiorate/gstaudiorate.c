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

#include <gst/gst.h>
#include <gst/audio/audio.h>

#define GST_TYPE_AUDIO_RATE \
  (gst_audio_rate_get_type())
#define GST_AUDIO_RATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_RATE,GstAudioRate))
#define GST_AUDIO_RATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_RATE,GstAudioRate))
#define GST_IS_AUDIO_RATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_RATE))
#define GST_IS_AUDIO_RATE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_RATE))

typedef struct _GstAudioRate GstAudioRate;
typedef struct _GstAudioRateClass GstAudioRateClass;

struct _GstAudioRate
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint bytes_per_sample;

  /* audio state */
  guint64 next_offset;

  guint64 in, out, add, drop;
  gboolean silent;
};

struct _GstAudioRateClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails audio_rate_details =
GST_ELEMENT_DETAILS ("Audio rate adjuster",
    "Filter/Effect/Audio",
    "Drops/duplicates/adjusts timestamps on audio samples to make a perfect stream",
    "Wim Taymans <wim@fluendo.com>");

/* GstAudioRate signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_SILENT  TRUE

enum
{
  ARG_0,
  ARG_IN,
  ARG_OUT,
  ARG_ADD,
  ARG_DROP,
  ARG_SILENT,
  /* FILL ME */
};

static GstStaticPadTemplate gst_audio_rate_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS)
    );

static GstStaticPadTemplate gst_audio_rate_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS)
    );

static void gst_audio_rate_base_init (gpointer g_class);
static void gst_audio_rate_class_init (GstAudioRateClass * klass);
static void gst_audio_rate_init (GstAudioRate * audiorate);
static GstFlowReturn gst_audio_rate_chain (GstPad * pad, GstBuffer * buf);

static void gst_audio_rate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audio_rate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_audio_rate_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

/*static guint gst_audio_rate_signals[LAST_SIGNAL] = { 0 }; */

static GType
gst_audio_rate_get_type (void)
{
  static GType audio_rate_type = 0;

  if (!audio_rate_type) {
    static const GTypeInfo audio_rate_info = {
      sizeof (GstAudioRateClass),
      gst_audio_rate_base_init,
      NULL,
      (GClassInitFunc) gst_audio_rate_class_init,
      NULL,
      NULL,
      sizeof (GstAudioRate),
      0,
      (GInstanceInitFunc) gst_audio_rate_init,
    };

    audio_rate_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstAudioRate", &audio_rate_info, 0);
  }

  return audio_rate_type;
}

static void
gst_audio_rate_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &audio_rate_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_audio_rate_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_audio_rate_src_template));
}
static void
gst_audio_rate_class_init (GstAudioRateClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->set_property = gst_audio_rate_set_property;
  object_class->get_property = gst_audio_rate_get_property;

  g_object_class_install_property (object_class, ARG_IN,
      g_param_spec_uint64 ("in", "In",
          "Number of input samples", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_OUT,
      g_param_spec_uint64 ("out", "Out",
          "Number of output samples", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_ADD,
      g_param_spec_uint64 ("add", "Add",
          "Number of added samples", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_DROP,
      g_param_spec_uint64 ("drop", "Drop",
          "Number of dropped samples", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_SILENT,
      g_param_spec_boolean ("silent", "silent",
          "Don't emit notify for dropped and duplicated frames",
          DEFAULT_SILENT, G_PARAM_READWRITE));

  element_class->change_state = gst_audio_rate_change_state;
}

static gboolean
gst_audio_rate_setcaps (GstPad * pad, GstCaps * caps)
{
  GstAudioRate *audiorate;
  GstStructure *structure;
  GstPad *otherpad;
  gint ret, channels, depth;

  audiorate = GST_AUDIO_RATE (gst_pad_get_parent (pad));

  otherpad = (pad == audiorate->srcpad) ? audiorate->sinkpad :
      audiorate->srcpad;

  if (!gst_pad_set_caps (otherpad, caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "channels", &channels);
  ret &= gst_structure_get_int (structure, "depth", &depth);

  if (!ret)
    return FALSE;

  audiorate->bytes_per_sample = channels * (depth / 8);
  if (audiorate->bytes_per_sample == 0)
    audiorate->bytes_per_sample = 1;

  return TRUE;
}

static void
gst_audio_rate_init (GstAudioRate * audiorate)
{
  audiorate->sinkpad =
      gst_pad_new_from_static_template (&gst_audio_rate_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (audiorate), audiorate->sinkpad);
  gst_pad_set_chain_function (audiorate->sinkpad, gst_audio_rate_chain);
  gst_pad_set_setcaps_function (audiorate->sinkpad, gst_audio_rate_setcaps);
  gst_pad_set_getcaps_function (audiorate->sinkpad, gst_pad_proxy_getcaps);

  audiorate->srcpad =
      gst_pad_new_from_static_template (&gst_audio_rate_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (audiorate), audiorate->srcpad);
  gst_pad_set_setcaps_function (audiorate->srcpad, gst_audio_rate_setcaps);
  gst_pad_set_getcaps_function (audiorate->srcpad, gst_pad_proxy_getcaps);

  audiorate->bytes_per_sample = 1;
  audiorate->in = 0;
  audiorate->out = 0;
  audiorate->drop = 0;
  audiorate->add = 0;
  audiorate->silent = DEFAULT_SILENT;
}

static GstFlowReturn
gst_audio_rate_chain (GstPad * pad, GstBuffer * buf)
{
  GstAudioRate *audiorate;
  GstClockTime in_time, in_duration;
  guint64 in_offset, in_offset_end;
  gint in_size;
  GstFlowReturn ret = GST_FLOW_OK;

  audiorate = GST_AUDIO_RATE (gst_pad_get_parent (pad));

  audiorate->in++;

  in_time = GST_BUFFER_TIMESTAMP (buf);
  in_duration = GST_BUFFER_DURATION (buf);
  in_size = GST_BUFFER_SIZE (buf);
  in_offset = GST_BUFFER_OFFSET (buf);
  in_offset_end = GST_BUFFER_OFFSET_END (buf);

  if (in_offset == GST_CLOCK_TIME_NONE || in_offset_end == GST_CLOCK_TIME_NONE) {
    GST_WARNING_OBJECT (audiorate, "audiorate got buffer without offsets");
  }

  /* do we need to insert samples */
  if (in_offset > audiorate->next_offset) {
    GstBuffer *fill;
    gint fillsize;
    guint64 fillsamples;

    fillsamples = in_offset - audiorate->next_offset;
    fillsize = fillsamples * audiorate->bytes_per_sample;

    fill = gst_buffer_new_and_alloc (fillsize);
    memset (GST_BUFFER_DATA (fill), 0, fillsize);

    GST_LOG_OBJECT (audiorate, "inserting %lld samples", fillsamples);

    GST_BUFFER_DURATION (fill) = in_duration * fillsize / in_size;
    GST_BUFFER_TIMESTAMP (fill) = in_time - GST_BUFFER_DURATION (fill);
    GST_BUFFER_OFFSET (fill) = audiorate->next_offset;
    GST_BUFFER_OFFSET_END (fill) = in_offset;

    if ((ret = gst_pad_push (audiorate->srcpad, fill) != GST_FLOW_OK))
      goto beach;
    audiorate->out++;
    audiorate->add += fillsamples;

    if (!audiorate->silent)
      g_object_notify (G_OBJECT (audiorate), "add");
  } else if (in_offset < audiorate->next_offset) {
    /* need to remove samples */
    if (in_offset_end <= audiorate->next_offset) {
      guint64 drop = in_size / audiorate->bytes_per_sample;

      audiorate->drop += drop;

      GST_LOG_OBJECT (audiorate, "dropping %lld samples", drop);

      /* we can drop the buffer completely */
      gst_buffer_unref (buf);

      if (!audiorate->silent)
        g_object_notify (G_OBJECT (audiorate), "drop");

      goto beach;
    } else {
      guint64 truncsamples, truncsize, leftsize;
      GstBuffer *trunc;

      /* truncate buffer */
      truncsamples = audiorate->next_offset - in_offset;
      truncsize = truncsamples * audiorate->bytes_per_sample;
      leftsize = in_size - truncsize;

      trunc = gst_buffer_create_sub (buf, truncsize, in_size);
      GST_BUFFER_DURATION (trunc) = in_duration * leftsize / in_size;
      GST_BUFFER_TIMESTAMP (trunc) =
          in_time + in_duration - GST_BUFFER_DURATION (trunc);
      GST_BUFFER_OFFSET (trunc) = audiorate->next_offset;
      GST_BUFFER_OFFSET_END (trunc) = in_offset_end;

      GST_LOG_OBJECT (audiorate, "truncating %lld samples", truncsamples);

      gst_buffer_unref (buf);
      buf = trunc;

      audiorate->drop += truncsamples;
    }
  }
  ret = gst_pad_push (audiorate->srcpad, buf);
  audiorate->out++;

  audiorate->next_offset = in_offset_end;
beach:
  return ret;
}

static void
gst_audio_rate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstAudioRate *audiorate = GST_AUDIO_RATE (object);

  switch (prop_id) {
    case ARG_SILENT:
      audiorate->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_rate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstAudioRate *audiorate = GST_AUDIO_RATE (object);

  switch (prop_id) {
    case ARG_IN:
      g_value_set_uint64 (value, audiorate->in);
      break;
    case ARG_OUT:
      g_value_set_uint64 (value, audiorate->out);
      break;
    case ARG_ADD:
      g_value_set_uint64 (value, audiorate->add);
      break;
    case ARG_DROP:
      g_value_set_uint64 (value, audiorate->drop);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, audiorate->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_audio_rate_change_state (GstElement * element, GstStateChange transition)
{
  GstAudioRate *audiorate = GST_AUDIO_RATE (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      audiorate->next_offset = 0;
      break;
    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "audiorate", GST_RANK_NONE,
      GST_TYPE_AUDIO_RATE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "audiorate",
    "Adjusts audio frames",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
