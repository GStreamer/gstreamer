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

#define GST_PLAY_CHAIN(c) (GstPlayChain *)(c)

/* holds the common data fields for the audio and video pipelines. We keep them
 * in a structure to more easily have all the info available. */
typedef struct
{
  GstPlaySink *playsink;
  GstElement *bin;
  gboolean added;
  gboolean activated;
} GstPlayChain;

typedef struct
{
  GstPlayChain chain;
  GstPad *sinkpad;
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
  GstPad *sinkpad;
  GstElement *queue;
  GstElement *conv;
  GstElement *scale;
  GstElement *sink;
  gboolean async;
} GstPlayVideoChain;

typedef struct
{
  GstPlayChain chain;
  GstPad *sinkpad;
  GstElement *queue;
  GstElement *conv;
  GstElement *resample;
  GstPad *blockpad;             /* srcpad of resample, used for switching the vis */
  GstPad *vissinkpad;           /* visualisation sinkpad, */
  GstElement *vis;
  GstPad *vissrcpad;            /* visualisation srcpad, */
  GstPad *srcpad;               /* outgoing srcpad, used to connect to the next
                                 * chain */
} GstPlayVisChain;

typedef struct
{
  GstPlayChain chain;
  GstPad *sinkpad;
  GstElement *conv;
  GstElement *overlay;
  GstPad *videosinkpad;
  GstPad *textsinkpad;
  GstPad *srcpad;               /* outgoing srcpad, used to connect to the next
                                 * chain */
} GstPlayTextChain;

#define GST_PLAY_SINK_GET_LOCK(playsink) (((GstPlaySink *)playsink)->lock)
#define GST_PLAY_SINK_LOCK(playsink)     g_mutex_lock (GST_PLAY_SINK_GET_LOCK (playsink))
#define GST_PLAY_SINK_UNLOCK(playsink)   g_mutex_unlock (GST_PLAY_SINK_GET_LOCK (playsink))

struct _GstPlaySink
{
  GstBin bin;

  GMutex *lock;

  GstPlayFlags flags;

  GstPlayAudioChain *audiochain;
  GstPlayVideoChain *videochain;
  GstPlayVisChain *vischain;
  GstPlayTextChain *textchain;

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

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_play_sink_dispose);
  gobject_klass->finalize = GST_DEBUG_FUNCPTR (gst_play_sink_finalize);

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

void
gst_play_sink_set_video_sink (GstPlaySink * playsink, GstElement * sink)
{
  GST_PLAY_SINK_LOCK (playsink);
  if (playsink->video_sink)
    gst_object_unref (playsink->video_sink);

  if (sink) {
    gst_object_ref (sink);
    gst_object_sink (sink);
  }
  playsink->video_sink = sink;
  GST_PLAY_SINK_UNLOCK (playsink);
}

GstElement *
gst_play_sink_get_video_sink (GstPlaySink * playsink)
{
  GstElement *result = NULL;
  GstPlayVideoChain *chain;

  GST_PLAY_SINK_LOCK (playsink);
  if ((chain = (GstPlayVideoChain *) playsink->videochain)) {
    /* we have an active chain, get the sink */
    if (chain->sink)
      result = gst_object_ref (chain->sink);
  }
  /* nothing found, return last configured sink */
  if (result == NULL && playsink->video_sink)
    result = gst_object_ref (playsink->video_sink);
  GST_PLAY_SINK_UNLOCK (playsink);

  return result;
}

void
gst_play_sink_set_audio_sink (GstPlaySink * playsink, GstElement * sink)
{
  GST_PLAY_SINK_LOCK (playsink);
  if (playsink->audio_sink)
    gst_object_unref (playsink->audio_sink);

  if (sink) {
    gst_object_ref (sink);
    gst_object_sink (sink);
  }
  playsink->audio_sink = sink;
  GST_PLAY_SINK_UNLOCK (playsink);
}

GstElement *
gst_play_sink_get_audio_sink (GstPlaySink * playsink)
{
  GstElement *result = NULL;
  GstPlayAudioChain *chain;

  GST_PLAY_SINK_LOCK (playsink);
  if ((chain = (GstPlayAudioChain *) playsink->audiochain)) {
    /* we have an active chain, get the sink */
    if (chain->sink)
      result = gst_object_ref (chain->sink);
  }
  /* nothing found, return last configured sink */
  if (result == NULL && playsink->audio_sink)
    result = gst_object_ref (playsink->audio_sink);
  GST_PLAY_SINK_UNLOCK (playsink);

  return result;
}

