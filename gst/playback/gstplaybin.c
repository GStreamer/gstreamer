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

  /* the configurable elements */
  GstElement *fakesink;
  GstElement *audio_sink;
  GstElement *video_sink;
  GstElement *visualisation;
  GstElement *volume_element;
  GstElement *textoverlay_element;
  gfloat volume;

  /* these are the currently active sinks */
  GList *sinks;

  /* the last captured frame for snapshots */
  GstBuffer *frame;

  /* our cache for the sinks */
  GHashTable *cache;

  /* font description */
  gchar *font_desc;
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
  ARG_FRAME,
  ARG_FONT_DESC
};

/* signals */
enum
{
  LAST_SIGNAL
};

static void gst_play_bin_class_init (GstPlayBinClass * klass);
static void gst_play_bin_init (GstPlayBin * play_bin);
static void gst_play_bin_dispose (GObject * object);

static void setup_sinks (GstPlayBaseBin * play_base_bin,
    GstPlayBaseGroup * group);
static void remove_sinks (GstPlayBin * play_bin);

static void gst_play_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_play_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);
static GstElementStateReturn gst_play_bin_change_state (GstElement * element);

static GstElementClass *parent_class;

//static guint gst_play_bin_signals[LAST_SIGNAL] = { 0 };

static GstElementDetails gst_play_bin_details = {
  "Player Bin",
  "Generic/Bin/Player",
  "Autoplug and play media from an uri",
  "Wim Taymans <wim@fluendo.com>"
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
  GstPlayBaseBinClass *playbasebin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;
  playbasebin_klass = (GstPlayBaseBinClass *) klass;

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
  g_object_class_install_property (gobject_klass, ARG_VOLUME,
      g_param_spec_double ("volume", "volume", "volume",
          0.0, VOLUME_MAX_DOUBLE, 1.0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_FRAME,
      gst_param_spec_mini_object ("frame", "Frame",
          "The last frame (NULL = no video available)",
          GST_TYPE_BUFFER, G_PARAM_READABLE));
  g_object_class_install_property (gobject_klass, ARG_FONT_DESC,
      g_param_spec_string ("subtitle-font-desc",
          "Subtitle font description",
          "Pango font description of font "
          "to be used for subtitle rendering", NULL, G_PARAM_WRITABLE));

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_play_bin_dispose);

  gst_element_class_set_details (gstelement_klass, &gst_play_bin_details);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_play_bin_change_state);

  playbasebin_klass->setup_output_pads = setup_sinks;
}

static void
gst_play_bin_init (GstPlayBin * play_bin)
{
  play_bin->video_sink = NULL;
  play_bin->audio_sink = NULL;
  play_bin->visualisation = NULL;
  play_bin->volume_element = NULL;
  play_bin->textoverlay_element = NULL;
  play_bin->volume = 1.0;
  play_bin->sinks = NULL;
  play_bin->frame = NULL;
  play_bin->font_desc = NULL;
  play_bin->cache = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) gst_object_unref);
}

