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

#include "gstplaybasebin.h"

GST_DEBUG_CATEGORY_STATIC (gst_play_bin_debug);
#define GST_CAT_DEFAULT gst_play_bin_debug

#define GST_TYPE_PLAY_BIN 		(gst_play_bin_get_type())
#define GST_PLAY_BIN(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLAY_BIN,GstPlayBin))
#define GST_PLAY_BIN_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PLAY_BIN,GstPlayBinClass))
#define GST_IS_PLAY_BIN(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLAY_BIN))
#define GST_IS_PLAY_BIN_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PLAY_BIN))

#define VOLUME_MAX_DOUBLE 4.0

typedef struct _GstPlayBin GstPlayBin;
typedef struct _GstPlayBinClass GstPlayBinClass;

struct _GstPlayBin
{
  GstPlayBaseBin parent;

  GstElement *audio_sink;
  GstElement *video_sink;
  GstElement *visualisation;
  GstElement *volume_element;
  gfloat volume;

  GList *sinks;

  GList *seekables;

  GstBuffer *frame;

  GHashTable *cache;
};

struct _GstPlayBinClass
{
  GstPlayBaseBinClass parent_class;
};

/* props */
enum
{
  ARG_0,
  ARG_AUDIO_SINK,
  ARG_VIDEO_SINK,
  ARG_VIS_PLUGIN,
  ARG_VOLUME,
  ARG_FRAME
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

    gst_play_bin_type = g_type_register_static (GST_TYPE_PLAY_BASE_BIN,
        "GstPlayBin", &gst_play_bin_info, 0);
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

  parent_class = g_type_class_ref (gst_play_base_bin_get_type ());

  gobject_klass->set_property = gst_play_bin_set_property;
  gobject_klass->get_property = gst_play_bin_get_property;

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
  g_object_class_install_property (G_OBJECT_CLASS (gobject_klass), ARG_VOLUME,
      g_param_spec_double ("volume", "volume", "volume",
          0.0, VOLUME_MAX_DOUBLE, 1.0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (gobject_klass), ARG_FRAME,
      g_param_spec_boxed ("frame", "Frame",
          "The last frame (NULL = no video available)",
          GST_TYPE_BUFFER, G_PARAM_READABLE));

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
}

static void
gst_play_bin_init (GstPlayBin * play_bin)
{
  play_bin->video_sink = NULL;
  play_bin->audio_sink = NULL;
  play_bin->visualisation = NULL;
  play_bin->volume_element = NULL;
  play_bin->volume = 1.0;
  play_bin->seekables = NULL;
  play_bin->sinks = NULL;
  play_bin->frame = NULL;
  play_bin->cache = g_hash_table_new (g_str_hash, g_str_equal);

  GST_FLAG_SET (play_bin, GST_BIN_SELF_SCHEDULABLE);
}

static void
gst_play_bin_dispose (GObject * object)
{
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (object);

  if (G_OBJECT_CLASS (parent_class)->dispose) {
    G_OBJECT_CLASS (parent_class)->dispose (object);
  }
}

static void
gst_play_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPlayBin *play_bin;

  g_return_if_fail (GST_IS_PLAY_BIN (object));

  play_bin = GST_PLAY_BIN (object);