static void
gst_play_sink_vis_unblocked (GstPad * tee_pad, gboolean blocked,
    gpointer user_data)
{
  GstPlaySink *playsink;

  playsink = GST_PLAY_SINK (user_data);
  /* nothing to do here, we need a dummy callback here to make the async call
   * non-blocking. */
  GST_DEBUG_OBJECT (playsink, "vis pad unblocked");
}

static void
gst_play_sink_vis_blocked (GstPad * tee_pad, gboolean blocked,
    gpointer user_data)
{
  GstPlaySink *playsink;
  GstPlayVisChain *chain;

  playsink = GST_PLAY_SINK (user_data);

  GST_PLAY_SINK_LOCK (playsink);
  GST_DEBUG_OBJECT (playsink, "vis pad blocked");
  /* now try to change the plugin in the running vis chain */
  if (!(chain = (GstPlayVisChain *) playsink->vischain))
    goto done;

  /* unlink the old plugin and unghost the pad */
  gst_pad_unlink (chain->blockpad, chain->vissinkpad);
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (chain->srcpad), NULL);

  /* set the old plugin to NULL and remove */
  gst_element_set_state (chain->vis, GST_STATE_NULL);
  gst_bin_remove (GST_BIN_CAST (chain->chain.bin), chain->vis);

  /* add new plugin and set state to playing */
  chain->vis = gst_object_ref (playsink->visualisation);
  gst_bin_add (GST_BIN_CAST (chain->chain.bin), chain->vis);
  gst_element_set_state (chain->vis, GST_STATE_PLAYING);

  /* get pads */
  chain->vissinkpad = gst_element_get_static_pad (chain->vis, "sink");
  chain->vissrcpad = gst_element_get_static_pad (chain->vis, "src");

  /* link pads */
  gst_pad_link (chain->blockpad, chain->vissinkpad);
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (chain->srcpad),
      chain->vissrcpad);

done:
  /* Unblock the pad */
  gst_pad_set_blocked_async (tee_pad, FALSE, gst_play_sink_vis_unblocked,
      playsink);
  GST_PLAY_SINK_UNLOCK (playsink);
}

void
gst_play_sink_set_vis_plugin (GstPlaySink * playsink, GstElement * vis)
{
  GstPlayVisChain *chain;

  /* setting NULL means creating the default vis plugin */
  if (vis == NULL)
    vis = gst_element_factory_make ("goom", "vis");

  /* simply return if we don't have a vis plugin here */
  if (vis == NULL)
    return;

  GST_PLAY_SINK_LOCK (playsink);
  /* first store the new vis */
  if (playsink->visualisation)
    gst_object_unref (playsink->visualisation);
  playsink->visualisation = gst_object_ref (vis);

  /* now try to change the plugin in the running vis chain, if we have no chain,
   * we don't bother, any future vis chain will be created with the new vis
   * plugin. */
  if (!(chain = (GstPlayVisChain *) playsink->vischain))
    goto done;

  /* block the pad, the next time the callback is called we can change the
   * visualisation. It's possible that this never happens or that the pad was
   * already blocked. If the callback never happens, we don't have new data so
   * we don't need the new vis plugin. If the pad was already blocked, the
   * function returns FALSE but the previous pad block will do the right thing
   * anyway. */
  GST_DEBUG_OBJECT (playsink, "blocking vis pad");
  gst_pad_set_blocked_async (chain->blockpad, TRUE, gst_play_sink_vis_blocked,
      playsink);
done:
  GST_PLAY_SINK_UNLOCK (playsink);

  return;
}

GstElement *
gst_play_sink_get_vis_plugin (GstPlaySink * playsink)
{
  GstElement *result = NULL;
  GstPlayVisChain *chain;

  GST_PLAY_SINK_LOCK (playsink);
  if ((chain = (GstPlayVisChain *) playsink->vischain)) {
    /* we have an active chain, get the sink */
    if (chain->vis)
      result = gst_object_ref (chain->vis);
  }
  /* nothing found, return last configured sink */
  if (result == NULL && playsink->visualisation)
    result = gst_object_ref (playsink->visualisation);
  GST_PLAY_SINK_UNLOCK (playsink);

  return result;
}

