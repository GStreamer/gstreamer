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

GST_DEBUG_CATEGORY_STATIC (gst_play_bin_debug);
#define GST_CAT_DEFAULT gst_play_bin_debug

#define GST_TYPE_PLAY_BIN 		(gst_play_bin_get_type())
#define GST_PLAY_BIN(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLAY_BIN,GstPlayBin))
#define GST_PLAY_BIN_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PLAY_BIN,GstPlayBinClass))
#define GST_IS_PLAY_BIN(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLAY_BIN))
#define GST_IS_PLAY_BIN_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PLAY_BIN))

typedef struct _GstPlayBin GstPlayBin;
typedef struct _GstPlayBinClass GstPlayBinClass;

struct _GstPlayBin
{
  GstBin bin;

  GstElement *thread;

  gboolean threaded;

  /* properties */
  gchar *uri;
  GstElement *source;

  GList *factories;
  GstElement *audio_sink;
  GstElement *video_sink;
  GstElement *vis_plugin;

  gboolean audio_added;
  gboolean video_added;

  GList *outpads;
};

struct _GstPlayBinClass
{
  GstBinClass parent_class;
};

/* props */
enum
{
  ARG_0,
  ARG_URI,
  ARG_THREADED,
  ARG_AUDIO_SINK,
  ARG_VIDEO_SINK,
  ARG_VIS_PLUGIN,
};

/* signals */
enum
{
  LAST_SIGNAL
};

static void gst_play_bin_class_init (GstPlayBinClass * klass);
static void gst_play_bin_init (GstPlayBin * play_bin);
static void gst_play_bin_dispose (GObject * object);

static void gst_play_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_play_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);
static GstElementStateReturn gst_play_bin_change_state (GstElement * element);

static const GstEventMask *gst_play_bin_get_event_masks (GstElement * element);
static gboolean gst_play_bin_send_event (GstElement * element,
    GstEvent * event);
static const GstFormat *gst_play_bin_get_formats (GstElement * element);
static gboolean gst_play_bin_convert (GstElement * element,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);
static const GstQueryType *gst_play_bin_get_query_types (GstElement * element);
static gboolean gst_play_bin_query (GstElement * element, GstQueryType type,
    GstFormat * format, gint64 * value);
static GstClock *gst_play_bin_get_clock (GstElement * element);
static void gst_play_bin_add_element (GstBin * bin, GstElement * element);
static void gst_play_bin_remove_element (GstBin * bin, GstElement * element);


static GstElementClass *parent_class;

//static guint gst_play_bin_signals[LAST_SIGNAL] = { 0 };

static GstElementDetails gst_play_bin_details = {
  "Player Bin",
  "Generic/Bin/Player",
  "Autoplug and play media from an uri",
  "Wim Taymans <wim.taymans@chello.be>"
};