  switch (prop_id) {
    case ARG_VIDEO_SINK:
    {
      GstElement *element;

      play_bin->video_sink = g_value_get_object (value);

      element = g_hash_table_lookup (play_bin->cache, "vbin");
      if (element != NULL) {
        g_hash_table_remove (play_bin->cache, "vbin");
        g_object_unref (G_OBJECT (element));
      }
      break;
    }
    case ARG_AUDIO_SINK:
    {
      GstElement *element;

      play_bin->audio_sink = g_value_get_object (value);

      element = g_hash_table_lookup (play_bin->cache, "abin");
      if (element != NULL) {
        g_hash_table_remove (play_bin->cache, "abin");
        g_object_unref (G_OBJECT (element));
      }
      break;
    }
    case ARG_VIS_PLUGIN:
      play_bin->visualisation = g_value_get_object (value);
      break;
    case ARG_VOLUME:
      play_bin->volume = g_value_get_double (value);
      if (play_bin->volume_element) {
        g_object_set (G_OBJECT (play_bin->volume_element), "volume",
            play_bin->volume, NULL);
      }
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
    case ARG_VIDEO_SINK:
      g_value_set_object (value, play_bin->video_sink);
      break;
    case ARG_AUDIO_SINK:
      g_value_set_object (value, play_bin->audio_sink);
      break;
    case ARG_VIS_PLUGIN:
      g_value_set_object (value, play_bin->visualisation);
      break;
    case ARG_VOLUME:
      g_value_set_double (value, play_bin->volume);
      break;
    case ARG_FRAME:
      g_value_set_boxed (value, play_bin->frame);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
handoff (GstElement * identity, GstBuffer * frame, gpointer data)
{
  GstPlayBin *play_bin = GST_PLAY_BIN (data);

  if (play_bin->frame) {
    gst_buffer_unref (play_bin->frame);
  }
  play_bin->frame = gst_buffer_ref (frame);
}

static GstElement *
gen_video_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *conv;
  GstElement *scale;
  GstElement *sink;
  GstElement *identity;

  element = g_hash_table_lookup (play_bin->cache, "vbin");
  if (element != NULL) {
    g_object_ref (G_OBJECT (element));
    sink = g_hash_table_lookup (play_bin->cache, "video_sink");
    goto done;
  }

  element = gst_bin_new ("vbin");
  identity = gst_element_factory_make ("identity", "id");
  g_signal_connect (identity, "handoff", G_CALLBACK (handoff), play_bin);
  conv = gst_element_factory_make ("ffmpegcolorspace", "vconv");
  scale = gst_element_factory_make ("videoscale", "vscale");
  if (play_bin->video_sink) {
    gst_object_ref (GST_OBJECT (play_bin->video_sink));
    sink = play_bin->video_sink;
  } else {
    sink = gst_element_factory_make ("xvimagesink", "sink");
  }
  g_hash_table_insert (play_bin->cache, "video_sink", sink);

  gst_bin_add (GST_BIN (element), identity);
  gst_bin_add (GST_BIN (element), conv);
  gst_bin_add (GST_BIN (element), scale);
  gst_bin_add (GST_BIN (element), sink);
  gst_element_link_pads (identity, "src", conv, "sink");
  gst_element_link_pads (conv, "src", scale, "sink");
  gst_element_link_pads (scale, "src", sink, "sink");

  gst_element_add_ghost_pad (element, gst_element_get_pad (identity, "sink"),
      "sink");

  gst_element_set_state (element, GST_STATE_READY);

  /* ref before adding to the cache */
  g_object_ref (G_OBJECT (element));
  g_hash_table_insert (play_bin->cache, "vbin", element);

done:
  play_bin->seekables = g_list_append (play_bin->seekables, sink);

  return element;
}

static GstElement *
gen_audio_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *conv;
  GstElement *sink;
  GstElement *volume;
  GstElement *scale;

  element = g_hash_table_lookup (play_bin->cache, "abin");
  if (element != NULL) {
    g_object_ref (G_OBJECT (element));
    sink = g_hash_table_lookup (play_bin->cache, "audio_sink");
    goto done;
  }
  element = gst_bin_new ("abin");
  conv = gst_element_factory_make ("audioconvert", "aconv");
  scale = gst_element_factory_make ("audioscale", "ascale");

  volume = gst_element_factory_make ("volume", "volume");
  g_object_set (G_OBJECT (volume), "volume", play_bin->volume, NULL);
  play_bin->volume_element = volume;

  if (play_bin->audio_sink) {
    gst_object_ref (GST_OBJECT (play_bin->audio_sink));
    sink = play_bin->audio_sink;
  } else {
    sink = gst_element_factory_make ("osssink", "sink");
  }
  g_hash_table_insert (play_bin->cache, "audio_sink", sink);

  gst_bin_add (GST_BIN (element), conv);
  gst_bin_add (GST_BIN (element), scale);
  gst_bin_add (GST_BIN (element), volume);
  gst_bin_add (GST_BIN (element), sink);

  gst_element_link_pads (conv, "src", scale, "sink");
  gst_element_link_pads (scale, "src", volume, "sink");
  gst_element_link_pads (volume, "src", sink, "sink");

  gst_element_add_ghost_pad (element,
      gst_element_get_pad (conv, "sink"), "sink");