void
gst_play_sink_set_volume (GstPlaySink * playsink, gdouble volume)
{
  GstPlayAudioChain *chain;

  GST_PLAY_SINK_LOCK (playsink);
  playsink->volume = volume;
  chain = (GstPlayAudioChain *) playsink->audiochain;
  if (chain && chain->volume) {
    /* if there is a mute element or we are not muted, set the volume */
    if (chain->mute || !playsink->mute)
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
  result = playsink->volume;
  if (chain && chain->volume) {
    if (chain->mute || !playsink->mute) {
      g_object_get (chain->volume, "volume", &result, NULL);
      playsink->volume = result;
    }
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
  if (chain) {
    if (chain->mute) {
      g_object_set (chain->mute, "mute", mute, NULL);
    } else if (chain->volume) {
      if (mute)
        g_object_set (chain->volume, "volume", (gdouble) 0.0, NULL);
      else
        g_object_set (chain->volume, "volume", (gdouble) playsink->volume,
            NULL);
    }
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

static gint
find_property (GstElement * element, const gchar * name)
{
  gint res;

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (element), name)) {
    res = 0;
    GST_DEBUG_OBJECT (element, "found %s property", name);
  } else {
    GST_DEBUG_OBJECT (element, "did not find %s property", name);
    res = 1;
    gst_object_unref (element);
  }
  return res;
}

/* find an object in the hierarchy with a property named @name */
static GstElement *
gst_play_sink_find_property (GstPlaySink * playsink, GstElement * obj,
    const gchar * name)
{
  GstElement *result = NULL;
  GstIterator *it;

  if (GST_IS_BIN (obj)) {
    it = gst_bin_iterate_recurse (GST_BIN_CAST (obj));
    result = gst_iterator_find_custom (it,
        (GCompareFunc) find_property, (gpointer) name);
    gst_iterator_free (it);
  } else {
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (obj), name)) {
      result = obj;
      gst_object_ref (obj);
    }
  }
  return result;
}

/* make the element (bin) that contains the elements needed to perform
 * video display. 
 *
 *  +------------------------------------------------------------+
 *  | vbin                                                       |
 *  |      +-------+   +----------+   +----------+   +---------+ |
 *  |      | queue |   |colorspace|   |videoscale|   |videosink| |
 *  |   +-sink    src-sink       src-sink       src-sink       | |
 *  |   |  +-------+   +----------+   +----------+   +---------+ |
 * sink-+                                                        |
 *  +------------------------------------------------------------+
 *           
 */
static GstPlayVideoChain *
gen_video_chain (GstPlaySink * playsink, gboolean raw, gboolean async)
{
  GstPlayVideoChain *chain;
  GstBin *bin;
  GstPad *pad;

  chain = g_new0 (GstPlayVideoChain, 1);
  chain->chain.playsink = gst_object_ref (playsink);

  GST_DEBUG_OBJECT (playsink, "making video chain %p", chain);

  if (playsink->video_sink) {
    chain->sink = gst_object_ref (playsink->video_sink);
  } else {
    chain->sink = gst_element_factory_make ("autovideosink", "videosink");
    if (chain->sink == NULL) {
      chain->sink = gst_element_factory_make ("xvimagesink", "videosink");
    }
    if (chain->sink == NULL)
      goto no_sinks;
  }

  /* if we can disable async behaviour of the sink, we can avoid adding a
   * queue for the audio chain. We can't use the deep property here because the
   * sink might change it's internal sink element later. */
  if (g_object_class_find_property (G_OBJECT_GET_CLASS (chain->sink), "async")) {
    GST_DEBUG_OBJECT (playsink, "setting async property to %d on video sink",
        async);
    g_object_set (chain->sink, "async", async, NULL);
    chain->async = async;
  } else
    chain->async = TRUE;

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

    pad = gst_element_get_static_pad (chain->queue, "sink");
  } else {
    if (!gst_element_link_pads (chain->queue, "src", chain->sink, NULL))
      goto link_failed;
    pad = gst_element_get_static_pad (chain->queue, "sink");
  }

  chain->sinkpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);
  gst_element_add_pad (chain->chain.bin, chain->sinkpad);

  return chain;

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