static GType
gst_play_bin_get_type (void)
{
  static GType gst_play_bin_type = 0;

  if (!gst_play_bin_type) {
    static const GTypeInfo gst_play_bin_info = {
      sizeof (GstPlayBinClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_play_bin_class_init,
      NULL,
      NULL,
      sizeof (GstPlayBin),
      0,
      (GInstanceInitFunc) gst_play_bin_init,
      NULL
    };

    gst_play_bin_type =
        g_type_register_static (GST_TYPE_BIN, "GstPlayBin", &gst_play_bin_info,
        0);
  }

  return gst_play_bin_type;
}

static void
gst_play_bin_class_init (GstPlayBinClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstBinClass *gstbin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;

  parent_class = g_type_class_ref (gst_bin_get_type ());

  gobject_klass->set_property = gst_play_bin_set_property;
  gobject_klass->get_property = gst_play_bin_get_property;

  g_object_class_install_property (gobject_klass, ARG_URI,
      g_param_spec_string ("uri", "URI", "URI of the media to play",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_THREADED,
      g_param_spec_boolean ("threaded", "Threaded", "Use threads",
          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_VIDEO_SINK,
      g_param_spec_object ("video-sink", "Video Sink",
          "the video output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_AUDIO_SINK,
      g_param_spec_object ("audio-sink", "Audio Sink",
          "the audio output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_VIS_PLUGIN,
      g_param_spec_object ("vis-plugin", "Vis plugin",
          "the visualization element to use (NULL = none)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_play_bin_dispose);

  gst_element_class_set_details (gstelement_klass, &gst_play_bin_details);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_play_bin_change_state);
  gstelement_klass->get_event_masks =
      GST_DEBUG_FUNCPTR (gst_play_bin_get_event_masks);
  gstelement_klass->send_event = GST_DEBUG_FUNCPTR (gst_play_bin_send_event);
  gstelement_klass->get_formats = GST_DEBUG_FUNCPTR (gst_play_bin_get_formats);
  gstelement_klass->convert = GST_DEBUG_FUNCPTR (gst_play_bin_convert);
  gstelement_klass->get_query_types =
      GST_DEBUG_FUNCPTR (gst_play_bin_get_query_types);
  gstelement_klass->query = GST_DEBUG_FUNCPTR (gst_play_bin_query);
  gstelement_klass->get_clock = GST_DEBUG_FUNCPTR (gst_play_bin_get_clock);

  gstbin_klass->add_element = GST_DEBUG_FUNCPTR (gst_play_bin_add_element);
  gstbin_klass->remove_element =
      GST_DEBUG_FUNCPTR (gst_play_bin_remove_element);
}

static void
gst_play_bin_init (GstPlayBin * play_bin)
{
  play_bin->uri = NULL;
  play_bin->video_sink = NULL;
  play_bin->audio_sink = NULL;
  play_bin->vis_plugin = NULL;
  play_bin->audio_added = FALSE;
  play_bin->video_added = FALSE;
  play_bin->threaded = TRUE;

  GST_FLAG_SET (play_bin, GST_BIN_SELF_SCHEDULABLE);
}

static void
gst_play_bin_dispose (GObject * object)
{
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (object);
  g_free (play_bin->uri);

  if (G_OBJECT_CLASS (parent_class)->dispose) {
    G_OBJECT_CLASS (parent_class)->dispose (object);
  }
}

static void
rebuild_pipeline (GstPlayBin * play_bin)
{
  GstElementState oldstate;

  if (play_bin->thread == NULL)
    return;

  oldstate = gst_element_get_state (play_bin->thread);

  gst_element_set_state (play_bin->thread, GST_STATE_NULL);
  /* remove old elements */

  /* set to old state again */
  gst_element_set_state (play_bin->thread, oldstate);
}

static GstElement *
get_audio_element (GstPlayBin * play_bin)
{
  GstElement *result;
  GstElement *queue;
  GstElement *conv;
  GstElement *sink;
  GstPad *sinkpad;

  conv = gst_element_factory_make ("audioconvert", "a_conv");

  if (play_bin->threaded) {
    result = gst_thread_new ("audio_thread");
    queue = gst_element_factory_make ("queue", "a_queue");
    gst_bin_add (GST_BIN (result), queue);
    gst_bin_add (GST_BIN (result), conv);
    gst_element_link_pads (queue, "src", conv, "sink");
    sinkpad = gst_element_get_pad (queue, "sink");
  } else {
    result = gst_bin_new ("audio_bin");
    gst_bin_add (GST_BIN (result), conv);
    sinkpad = gst_element_get_pad (conv, "sink");
  }

  sink = gst_element_factory_make ("osssink", "a_sink");
  gst_bin_add (GST_BIN (result), sink);

  gst_element_link_pads (conv, "src", sink, "sink");

  gst_element_add_ghost_pad (result, sinkpad, "sink");

  return result;
}

static GstElement *
get_video_element (GstPlayBin * play_bin)
{
  GstElement *result;
  GstElement *queue;
  GstElement *conv;
  GstElement *sink;
  GstPad *sinkpad;

  conv = gst_element_factory_make ("ffmpegcolorspace", "v_conv");

  if (play_bin->threaded) {
    result = gst_thread_new ("video_thread");
    queue = gst_element_factory_make ("queue", "v_queue");
    gst_bin_add (GST_BIN (result), queue);
    gst_bin_add (GST_BIN (result), conv);
    gst_element_link_pads (queue, "src", conv, "sink");
    sinkpad = gst_element_get_pad (queue, "sink");
  } else {
    result = gst_bin_new ("video_bin");
    gst_bin_add (GST_BIN (result), conv);
    sinkpad = gst_element_get_pad (conv, "sink");
  }

  sink = gst_element_factory_make ("ximagesink", "v_sink");
  gst_bin_add (GST_BIN (result), sink);

  gst_element_link_pads (conv, "src", sink, "sink");

  gst_element_add_ghost_pad (result, sinkpad, "sink");

  return result;
}

static void
new_pad (GstElement * element, GstPad * pad, GstPlayBin * play_bin)
{
  GstStructure *structure;
  const gchar *mimetype;
  GstCaps *caps;
  GstClock *clock;
  GstElement *new_element = NULL;

  caps = gst_pad_get_caps (pad);

  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  if (g_str_has_prefix (mimetype, "audio/")) {
    if (!play_bin->audio_added) {
      new_element = get_audio_element (play_bin);
      play_bin->audio_added = TRUE;
    }
  } else if (g_str_has_prefix (mimetype, "video/")) {
    if (!play_bin->video_added) {
      new_element = get_video_element (play_bin);
      play_bin->video_added = TRUE;
    }
  } else {
  }

  if (new_element) {
    GstScheduler *sched;

    gst_bin_add (GST_BIN (play_bin->thread), new_element);

    gst_pad_link (pad, gst_element_get_pad (new_element, "sink"));

    sched = gst_element_get_scheduler (GST_ELEMENT (play_bin->thread));

    gst_bin_auto_clock (GST_BIN (play_bin->thread));
    clock = gst_bin_get_clock (GST_BIN (play_bin->thread));
    gst_scheduler_set_clock (sched, clock);
    gst_element_sync_state_with_parent (new_element);
  }
}

static gboolean
setup_source (GstPlayBin * play_bin)
{
  if (play_bin->source) {
    gst_bin_remove (GST_BIN (play_bin->thread), play_bin->source);
    gst_object_unref (GST_OBJECT (play_bin->source));
  }

  play_bin->source =
      gst_element_make_from_uri (GST_URI_SRC, play_bin->uri, "source");
  if (!play_bin->source) {
    g_warning ("don't know how to read %s", play_bin->uri);
    return FALSE;
  }

  {
    GstElement *decoder;
    gboolean res;

    gst_bin_add (GST_BIN (play_bin->thread), play_bin->source);

    decoder = gst_element_factory_make ("decodebin", "decoder");
    if (!decoder) {
      g_warning ("can't find decoder element");
      return FALSE;
    }

    gst_bin_add (GST_BIN (play_bin->thread), decoder);
    res = gst_element_link_pads (play_bin->source, "src", decoder, "sink");
    if (!res) {
      g_warning ("can't link source to typefind element");
      return FALSE;
    }
    g_signal_connect (G_OBJECT (decoder), "new_pad",
        G_CALLBACK (new_pad), play_bin);
  }

  return TRUE;
}

static void
gst_play_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPlayBin *play_bin;

  g_return_if_fail (GST_IS_PLAY_BIN (object));

  play_bin = GST_PLAY_BIN (object);

  switch (prop_id) {
    case ARG_URI:
    {
      const gchar *uri = g_value_get_string (value);

      if (uri == NULL) {
        g_warning ("cannot set NULL uri");
        return;
      }
      if (!play_bin->uri || !strcmp (play_bin->uri, uri)) {
        g_free (play_bin->uri);
        play_bin->uri = g_strdup (uri);

        rebuild_pipeline (play_bin);
      }
      break;
    }
    case ARG_THREADED:
      play_bin->threaded = g_value_get_boolean (value);
      break;
    case ARG_VIDEO_SINK:
      break;
    case ARG_AUDIO_SINK:
      break;
    case ARG_VIS_PLUGIN:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_play_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPlayBin *play_bin;

  g_return_if_fail (GST_IS_PLAY_BIN (object));

  play_bin = GST_PLAY_BIN (object);

  switch (prop_id) {
    case ARG_URI:
      g_value_set_string (value, play_bin->uri);
      break;
    case ARG_THREADED:
      g_value_set_boolean (value, play_bin->threaded);
      break;
    case ARG_VIDEO_SINK:
      break;
    case ARG_AUDIO_SINK:
      break;
    case ARG_VIS_PLUGIN:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_play_bin_change_state (GstElement * element)
{
  GstElementStateReturn ret = GST_STATE_SUCCESS;
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
    {
      GstScheduler *sched;

      play_bin->thread = gst_thread_new ("internal_thread");
      sched = gst_scheduler_factory_make ("opt", play_bin->thread);
      if (sched) {
        gst_element_set_scheduler (play_bin->thread, sched);

        gst_object_set_parent (GST_OBJECT (play_bin->thread),
            GST_OBJECT (play_bin));

        gst_element_set_state (play_bin->thread, GST_STATE_READY);
      } else {
        g_warning ("could not get 'opt' scheduler");
        gst_object_unref (GST_OBJECT (play_bin->thread));
        play_bin->thread = NULL;

        ret = GST_STATE_FAILURE;
      }
      break;
    }
    case GST_STATE_READY_TO_PAUSED:
    {
      if (!setup_source (play_bin)) {
        GST_ELEMENT_ERROR (GST_ELEMENT (play_bin), LIBRARY, TOO_LAZY, (NULL),
            ("cannot handle uri \"%s\"", play_bin->uri));
        ret = GST_STATE_FAILURE;
      } else {
        ret = gst_element_set_state (play_bin->thread, GST_STATE_PAUSED);
      }
      break;
    }
    case GST_STATE_PAUSED_TO_PLAYING:
      ret = gst_element_set_state (play_bin->thread, GST_STATE_PLAYING);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      ret = gst_element_set_state (play_bin->thread, GST_STATE_PAUSED);
      break;
    case GST_STATE_PAUSED_TO_READY:
      ret = gst_element_set_state (play_bin->thread, GST_STATE_READY);
      break;
    case GST_STATE_READY_TO_NULL:
      ret = gst_element_set_state (play_bin->thread, GST_STATE_NULL);

      gst_object_unref (GST_OBJECT (play_bin->thread));
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (ret == GST_STATE_SUCCESS) {
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  }

  return ret;
}

static void
gst_play_bin_add_element (GstBin * bin, GstElement * element)
{
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (bin);
}

static void
gst_play_bin_remove_element (GstBin * bin, GstElement * element)
{
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (bin);
}

static const GstEventMask *
gst_play_bin_get_event_masks (GstElement * element)
{
  return NULL;
}

static gboolean
gst_play_bin_send_event (GstElement * element, GstEvent * event)
{
  gboolean res = FALSE;

  return res;
}

static const GstFormat *
gst_play_bin_get_formats (GstElement * element)
{
  static GstFormat formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,
    0,
  };

  return formats;
}

static gboolean
gst_play_bin_convert (GstElement * element,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  return FALSE;
}

static const GstQueryType *
gst_play_bin_get_query_types (GstElement * element)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    GST_QUERY_START,
    GST_QUERY_SEGMENT_END,
    0
  };

  return query_types;
}

static gboolean
gst_play_bin_query (GstElement * element, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  gboolean res = FALSE;

  return res;
}

static GstClock *
gst_play_bin_get_clock (GstElement * element)
{
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (element);

  return gst_bin_get_clock (GST_BIN (play_bin->thread));
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_play_bin_debug, "playbin", 0, "play bin");

  return gst_element_register (plugin, "playbin", GST_RANK_NONE,
      GST_TYPE_PLAY_BIN);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "playbin",
    "player bin", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