  gst_element_set_state (element, GST_STATE_READY);

  /* ref before adding to the cache */
  g_object_ref (G_OBJECT (element));
  g_hash_table_insert (play_bin->cache, "abin", element);

done:
  play_bin->seekables = g_list_prepend (play_bin->seekables, sink);

  return element;
}

static GstElement *
gen_vis_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *tee;
  GstElement *asink;
  GstElement *vsink;
  GstElement *conv;
  GstElement *vis;
  GstElement *vqueue;
  GstElement *vthread;

  element = gst_bin_new ("visbin");
  tee = gst_element_factory_make ("tee", "tee");

  vqueue = gst_element_factory_make ("queue", "vqueue");
  vthread = gst_element_factory_make ("thread", "vthread");

  asink = gen_audio_element (play_bin);
  vsink = gen_video_element (play_bin);

  gst_bin_add (GST_BIN (element), asink);
  gst_bin_add (GST_BIN (element), vqueue);
  gst_bin_add (GST_BIN (vthread), vsink);
  gst_bin_add (GST_BIN (element), vthread);
  gst_bin_add (GST_BIN (element), tee);

  conv = gst_element_factory_make ("audioconvert", "aconv");
  if (play_bin->visualisation) {
    gst_object_ref (GST_OBJECT (play_bin->visualisation));
    vis = play_bin->visualisation;
  } else {
    vis = gst_element_factory_make ("goom", "vis");
  }

  gst_bin_add (GST_BIN (element), conv);
  gst_bin_add (GST_BIN (element), vis);

  gst_element_link_pads (conv, "src", vis, "sink");
  gst_element_link_pads (vis, "src", vqueue, "sink");

  gst_element_link_pads (vqueue, "src", vsink, "sink");

  gst_pad_link (gst_element_get_request_pad (tee, "src%d"),
      gst_element_get_pad (asink, "sink"));

  gst_pad_link (gst_element_get_request_pad (tee, "src%d"),
      gst_element_get_pad (conv, "sink"));

  gst_element_add_ghost_pad (element,
      gst_element_get_pad (tee, "sink"), "sink");

  //gst_element_set_state (element, GST_STATE_READY);

  return element;
}

static void
remove_sinks (GstPlayBin * play_bin)
{
  GList *sinks;
  GstObject *parent;
  GstElement *element;

  element = g_hash_table_lookup (play_bin->cache, "abin");
  if (element != NULL) {
    parent = gst_element_get_parent (element);
    if (parent != NULL) {
      gst_bin_remove (GST_BIN (parent), element);
    }
  }
  element = g_hash_table_lookup (play_bin->cache, "vbin");
  if (element != NULL) {
    parent = gst_element_get_parent (element);
    if (parent != NULL) {
      gst_bin_remove (GST_BIN (parent), element);
    }
  }

  for (sinks = play_bin->sinks; sinks; sinks = g_list_next (sinks)) {
    GstElement *element = GST_ELEMENT (sinks->data);

    GST_LOG ("removing sink %p", element);
    gst_bin_remove (GST_BIN (play_bin), element);
  }
  g_list_free (play_bin->sinks);
  g_list_free (play_bin->seekables);
  play_bin->sinks = NULL;
  play_bin->seekables = NULL;

  if (play_bin->frame) {
    gst_buffer_unref (play_bin->frame);
    play_bin->frame = NULL;
  }
}