/* make an element for playback of video with subtitles embedded.
 *
 *  +----------------------------------------------+
 *  | tbin                  +-------------+        |
 *  |          +-----+      | textoverlay |        |
 *  |          | csp | +--video_sink      |        |
 * video_sink-sink  src+ +-text_sink     src--+    |
 *  |          +-----+   |  +-------------+   +-- src   
 * text_sink-------------+                         |
 *  +----------------------------------------------+
 */
static GstPlayTextChain *
gen_text_chain (GstPlaySink * playsink)
{
  GstPlayTextChain *chain;
  GstBin *bin;
  GstPad *pad;

  chain = g_new0 (GstPlayTextChain, 1);
  chain->chain.playsink = gst_object_ref (playsink);

  GST_DEBUG_OBJECT (playsink, "making text chain %p", chain);

  chain->chain.bin = gst_bin_new ("tbin");
  bin = GST_BIN_CAST (chain->chain.bin);
  gst_object_ref (bin);
  gst_object_sink (bin);

  chain->conv = gst_element_factory_make ("ffmpegcolorspace", "tconv");
  if (chain->conv == NULL)
    goto no_colorspace;
  gst_bin_add (bin, chain->conv);

  chain->overlay = gst_element_factory_make ("textoverlay", "overlay");
  if (chain->overlay == NULL)
    goto no_overlay;
  gst_bin_add (bin, chain->overlay);

  /* Set some parameters */
  g_object_set (G_OBJECT (chain->overlay),
      "halign", "center", "valign", "bottom", NULL);
  if (playsink->font_desc) {
    g_object_set (G_OBJECT (chain->overlay), "font-desc", playsink->font_desc,
        NULL);
  }
  g_object_set (G_OBJECT (chain->overlay), "wait-text", FALSE, NULL);

  /* Link */
  gst_element_link_pads (chain->conv, "src", chain->overlay, "video_sink");

  /* Add ghost pads on the subtitle bin */
  pad = gst_element_get_static_pad (chain->overlay, "text_sink");
  chain->textsinkpad = gst_ghost_pad_new ("text_sink", pad);
  gst_object_unref (pad);
  gst_element_add_pad (chain->chain.bin, chain->textsinkpad);

  pad = gst_element_get_static_pad (chain->conv, "sink");
  chain->videosinkpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);
  gst_element_add_pad (chain->chain.bin, chain->videosinkpad);

  pad = gst_element_get_static_pad (chain->overlay, "src");
  chain->srcpad = gst_ghost_pad_new ("src", pad);
  gst_object_unref (pad);
  gst_element_add_pad (chain->chain.bin, chain->srcpad);

  return chain;

  /* ERRORS */
no_colorspace:
  {
    post_missing_element_message (playsink, "ffmpegcolorspace");
    GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "ffmpegcolorspace"), (NULL));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
no_overlay:
  {
    post_missing_element_message (playsink, "textoverlay");
    GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "textoverlay"), (NULL));
    free_chain ((GstPlayChain *) chain);
    return NULL;
  }
}

/* make the chain that contains the elements needed to perform
 * audio playback. 
 *
 * We add a tee as the first element so that we can link the visualisation chain
 * to it when requested.
 *
 *  +-------------------------------------------------------------+
 *  | abin                                                        |
 *  |      +---------+   +----------+   +---------+   +---------+ |
 *  |      |audioconv|   |audioscale|   | volume  |   |audiosink| |
 *  |   +-srck      src-sink       src-sink      src-sink       | |
 *  |   |  +---------+   +----------+   +---------+   +---------+ |
 * sink-+                                                         |
 *  +-------------------------------------------------------------+
 */
