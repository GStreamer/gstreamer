/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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

#include <gst/gst-i18n-plugin.h>
#include <gst/pbutils/pbutils.h>

#include "gstplaysink.h"

GST_DEBUG_CATEGORY_STATIC (gst_play_sink_debug);
#define GST_CAT_DEFAULT gst_play_sink_debug

#define VOLUME_MAX_DOUBLE 10.0

/* holds the common data fields for the audio and video pipelines. We keep them
 * in a structure to more easily have all the info available. */
typedef struct
{
  GstPlaySink *playsink;
  GstPad *sinkpad;
  GstElement *bin;
  gboolean added;
  gboolean activated;
} GstPlayChain;

typedef struct
{
  GstPlayChain chain;
  GstElement *queue;
  GstElement *conv;
  GstElement *resample;
  GstElement *volume;           /* element with the volume property */
  GstElement *mute;             /* element with the mute property */
  GstElement *sink;
} GstPlayAudioChain;

typedef struct
{
  GstPlayChain chain;
  GstElement *conv;
  GstElement *queue;
  GstElement *scale;
  GstElement *sink;
} GstPlayVideoChain;

typedef struct
{
  GstPlayChain chain;
  GstElement *queue;
  GstElement *conv;
  GstElement *resample;
  GstElement *vis;
  GstPad *srcpad;
} GstPlayVisChain;

#define GST_PLAY_SINK_GET_LOCK(playsink) (((GstPlaySink *)playsink)->lock)
#define GST_PLAY_SINK_LOCK(playsink)     g_mutex_lock (GST_PLAY_SINK_GET_LOCK (playsink))
#define GST_PLAY_SINK_UNLOCK(playsink)   g_mutex_unlock (GST_PLAY_SINK_GET_LOCK (playsink))

struct _GstPlaySink
{
  GstBin bin;

  GMutex *lock;

  GstPlayFlags flags;

  GstPlayChain *audiochain;
  GstPlayChain *videochain;
  GstPlayChain *vischain;

  GstPad *audio_pad;
  gboolean audio_pad_raw;
  GstElement *audio_tee;
  GstPad *audio_tee_sink;
  GstPad *audio_tee_asrc;
  GstPad *audio_tee_vissrc;

  GstPad *video_pad;
  gboolean video_pad_raw;

  GstPad *text_pad;

  /* properties */
  GstElement *audio_sink;
  GstElement *video_sink;
  GstElement *visualisation;
  gfloat volume;
  gboolean mute;
  gchar *font_desc;             /* font description */
  guint connection_speed;       /* connection speed in bits/sec (0 = unknown) */

  /* internal elements */
  GstElement *textoverlay_element;

  GstElement *pending_visualisation;
  GstElement *fakesink;
};

struct _GstPlaySinkClass
{
  GstBinClass parent_class;
};


/* props */
enum
{
  PROP_0,
  PROP_AUDIO_SINK,
  PROP_VIDEO_SINK,
  PROP_VIS_PLUGIN,
  PROP_VOLUME,
  PROP_FRAME,
  PROP_FONT_DESC,
  PROP_LAST
};

/* signals */
enum
{
  LAST_SIGNAL
};

static void gst_play_sink_class_init (GstPlaySinkClass * klass);
static void gst_play_sink_init (GstPlaySink * playsink);
static void gst_play_sink_dispose (GObject * object);
static void gst_play_sink_finalize (GObject * object);

static void gst_play_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_play_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);

static gboolean gst_play_sink_send_event (GstElement * element,
    GstEvent * event);
static GstStateChangeReturn gst_play_sink_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class;

/* static guint gst_play_sink_signals[LAST_SIGNAL] = { 0 }; */

static const GstElementDetails gst_play_sink_details =
GST_ELEMENT_DETAILS ("Player Sink",
    "Generic/Bin/Player",
    "Autoplug and play media from an uri",
    "Wim Taymans <wim.taymans@gmail.com>");