static void
gst_play_bin_dispose (GObject * object)
{
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (object);

  if (play_bin->cache != NULL) {
    remove_sinks (play_bin);
    g_hash_table_destroy (play_bin->cache);
    play_bin->cache = NULL;
  }

  if (play_bin->audio_sink != NULL) {
    gst_object_unref (play_bin->audio_sink);
    play_bin->audio_sink = NULL;
  }
  if (play_bin->video_sink != NULL) {
    gst_object_unref (play_bin->video_sink);
    play_bin->video_sink = NULL;
  }
  if (play_bin->visualisation != NULL) {
    gst_object_unref (play_bin->visualisation);
    play_bin->visualisation = NULL;
  }
  g_free (play_bin->font_desc);
  play_bin->font_desc = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
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
      if (play_bin->video_sink != NULL) {
        gst_object_unref (play_bin->video_sink);
      }
      play_bin->video_sink = g_value_get_object (value);
      if (play_bin->video_sink != NULL) {
        gst_object_ref (play_bin->video_sink);
        gst_object_sink (GST_OBJECT (play_bin->video_sink));
      }
      /* when changing the videosink, we just remove the
       * video pipeline from the cache so that it will be 
       * regenerated with the new sink element */
      g_hash_table_remove (play_bin->cache, "vbin");
      break;
    case ARG_AUDIO_SINK:
      if (play_bin->audio_sink != NULL) {
        gst_object_unref (play_bin->audio_sink);
      }
      play_bin->audio_sink = g_value_get_object (value);
      if (play_bin->audio_sink != NULL) {
        gst_object_ref (play_bin->audio_sink);
        gst_object_sink (GST_OBJECT (play_bin->audio_sink));
      }
      g_hash_table_remove (play_bin->cache, "abin");
      break;
    case ARG_VIS_PLUGIN:
      if (play_bin->visualisation != NULL) {
        gst_object_unref (play_bin->visualisation);
      }
      play_bin->visualisation = g_value_get_object (value);
      if (play_bin->visualisation != NULL) {
        gst_object_ref (play_bin->visualisation);
        gst_object_sink (GST_OBJECT (play_bin->visualisation));
      }
      break;
    case ARG_VOLUME:
      play_bin->volume = g_value_get_double (value);
      if (play_bin->volume_element) {
        g_object_set (G_OBJECT (play_bin->volume_element), "volume",
            play_bin->volume, NULL);
      }
      break;
    case ARG_FONT_DESC:
      g_free (play_bin->font_desc);
      play_bin->font_desc = g_strdup (g_value_get_string (value));
      if (play_bin->textoverlay_element) {
        g_object_set (G_OBJECT (play_bin->textoverlay_element),
            "font-desc", g_value_get_string (value), NULL);
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
      gst_value_set_mini_object (value, GST_MINI_OBJECT (play_bin->frame));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* signal fired when the identity has received a new buffer. This is used for
 * making screenshots.
 */
static void
handoff (GstElement * identity, GstBuffer * frame, gpointer data)
{
  GstPlayBin *play_bin = GST_PLAY_BIN (data);

  if (play_bin->frame) {
    gst_buffer_unref (play_bin->frame);
  }
  play_bin->frame = gst_buffer_ref (frame);
}

/* make the element (bin) that contains the elements needed to perform
 * video display. We connect a handoff signal to identity so that we
 * can grab snapshots. Identity's sinkpad is ghosted to vbin.
 *
 *  +-------------------------------------------------------------+
 *  | vbin                                                        |
 *  |      +--------+   +----------+   +----------+   +---------+ |
 *  |      |identity|   |colorspace|   |videoscale|   |videosink| |
 *  |   +-sink     src-sink       src-sink       src-sink       | |
 *  |   |  +---+----+   +----------+   +----------+   +---------+ |
 * sink-+      |                                                  |
 *  +----------|--------------------------------------------------+
 *           handoff
 */
static GstElement *
gen_video_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *conv;

  //GstElement *scale;
  GstElement *sink;
  GstElement *identity;
  GstPad *pad;

  /* first see if we have it in the cache */
  element = g_hash_table_lookup (play_bin->cache, "vbin");
  if (element != NULL) {
    return element;
  }

  element = gst_bin_new ("vbin");
  identity = gst_element_factory_make ("identity", "id");
  g_object_set (identity, "silent", TRUE, NULL);
  g_signal_connect (identity, "handoff", G_CALLBACK (handoff), play_bin);
  conv = gst_element_factory_make ("ffmpegcolorspace", "vconv");
  //scale = gst_element_factory_make ("videoscale", "vscale");
  if (play_bin->video_sink) {
    sink = play_bin->video_sink;
  } else {
    sink = gst_element_factory_make ("xvimagesink", "videosink");
  }
  gst_object_ref (sink);
  g_hash_table_insert (play_bin->cache, "video_sink", sink);

  gst_bin_add (GST_BIN (element), identity);
  gst_bin_add (GST_BIN (element), conv);
  //gst_bin_add (GST_BIN (element), scale);
  gst_bin_add (GST_BIN (element), sink);
  gst_element_link_pads (identity, "src", conv, "sink");
  gst_element_link_pads (conv, "src",   /*scale, "sink");
                                           gst_element_link_pads (scale, "src", */ sink, "sink");

  pad = gst_element_get_pad (identity, "sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  gst_element_set_state (element, GST_STATE_READY);

  /* since we're gonna add it to a bin but don't want to lose it,
   * we keep a reference. */
  gst_object_ref (element);
  g_hash_table_insert (play_bin->cache, "vbin", element);

  return element;
}

/* make an element for playback of video with subtitles embedded.
 *
 *  +--------------------------------------------------+
 *  | tbin                  +-------------+            |
 *  |          +-----+      | textoverlay |   +------+ |
 *  |          | csp | +--video_sink      |   | vbin | |
 * video_sink-sink  src+ +-text_sink     src-sink    | |
 *  |          +-----+   |  +-------------+   +------+ |
 * text_sink-------------+                             |
 *  +--------------------------------------------------+
 */

static GstElement *
gen_text_element (GstPlayBin * play_bin)
{
  GstElement *element, *csp, *overlay, *vbin;
  GstPad *pad;

  overlay = gst_element_factory_make ("textoverlay", "overlay");
  g_object_set (G_OBJECT (overlay),
      "halign", "center", "valign", "bottom", NULL);
  play_bin->textoverlay_element = overlay;
  if (play_bin->font_desc) {
    g_object_set (G_OBJECT (play_bin->textoverlay_element),
        "font-desc", play_bin->font_desc, NULL);
  }
  vbin = gen_video_element (play_bin);
  if (!overlay) {
    g_warning ("No overlay (pango) element, subtitles disabled");
    return vbin;
  }
  csp = gst_element_factory_make ("ffmpegcolorspace", "subtitlecsp");
  element = gst_bin_new ("textbin");
  gst_element_link_many (csp, overlay, vbin, NULL);
  gst_bin_add_many (GST_BIN (element), csp, overlay, vbin, NULL);

  pad = gst_element_get_pad (overlay, "text_sink");
#define gst_element_add_ghost_pad(element, pad, name) \
    gst_element_add_pad (element, gst_ghost_pad_new (name, pad))
  gst_element_add_ghost_pad (element, pad, "text_sink");
  gst_object_unref (pad);

  pad = gst_element_get_pad (csp, "sink");
  gst_element_add_ghost_pad (element, pad, "sink");
  gst_object_unref (pad);

  return element;
}

/* make the element (bin) that contains the elements needed to perform
 * audio playback. 
 *
 *  +-------------------------------------------------------------+
 *  | abin                                                        |
 *  |      +---------+   +----------+   +---------+   +---------+ |
 *  |      |audioconv|   |audioscale|   | volume  |   |audiosink| |
 *  |   +-sink      src-sink       src-sink      src-sink       | |
 *  |   |  +---------+   +----------+   +---------+   +---------+ |
 * sink-+                                                         |
 *  +-------------------------------------------------------------+
 *                  
 */
static GstElement *
gen_audio_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *conv;
  GstElement *sink;
  GstElement *volume;
  GstElement *scale;
  GstPad *pad;

  element = g_hash_table_lookup (play_bin->cache, "abin");
  if (element != NULL) {
    return element;
  }
  element = gst_bin_new ("abin");
  conv = gst_element_factory_make ("audioconvert", "aconv");
  scale = gst_element_factory_make ("audioscale", "ascale");

  volume = gst_element_factory_make ("volume", "volume");
  g_object_set (G_OBJECT (volume), "volume", play_bin->volume, NULL);
  play_bin->volume_element = volume;

  if (play_bin->audio_sink) {
    sink = play_bin->audio_sink;
  } else {
    sink = gst_element_factory_make ("alsasink", "audiosink");
    play_bin->audio_sink = GST_ELEMENT (gst_object_ref (sink));
  }

  gst_object_ref (sink);
  g_hash_table_insert (play_bin->cache, "audio_sink", sink);

  gst_bin_add (GST_BIN (element), conv);
  //gst_bin_add (GST_BIN (element), scale);
  //gst_bin_add (GST_BIN (element), volume);
  gst_bin_add (GST_BIN (element), sink);

  gst_element_link_pads (conv, "src",   /*scale, "sink");
                                           gst_element_link_pads (scale, "src", volume, "sink");
                                           gst_element_link_pads (volume, "src", */ sink, "sink");

  pad = gst_element_get_pad (conv, "sink");
  gst_element_add_ghost_pad (element, pad, "sink");
  gst_object_unref (pad);

  gst_element_set_state (element, GST_STATE_READY);

  /* since we're gonna add it to a bin but don't want to lose it,
   * we keep a reference. */
  gst_object_ref (element);
  g_hash_table_insert (play_bin->cache, "abin", element);

  return element;
}

/* make the element (bin) that contains the elements needed to perform
 * visualisation ouput.  The idea is to split the audio using tee, then 
 * sending the output to the regular audio bin and the other output to
 * the vis plugin that transforms it into a video that is rendered with the
 * normal video bin. The video bin is run in a thread to make sure it does
 * not block the audio playback pipeline.
 *
 *  +--------------------------------------------------------------------------+
 *  | visbin                                                                   |
 *  |      +------+   +----------------+                                       |
 *  |      | tee  |   |   abin ...     |                                       |
 *  |   +-sink   src-sink              |                                       |
 *  |   |  |      |   +----------------+                 +-------------------+ |
 *  |   |  |      |                                      | vthread           | |
 *  |   |  |      |   +---------+   +------+   +------+  | +--------------+  | |
 *  |   |  |      |   |audioconv|   | vis  |   |vqueue|  | | vbin ...     |  | |
 *  |   |  |     src-sink      src-sink   src-sink   src-sink             |  | |
 *  |   |  |      |   +---------+   +------+   +------+  | +--------------+  | |
 *  |   |  |      |                                      +-------------------+ |
 *  |   |  +------+                                                            |
 * sink-+                                                                      |
   +--------------------------------------------------------------------------+
 */
static GstElement *
gen_vis_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *tee;
  GstElement *asink;
  GstElement *vsink;
  GstElement *conv;
  GstElement *vis;
  GstElement *vqueue, *aqueue;
  GstPad *pad, *rpad;

  element = gst_bin_new ("visbin");
  tee = gst_element_factory_make ("tee", "tee");

  vqueue = gst_element_factory_make ("queue", "vqueue");
  aqueue = gst_element_factory_make ("queue", "aqueue");

  asink = gen_audio_element (play_bin);
  vsink = gen_video_element (play_bin);

  gst_bin_add (GST_BIN (element), asink);
  gst_bin_add (GST_BIN (element), vqueue);
  gst_bin_add (GST_BIN (element), aqueue);
  gst_bin_add (GST_BIN (element), vsink);
  gst_bin_add (GST_BIN (element), tee);

  conv = gst_element_factory_make ("audioconvert", "aconv");
  if (play_bin->visualisation) {
    gst_object_ref (play_bin->visualisation);
    vis = play_bin->visualisation;
  } else {
    vis = gst_element_factory_make ("goom", "vis");
  }

  gst_bin_add (GST_BIN (element), conv);
  gst_bin_add (GST_BIN (element), vis);

  gst_element_link_pads (conv, "src", vis, "sink");
  gst_element_link_pads (vis, "src", vqueue, "sink");

  gst_element_link_pads (vqueue, "src", vsink, "sink");

  pad = gst_element_get_pad (aqueue, "sink");
  rpad = gst_element_get_request_pad (tee, "src%d");
  gst_pad_link (rpad, pad);
  gst_object_unref (rpad);
  gst_object_unref (pad);
  gst_element_link_pads (aqueue, "src", asink, "sink");

  pad = gst_element_get_pad (conv, "sink");
  rpad = gst_element_get_request_pad (tee, "src%d");
  gst_pad_link (rpad, pad);
  gst_object_unref (rpad);
  gst_object_unref (pad);

  pad = gst_element_get_pad (tee, "sink");
  gst_element_add_ghost_pad (element, pad, "sink");
  gst_object_unref (pad);

  return element;
}