static void
setup_sinks (GstPlayBin * play_bin)
{
  GList *streaminfo;
  GList *s;
  gint num_audio = 0;
  gint num_video = 0;
  gboolean need_vis = FALSE;

  /* get info about the stream */
  g_object_get (G_OBJECT (play_bin), "stream-info", &streaminfo, NULL);

  /* first examine the streams we have */
  for (s = streaminfo; s; s = g_list_next (s)) {
    GObject *obj = G_OBJECT (s->data);
    gint type;
    GstPad *srcpad;

    g_object_get (obj, "type", &type, NULL);
    g_object_get (obj, "pad", &srcpad, NULL);

    if (gst_pad_is_linked (srcpad))
      continue;

    if (type == 1) {
      num_audio++;
    } else if (type == 2) {
      num_video++;
    }
  }
  /* no video, use vis */
  if (num_video == 0 && num_audio > 0 && play_bin->visualisation) {
    need_vis = TRUE;
  }

  num_audio = 0;
  num_video = 0;

  for (s = streaminfo; s; s = g_list_next (s)) {
    GObject *obj = G_OBJECT (s->data);
    gint type;
    GstPad *srcpad, *sinkpad;
    GstElement *sink = NULL;
    gboolean res;
    gboolean mute = FALSE;

    g_object_get (obj, "type", &type, NULL);
    g_object_get (obj, "pad", &srcpad, NULL);

    if (gst_pad_is_linked (srcpad))
      continue;

    if (type == 1) {
      if (num_audio > 0) {
        g_warning ("two audio streams found, playing first one");
        mute = TRUE;
      } else {
        if (need_vis) {
          sink = gen_vis_element (play_bin);
        } else {
          sink = gen_audio_element (play_bin);
        }
        num_audio++;
      }
    } else if (type == 2) {
      if (num_video > 0) {
        g_warning ("two video streams found, playing first one");
        mute = TRUE;
      } else {
        sink = gen_video_element (play_bin);
        num_video++;
      }
    } else {
      g_warning ("unknown stream found");
      mute = TRUE;
    }

    if (sink != NULL) {
      gst_bin_add (GST_BIN (play_bin), sink);
      sinkpad = gst_element_get_pad (sink, "sink");
      res = gst_pad_link (srcpad, sinkpad);
      if (!res) {
        gchar *capsstr;

        capsstr = gst_caps_to_string (gst_pad_get_caps (srcpad));
        g_warning ("could not link %s", capsstr);
        g_free (capsstr);
        GST_LOG ("removing sink %p", sink);
        gst_bin_remove (GST_BIN (play_bin), sink);
        mute = TRUE;
      } else {
        play_bin->sinks = g_list_prepend (play_bin->sinks, sink);
      }
    }
    if (mute) {
      g_object_set (G_OBJECT (obj), "mute", TRUE, NULL);
    }
  }
}

static GstElementStateReturn
gst_play_bin_change_state (GstElement * element)
{
  GstElementStateReturn ret;
  GstPlayBin *play_bin;
  int transition;

  play_bin = GST_PLAY_BIN (element);

  transition = GST_STATE_TRANSITION (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  if (ret == GST_STATE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      setup_sinks (play_bin);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      remove_sinks (play_bin);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
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
  GList *s;
  GstPlayBin *play_bin;
  GstElementState state;
  gboolean need_pause = FALSE;

  play_bin = GST_PLAY_BIN (element);

  state = gst_element_get_state (element);
  if (state == GST_STATE_PLAYING) {
    need_pause = TRUE;
    gst_element_set_state (element, GST_STATE_PAUSED);
  }

  s = play_bin->seekables;

  for (s = play_bin->seekables; s; s = g_list_next (s)) {
    GstElement *element = GST_ELEMENT (s->data);
    gboolean ok;

    gst_event_ref (event);
    ok = gst_element_send_event (element, event);
    res |= ok;
  }
  gst_event_unref (event);

  if (need_pause)
    gst_element_set_state (element, GST_STATE_PLAYING);

  return res;
}

static const GstFormat *
gst_play_bin_get_formats (GstElement * element)
{
  static GstFormat formats[] = {
    GST_FORMAT_TIME,
    0,
  };

  return formats;
}

static gboolean
gst_play_bin_convert (GstElement * element,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = FALSE;
  GList *s;
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (element);

  s = play_bin->seekables;

  for (s = play_bin->seekables; s; s = g_list_next (s)) {
    GstElement *element = GST_ELEMENT (s->data);

    res = gst_element_convert (element, src_format, src_value,
        dest_format, dest_value);
    if (res)
      break;
  }
  return res;
}

static const GstQueryType *
gst_play_bin_get_query_types (GstElement * element)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return query_types;
}

static gboolean
gst_play_bin_query (GstElement * element, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  gboolean res = FALSE;
  GList *s;
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (element);

  s = play_bin->seekables;

  for (s = play_bin->seekables; s; s = g_list_next (s)) {
    GstElement *element = GST_ELEMENT (s->data);

    res = gst_element_query (element, type, format, value);
    if (res)
      break;
  }
  return res;
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