GType
gst_play_sink_get_type (void)
{
  static GType gst_play_sink_type = 0;

  if (!gst_play_sink_type) {
    static const GTypeInfo gst_play_sink_info = {
      sizeof (GstPlaySinkClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_play_sink_class_init,
      NULL,
      NULL,
      sizeof (GstPlaySink),
      0,
      (GInstanceInitFunc) gst_play_sink_init,
      NULL
    };

    gst_play_sink_type = g_type_register_static (GST_TYPE_BIN,
        "GstPlaySink", &gst_play_sink_info, 0);
  }

  return gst_play_sink_type;
}

static void
gst_play_sink_class_init (GstPlaySinkClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstBinClass *gstbin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_klass->set_property = gst_play_sink_set_property;
  gobject_klass->get_property = gst_play_sink_get_property;

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_play_sink_dispose);
  gobject_klass->finalize = GST_DEBUG_FUNCPTR (gst_play_sink_finalize);

  g_object_class_install_property (gobject_klass, PROP_VIDEO_SINK,
      g_param_spec_object ("video-sink", "Video Sink",
          "the video output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, PROP_AUDIO_SINK,
      g_param_spec_object ("audio-sink", "Audio Sink",
          "the audio output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, PROP_VIS_PLUGIN,
      g_param_spec_object ("vis-plugin", "Vis plugin",
          "the visualization element to use (NULL = none)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, PROP_VOLUME,
      g_param_spec_double ("volume", "volume", "volume",
          0.0, VOLUME_MAX_DOUBLE, 1.0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, PROP_FRAME,
      gst_param_spec_mini_object ("frame", "Frame",
          "The last frame (NULL = no video available)",
          GST_TYPE_BUFFER, G_PARAM_READABLE));
  g_object_class_install_property (gobject_klass, PROP_FONT_DESC,
      g_param_spec_string ("subtitle-font-desc",
          "Subtitle font description",
          "Pango font description of font "
          "to be used for subtitle rendering", NULL, G_PARAM_WRITABLE));

  gst_element_class_set_details (gstelement_klass, &gst_play_sink_details);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_play_sink_change_state);
  gstelement_klass->send_event = GST_DEBUG_FUNCPTR (gst_play_sink_send_event);

  GST_DEBUG_CATEGORY_INIT (gst_play_sink_debug, "playsink", 0, "play bin");
}

static void
gst_play_sink_init (GstPlaySink * playsink)
{
  /* init groups */
  playsink->video_sink = NULL;
  playsink->audio_sink = NULL;
  playsink->visualisation = NULL;
  playsink->pending_visualisation = NULL;
  playsink->textoverlay_element = NULL;
  playsink->volume = 1.0;
  playsink->font_desc = NULL;
  playsink->flags = GST_PLAY_FLAG_SOFT_VOLUME;

  playsink->lock = g_mutex_new ();
}

static void
gst_play_sink_dispose (GObject * object)
{
  GstPlaySink *playsink;

  playsink = GST_PLAY_SINK (object);

  if (playsink->audio_sink != NULL) {
    gst_element_set_state (playsink->audio_sink, GST_STATE_NULL);
    gst_object_unref (playsink->audio_sink);
    playsink->audio_sink = NULL;
  }
  if (playsink->video_sink != NULL) {
    gst_element_set_state (playsink->video_sink, GST_STATE_NULL);
    gst_object_unref (playsink->video_sink);
    playsink->video_sink = NULL;
  }
  if (playsink->visualisation != NULL) {
    gst_element_set_state (playsink->visualisation, GST_STATE_NULL);
    gst_object_unref (playsink->visualisation);
    playsink->visualisation = NULL;
  }
  if (playsink->pending_visualisation != NULL) {
    gst_element_set_state (playsink->pending_visualisation, GST_STATE_NULL);
    gst_object_unref (playsink->pending_visualisation);
    playsink->pending_visualisation = NULL;
  }
  if (playsink->textoverlay_element != NULL) {
    gst_object_unref (playsink->textoverlay_element);
    playsink->textoverlay_element = NULL;
  }
  g_free (playsink->font_desc);
  playsink->font_desc = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_play_sink_finalize (GObject * object)
{
  GstPlaySink *playsink;

  playsink = GST_PLAY_SINK (object);

  g_mutex_free (playsink->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_play_sink_vis_unblocked (GstPad * tee_pad, gboolean blocked,
    gpointer user_data)
{
  GstPlaySink *playsink = GST_PLAY_SINK (user_data);

  if (playsink->pending_visualisation)
    gst_pad_set_blocked_async (tee_pad, FALSE, gst_play_sink_vis_unblocked,
        playsink);
}

static void
gst_play_sink_vis_blocked (GstPad * tee_pad, gboolean blocked,
    gpointer user_data)
{
  GstPlaySink *playsink = GST_PLAY_SINK (user_data);
  GstBin *vis_bin = NULL;
  GstPad *vis_sink_pad = NULL, *vis_src_pad = NULL, *vqueue_pad = NULL;
  GstState bin_state;
  GstElement *pending_visualisation;

  GST_OBJECT_LOCK (playsink);
  pending_visualisation = playsink->pending_visualisation;
  playsink->pending_visualisation = NULL;
  GST_OBJECT_UNLOCK (playsink);

  /* We want to disable visualisation */
  if (!GST_IS_ELEMENT (pending_visualisation)) {
    /* Set visualisation element to READY */
    gst_element_set_state (playsink->visualisation, GST_STATE_READY);
    goto beach;
  }

  vis_bin =
      GST_BIN_CAST (gst_object_get_parent (GST_OBJECT_CAST (playsink->
              visualisation)));

  if (!GST_IS_BIN (vis_bin) || !GST_IS_PAD (tee_pad)) {
    goto beach;
  }

  vis_src_pad = gst_element_get_pad (playsink->visualisation, "src");
  vis_sink_pad = gst_pad_get_peer (tee_pad);

  /* Can be fakesink */
  if (GST_IS_PAD (vis_src_pad)) {
    vqueue_pad = gst_pad_get_peer (vis_src_pad);
  }

  if (!GST_IS_PAD (vis_sink_pad)) {
    goto beach;
  }

  /* Check the bin's state */
  GST_OBJECT_LOCK (vis_bin);
  bin_state = GST_STATE (vis_bin);
  GST_OBJECT_UNLOCK (vis_bin);

  /* Unlink */
  gst_pad_unlink (tee_pad, vis_sink_pad);
  gst_object_unref (vis_sink_pad);
  vis_sink_pad = NULL;

  if (GST_IS_PAD (vqueue_pad)) {
    gst_pad_unlink (vis_src_pad, vqueue_pad);
    gst_object_unref (vis_src_pad);
    vis_src_pad = NULL;
  }

  /* Remove from vis_bin */
  gst_bin_remove (vis_bin, playsink->visualisation);
  /* Set state to NULL */
  gst_element_set_state (playsink->visualisation, GST_STATE_NULL);
  /* And loose our ref */
  gst_object_unref (playsink->visualisation);

  if (pending_visualisation) {
    /* Ref this new visualisation element before adding to the bin */
    gst_object_ref (pending_visualisation);
    /* Add the new one */
    gst_bin_add (vis_bin, pending_visualisation);
    /* Synchronizing state */
    gst_element_set_state (pending_visualisation, bin_state);

    vis_sink_pad = gst_element_get_pad (pending_visualisation, "sink");
    vis_src_pad = gst_element_get_pad (pending_visualisation, "src");

    if (!GST_IS_PAD (vis_sink_pad) || !GST_IS_PAD (vis_src_pad)) {
      goto beach;
    }

    /* Link */
    gst_pad_link (tee_pad, vis_sink_pad);
    gst_pad_link (vis_src_pad, vqueue_pad);
  }

  /* We are done */
  gst_object_unref (playsink->visualisation);
  playsink->visualisation = pending_visualisation;

beach:
  if (vis_sink_pad) {
    gst_object_unref (vis_sink_pad);
  }
  if (vis_src_pad) {
    gst_object_unref (vis_src_pad);
  }
  if (vqueue_pad) {
    gst_object_unref (vqueue_pad);
  }
  if (vis_bin) {
    gst_object_unref (vis_bin);
  }

  /* Unblock the pad */
  gst_pad_set_blocked_async (tee_pad, FALSE, gst_play_sink_vis_unblocked,
      playsink);
}

void
gst_play_sink_set_video_sink (GstPlaySink * playsink, GstElement * sink)
{
  GST_OBJECT_LOCK (playsink);
  if (playsink->video_sink)
    gst_object_unref (playsink->video_sink);

  if (sink) {
    gst_object_ref (sink);
    gst_object_sink (sink);
  }
  playsink->video_sink = sink;
  GST_OBJECT_UNLOCK (playsink);
}

void
gst_play_sink_set_audio_sink (GstPlaySink * playsink, GstElement * sink)
{
  GST_OBJECT_LOCK (playsink);
  if (playsink->audio_sink)
    gst_object_unref (playsink->audio_sink);

  if (sink) {
    gst_object_ref (sink);
    gst_object_sink (sink);
  }
  playsink->audio_sink = sink;
  GST_OBJECT_UNLOCK (playsink);
}

void
gst_play_sink_set_vis_plugin (GstPlaySink * playsink,
    GstElement * pending_visualisation)
{
  /* Take ownership */
  if (pending_visualisation) {
    gst_object_ref (pending_visualisation);
    gst_object_sink (pending_visualisation);
  }

  /* Do we already have a visualisation change pending? If yes, change the
   * pending vis with the new one. */
  GST_OBJECT_LOCK (playsink);
  if (playsink->pending_visualisation) {
    gst_object_unref (playsink->pending_visualisation);
    playsink->pending_visualisation = pending_visualisation;
    GST_OBJECT_UNLOCK (playsink);
  } else {
    GST_OBJECT_UNLOCK (playsink);
    /* Was there a visualisation already set ? */
    if (playsink->visualisation != NULL) {
      GstBin *vis_bin = NULL;

      vis_bin =
          GST_BIN_CAST (gst_object_get_parent (GST_OBJECT_CAST (playsink->
                  visualisation)));

      /* Check if the visualisation is already in a bin */
      if (GST_IS_BIN (vis_bin)) {
        GstPad *vis_sink_pad = NULL, *tee_pad = NULL;

        /* Now get tee pad and block it async */
        vis_sink_pad = gst_element_get_pad (playsink->visualisation, "sink");
        if (!GST_IS_PAD (vis_sink_pad)) {
          goto beach;
        }
        tee_pad = gst_pad_get_peer (vis_sink_pad);
        if (!GST_IS_PAD (tee_pad)) {
          goto beach;
        }

        playsink->pending_visualisation = pending_visualisation;
        /* Block with callback */
        gst_pad_set_blocked_async (tee_pad, TRUE, gst_play_sink_vis_blocked,
            playsink);
      beach:
        if (vis_sink_pad) {
          gst_object_unref (vis_sink_pad);
        }
        if (tee_pad) {
          gst_object_unref (tee_pad);
        }
        gst_object_unref (vis_bin);
      } else {
        playsink->visualisation = pending_visualisation;
      }
    } else {
      playsink->visualisation = pending_visualisation;
    }
  }
}

void
gst_play_sink_set_volume (GstPlaySink * playsink, gdouble volume)
{
  GstPlayAudioChain *chain;

  GST_PLAY_SINK_LOCK (playsink);
  playsink->volume = volume;
  chain = (GstPlayAudioChain *) playsink->audiochain;
  if (chain && chain->volume) {
    g_object_set (chain->volume, "volume", volume, NULL);
  }
  GST_PLAY_SINK_UNLOCK (playsink);
}

gdouble
gst_play_sink_get_volume (GstPlaySink * playsink)
{
  gdouble result;
  GstPlayAudioChain *chain;

  GST_PLAY_SINK_LOCK (playsink);
  chain = (GstPlayAudioChain *) playsink->audiochain;
  if (chain && chain->volume) {
    g_object_get (chain->volume, "volume", &result, NULL);
    playsink->volume = result;
  } else {
    result = playsink->volume;
  }
  GST_PLAY_SINK_UNLOCK (playsink);

  return result;
}

void
gst_play_sink_set_mute (GstPlaySink * playsink, gboolean mute)
{
  GstPlayAudioChain *chain;

  GST_PLAY_SINK_LOCK (playsink);
  playsink->mute = mute;
  chain = (GstPlayAudioChain *) playsink->audiochain;
  if (chain && chain->mute) {
    g_object_set (chain->mute, "mute", mute, NULL);
  }
  GST_PLAY_SINK_UNLOCK (playsink);
}

gboolean
gst_play_sink_get_mute (GstPlaySink * playsink)
{
  gboolean result;
  GstPlayAudioChain *chain;

  GST_PLAY_SINK_LOCK (playsink);
  chain = (GstPlayAudioChain *) playsink->audiochain;
  if (chain && chain->mute) {
    g_object_get (chain->mute, "mute", &result, NULL);
    playsink->mute = result;
  } else {
    result = playsink->mute;
  }
  GST_PLAY_SINK_UNLOCK (playsink);

  return result;
}

static void
gst_play_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPlaySink *playsink;

  playsink = GST_PLAY_SINK (object);

  switch (prop_id) {
    case PROP_VIDEO_SINK:
      gst_play_sink_set_video_sink (playsink, g_value_get_object (value));
      break;
    case PROP_AUDIO_SINK:
      gst_play_sink_set_audio_sink (playsink, g_value_get_object (value));
      break;
    case PROP_VIS_PLUGIN:
      gst_play_sink_set_vis_plugin (playsink, g_value_get_object (value));
      break;
    case PROP_VOLUME:
      gst_play_sink_set_volume (playsink, g_value_get_double (value));
      break;
    case PROP_FONT_DESC:
      GST_OBJECT_LOCK (playsink);
      g_free (playsink->font_desc);
      playsink->font_desc = g_strdup (g_value_get_string (value));
      if (playsink->textoverlay_element) {
        g_object_set (G_OBJECT (playsink->textoverlay_element),
            "font-desc", g_value_get_string (value), NULL);
      }
      GST_OBJECT_UNLOCK (playsink);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_play_sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPlaySink *playsink;

  playsink = GST_PLAY_SINK (object);

  switch (prop_id) {
    case PROP_VIDEO_SINK:
      GST_OBJECT_LOCK (playsink);
      g_value_set_object (value, playsink->video_sink);
      GST_OBJECT_UNLOCK (playsink);
      break;
    case PROP_AUDIO_SINK:
      GST_OBJECT_LOCK (playsink);
      g_value_set_object (value, playsink->audio_sink);
      GST_OBJECT_UNLOCK (playsink);
      break;
    case PROP_VIS_PLUGIN:
      GST_OBJECT_LOCK (playsink);
      g_value_set_object (value, playsink->visualisation);
      GST_OBJECT_UNLOCK (playsink);
      break;
    case PROP_VOLUME:
      g_value_set_double (value, gst_play_sink_get_volume (playsink));
      break;
    case PROP_FRAME:
    {
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
post_missing_element_message (GstPlaySink * playsink, const gchar * name)
{
  GstMessage *msg;

  msg = gst_missing_element_message_new (GST_ELEMENT_CAST (playsink), name);
  gst_element_post_message (GST_ELEMENT_CAST (playsink), msg);
}

static void
free_chain (GstPlayChain * chain)
{
  if (chain->bin)
    gst_object_unref (chain->bin);
  gst_object_unref (chain->playsink);
  g_free (chain);
}

static gboolean
add_chain (GstPlayChain * chain, gboolean add)
{
  if (chain->added == add)
    return TRUE;

  if (add)
    gst_bin_add (GST_BIN_CAST (chain->playsink), chain->bin);
  else
    gst_bin_remove (GST_BIN_CAST (chain->playsink), chain->bin);

  chain->added = add;

  return TRUE;
}

static gboolean
activate_chain (GstPlayChain * chain, gboolean activate)
{
  if (chain->activated == activate)
    return TRUE;

  if (activate)
    gst_element_set_state (chain->bin, GST_STATE_PAUSED);
  else
    gst_element_set_state (chain->bin, GST_STATE_NULL);

  chain->activated = activate;

  return TRUE;
}

/* make the element (bin) that contains the elements needed to perform
 * video display. 
 *
 *  +------------------------------------------------+
 *  | vbin                                           |
 *  |      +----------+   +----------+   +---------+ |
 *  |      |colorspace|   |videoscale|   |videosink| |
 *  |   +-sink       src-sink       src-sink       | |
 *  |   |  +----------+   +----------+   +---------+ |
 * sink-+                                            |
 *  +------------------------------------------------+
 *           
 */
static GstPlayChain *
gen_video_chain (GstPlaySink * playsink, gboolean raw, gboolean async)
{
  GstPlayVideoChain *chain;
  GstBin *bin;
  GstPad *pad;

  chain = g_new0 (GstPlayVideoChain, 1);
  chain->chain.playsink = gst_object_ref (playsink);

  if (playsink->video_sink) {
    chain->sink = playsink->video_sink;
  } else {
    chain->sink = gst_element_factory_make ("autovideosink", "videosink");
    if (chain->sink == NULL) {
      chain->sink = gst_element_factory_make ("xvimagesink", "videosink");
    }
    if (chain->sink == NULL)
      goto no_sinks;
  }

  /* FIXME, proxy property in sinks */
  /* g_object_set (chain->sink, "async", async, NULL); */

  /* create a bin to hold objects, as we create them we add them to this bin so
   * that when something goes wrong we only need to unref the bin */
  chain->chain.bin = gst_bin_new ("vbin");
  bin = GST_BIN_CAST (chain->chain.bin);
  gst_object_ref (bin);
  gst_object_sink (bin);
  gst_bin_add (bin, chain->sink);

  if (raw) {
    chain->conv = gst_element_factory_make ("ffmpegcolorspace", "vconv");
    if (chain->conv == NULL)
      goto no_colorspace;
    gst_bin_add (bin, chain->conv);

    chain->scale = gst_element_factory_make ("videoscale", "vscale");
    if (chain->scale == NULL)
      goto no_videoscale;
    gst_bin_add (bin, chain->scale);
  }

  /* decouple decoder from sink, this improves playback quite a lot since the
   * decoder can continue while the sink blocks for synchronisation. We don't
   * need a lot of buffers as this consumes a lot of memory and we don't want
   * too little because else we would be context switching too quickly. */
  chain->queue = gst_element_factory_make ("queue", "vqueue");
  g_object_set (G_OBJECT (chain->queue), "max-size-buffers", 3,
      "max-size-bytes", 0, "max-size-time", (gint64) 0, NULL);
  gst_bin_add (bin, chain->queue);

  if (raw) {
    gst_element_link_pads (chain->queue, "src", chain->conv, "sink");
    gst_element_link_pads (chain->conv, "src", chain->scale, "sink");
    /* be more careful with the pad from the custom sink element, it might not
     * be named 'sink' */
    if (!gst_element_link_pads (chain->scale, "src", chain->sink, NULL))
      goto link_failed;

    pad = gst_element_get_pad (chain->queue, "sink");
  } else {
    if (!gst_element_link_pads (chain->queue, "src", chain->sink, NULL))
      goto link_failed;
    pad = gst_element_get_pad (chain->queue, "sink");
  }

  chain->chain.sinkpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);
  gst_element_add_pad (chain->chain.bin, chain->chain.sinkpad);

  return (GstPlayChain *) chain;

  /* ERRORS */
no_sinks:
  {
    post_missing_element_message (playsink, "autovideosink");
    GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
        (_("Both autovideosink and xvimagesink elements are missing.")),
        (NULL));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
no_colorspace:
  {
    post_missing_element_message (playsink, "ffmpegcolorspace");
    GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "ffmpegcolorspace"), (NULL));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }

no_videoscale:
  {
    post_missing_element_message (playsink, "videoscale");
    GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "videoscale"), ("possibly a liboil version mismatch?"));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
link_failed:
  {
    GST_ELEMENT_ERROR (playsink, CORE, PAD,
        (NULL), ("Failed to configure the video sink."));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
}

#if 0
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
 *
 *  If there is no subtitle renderer this function will simply return the
 *  videosink without the text_sink pad.
 */
static GstElement *
gen_text_element (GstPlaySink * playsink)
{
  GstElement *element, *csp, *overlay, *vbin;
  GstPad *pad;

  /* Create the video rendering bin, error is posted when this fails. */
  vbin = gen_video_element (playsink);
  if (!vbin)
    return NULL;

  /* Text overlay */
  overlay = gst_element_factory_make ("textoverlay", "overlay");

  /* If no overlay return the video bin without subtitle support. */
  if (!overlay)
    goto no_overlay;

  /* Create our bin */
  element = gst_bin_new ("textbin");

  /* Set some parameters */
  g_object_set (G_OBJECT (overlay),
      "halign", "center", "valign", "bottom", NULL);
  if (playsink->font_desc) {
    g_object_set (G_OBJECT (overlay), "font-desc", playsink->font_desc, NULL);
  }

  /* Take a ref */
  playsink->textoverlay_element = GST_ELEMENT_CAST (gst_object_ref (overlay));

  /* we know this will succeed, as the video bin already created one before */
  csp = gst_element_factory_make ("ffmpegcolorspace", "subtitlecsp");

  /* Add our elements */
  gst_bin_add_many (GST_BIN_CAST (element), csp, overlay, vbin, NULL);

  /* Link */
  gst_element_link_pads (csp, "src", overlay, "video_sink");
  gst_element_link_pads (overlay, "src", vbin, "sink");

  /* Add ghost pads on the subtitle bin */
  pad = gst_element_get_pad (overlay, "text_sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("text_sink", pad));
  gst_object_unref (pad);

  pad = gst_element_get_pad (csp, "sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  /* Set state to READY */
  gst_element_set_state (element, GST_STATE_READY);

  return element;

  /* ERRORS */
no_overlay:
  {
    post_missing_element_message (playsink, "textoverlay");
    GST_WARNING_OBJECT (playsink,
        "No overlay (pango) element, subtitles disabled");
    return vbin;
  }
}
#endif


/* make the chain that contains the elements needed to perform
 * audio playback. 
 *
 * We add a tee as the first element so that we can link the visualisation chain
 * to it when requested.
 *
 *  +-----------------------------------------------------------------------+
 *  | abin                                                                  |
 *  |      +-----+   +---------+   +----------+   +---------+   +---------+ |
 *  |      | tee |   |audioconv|   |audioscale|   | volume  |   |audiosink| |
 *  |   +-sink  src-sink      src-sink       src-sink      src-sink       | |
 *  |   |  +-----+   +---------+   +----------+   +---------+   +---------+ |
 * sink-+                                                                   |
 *  +-----------------------------------------------------------------------+
 */
static GstPlayChain *
gen_audio_chain (GstPlaySink * playsink, gboolean raw)
{
  GstPlayAudioChain *chain;
  GstBin *bin;
  gboolean res;
  GstPad *pad;

  chain = g_new0 (GstPlayAudioChain, 1);
  chain->chain.playsink = gst_object_ref (playsink);

  if (playsink->audio_sink) {
    chain->sink = playsink->audio_sink;
  } else {
    chain->sink = gst_element_factory_make ("autoaudiosink", "audiosink");
    if (chain->sink == NULL) {
      chain->sink = gst_element_factory_make ("alsasink", "audiosink");
    }
    if (chain->sink == NULL)
      goto no_sinks;
  }
  chain->chain.bin = gst_bin_new ("abin");
  bin = GST_BIN_CAST (chain->chain.bin);
  gst_object_ref (bin);
  gst_object_sink (bin);
  gst_bin_add (bin, chain->sink);

  if (raw) {
    chain->queue = gst_element_factory_make ("queue", "aqueue");
    gst_bin_add (bin, chain->queue);

    chain->conv = gst_element_factory_make ("audioconvert", "aconv");
    if (chain->conv == NULL)
      goto no_audioconvert;
    gst_bin_add (bin, chain->conv);

    chain->resample = gst_element_factory_make ("audioresample", "aresample");
    if (chain->resample == NULL)
      goto no_audioresample;
    gst_bin_add (bin, chain->resample);

    /* FIXME, remove queue */
    res = gst_element_link_pads (chain->queue, "src", chain->conv, "sink");
    res &= gst_element_link_pads (chain->conv, "src", chain->resample, "sink");

    /* FIXME check if the sink has the volume property */

    if (playsink->flags & GST_PLAY_FLAG_SOFT_VOLUME) {
      chain->volume = gst_element_factory_make ("volume", "volume");
      if (chain->volume == NULL)
        goto no_volume;

      /* volume also has the mute property */
      chain->mute = gst_object_ref (chain->volume);

      /* configure with the latest volume */
      g_object_set (G_OBJECT (chain->volume), "volume", playsink->volume, NULL);
      gst_bin_add (bin, chain->volume);

      res &=
          gst_element_link_pads (chain->resample, "src", chain->volume, "sink");
      res &= gst_element_link_pads (chain->volume, "src", chain->sink, NULL);
    } else {
      res &= gst_element_link_pads (chain->resample, "src", chain->sink, NULL);
    }
    if (!res)
      goto link_failed;

    pad = gst_element_get_pad (chain->queue, "sink");
  } else {
    pad = gst_element_get_pad (chain->sink, "sink");
  }
  chain->chain.sinkpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);
  gst_element_add_pad (chain->chain.bin, chain->chain.sinkpad);

  return (GstPlayChain *) chain;

  /* ERRORS */
no_sinks:
  {
    post_missing_element_message (playsink, "autoaudiosink");
    GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
        (_("Both autoaudiosink and alsasink elements are missing.")), (NULL));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
no_audioconvert:
  {
    post_missing_element_message (playsink, "audioconvert");
    GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "audioconvert"), ("possibly a liboil version mismatch?"));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
no_audioresample:
  {
    post_missing_element_message (playsink, "audioresample");
    GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "audioresample"), ("possibly a liboil version mismatch?"));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
no_volume:
  {
    post_missing_element_message (playsink, "volume");
    GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "volume"), ("possibly a liboil version mismatch?"));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
link_failed:
  {
    GST_ELEMENT_ERROR (playsink, CORE, PAD,
        (NULL), ("Failed to configure the audio sink."));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
}

/*
 *  +----------------------------------------------------+
 *  | visbin                                             |
 *  |      +----------+   +------------+   +-------+     |
 *  |      | visqueue |   | audioconv  |   |  vis  |     |
 *  |   +-sink       src-sink + samp  src-sink    src-+  |
 *  |   |  +----------+   +------------+   +-------+  |  |
 * sink-+                                             +-src
 *  +----------------------------------------------------+
 *           
 */
static GstPlayChain *
gen_vis_chain (GstPlaySink * playsink)
{
  GstPlayVisChain *chain;
  GstBin *bin;
  gboolean res;
  GstPad *pad;

  chain = g_new0 (GstPlayVisChain, 1);
  chain->chain.playsink = gst_object_ref (playsink);

  chain->chain.bin = gst_bin_new ("visbin");
  bin = GST_BIN_CAST (chain->chain.bin);
  gst_object_ref (bin);
  gst_object_sink (bin);

  chain->queue = gst_element_factory_make ("queue", "visqueue");
  gst_bin_add (bin, chain->queue);

  chain->conv = gst_element_factory_make ("audioconvert", "aconv");
  if (chain->conv == NULL)
    goto no_audioconvert;
  gst_bin_add (bin, chain->conv);

  chain->resample = gst_element_factory_make ("audioresample", "aresample");
  if (chain->resample == NULL)
    goto no_audioresample;
  gst_bin_add (bin, chain->resample);

  if (playsink->visualisation) {
    chain->vis = playsink->visualisation;
  } else {
    chain->vis = gst_element_factory_make ("goom", "vis");
    if (!chain->vis)
      goto no_goom;
  }
  gst_bin_add (bin, chain->vis);

  res = gst_element_link_pads (chain->queue, "src", chain->conv, "sink");
  res &= gst_element_link_pads (chain->conv, "src", chain->resample, "sink");
  res &= gst_element_link_pads (chain->resample, "src", chain->vis, "sink");
  if (!res)
    goto link_failed;

  pad = gst_element_get_pad (chain->queue, "sink");
  chain->chain.sinkpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);
  gst_element_add_pad (chain->chain.bin, chain->chain.sinkpad);

  pad = gst_element_get_pad (chain->vis, "src");
  chain->srcpad = gst_ghost_pad_new ("src", pad);
  gst_object_unref (pad);
  gst_element_add_pad (chain->chain.bin, chain->srcpad);

  return (GstPlayChain *) chain;

  /* ERRORS */
no_audioconvert:
  {
    post_missing_element_message (playsink, "audioconvert");
    GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "audioconvert"), ("possibly a liboil version mismatch?"));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
no_audioresample:
  {
    post_missing_element_message (playsink, "audioresample");
    GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "audioresample"), (NULL));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
no_goom:
  {
    post_missing_element_message (playsink, "goom");
    GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "goom"), (NULL));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
link_failed:
  {
    GST_ELEMENT_ERROR (playsink, CORE, PAD,
        (NULL), ("Failed to configure the visualisation element."));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
}

#if 0
static gboolean
activate_vis (GstPlaySink * playsink, gboolean activate)
{
  /* need to have an audio chain */
  if (!playsink->audiochain || !playsink->vischain)
    return FALSE;

  if (playsink->vischain->activated == activate)
    return TRUE;

  if (activate) {
    /* activation: Add the vis chain to the sink bin . Take a new srcpad from
     * the tee of the audio chain and link it to the sinkpad of the vis chain.
     */

  } else {
    /* deactivation: release the srcpad from the tee of the audio chain. Set the
     * vis chain to NULL and remove it from the sink bin */

  }
  return TRUE;
}
#endif

/* this function is called when all the request pads are requested and when we
 * have to construct the final pipeline.
 */
gboolean
gst_play_sink_reconfigure (GstPlaySink * playsink)
{
  GstPlayFlags flags;
  gboolean need_audio, need_video, need_vis;

  GST_DEBUG_OBJECT (playsink, "reconfiguring");

  /* assume we need nothing */
  need_audio = need_video = need_vis = FALSE;

  GST_PLAY_SINK_LOCK (playsink);
  GST_OBJECT_LOCK (playsink);
  /* get flags, there are protected with the object lock */
  flags = playsink->flags;
  GST_OBJECT_UNLOCK (playsink);

  /* figure out which components we need */
  if (flags & GST_PLAY_FLAG_VIDEO && playsink->video_pad) {
    /* we have video and we are requested to show it */
    need_video = TRUE;
  }
  if (playsink->audio_pad) {
    if (flags & GST_PLAY_FLAG_AUDIO) {
      need_audio = TRUE;
    }
    if (flags & GST_PLAY_FLAG_VIS && !need_video) {
      /* also add video when we add visualisation */
      need_video = TRUE;
      need_vis = TRUE;
    }
  }

  if (need_video) {
    GST_DEBUG_OBJECT (playsink, "adding video, raw %d",
        playsink->video_pad_raw);
    if (!playsink->videochain)
      playsink->videochain =
          gen_video_chain (playsink, need_vis ? TRUE : playsink->video_pad_raw,
          !need_vis);
    add_chain (playsink->videochain, TRUE);
    activate_chain (playsink->videochain, TRUE);
    if (!need_vis)
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->video_pad),
          playsink->videochain->sinkpad);
  } else {
    if (playsink->videochain) {
      add_chain (playsink->videochain, FALSE);
      activate_chain (playsink->videochain, FALSE);
    }
    if (playsink->video_pad)
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->video_pad), NULL);
  }

  if (need_audio) {
    GST_DEBUG_OBJECT (playsink, "adding audio");
    if (!playsink->audiochain)
      playsink->audiochain =
          gen_audio_chain (playsink, playsink->audio_pad_raw);
    add_chain (playsink->audiochain, TRUE);
    gst_pad_link (playsink->audio_tee_asrc, playsink->audiochain->sinkpad);
    activate_chain (playsink->audiochain, TRUE);
  } else {
    /* we have no audio or we are requested to not play audio */
    if (playsink->audiochain) {
      gst_pad_unlink (playsink->audio_tee_asrc, playsink->audiochain->sinkpad);
      add_chain (playsink->audiochain, FALSE);
      activate_chain (playsink->audiochain, FALSE);
    }
  }

  if (need_vis) {
    GstPad *srcpad;

    if (!playsink->vischain)
      playsink->vischain = gen_vis_chain (playsink);

    GST_DEBUG_OBJECT (playsink, "adding visualisation");

    srcpad =
        gst_element_get_pad (GST_ELEMENT_CAST (playsink->vischain->bin), "src");
    add_chain (playsink->vischain, TRUE);
    gst_pad_link (playsink->audio_tee_vissrc, playsink->vischain->sinkpad);
    gst_pad_link (srcpad, playsink->videochain->sinkpad);
    gst_object_unref (srcpad);
    activate_chain (playsink->vischain, TRUE);
  } else {
    if (playsink->vischain) {
      add_chain (playsink->vischain, FALSE);
      activate_chain (playsink->vischain, FALSE);
    }
  }
  GST_PLAY_SINK_UNLOCK (playsink);

  return TRUE;
}