/* get rid of all installed sinks */
static void
remove_sinks (GstPlayBin * play_bin)
{
  GList *sinks;
  GstObject *parent;
  GstElement *element;
  GstPad *pad, *peer;

  GST_DEBUG ("removesinks");
  element = g_hash_table_lookup (play_bin->cache, "abin");
  if (element != NULL) {
    parent = gst_element_get_parent (element);
    if (parent != NULL) {
      /* we remove the element from the parent so that
       * there is no unwanted state change when the parent
       * is disposed */
      play_bin->sinks = g_list_remove (play_bin->sinks, element);
      gst_bin_remove (GST_BIN (parent), element);
      gst_object_unref (parent);
    }
    pad = gst_element_get_pad (element, "sink");
    if (pad != NULL) {
      peer = gst_pad_get_peer (pad);
      if (peer != NULL) {
        gst_pad_unlink (peer, pad);
        gst_object_unref (peer);
      }
      gst_object_unref (pad);
    }
  }
  element = g_hash_table_lookup (play_bin->cache, "vbin");
  if (element != NULL) {
    parent = gst_element_get_parent (element);
    if (parent != NULL) {
      play_bin->sinks = g_list_remove (play_bin->sinks, element);
      gst_bin_remove (GST_BIN (parent), element);
      gst_object_unref (parent);
    }
    pad = gst_element_get_pad (element, "sink");
    if (pad != NULL) {
      peer = gst_pad_get_peer (pad);
      if (peer != NULL) {
        gst_pad_unlink (peer, pad);
        gst_object_unref (peer);
      }
      gst_object_unref (pad);
    }
  }

  for (sinks = play_bin->sinks; sinks; sinks = g_list_next (sinks)) {
    GstElement *element = GST_ELEMENT (sinks->data);
    GstPad *pad;
    GstPad *peer;

    pad = gst_element_get_pad (element, "sink");

    GST_LOG ("removing sink %p", element);

    peer = gst_pad_get_peer (pad);
    if (peer) {
      gst_pad_unlink (peer, pad);
      gst_object_unref (peer);
    }
    gst_object_unref (pad);

    gst_bin_remove (GST_BIN (play_bin), element);
  }
  g_list_free (play_bin->sinks);
  play_bin->sinks = NULL;

  if (play_bin->frame) {
    gst_buffer_unref (play_bin->frame);
    play_bin->frame = NULL;
  }

  play_bin->textoverlay_element = NULL;
}