static GstPlayAudioChain *
gen_audio_chain (GstPlaySink * playsink, gboolean raw, gboolean queue)
{
  GstPlayAudioChain *chain;
  GstBin *bin;
  gboolean res, have_volume;
  GstPad *pad;

  chain = g_new0 (GstPlayAudioChain, 1);
  chain->chain.playsink = gst_object_ref (playsink);

  GST_DEBUG_OBJECT (playsink, "making audio chain %p", chain);

  if (playsink->audio_sink) {
    chain->sink = gst_object_ref (playsink->audio_sink);
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

  if (queue) {
    /* we have to add a queue when we need to decouple for the video sink in
     * visualisations */
    GST_DEBUG_OBJECT (playsink, "adding audio queue");
    chain->queue = gst_element_factory_make ("queue", "aqueue");
    gst_bin_add (bin, chain->queue);
  }

  /* check if the sink has the volume property, if it does we don't need to
   * add a volume element. */
  if (g_object_class_find_property (G_OBJECT_GET_CLASS (chain->sink), "volume")) {
    GST_DEBUG_OBJECT (playsink, "the sink has a volume property");
    have_volume = TRUE;
    /* take ref to sink to control the volume */
    chain->volume = gst_object_ref (chain->sink);
    g_object_set (G_OBJECT (chain->volume), "volume", playsink->volume, NULL);
    /* if the sink also has a mute property we can use this as well. We'll only
     * use the mute property if there is a volume property. We can simulate the
     * mute with the volume otherwise. */
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (chain->sink), "mute")) {
      GST_DEBUG_OBJECT (playsink, "the sink has a mute property");
      chain->mute = gst_object_ref (chain->sink);
    }
  } else {
    /* no volume, we need to add a volume element when we can */
    GST_DEBUG_OBJECT (playsink, "the sink has no volume property");
    have_volume = FALSE;
  }

  if (raw) {
    chain->conv = gst_element_factory_make ("audioconvert", "aconv");
    if (chain->conv == NULL)
      goto no_audioconvert;
    gst_bin_add (bin, chain->conv);

    chain->resample = gst_element_factory_make ("audioresample", "aresample");
    if (chain->resample == NULL)
      goto no_audioresample;
    gst_bin_add (bin, chain->resample);

    res = gst_element_link_pads (chain->conv, "src", chain->resample, "sink");

    if (!have_volume && playsink->flags & GST_PLAY_FLAG_SOFT_VOLUME) {
      chain->volume = gst_element_factory_make ("volume", "volume");
      if (chain->volume == NULL)
        goto no_volume;

      have_volume = TRUE;

      /* volume also has the mute property */
      chain->mute = gst_object_ref (chain->volume);

      /* configure with the latest volume and mute */
      g_object_set (G_OBJECT (chain->volume), "volume", playsink->volume, NULL);
      g_object_set (G_OBJECT (chain->mute), "mute", playsink->mute, NULL);
      gst_bin_add (bin, chain->volume);

      res &=
          gst_element_link_pads (chain->resample, "src", chain->volume, "sink");
      res &= gst_element_link_pads (chain->volume, "src", chain->sink, NULL);
    } else {
      res &= gst_element_link_pads (chain->resample, "src", chain->sink, NULL);
    }
    if (!res)
      goto link_failed;

    if (queue) {
      res = gst_element_link_pads (chain->queue, "src", chain->conv, "sink");
      pad = gst_element_get_static_pad (chain->queue, "sink");
    } else {
      pad = gst_element_get_static_pad (chain->conv, "sink");
    }
  } else {
    if (queue) {
      res = gst_element_link_pads (chain->queue, "src", chain->sink, "sink");
      pad = gst_element_get_static_pad (chain->queue, "sink");
    } else {
      pad = gst_element_get_static_pad (chain->sink, "sink");
    }
  }
  /* post a warning if we have no way to configure the volume */
  if (!have_volume) {
    GST_ELEMENT_ERROR (playsink, STREAM, NOT_IMPLEMENTED,
        (_("No volume control found")), ("No volume control found"));
  }
  chain->sinkpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);
  gst_element_add_pad (chain->chain.bin, chain->sinkpad);

  return chain;

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
 *  +-------------------------------------------------------------------+
 *  | visbin                                                            |
 *  |      +----------+   +------------+   +----------+   +-------+     |
 *  |      | visqueue |   | audioconv  |   | audiores |   |  vis  |     |
 *  |   +-sink       src-sink + samp  src-sink       src-sink    src-+  |
 *  |   |  +----------+   +------------+   +----------+   +-------+  |  |
 * sink-+                                                            +-src
 *  +-------------------------------------------------------------------+
 *           
 */