gboolean
gst_play_sink_set_flags (GstPlaySink * playsink, GstPlayFlags flags)
{
  g_return_val_if_fail (GST_IS_PLAY_SINK (playsink), FALSE);

  GST_OBJECT_LOCK (playsink);
  playsink->flags = flags;
  GST_OBJECT_UNLOCK (playsink);

  return TRUE;
}

GstPlayFlags
gst_play_sink_get_flags (GstPlaySink * playsink)
{
  GstPlayFlags res;

  g_return_val_if_fail (GST_IS_PLAY_SINK (playsink), 0);

  GST_OBJECT_LOCK (playsink);
  res = playsink->flags;
  GST_OBJECT_UNLOCK (playsink);

  return res;
}


GstPad *
gst_play_sink_request_pad (GstPlaySink * playsink, GstPlaySinkType type)
{
  GstPad *res = NULL;
  gboolean created = FALSE;
  gboolean raw = FALSE;

  GST_PLAY_SINK_LOCK (playsink);
  switch (type) {
    case GST_PLAY_SINK_TYPE_AUDIO_RAW:
      raw = TRUE;
    case GST_PLAY_SINK_TYPE_AUDIO:
      if (!playsink->audio_tee) {
        /* create tee when needed. This element will feed the audio sink chain
         * and the vis chain. */
        playsink->audio_tee = gst_element_factory_make ("tee", "audiotee");
        playsink->audio_tee_sink =
            gst_element_get_pad (playsink->audio_tee, "sink");
        /* get two request pads */
        playsink->audio_tee_asrc =
            gst_element_get_request_pad (playsink->audio_tee, "src%d");
        playsink->audio_tee_vissrc =
            gst_element_get_request_pad (playsink->audio_tee, "src%d");
        gst_bin_add (GST_BIN_CAST (playsink), playsink->audio_tee);
        gst_element_set_state (playsink->audio_tee, GST_STATE_PAUSED);
      }
      if (!playsink->audio_pad) {
        playsink->audio_pad =
            gst_ghost_pad_new ("audio_sink", playsink->audio_tee_sink);
        created = TRUE;
      }
      playsink->audio_pad_raw = raw;
      res = playsink->audio_pad;
      break;
    case GST_PLAY_SINK_TYPE_VIDEO_RAW:
      raw = TRUE;
    case GST_PLAY_SINK_TYPE_VIDEO:
      if (!playsink->video_pad) {
        playsink->video_pad =
            gst_ghost_pad_new_no_target ("video_sink", GST_PAD_SINK);
        created = TRUE;
      }
      playsink->video_pad_raw = raw;
      res = playsink->video_pad;
      break;
    case GST_PLAY_SINK_TYPE_TEXT:
      if (!playsink->text_pad) {
        playsink->text_pad =
            gst_ghost_pad_new_no_target ("text_sink", GST_PAD_SINK);
        created = TRUE;
      }
      res = playsink->text_pad;
      break;
    default:
      res = NULL;
      break;
  }
  GST_PLAY_SINK_UNLOCK (playsink);

  if (created && res) {
    gst_pad_set_active (res, TRUE);
    gst_element_add_pad (GST_ELEMENT_CAST (playsink), res);
  }

  return res;
}