/* loop over the streams and set up the pipeline to play this
 * media file. First we count the number of audio and video streams.
 * If there is no video stream but there exists an audio stream,
 * we install a visualisation pipeline.
 * 
 * Also make sure to only connect the first audio and video pad. FIXME
 * this should eventually be handled with a tuner interface so that
 * one can switch the streams.
 */
static gboolean
add_sink (GstPlayBin * play_bin, GstElement * sink, GstPad * srcpad)
{
  GstPad *sinkpad;
  GstPadLinkReturn res;
  GstElement *parent;

  gst_bin_add (GST_BIN (play_bin), sink);

  /* we found a sink for this stream, now try to install it */
  sinkpad = gst_element_get_pad (sink, "sink");
  res = gst_pad_link (srcpad, sinkpad);
  gst_object_unref (sinkpad);

  /* this is only for debugging */
  parent = gst_pad_get_parent_element (srcpad);
  if (parent) {
    GST_DEBUG ("Adding sink with state %d (parent: %d, peer: %d)\n",
        GST_STATE (sink), GST_STATE (play_bin), GST_STATE (parent));
    gst_object_unref (parent);
  }

  /* try to link the pad of the sink to the stream */
  if (res < 0) {
    gchar *capsstr;

    /* could not link this stream */
    capsstr = gst_caps_to_string (gst_pad_get_caps (srcpad));
    g_warning ("could not link %s: %d", capsstr, res);
    g_free (capsstr);

    gst_bin_remove (GST_BIN (play_bin), sink);
  } else {
    /* we got the sink succesfully linked, now keep the sink
     * in out internal list */
    play_bin->sinks = g_list_prepend (play_bin->sinks, sink);
    gst_element_set_state (sink,
        (GST_STATE (play_bin) == GST_STATE_PLAYING) ?
        GST_STATE_PLAYING : GST_STATE_PAUSED);
  }

  return res;
}