static GstPlayVisChain *
gen_vis_chain (GstPlaySink * playsink)
{
  GstPlayVisChain *chain;
  GstBin *bin;
  gboolean res;
  GstPad *pad;

  chain = g_new0 (GstPlayVisChain, 1);
  chain->chain.playsink = gst_object_ref (playsink);

  GST_DEBUG_OBJECT (playsink, "making vis chain %p", chain);

  chain->chain.bin = gst_bin_new ("visbin");
  bin = GST_BIN_CAST (chain->chain.bin);
  gst_object_ref (bin);
  gst_object_sink (bin);

  /* we're queuing raw audio here, we can remove this queue when we can disable
   * async behaviour in the video sink. */
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

  /* this pad will be used for blocking the dataflow and switching the vis
   * plugin */
  chain->blockpad = gst_element_get_static_pad (chain->resample, "src");

  if (playsink->visualisation) {
    chain->vis = gst_object_ref (playsink->visualisation);
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

  chain->vissinkpad = gst_element_get_static_pad (chain->vis, "sink");
  chain->vissrcpad = gst_element_get_static_pad (chain->vis, "src");

  pad = gst_element_get_static_pad (chain->queue, "sink");
  chain->sinkpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);
  gst_element_add_pad (chain->chain.bin, chain->sinkpad);

  chain->srcpad = gst_ghost_pad_new ("src", chain->vissrcpad);
  gst_element_add_pad (chain->chain.bin, chain->srcpad);

  return chain;

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

/* this function is called when all the request pads are requested and when we
 * have to construct the final pipeline. Based on the flags we construct the
 * final output pipelines.
 */
gboolean
gst_play_sink_reconfigure (GstPlaySink * playsink)
{
  GstPlayFlags flags;
  gboolean need_audio, need_video, need_vis, need_text;

  GST_DEBUG_OBJECT (playsink, "reconfiguring");

  /* assume we need nothing */
  need_audio = need_video = need_vis = need_text = FALSE;

  GST_PLAY_SINK_LOCK (playsink);
  GST_OBJECT_LOCK (playsink);
  /* get flags, there are protected with the object lock */
  flags = playsink->flags;
  GST_OBJECT_UNLOCK (playsink);

  /* figure out which components we need */
  if (flags & GST_PLAY_FLAG_TEXT && playsink->text_pad) {
    /* we need video too */
    if (!playsink->video_pad)
      goto subs_but_no_video;

    /* we have subtitles and we are requested to show it, we also need to show
     * video in this case. */
    need_video = TRUE;
    need_text = TRUE;
  } else if (flags & GST_PLAY_FLAG_VIDEO && playsink->video_pad) {
    /* we have video and we are requested to show it */
    need_video = TRUE;
  }
  if (playsink->audio_pad) {
    if (flags & GST_PLAY_FLAG_AUDIO) {
      need_audio = TRUE;
    }
    if (playsink->audio_pad_raw) {
      /* only can do vis with raw uncompressed audio */
      if (flags & GST_PLAY_FLAG_VIS && !need_video) {
        /* also add video when we add visualisation */
        need_video = TRUE;
        need_vis = TRUE;
      }
    }
  }

  /* set up video pipeline */
  if (need_video) {
    GST_DEBUG_OBJECT (playsink, "adding video, raw %d",
        playsink->video_pad_raw);
    if (!playsink->videochain) {
      gboolean raw, async;

      /* we need a raw sink when we do vis or when we have a raw pad */
      raw = need_vis ? TRUE : playsink->video_pad_raw;
      /* we try to set the sink async=FALSE when we need vis, this way we can
       * avoid a queue in the audio chain. */
      async = !need_vis;

      playsink->videochain = gen_video_chain (playsink, raw, async);
    }
    add_chain (GST_PLAY_CHAIN (playsink->videochain), TRUE);
    activate_chain (GST_PLAY_CHAIN (playsink->videochain), TRUE);
    /* if we are not part of vis or subtitles, set the ghostpad target */
    if (!need_vis && !need_text) {
      GST_DEBUG_OBJECT (playsink, "ghosting video sinkpad");
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->video_pad),
          playsink->videochain->sinkpad);
    }
  } else {
    if (playsink->videochain) {
      add_chain (GST_PLAY_CHAIN (playsink->videochain), FALSE);
      activate_chain (GST_PLAY_CHAIN (playsink->videochain), FALSE);
    }
    if (playsink->video_pad)
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->video_pad), NULL);
  }

  if (need_text) {
    GST_DEBUG_OBJECT (playsink, "adding text");
    if (!playsink->textchain) {
      playsink->textchain = gen_text_chain (playsink);
    }
    add_chain (GST_PLAY_CHAIN (playsink->textchain), TRUE);
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->text_pad),
        playsink->textchain->textsinkpad);
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->video_pad),
        playsink->textchain->videosinkpad);
    gst_pad_link (playsink->textchain->srcpad, playsink->videochain->sinkpad);
    activate_chain (GST_PLAY_CHAIN (playsink->textchain), TRUE);
  } else {
    /* we have no subtitles/text or we are requested to not show them */
    if (playsink->textchain) {
      add_chain (GST_PLAY_CHAIN (playsink->textchain), FALSE);
      activate_chain (GST_PLAY_CHAIN (playsink->textchain), FALSE);
    }
    if (!need_video && playsink->video_pad)
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->video_pad), NULL);
    if (playsink->text_pad)
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->text_pad), NULL);
  }

  if (need_audio) {
    GST_DEBUG_OBJECT (playsink, "adding audio");
    if (!playsink->audiochain) {
      gboolean raw, queue;

      /* get a raw sink if we are asked for a raw pad */
      raw = playsink->audio_pad_raw;
      if (need_vis) {
        /* If we are dealing with visualisations, we need to add a queue to
         * decouple the audio from the video part. We only have to do this when
         * the video part is async=true */
        queue = ((GstPlayVideoChain *) playsink->videochain)->async;
        GST_DEBUG_OBJECT (playsink, "need audio queue for vis: %d", queue);
      } else {
        /* no vis, we can avoid a queue */
        GST_DEBUG_OBJECT (playsink, "don't need audio queue");
        queue = FALSE;
      }

      playsink->audiochain = gen_audio_chain (playsink, raw, queue);
    }
    add_chain (GST_PLAY_CHAIN (playsink->audiochain), TRUE);
    gst_pad_link (playsink->audio_tee_asrc, playsink->audiochain->sinkpad);
    activate_chain (GST_PLAY_CHAIN (playsink->audiochain), TRUE);
  } else {
    /* we have no audio or we are requested to not play audio */
    if (playsink->audiochain) {
      gst_pad_unlink (playsink->audio_tee_asrc, playsink->audiochain->sinkpad);
      add_chain (GST_PLAY_CHAIN (playsink->audiochain), FALSE);
      activate_chain (GST_PLAY_CHAIN (playsink->audiochain), FALSE);
    }
  }

  if (need_vis) {
    GstPad *srcpad;

    if (!playsink->vischain)
      playsink->vischain = gen_vis_chain (playsink);

    GST_DEBUG_OBJECT (playsink, "adding visualisation");

    srcpad =
        gst_element_get_static_pad (GST_ELEMENT_CAST (playsink->vischain->chain.
            bin), "src");
    add_chain (GST_PLAY_CHAIN (playsink->vischain), TRUE);
    gst_pad_link (playsink->audio_tee_vissrc, playsink->vischain->sinkpad);
    gst_pad_link (srcpad, playsink->videochain->sinkpad);
    gst_object_unref (srcpad);
    activate_chain (GST_PLAY_CHAIN (playsink->vischain), TRUE);
  } else {
    if (playsink->vischain) {
      add_chain (GST_PLAY_CHAIN (playsink->vischain), FALSE);
      activate_chain (GST_PLAY_CHAIN (playsink->vischain), FALSE);
    }
  }
  GST_PLAY_SINK_UNLOCK (playsink);

  return TRUE;

  /* ERRORS */