void
gst_play_sink_release_pad (GstPlaySink * playsink, GstPad * pad)
{
  GstPad **res = NULL;

  GST_PLAY_SINK_LOCK (playsink);
  if (pad == playsink->video_pad) {
    res = &playsink->video_pad;
  } else if (pad == playsink->audio_pad) {
    res = &playsink->audio_pad;
  } else if (pad == playsink->text_pad) {
    res = &playsink->text_pad;
  }
  GST_PLAY_SINK_UNLOCK (playsink);

  if (*res) {
    gst_pad_set_active (*res, FALSE);
    gst_element_remove_pad (GST_ELEMENT_CAST (playsink), *res);
    *res = NULL;
  }
}

/* Send an event to our sinks until one of them works; don't then send to the
 * remaining sinks (unlike GstBin)
 */
static gboolean
gst_play_sink_send_event_to_sink (GstPlaySink * playsink, GstEvent * event)
{
  gboolean res = TRUE;

  if (playsink->audiochain) {
    gst_event_ref (event);
    if ((res = gst_element_send_event (playsink->audiochain->bin, event))) {
      GST_DEBUG_OBJECT (playsink, "Sent event succesfully to audio sink");
      goto done;
    }
    GST_DEBUG_OBJECT (playsink, "Event failed when sent to audio sink");
  }
  if (playsink->videochain) {
    gst_event_ref (event);
    if ((res = gst_element_send_event (playsink->videochain->bin, event))) {
      GST_DEBUG_OBJECT (playsink, "Sent event succesfully to video sink");
      goto done;
    }
    GST_DEBUG_OBJECT (playsink, "Event failed when sent to video sink");
  }
done:
  gst_event_unref (event);
  return res;
}

/* We only want to send the event to a single sink (overriding GstBin's
 * behaviour), but we want to keep GstPipeline's behaviour - wrapping seek
 * events appropriately. So, this is a messy duplication of code. */
static gboolean
gst_play_sink_send_event (GstElement * element, GstEvent * event)
{
  gboolean res = FALSE;
  GstEventType event_type = GST_EVENT_TYPE (event);

  switch (event_type) {
    case GST_EVENT_SEEK:
      GST_DEBUG_OBJECT (element, "Sending seek event to a sink");
      res = gst_play_sink_send_event_to_sink (GST_PLAY_SINK (element), event);
      break;
    default:
      res = parent_class->send_event (element, event);
      break;
  }
  return res;
}

static GstStateChangeReturn
gst_play_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstPlaySink *playsink;

  playsink = GST_PLAY_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* FIXME Release audio device when we implement that */
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* remove sinks we added */
      if (playsink->videochain) {
        activate_chain (playsink->videochain, FALSE);
        add_chain (playsink->videochain, FALSE);
      }
      if (playsink->audiochain) {
        activate_chain (playsink->audiochain, FALSE);
        add_chain (playsink->audiochain, FALSE);
      }
      break;
    default:
      break;
  }

  return ret;
}
