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

#define GST_TYPE_AUDIORATE \
  (gst_audiorate_get_type())
#define GST_AUDIORATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIORATE,GstAudiorate))
#define GST_AUDIORATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIORATE,GstAudiorate))
#define GST_IS_AUDIORATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIORATE))
#define GST_IS_AUDIORATE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIORATE))

typedef struct _GstAudiorate GstAudiorate;
typedef struct _GstAudiorateClass GstAudiorateClass;

struct _GstAudiorate
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint bytes_per_sample;

  /* audio state */
  guint64 next_offset;

  guint64 in, out, add, drop;
  gboolean silent;
};

struct _GstAudiorateClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails audiorate_details =
GST_ELEMENT_DETAILS ("Audio rate adjuster",
    "Filter/Effect/Audio",
    "Drops/duplicates/adjusts timestamps on audio samples to make a perfect stream",
    "Wim Taymans <wim@fluendo.com>");

/* GstAudiorate signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_SILENT	TRUE

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

static GstStaticPadTemplate gst_audiorate_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS)
    );

static GstStaticPadTemplate gst_audiorate_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS)
    );

static void gst_audiorate_base_init (gpointer g_class);
static void gst_audiorate_class_init (GstAudiorateClass * klass);
static void gst_audiorate_init (GstAudiorate * audiorate);
static void gst_audiorate_chain (GstPad * pad, GstData * _data);

static void gst_audiorate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audiorate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstElementStateReturn gst_audiorate_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/*static guint gst_audiorate_signals[LAST_SIGNAL] = { 0 }; */

static GType
gst_audiorate_get_type (void)
{
  static GType audiorate_type = 0;

  if (!audiorate_type) {
    static const GTypeInfo audiorate_info = {
      sizeof (GstAudiorateClass),
      gst_audiorate_base_init,
      NULL,
      (GClassInitFunc) gst_audiorate_class_init,
      NULL,
      NULL,
      sizeof (GstAudiorate),
      0,
      (GInstanceInitFunc) gst_audiorate_init,
    };

    audiorate_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstAudiorate", &audiorate_info, 0);
  }

  return audiorate_type;
}

static void
gst_audiorate_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &audiorate_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_audiorate_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_audiorate_src_template));
}
static void
gst_audiorate_class_init (GstAudiorateClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

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


  object_class->set_property = gst_audiorate_set_property;
  object_class->get_property = gst_audiorate_get_property;

  element_class->change_state = gst_audiorate_change_state;
}

static GstPadLinkReturn
gst_audiorate_link (GstPad * pad, const GstCaps * caps)
{
  GstAudiorate *audiorate;
  GstStructure *structure;
  GstPad *otherpad;
  GstPadLinkReturn res;
  gint ret, channels, depth;

  audiorate = GST_AUDIORATE (gst_pad_get_parent (pad));

  otherpad = (pad == audiorate->srcpad) ? audiorate->sinkpad :
      audiorate->srcpad;

  res = gst_pad_try_set_caps (otherpad, caps);
  if (GST_PAD_LINK_FAILED (res))
    return res;

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "channels", &channels);
  ret &= gst_structure_get_int (structure, "depth", &depth);

  audiorate->bytes_per_sample = channels * (depth / 8);
  if (audiorate->bytes_per_sample == 0)
    audiorate->bytes_per_sample = 1;

  return GST_PAD_LINK_OK;
}

static void
gst_audiorate_init (GstAudiorate * audiorate)
{
  GST_FLAG_SET (audiorate, GST_ELEMENT_EVENT_AWARE);

  GST_DEBUG ("gst_audiorate_init");
  audiorate->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_audiorate_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (audiorate), audiorate->sinkpad);
  gst_pad_set_chain_function (audiorate->sinkpad, gst_audiorate_chain);
  gst_pad_set_link_function (audiorate->sinkpad, gst_audiorate_link);
  gst_pad_set_getcaps_function (audiorate->sinkpad, gst_pad_proxy_getcaps);

  audiorate->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_audiorate_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (audiorate), audiorate->srcpad);
  gst_pad_set_link_function (audiorate->srcpad, gst_audiorate_link);
  gst_pad_set_getcaps_function (audiorate->srcpad, gst_pad_proxy_getcaps);

  audiorate->bytes_per_sample = 1;
  audiorate->in = 0;
  audiorate->out = 0;
  audiorate->drop = 0;
  audiorate->add = 0;
  audiorate->silent = DEFAULT_SILENT;
}

static void
gst_audiorate_chain (GstPad * pad, GstData * data)
{
  GstAudiorate *audiorate;
  GstBuffer *buf;
  GstClockTime in_time, in_duration;
  guint64 in_offset, in_offset_end;
  gint in_size;

  audiorate = GST_AUDIORATE (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    gst_pad_event_default (pad, event);
    return;
  }

  audiorate->in++;

  buf = GST_BUFFER (data);
  in_time = GST_BUFFER_TIMESTAMP (buf);
  in_duration = GST_BUFFER_DURATION (buf);
  in_size = GST_BUFFER_SIZE (buf);
  in_offset = GST_BUFFER_OFFSET (buf);
  in_offset_end = GST_BUFFER_OFFSET_END (buf);

  if (in_offset == GST_CLOCK_TIME_NONE || in_offset_end == GST_CLOCK_TIME_NONE) {
    g_warning ("audiorate got buffer without offsets");
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

    gst_pad_push (audiorate->srcpad, GST_DATA (fill));
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

      return;
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
  gst_pad_push (audiorate->srcpad, GST_DATA (buf));
  audiorate->out++;

  audiorate->next_offset = in_offset_end;
}

static void
gst_audiorate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstAudiorate *audiorate = GST_AUDIORATE (object);

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
gst_audiorate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstAudiorate *audiorate = GST_AUDIORATE (object);

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

static GstElementStateReturn
gst_audiorate_change_state (GstElement * element)
{
  GstAudiorate *audiorate = GST_AUDIORATE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      audiorate->next_offset = 0;
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
  return gst_element_register (plugin, "audiorate", GST_RANK_NONE,
      GST_TYPE_AUDIORATE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "audiorate",
    "Adjusts audio frames",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