subs_but_no_video:
  {
    GST_ELEMENT_ERROR (playsink, STREAM, FORMAT,
        (_("Can't play a text file without video.")),
        ("Have text pad but no video pad"));
    GST_PLAY_SINK_UNLOCK (playsink);
    return FALSE;
  }
}

/**
 * gst_play_sink_set_flags:
 * @playsink: a #GstPlaySink
 * @flags: #GstPlayFlags
 *
 * Configure @flags on @playsink. The flags control the behaviour of @playsink
 * when constructing the sink pipelins.
 *
 * Returns: TRUE if the flags could be configured.
 */
gboolean
gst_play_sink_set_flags (GstPlaySink * playsink, GstPlayFlags flags)
{
  g_return_val_if_fail (GST_IS_PLAY_SINK (playsink), FALSE);

  GST_OBJECT_LOCK (playsink);
  playsink->flags = flags;
  GST_OBJECT_UNLOCK (playsink);

  return TRUE;
}

/**
 * gst_play_sink_get_flags:
 * @playsink: a #GstPlaySink
 *
 * Get the flags of @playsink. That flags control the behaviour of the sink when
 * it constructs the sink pipelines.
 *
 * Returns: the currently configured #GstPlayFlags.
 */
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

void
gst_play_sink_set_font_desc (GstPlaySink * playsink, const gchar * desc)
{
  GstPlayTextChain *chain;

  GST_PLAY_SINK_LOCK (playsink);
  chain = (GstPlayTextChain *) playsink->textchain;
  g_free (playsink->font_desc);
  playsink->font_desc = g_strdup (desc);
  if (chain && chain->overlay) {
    g_object_set (chain->overlay, "font-desc", desc, NULL);
  }
  GST_PLAY_SINK_UNLOCK (playsink);
}

