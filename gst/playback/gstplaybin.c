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

typedef struct _GstPlayBin GstPlayBin;
typedef struct _GstPlayBinClass GstPlayBinClass;

struct _GstPlayBin
{
  GstPlayBaseBin parent;

  GstElement *audio_sink;
  GstElement *video_sink;
  GstElement *visualisation;

  GList *seekables;
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
  play_bin->seekables = NULL;

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


static GstElement *
gen_video_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *conv;
  GstElement *sink;

  element = gst_bin_new ("vbin");
  conv = gst_element_factory_make ("ffmpegcolorspace", "conv");
  if (play_bin->video_sink) {
    sink = play_bin->video_sink;
  } else {
    sink = gst_element_factory_make ("ximagesink", "sink");
  }

  play_bin->seekables = g_list_prepend (play_bin->seekables, sink);

  gst_bin_add (GST_BIN (element), conv);
  gst_bin_add (GST_BIN (element), sink);
  gst_element_link_pads (conv, "src", sink, "sink");

  gst_element_add_ghost_pad (element, gst_element_get_pad (conv, "sink"),
      "sink");

  return element;
}

static GstElement *
gen_audio_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *conv;
  GstElement *sink;

  element = gst_bin_new ("abin");
  conv = gst_element_factory_make ("audioconvert", "conv");
  if (play_bin->audio_sink) {
    sink = play_bin->audio_sink;
  } else {
    sink = gst_element_factory_make ("osssink", "sink");
  }

  play_bin->seekables = g_list_prepend (play_bin->seekables, sink);

  gst_bin_add (GST_BIN (element), conv);
  gst_bin_add (GST_BIN (element), sink);
  gst_element_link_pads (conv, "src", sink, "sink");

  gst_element_add_ghost_pad (element,
      gst_element_get_pad (conv, "sink"), "sink");

  return element;
}

static void
setup_sinks (GstPlayBin * play_bin)
{
  GList *streaminfo;
  GList *s;

  /* get info about the stream */
  g_object_get (G_OBJECT (play_bin), "stream-info", &streaminfo, NULL);

  for (s = streaminfo; s; s = g_list_next (s)) {
    GObject *obj = G_OBJECT (s->data);
    int type;
    GstPad *srcpad, *sinkpad;
    GstElement *sink;
    gboolean res;

    g_object_get (obj, "type", &type, NULL);
    g_object_get (obj, "pad", &srcpad, NULL);

    if (type == 1) {
      sink = gen_audio_element (play_bin);
    } else if (type == 2) {
      sink = gen_video_element (play_bin);
    } else {
      g_warning ("unknown stream found");
      continue;
    }

    gst_bin_add (GST_BIN (play_bin), sink);
    sinkpad = gst_element_get_pad (sink, "sink");
    res = gst_pad_link (srcpad, sinkpad);
    if (!res) {
      gchar *capsstr;

      capsstr = gst_caps_to_string (gst_pad_get_caps (srcpad));
      g_warning ("could not link %s", capsstr);
      g_free (capsstr);
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

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      setup_sinks (play_bin);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_PLAYING_TO_PAUSED:
    case GST_STATE_PAUSED_TO_READY:
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

  play_bin = GST_PLAY_BIN (element);

  s = play_bin->seekables;

  for (s = play_bin->seekables; s; s = g_list_next (s)) {
    GstElement *element = GST_ELEMENT (s->data);
    gboolean ok;

    ok = gst_element_send_event (element, event);
    res |= ok;
  }
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