static void
setup_sinks (GstPlayBaseBin * play_base_bin, GstPlayBaseGroup * group)
{
  GstPlayBin *play_bin = GST_PLAY_BIN (play_base_bin);
  GList *streaminfo = NULL, *s;
  gboolean need_vis = FALSE;
  gboolean need_text = FALSE;
  GstPad *textsrcpad = NULL, *textsinkpad = NULL, *pad;
  GstElement *sink;

  /* get rid of existing sinks */
  if (play_bin->sinks) {
    remove_sinks (play_bin);
  }
  GST_DEBUG ("setupsinks");

  /* find out what to do */
  if (group->type[GST_STREAM_TYPE_VIDEO - 1].npads > 0 &&
      group->type[GST_STREAM_TYPE_TEXT - 1].npads > 0) {
    need_text = TRUE;
  } else if (group->type[GST_STREAM_TYPE_VIDEO - 1].npads == 0 &&
      group->type[GST_STREAM_TYPE_AUDIO - 1].npads > 0 &&
      play_bin->visualisation != NULL) {
    need_vis = TRUE;
  }

  /* now actually connect everything */
  g_object_get (G_OBJECT (play_base_bin), "stream-info", &streaminfo, NULL);
  for (s = streaminfo; s; s = g_list_next (s)) {
    GObject *obj = G_OBJECT (s->data);
    gint type;
    GstObject *object;

    g_object_get (obj, "type", &type, NULL);
    g_object_get (obj, "object", &object, NULL);
  }

  /* link audio */
  if (group->type[GST_STREAM_TYPE_AUDIO - 1].npads > 0) {
    if (need_vis) {
      sink = gen_vis_element (play_bin);
    } else {
      sink = gen_audio_element (play_bin);
    }
    pad = gst_element_get_pad (group->type[GST_STREAM_TYPE_AUDIO - 1].preroll,
        "src");
    add_sink (play_bin, sink, pad);
    gst_object_unref (pad);
  }

  /* link video */
  if (group->type[GST_STREAM_TYPE_VIDEO - 1].npads > 0) {
    if (need_text) {
      sink = gen_text_element (play_bin);

      textsinkpad = gst_element_get_pad (sink, "text_sink");
      textsrcpad =
          gst_element_get_pad (group->type[GST_STREAM_TYPE_TEXT - 1].preroll,
          "src");
      gst_pad_link (textsrcpad, textsinkpad);
      gst_object_unref (textsinkpad);
      gst_object_unref (textsrcpad);
    } else {
      sink = gen_video_element (play_bin);
    }
    pad = gst_element_get_pad (group->type[GST_STREAM_TYPE_VIDEO - 1].preroll,
        "src");
    add_sink (play_bin, sink, pad);
    gst_object_unref (pad);
  }

  if (play_bin->fakesink) {
    gst_bin_remove (GST_BIN (play_bin), play_bin->fakesink);
    play_bin->fakesink = NULL;
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

  switch (transition) {
    case GST_STATE_READY_TO_PAUSED:
      if (!play_bin->fakesink) {
        play_bin->fakesink = gst_element_factory_make ("fakesink", "test");
        gst_bin_add (GST_BIN (play_bin), play_bin->fakesink);
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  if (ret == GST_STATE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
      /* Set audio sink state to NULL to release the sound device,
       * but only if we own it (else we might be in chain-transition). */
      //if (play_bin->audio_sink != NULL &&
      //    GST_STATE (play_bin->audio_sink) == GST_STATE_PAUSED) {
      //  gst_element_set_state (play_bin->audio_sink, GST_STATE_NULL);
      //}
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* Check for NULL because the state transition may be done by
       * gst_bin_dispose which is called by gst_play_bin_dispose, and in that
       * case, we don't want to run remove_sinks.
       * FIXME: should the NULL test be done in remove_sinks? Should we just
       * set the state to NULL in gst_play_bin_dispose?
       */
      if (play_bin->cache != NULL) {
        remove_sinks (play_bin);
      }
      if (play_bin->fakesink) {
        gst_bin_remove (GST_BIN (play_bin), play_bin->fakesink);
        play_bin->fakesink = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
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