gchar *
gst_play_sink_get_font_desc (GstPlaySink * playsink)
{
  gchar *result = NULL;
  GstPlayTextChain *chain;

  GST_PLAY_SINK_LOCK (playsink);
  chain = (GstPlayTextChain *) playsink->textchain;
  if (chain && chain->overlay) {
    g_object_get (chain->overlay, "font-desc", &result, NULL);
    playsink->font_desc = g_strdup (result);
  } else {
    result = g_strdup (playsink->font_desc);
  }
  GST_PLAY_SINK_UNLOCK (playsink);

  return result;
}

/**
 * gst_play_sink_get_last_frame:
 * @playsink: a #GstPlaySink
 *
 * Get the last displayed frame from @playsink. This frame is in the native
 * format of the sink element, the caps on the result buffer contain the format
 * of the frame data.
 *
 * Returns: a #GstBuffer with the frame data or %NULL when no video frame is
 * available.
 */
GstBuffer *
gst_play_sink_get_last_frame (GstPlaySink * playsink)
{
  GstBuffer *result = NULL;
  GstPlayVideoChain *chain;

  GST_PLAY_SINK_LOCK (playsink);
  GST_DEBUG_OBJECT (playsink, "taking last frame");
  /* get the video chain if we can */
  if ((chain = (GstPlayVideoChain *) playsink->videochain)) {
    GST_DEBUG_OBJECT (playsink, "found video chain");
    /* see if the chain is active */
    if (chain->chain.activated && chain->sink) {
      GstElement *elem;

      GST_DEBUG_OBJECT (playsink, "video chain active and has a sink");

      /* find and get the last-buffer property now */
      if ((elem =
              gst_play_sink_find_property (playsink, chain->sink,
                  "last-buffer"))) {
        GST_DEBUG_OBJECT (playsink, "getting last-buffer property");
        g_object_get (elem, "last-buffer", &result, NULL);
        gst_object_unref (elem);
      }
    }
  }
  GST_PLAY_SINK_UNLOCK (playsink);

  return result;
}

/**
 * gst_play_sink_request_pad
 * @playsink: a #GstPlaySink
 * @type: a #GstPlaySinkType
 *
 * Create or return a pad of @type.
 *
 * Returns: a #GstPad of @type or %NULL when the pad could not be created.
 */
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
            gst_element_get_static_pad (playsink->audio_tee, "sink");
        /* get two request pads */
        playsink->audio_tee_vissrc =
            gst_element_get_request_pad (playsink->audio_tee, "src%d");
        playsink->audio_tee_asrc =
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

  if (playsink->videochain) {
    gst_event_ref (event);
    if ((res = gst_element_send_event (playsink->videochain->chain.bin, event))) {
      GST_DEBUG_OBJECT (playsink, "Sent event succesfully to video sink");
      goto done;
    }
    GST_DEBUG_OBJECT (playsink, "Event failed when sent to video sink");
  }
  if (playsink->audiochain) {
    gst_event_ref (event);
    if ((res = gst_element_send_event (playsink->audiochain->chain.bin, event))) {
      GST_DEBUG_OBJECT (playsink, "Sent event succesfully to audio sink");
      goto done;
    }
    GST_DEBUG_OBJECT (playsink, "Event failed when sent to audio sink");
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
        activate_chain (GST_PLAY_CHAIN (playsink->videochain), FALSE);
        add_chain (GST_PLAY_CHAIN (playsink->videochain), FALSE);
      }
      if (playsink->audiochain) {
        activate_chain (GST_PLAY_CHAIN (playsink->audiochain), FALSE);
        add_chain (GST_PLAY_CHAIN (playsink->audiochain), FALSE);
      }
      break;
    default:
      break;
  }

  return ret;
}
