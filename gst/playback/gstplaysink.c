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

#define DEFAULT_FLAGS             GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_TEXT | \
                                  GST_PLAY_FLAG_SOFT_VOLUME

#define GST_PLAY_CHAIN(c) ((GstPlayChain *)(c))

/* holds the common data fields for the audio and video pipelines. We keep them
 * in a structure to more easily have all the info available. */
typedef struct
{
  GstPlaySink *playsink;
  GstElement *bin;
  gboolean added;
  gboolean activated;
  gboolean raw;
} GstPlayChain;

typedef struct
{
  GstPlayChain chain;
  GstPad *sinkpad;
  GstElement *queue;
  GstElement *conv;
  GstElement *resample;
  GstElement *volume;           /* element with the volume property */
  gboolean sink_volume;         /* if the volume was provided by the sink */
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
  GstElement *sink;             /* custom sink to receive subtitle buffers */
} GstPlayTextChain;

typedef struct
{
  GstPlayChain chain;
  GstPad *sinkpad;
  GstElement *queue;
  GstElement *conv;
  GstElement *overlay;
  GstPad *videosinkpad;
  GstPad *subpsinkpad;
  GstPad *srcpad;               /* outgoing srcpad, used to connect to the next
                                 * chain */
  GstElement *sink;             /* custom sink to receive subpicture buffers */
} GstPlaySubpChain;

#define GST_PLAY_SINK_GET_LOCK(playsink) (&((GstPlaySink *)playsink)->lock)
#define GST_PLAY_SINK_LOCK(playsink)     g_static_rec_mutex_lock (GST_PLAY_SINK_GET_LOCK (playsink))
#define GST_PLAY_SINK_UNLOCK(playsink)   g_static_rec_mutex_unlock (GST_PLAY_SINK_GET_LOCK (playsink))

struct _GstPlaySink
{
  GstBin bin;

  GStaticRecMutex lock;

  gboolean async_pending;
  gboolean need_async_start;

  GstPlayFlags flags;

  /* chains */
  GstPlayAudioChain *audiochain;
  GstPlayVideoChain *videochain;
  GstPlayVisChain *vischain;
  GstPlayTextChain *textchain;
  GstPlaySubpChain *subpchain;

  /* audio */
  GstPad *audio_pad;
  gboolean audio_pad_raw;
  /* audio tee */
  GstElement *audio_tee;
  GstPad *audio_tee_sink;
  GstPad *audio_tee_asrc;
  GstPad *audio_tee_vissrc;
  /* video */
  GstPad *video_pad;
  gboolean video_pad_raw;
  /* text */
  GstPad *text_pad;
  /* subpictures */
  GstPad *subp_pad;

  /* properties */
  GstElement *audio_sink;
  GstElement *video_sink;
  GstElement *visualisation;
  GstElement *text_sink;
  GstElement *subp_sink;
  gfloat volume;
  gboolean mute;
  gchar *font_desc;             /* font description */
  guint connection_speed;       /* connection speed in bits/sec (0 = unknown) */
  gint count;
  gboolean volume_changed;      /* volume/mute changed while no audiochain */
  gboolean mute_changed;        /* ... has been reated yet */
};

struct _GstPlaySinkClass
{
  GstBinClass parent_class;

    gboolean (*reconfigure) (GstPlaySink * playsink);
};

static GstStaticPadTemplate audiorawtemplate =
GST_STATIC_PAD_TEMPLATE ("audio_raw_sink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate audiotemplate =
GST_STATIC_PAD_TEMPLATE ("audio_sink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate videorawtemplate =
GST_STATIC_PAD_TEMPLATE ("video_raw_sink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate videotemplate =
GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate texttemplate = GST_STATIC_PAD_TEMPLATE ("text_sink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate subpictemplate =
GST_STATIC_PAD_TEMPLATE ("subpic_sink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);



/* props */
enum
{
  PROP_0,
  PROP_FLAGS,
  PROP_MUTE,
  PROP_VOLUME,
  PROP_FONT_DESC,
  PROP_VIS_PLUGIN,
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

static GstPad *gst_play_sink_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_play_sink_release_request_pad (GstElement * element,
    GstPad * pad);
static gboolean gst_play_sink_send_event (GstElement * element,
    GstEvent * event);
static GstStateChangeReturn gst_play_sink_change_state (GstElement * element,
    GstStateChange transition);

static void gst_play_sink_handle_message (GstBin * bin, GstMessage * message);

/* static guint gst_play_sink_signals[LAST_SIGNAL] = { 0 }; */

static const GstElementDetails gst_play_sink_details =
GST_ELEMENT_DETAILS ("Player Sink",
    "Generic/Bin/Sink",
    "Convenience sink for multiple streams",
    "Wim Taymans <wim.taymans@gmail.com>");

G_DEFINE_TYPE (GstPlaySink, gst_play_sink, GST_TYPE_BIN);

static void
gst_play_sink_class_init (GstPlaySinkClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstBinClass *gstbin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;

  gobject_klass->dispose = gst_play_sink_dispose;
  gobject_klass->finalize = gst_play_sink_finalize;
  gobject_klass->set_property = gst_play_sink_set_property;
  gobject_klass->get_property = gst_play_sink_get_property;


  /**
   * GstPlaySink:flags
   *
   * Control the behaviour of playsink.
   */
  g_object_class_install_property (gobject_klass, PROP_FLAGS,
      g_param_spec_flags ("flags", "Flags", "Flags to control behaviour",
          GST_TYPE_PLAY_FLAGS, DEFAULT_FLAGS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlaySink:volume:
   *
   * Get or set the current audio stream volume. 1.0 means 100%,
   * 0.0 means mute. This uses a linear volume scale.
   *
   */
  g_object_class_install_property (gobject_klass, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "The audio volume, 1.0=100%",
          0.0, VOLUME_MAX_DOUBLE, 1.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute",
          "Mute the audio channel without changing the volume", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_FONT_DESC,
      g_param_spec_string ("subtitle-font-desc",
          "Subtitle font description",
          "Pango font description of font "
          "to be used for subtitle rendering", NULL,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_VIS_PLUGIN,
      g_param_spec_object ("vis-plugin", "Vis plugin",
          "the visualization element to use (NULL = default)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_signal_new ("reconfigure", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstPlaySinkClass,
          reconfigure), NULL, NULL, gst_marshal_BOOLEAN__VOID, G_TYPE_BOOLEAN,
      0, G_TYPE_NONE);

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&audiorawtemplate));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&audiotemplate));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&videorawtemplate));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&videotemplate));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&texttemplate));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&subpictemplate));
  gst_element_class_set_details (gstelement_klass, &gst_play_sink_details);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_play_sink_change_state);
  gstelement_klass->send_event = GST_DEBUG_FUNCPTR (gst_play_sink_send_event);
  gstelement_klass->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_play_sink_request_new_pad);
  gstelement_klass->release_pad =
      GST_DEBUG_FUNCPTR (gst_play_sink_release_request_pad);

  gstbin_klass->handle_message =
      GST_DEBUG_FUNCPTR (gst_play_sink_handle_message);

  klass->reconfigure = GST_DEBUG_FUNCPTR (gst_play_sink_reconfigure);
}

static void
gst_play_sink_init (GstPlaySink * playsink)
{
  /* init groups */
  playsink->video_sink = NULL;
  playsink->audio_sink = NULL;
  playsink->visualisation = NULL;
  playsink->text_sink = NULL;
  playsink->volume = 1.0;
  playsink->font_desc = NULL;
  playsink->flags = DEFAULT_FLAGS;

  g_static_rec_mutex_init (&playsink->lock);
  playsink->need_async_start = TRUE;
  GST_OBJECT_FLAG_SET (playsink, GST_ELEMENT_IS_SINK);
}

static void
free_chain (GstPlayChain * chain)
{
  if (chain) {
    if (chain->bin)
      gst_object_unref (chain->bin);
    g_free (chain);
  }
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
  if (playsink->text_sink != NULL) {
    gst_element_set_state (playsink->text_sink, GST_STATE_NULL);
    gst_object_unref (playsink->text_sink);
    playsink->text_sink = NULL;
  }

  free_chain ((GstPlayChain *) playsink->videochain);
  playsink->videochain = NULL;
  free_chain ((GstPlayChain *) playsink->audiochain);
  playsink->audiochain = NULL;
  free_chain ((GstPlayChain *) playsink->vischain);
  playsink->vischain = NULL;
  free_chain ((GstPlayChain *) playsink->textchain);
  playsink->textchain = NULL;

  if (playsink->audio_tee_sink) {
    gst_object_unref (playsink->audio_tee_sink);
    playsink->audio_tee_sink = NULL;
  }

  if (playsink->audio_tee_vissrc) {
    gst_element_release_request_pad (playsink->audio_tee,
        playsink->audio_tee_vissrc);
    gst_object_unref (playsink->audio_tee_vissrc);
    playsink->audio_tee_vissrc = NULL;
  }

  if (playsink->audio_tee_asrc) {
    gst_element_release_request_pad (playsink->audio_tee,
        playsink->audio_tee_asrc);
    gst_object_unref (playsink->audio_tee_asrc);
    playsink->audio_tee_asrc = NULL;
  }

  g_free (playsink->font_desc);
  playsink->font_desc = NULL;

  G_OBJECT_CLASS (gst_play_sink_parent_class)->dispose (object);
}

static void
gst_play_sink_finalize (GObject * object)
{
  GstPlaySink *playsink;

  playsink = GST_PLAY_SINK (object);

  g_static_rec_mutex_free (&playsink->lock);

  G_OBJECT_CLASS (gst_play_sink_parent_class)->finalize (object);
}

void
gst_play_sink_set_sink (GstPlaySink * playsink, GstPlaySinkType type,
    GstElement * sink)
{
  GstElement **elem = NULL, *old = NULL;

  GST_LOG ("Setting sink %" GST_PTR_FORMAT " as sink type %d", sink, type);

  GST_PLAY_SINK_LOCK (playsink);
  switch (type) {
    case GST_PLAY_SINK_TYPE_AUDIO:
    case GST_PLAY_SINK_TYPE_AUDIO_RAW:
      elem = &playsink->audio_sink;
      break;
    case GST_PLAY_SINK_TYPE_VIDEO:
    case GST_PLAY_SINK_TYPE_VIDEO_RAW:
      elem = &playsink->video_sink;
      break;
    case GST_PLAY_SINK_TYPE_TEXT:
      elem = &playsink->text_sink;
      break;
    case GST_PLAY_SINK_TYPE_SUBPIC:
      elem = &playsink->subp_sink;
      break;
    default:
      break;
  }
  if (elem) {
    old = *elem;
    if (sink)
      gst_object_ref (sink);
    *elem = sink;
  }
  GST_PLAY_SINK_UNLOCK (playsink);

  if (old)
    gst_object_unref (old);
}

GstElement *
gst_play_sink_get_sink (GstPlaySink * playsink, GstPlaySinkType type)
{
  GstElement *result = NULL;
  GstElement *elem = NULL, *chainp = NULL;

  GST_PLAY_SINK_LOCK (playsink);
  switch (type) {
    case GST_PLAY_SINK_TYPE_AUDIO:
    {
      GstPlayAudioChain *chain;
      if ((chain = (GstPlayAudioChain *) playsink->audiochain))
        chainp = chain->sink;
      elem = playsink->audio_sink;
      break;
    }
    case GST_PLAY_SINK_TYPE_VIDEO:
    {
      GstPlayVideoChain *chain;
      if ((chain = (GstPlayVideoChain *) playsink->videochain))
        chainp = chain->sink;
      elem = playsink->video_sink;
      break;
    }
    case GST_PLAY_SINK_TYPE_TEXT:
    {
      GstPlayTextChain *chain;
      if ((chain = (GstPlayTextChain *) playsink->textchain))
        chainp = chain->sink;
      elem = playsink->text_sink;
      break;
    }
    case GST_PLAY_SINK_TYPE_SUBPIC:
    {
      GstPlaySubpChain *chain;
      if ((chain = (GstPlaySubpChain *) playsink->subpchain))
        chainp = chain->sink;
      elem = playsink->subp_sink;
      break;
    }
    default:
      break;
  }
  if (chainp) {
    /* we have an active chain with a sink, get the sink */
    result = gst_object_ref (chainp);
  }
  /* nothing found, return last configured sink */
  if (result == NULL && elem)
    result = gst_object_ref (elem);
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
  chain->vis = playsink->visualisation;
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
  /* take ownership */
  gst_object_ref_sink (vis);
  playsink->visualisation = vis;

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
    GST_LOG_OBJECT (playsink, "elements: volume=%" GST_PTR_FORMAT ", mute=%"
        GST_PTR_FORMAT "; new volume=%.03f, mute=%d", chain->volume,
        chain->mute, volume, playsink->mute);
    /* if there is a mute element or we are not muted, set the volume */
    if (chain->mute || !playsink->mute)
      g_object_set (chain->volume, "volume", volume, NULL);
  } else {
    GST_LOG_OBJECT (playsink, "no volume element");
    playsink->volume_changed = TRUE;
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
  } else {
    playsink->mute_changed = TRUE;
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
  GstState state;

  if (chain->activated == activate)
    return TRUE;

  GST_OBJECT_LOCK (chain->playsink);
  state = GST_STATE_TARGET (chain->playsink);
  GST_OBJECT_UNLOCK (chain->playsink);

  if (activate)
    gst_element_set_state (chain->bin, state);
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

static gint
find_property_sink (GstElement * element, const gchar * name)
{
  gint res;
  gboolean is_sink;

  GST_OBJECT_LOCK (element);
  is_sink = GST_OBJECT_FLAG_IS_SET (element, GST_ELEMENT_IS_SINK);
  GST_OBJECT_UNLOCK (element);

  if (is_sink &&
      g_object_class_find_property (G_OBJECT_GET_CLASS (element), name)) {
    res = 0;
    GST_DEBUG_OBJECT (element, "found %s property on sink", name);
  } else {
    GST_DEBUG_OBJECT (element, "did not find %s property", name);
    res = 1;
    gst_object_unref (element);
  }
  return res;
}

/* find a sink in the hierarchy with a property named @name. This function does
 * not increase the refcount of the returned object and thus remains valid as
 * long as the bin is valid. */
static GstElement *
gst_play_sink_find_property_sinks (GstPlaySink * playsink, GstElement * obj,
    const gchar * name)
{
  GstElement *result = NULL;
  GstIterator *it;

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (obj), name)) {
    result = obj;
  } else if (GST_IS_BIN (obj)) {
    it = gst_bin_iterate_recurse (GST_BIN_CAST (obj));
    result = gst_iterator_find_custom (it,
        (GCompareFunc) find_property_sink, (gpointer) name);
    gst_iterator_free (it);
    /* we don't need the extra ref */
    if (result)
      gst_object_unref (result);
  }
  return result;
}

static void
do_async_start (GstPlaySink * playsink)
{
  GstMessage *message;

  if (!playsink->need_async_start)
    return;

  playsink->async_pending = TRUE;

  GST_INFO_OBJECT (playsink, "Sending async_start message");
  message = gst_message_new_async_start (GST_OBJECT_CAST (playsink), FALSE);
  GST_BIN_CLASS (gst_play_sink_parent_class)->handle_message (GST_BIN_CAST
      (playsink), message);
}

static void
do_async_done (GstPlaySink * playsink)
{
  GstMessage *message;

  if (playsink->async_pending) {
    GST_INFO_OBJECT (playsink, "Sending async_done message");
    message = gst_message_new_async_done (GST_OBJECT_CAST (playsink));
    GST_BIN_CLASS (gst_play_sink_parent_class)->handle_message (GST_BIN_CAST
        (playsink), message);

    playsink->async_pending = FALSE;
  }

  playsink->need_async_start = FALSE;
}

/* try to change the state of an element. This function returns the element when
 * the state change could be performed. When this function returns NULL an error
 * occured and the element is unreffed if @unref is TRUE. */
static GstElement *
try_element (GstPlaySink * playsink, GstElement * element, gboolean unref)
{
  GstStateChangeReturn ret;

  if (element) {
    ret = gst_element_set_state (element, GST_STATE_READY);
    if (ret == GST_STATE_CHANGE_FAILURE) {
      GST_DEBUG_OBJECT (playsink, "failed state change..");
      gst_element_set_state (element, GST_STATE_NULL);
      if (unref)
        gst_object_unref (element);
      element = NULL;
    }
  }
  return element;
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
gen_video_chain (GstPlaySink * playsink, gboolean raw, gboolean async,
    gboolean queue)
{
  GstPlayVideoChain *chain;
  GstBin *bin;
  GstPad *pad;
  GstElement *head, *prev, *elem = NULL;

  chain = g_new0 (GstPlayVideoChain, 1);
  chain->chain.playsink = playsink;
  chain->chain.raw = raw;

  GST_DEBUG_OBJECT (playsink, "making video chain %p", chain);

  if (playsink->video_sink) {
    GST_DEBUG_OBJECT (playsink, "trying configured videosink");
    chain->sink = try_element (playsink, playsink->video_sink, FALSE);
  } else {
    /* only try fallback if no specific sink was chosen */
    if (chain->sink == NULL) {
      GST_DEBUG_OBJECT (playsink, "trying autovideosink");
      elem = gst_element_factory_make ("autovideosink", "videosink");
      chain->sink = try_element (playsink, elem, TRUE);
    }
    if (chain->sink == NULL) {
      /* if default sink from config.h is different then try it too */
      if (strcmp (DEFAULT_VIDEOSINK, "autovideosink")) {
        GST_DEBUG_OBJECT (playsink, "trying " DEFAULT_VIDEOSINK);
        elem = gst_element_factory_make (DEFAULT_VIDEOSINK, "videosink");
        chain->sink = try_element (playsink, elem, TRUE);
      }
    }
  }
  if (chain->sink == NULL)
    goto no_sinks;

  /* if we can disable async behaviour of the sink, we can avoid adding a
   * queue for the audio chain. */
  elem = gst_play_sink_find_property_sinks (playsink, chain->sink, "async");
  if (elem) {
    GST_DEBUG_OBJECT (playsink, "setting async property to %d on element %s",
        async, GST_ELEMENT_NAME (elem));
    g_object_set (elem, "async", async, NULL);
    chain->async = async;
  } else {
    GST_DEBUG_OBJECT (playsink, "no async property on the sink");
    chain->async = TRUE;
  }

  /* create a bin to hold objects, as we create them we add them to this bin so
   * that when something goes wrong we only need to unref the bin */
  chain->chain.bin = gst_bin_new ("vbin");
  bin = GST_BIN_CAST (chain->chain.bin);
  gst_object_ref_sink (bin);
  gst_bin_add (bin, chain->sink);

  if (queue) {
    /* decouple decoder from sink, this improves playback quite a lot since the
     * decoder can continue while the sink blocks for synchronisation. We don't
     * need a lot of buffers as this consumes a lot of memory and we don't want
     * too little because else we would be context switching too quickly. */
    chain->queue = gst_element_factory_make ("queue", "vqueue");
    g_object_set (G_OBJECT (chain->queue), "max-size-buffers", 3,
        "max-size-bytes", 0, "max-size-time", (gint64) 0, NULL);
    gst_bin_add (bin, chain->queue);
    head = prev = chain->queue;
  } else {
    head = chain->sink;
    prev = NULL;
  }

  if (raw && !(playsink->flags & GST_PLAY_FLAG_NATIVE_VIDEO)) {
    GST_DEBUG_OBJECT (playsink, "creating ffmpegcolorspace");
    chain->conv = gst_element_factory_make ("ffmpegcolorspace", "vconv");
    if (chain->conv == NULL) {
      post_missing_element_message (playsink, "ffmpegcolorspace");
      GST_ELEMENT_WARNING (playsink, CORE, MISSING_PLUGIN,
          (_("Missing element '%s' - check your GStreamer installation."),
              "ffmpegcolorspace"), ("video rendering might fail"));
    } else {
      gst_bin_add (bin, chain->conv);
      if (prev) {
        if (!gst_element_link_pads (prev, "src", chain->conv, "sink"))
          goto link_failed;
      } else {
        head = chain->conv;
      }
      prev = chain->conv;
    }

    GST_DEBUG_OBJECT (playsink, "creating videoscale");
    chain->scale = gst_element_factory_make ("videoscale", "vscale");
    if (chain->scale == NULL) {
      post_missing_element_message (playsink, "videoscale");
      GST_ELEMENT_WARNING (playsink, CORE, MISSING_PLUGIN,
          (_("Missing element '%s' - check your GStreamer installation."),
              "videoscale"), ("possibly a liboil version mismatch?"));
    } else {
      gst_bin_add (bin, chain->scale);
      if (prev) {
        if (!gst_element_link_pads (prev, "src", chain->scale, "sink"))
          goto link_failed;
      } else {
        head = chain->scale;
      }
      prev = chain->scale;
    }
  }

  if (prev) {
    GST_DEBUG_OBJECT (playsink, "linking to sink");
    if (!gst_element_link_pads (prev, "src", chain->sink, NULL))
      goto link_failed;
  }

  pad = gst_element_get_static_pad (head, "sink");
  chain->sinkpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);

  gst_element_add_pad (chain->chain.bin, chain->sinkpad);

  return chain;

  /* ERRORS */
no_sinks:
  {
    if (!elem) {
      post_missing_element_message (playsink, "autovideosink");
      if (strcmp (DEFAULT_VIDEOSINK, "autovideosink")) {
        post_missing_element_message (playsink, DEFAULT_VIDEOSINK);
        GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
            (_("Both autovideosink and %s elements are missing."),
                DEFAULT_VIDEOSINK), (NULL));
      } else {
        GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
            (_("The autovideosink element is missing.")), (NULL));
      }
    } else {
      if (strcmp (DEFAULT_VIDEOSINK, "autovideosink")) {
        GST_ELEMENT_ERROR (playsink, CORE, STATE_CHANGE,
            (_("Both autovideosink and %s elements are not working."),
                DEFAULT_VIDEOSINK), (NULL));
      } else {
        GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
            (_("The autovideosink element is not working.")), (NULL));
      }
    }
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

static gboolean
setup_video_chain (GstPlaySink * playsink, gboolean raw, gboolean async,
    gboolean queue)
{
  GstElement *elem;
  GstPlayVideoChain *chain;
  GstStateChangeReturn ret;

  chain = playsink->videochain;

  /* if the chain was active we don't do anything */
  if (GST_PLAY_CHAIN (chain)->activated == TRUE)
    return TRUE;

  if (chain->chain.raw != raw)
    return FALSE;

  /* try to set the sink element to READY again */
  ret = gst_element_set_state (chain->sink, GST_STATE_READY);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return FALSE;

  /* if we can disable async behaviour of the sink, we can avoid adding a
   * queue for the audio chain. */
  elem = gst_play_sink_find_property_sinks (playsink, chain->sink, "async");
  if (elem) {
    GST_DEBUG_OBJECT (playsink, "setting async property to %d on element %s",
        async, GST_ELEMENT_NAME (elem));
    g_object_set (elem, "async", async, NULL);
    chain->async = async;
  } else {
    GST_DEBUG_OBJECT (playsink, "no async property on the sink");
    chain->async = TRUE;
  }
  return TRUE;
}

/* make an element for playback of video with subtitles embedded.
 *
 *  +----------------------------------------------+
 *  | tbin                  +-------------+        |
 *  |          +-----+      | textoverlay |        |
 *  |          | csp | +--video_sink      |        |
 * sink-------sink  src+ +-text_sink     src--+    |
 *  |          +-----+   |  +-------------+   +-- src
 * text_sink-------------+                         |
 *  +----------------------------------------------+
 */
static GstPlayTextChain *
gen_text_chain (GstPlaySink * playsink)
{
  GstPlayTextChain *chain;
  GstBin *bin;
  GstElement *elem;
  GstPad *videosinkpad, *textsinkpad, *srcpad;

  chain = g_new0 (GstPlayTextChain, 1);
  chain->chain.playsink = playsink;

  GST_DEBUG_OBJECT (playsink, "making text chain %p", chain);

  chain->chain.bin = gst_bin_new ("tbin");
  bin = GST_BIN_CAST (chain->chain.bin);
  gst_object_ref_sink (bin);

  videosinkpad = textsinkpad = srcpad = NULL;

  /* first try to hook the text pad to the custom sink */
  if (playsink->text_sink) {
    GST_DEBUG_OBJECT (playsink, "trying configured textsink");
    chain->sink = try_element (playsink, playsink->text_sink, FALSE);
    if (chain->sink) {
      elem = gst_play_sink_find_property_sinks (playsink, chain->sink, "async");
      if (elem) {
        /* make sure the sparse subtitles don't participate in the preroll */
        g_object_set (elem, "async", FALSE, NULL);
        /* we have a custom sink, this will be our textsinkpad */
        textsinkpad = gst_element_get_static_pad (chain->sink, "sink");
        if (textsinkpad) {
          /* we're all fine now and we can add the sink to the chain */
          GST_DEBUG_OBJECT (playsink, "adding custom text sink");
          gst_bin_add (bin, chain->sink);
        } else {
          GST_WARNING_OBJECT (playsink,
              "can't find a sink pad on custom text sink");
          gst_object_unref (chain->sink);
          chain->sink = NULL;
        }
        /* try to set sync to true but it's no biggie when we can't */
        if ((elem =
                gst_play_sink_find_property_sinks (playsink, chain->sink,
                    "sync")))
          g_object_set (elem, "sync", TRUE, NULL);
      } else {
        GST_WARNING_OBJECT (playsink,
            "can't find async property in custom text sink");
      }
    }
    if (textsinkpad == NULL) {
      GST_ELEMENT_WARNING (playsink, CORE, MISSING_PLUGIN,
          (_("Custom text sink element is not usable.")),
          ("fallback to default textoverlay"));
    }
  }

  if (textsinkpad == NULL) {
    if (!(playsink->flags & GST_PLAY_FLAG_NATIVE_VIDEO)) {
      /* no custom sink, try to setup the colorspace and textoverlay elements */
      chain->conv = gst_element_factory_make ("ffmpegcolorspace", "tconv");
      if (chain->conv == NULL) {
        /* not really needed, it might work without colorspace */
        post_missing_element_message (playsink, "ffmpegcolorspace");
        GST_ELEMENT_WARNING (playsink, CORE, MISSING_PLUGIN,
            (_("Missing element '%s' - check your GStreamer installation."),
                "ffmpegcolorspace"), ("subtitle rendering might fail"));
      } else {
        gst_bin_add (bin, chain->conv);
        videosinkpad = gst_element_get_static_pad (chain->conv, "sink");
      }
    }

    chain->overlay = gst_element_factory_make ("textoverlay", "overlay");
    if (chain->overlay == NULL) {
      post_missing_element_message (playsink, "textoverlay");
      GST_ELEMENT_WARNING (playsink, CORE, MISSING_PLUGIN,
          (_("Missing element '%s' - check your GStreamer installation."),
              "textoverlay"), ("subtitle rendering disabled"));
    } else {
      gst_bin_add (bin, chain->overlay);

      /* Set some parameters */
      g_object_set (G_OBJECT (chain->overlay),
          "halign", "center", "valign", "bottom", NULL);
      if (playsink->font_desc) {
        g_object_set (G_OBJECT (chain->overlay), "font-desc",
            playsink->font_desc, NULL);
      }
      g_object_set (G_OBJECT (chain->overlay), "wait-text", FALSE, NULL);

      textsinkpad = gst_element_get_static_pad (chain->overlay, "text_sink");

      srcpad = gst_element_get_static_pad (chain->overlay, "src");

      if (videosinkpad) {
        /* if we had a videosinkpad, we had a converter and we can link it, we
         * know that this will work */
        gst_element_link_pads (chain->conv, "src", chain->overlay,
            "video_sink");
      } else {
        /* no videopad, expose our own video pad then */
        videosinkpad =
            gst_element_get_static_pad (chain->overlay, "video_sink");
      }
    }
  }

  if (videosinkpad == NULL) {
    /* if we still don't have a videosink, we don't have a converter nor an
     * overlay. the only thing we can do is insert an identity and ghost the src
     * and sink pads. */
    chain->conv = gst_element_factory_make ("identity", "tidentity");
    g_object_set (chain->conv, "signal-handoffs", FALSE, NULL);
    g_object_set (chain->conv, "silent", TRUE, NULL);
    gst_bin_add (bin, chain->conv);
    srcpad = gst_element_get_static_pad (chain->conv, "src");
    videosinkpad = gst_element_get_static_pad (chain->conv, "sink");
  } else {
    /* we have a videosink but maybe not a srcpad because there was no
     * overlay */
    if (srcpad == NULL) {
      /* ghost the source pad of the converter then */
      srcpad = gst_element_get_static_pad (chain->conv, "src");
    }
  }

  /* expose the ghostpads */
  if (videosinkpad) {
    chain->videosinkpad = gst_ghost_pad_new ("sink", videosinkpad);
    gst_object_unref (videosinkpad);
    gst_element_add_pad (chain->chain.bin, chain->videosinkpad);
  }
  if (textsinkpad) {
    chain->textsinkpad = gst_ghost_pad_new ("text_sink", textsinkpad);
    gst_object_unref (textsinkpad);
    gst_element_add_pad (chain->chain.bin, chain->textsinkpad);
  }
  if (srcpad) {
    chain->srcpad = gst_ghost_pad_new ("src", srcpad);
    gst_object_unref (srcpad);
    gst_element_add_pad (chain->chain.bin, chain->srcpad);
  }

  return chain;
}

/* make an element for playback of video with subpictures embedded.
 *
 *  +--------------------------------------------------------+
 *  | pbin                            +-------------+        |
 *  |       +-------+    +-----+      |   dvdspu    |        |
 *  |       | queue |    | csp | +---video          |        |
 * sink----sink    src--sink  src+ +-subpicture    src--+    |
 *  |       +-------+    +-----+   |  +-------------+   +-- src
 * subpicture----------------------+                         |
 *  +--------------------------------------------------------+
 */
static GstPlaySubpChain *
gen_subp_chain (GstPlaySink * playsink)
{
  GstPlaySubpChain *chain;
  GstBin *bin;
  GstElement *elem, *head;
  GstPad *videosinkpad, *subpsinkpad, *srcpad;

  chain = g_new0 (GstPlaySubpChain, 1);
  chain->chain.playsink = playsink;

  GST_DEBUG_OBJECT (playsink, "making subpicture chain %p", chain);

  chain->chain.bin = gst_bin_new ("pbin");
  bin = GST_BIN_CAST (chain->chain.bin);
  gst_object_ref_sink (bin);

  subpsinkpad = srcpad = NULL;

  /* first try to hook the text pad to the custom sink */
  if (playsink->subp_sink) {
    GST_DEBUG_OBJECT (playsink, "trying configured subpsink");
    chain->sink = try_element (playsink, playsink->text_sink, FALSE);
    if (chain->sink) {
      elem = gst_play_sink_find_property_sinks (playsink, chain->sink, "async");
      if (elem) {
        /* make sure the sparse subtitles don't participate in the preroll */
        g_object_set (elem, "async", FALSE, NULL);
        /* we have a custom sink, this will be our subpsinkpad */
        subpsinkpad = gst_element_get_static_pad (chain->sink, "sink");
        if (subpsinkpad) {
          /* we're all fine now and we can add the sink to the chain */
          GST_DEBUG_OBJECT (playsink, "adding custom text sink");
          gst_bin_add (bin, chain->sink);
        } else {
          GST_WARNING_OBJECT (playsink,
              "can't find a sink pad on custom text sink");
          gst_object_unref (chain->sink);
          chain->sink = NULL;
        }
        /* try to set sync to true but it's no biggie when we can't */
        if ((elem =
                gst_play_sink_find_property_sinks (playsink, chain->sink,
                    "sync")))
          g_object_set (elem, "sync", TRUE, NULL);
      } else {
        GST_WARNING_OBJECT (playsink,
            "can't find async property in custom text sink");
      }
    }
    if (subpsinkpad == NULL) {
      GST_ELEMENT_WARNING (playsink, CORE, MISSING_PLUGIN,
          (_("Custom text sink element is not usable.")),
          ("fallback to default dvdspu overlay"));
    }
  }

  /* make a little queue */
  chain->queue = gst_element_factory_make ("queue", "vqueue");
  g_object_set (G_OBJECT (chain->queue), "max-size-buffers", 3,
      "max-size-bytes", 0, "max-size-time", (gint64) 0, NULL);
  gst_bin_add (bin, chain->queue);
  head = chain->queue;

  /* video goes into the queue */
  videosinkpad = gst_element_get_static_pad (chain->queue, "sink");

  if (subpsinkpad == NULL) {
    if (!(playsink->flags & GST_PLAY_FLAG_NATIVE_VIDEO)) {
      /* no custom sink, try to setup the colorspace and textoverlay elements */
      chain->conv = gst_element_factory_make ("ffmpegcolorspace", "tconv");
      if (chain->conv == NULL) {
        /* not really needed, it might work without colorspace */
        post_missing_element_message (playsink, "ffmpegcolorspace");
        GST_ELEMENT_WARNING (playsink, CORE, MISSING_PLUGIN,
            (_("Missing element '%s' - check your GStreamer installation."),
                "ffmpegcolorspace"), ("subpicture rendering might fail"));
      } else {
        gst_bin_add (bin, chain->conv);
        gst_element_link_pads (head, "src", chain->conv, "sink");
        head = chain->conv;
      }
    }

    chain->overlay = gst_element_factory_make ("dvdspu", "spuoverlay");
    if (chain->overlay == NULL) {
      post_missing_element_message (playsink, "dvdspu");
      GST_ELEMENT_WARNING (playsink, CORE, MISSING_PLUGIN,
          (_("Missing element '%s' - check your GStreamer installation."),
              "dvdspu"), ("subpicture rendering disabled"));
    } else {
      gst_bin_add (bin, chain->overlay);
      /* Set some parameters */
      subpsinkpad = gst_element_get_static_pad (chain->overlay, "subpicture");
      /* link to the next element */
      gst_element_link_pads (head, "src", chain->overlay, "video");
      head = chain->overlay;
    }
  }
  srcpad = gst_element_get_static_pad (head, "src");
  chain->srcpad = gst_ghost_pad_new ("src", srcpad);
  gst_object_unref (srcpad);
  gst_element_add_pad (chain->chain.bin, chain->srcpad);

  /* expose the ghostpads */
  chain->videosinkpad = gst_ghost_pad_new ("sink", videosinkpad);
  gst_object_unref (videosinkpad);
  gst_element_add_pad (chain->chain.bin, chain->videosinkpad);

  if (subpsinkpad) {
    chain->subpsinkpad = gst_ghost_pad_new ("subpicture", subpsinkpad);
    gst_object_unref (subpsinkpad);
    gst_element_add_pad (chain->chain.bin, chain->subpsinkpad);
  }
  return chain;
}

static void
notify_volume_cb (GObject * object, GParamSpec * pspec, GstPlaySink * playsink)
{
  gdouble vol;

  g_object_get (object, "volume", &vol, NULL);
  playsink->volume = vol;

  g_object_notify (G_OBJECT (playsink), "volume");
}

static void
notify_mute_cb (GObject * object, GParamSpec * pspec, GstPlaySink * playsink)
{
  gboolean mute;

  g_object_get (object, "mute", &mute, NULL);
  playsink->mute = mute;

  g_object_notify (G_OBJECT (playsink), "mute");
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
  gboolean have_volume;
  GstPad *pad;
  GstElement *head, *prev, *elem = NULL;

  chain = g_new0 (GstPlayAudioChain, 1);
  chain->chain.playsink = playsink;
  chain->chain.raw = raw;

  GST_DEBUG_OBJECT (playsink, "making audio chain %p", chain);

  if (playsink->audio_sink) {
    GST_DEBUG_OBJECT (playsink, "trying configured audiosink %" GST_PTR_FORMAT,
        playsink->audio_sink);
    chain->sink = try_element (playsink, playsink->audio_sink, FALSE);
  } else {
    /* only try fallback if no specific sink was chosen */
    if (chain->sink == NULL) {
      GST_DEBUG_OBJECT (playsink, "trying autoaudiosink");
      elem = gst_element_factory_make ("autoaudiosink", "audiosink");
      chain->sink = try_element (playsink, elem, TRUE);
    }
    if (chain->sink == NULL) {
      /* if default sink from config.h is different then try it too */
      if (strcmp (DEFAULT_AUDIOSINK, "autoaudiosink")) {
        GST_DEBUG_OBJECT (playsink, "trying " DEFAULT_AUDIOSINK);
        elem = gst_element_factory_make (DEFAULT_AUDIOSINK, "audiosink");
        chain->sink = try_element (playsink, elem, TRUE);
      }
    }
  }
  if (chain->sink == NULL)
    goto no_sinks;

  chain->chain.bin = gst_bin_new ("abin");
  bin = GST_BIN_CAST (chain->chain.bin);
  gst_object_ref_sink (bin);
  gst_bin_add (bin, chain->sink);

  if (queue) {
    /* we have to add a queue when we need to decouple for the video sink in
     * visualisations */
    GST_DEBUG_OBJECT (playsink, "adding audio queue");
    chain->queue = gst_element_factory_make ("queue", "aqueue");
    gst_bin_add (bin, chain->queue);
    prev = head = chain->queue;
  } else {
    head = chain->sink;
    prev = NULL;
  }

  /* check if the sink, or something within the sink, has the volume property.
   * If it does we don't need to add a volume element.  */
  elem = gst_play_sink_find_property_sinks (playsink, chain->sink, "volume");
  if (elem) {
    chain->volume = elem;

    g_signal_connect (chain->volume, "notify::volume",
        G_CALLBACK (notify_volume_cb), playsink);

    GST_DEBUG_OBJECT (playsink, "the sink has a volume property");
    have_volume = TRUE;
    chain->sink_volume = TRUE;
    /* if the sink also has a mute property we can use this as well. We'll only
     * use the mute property if there is a volume property. We can simulate the
     * mute with the volume otherwise. */
    chain->mute =
        gst_play_sink_find_property_sinks (playsink, chain->sink, "mute");
    if (chain->mute) {
      GST_DEBUG_OBJECT (playsink, "the sink has a mute property");
      g_signal_connect (chain->mute, "notify::mute",
          G_CALLBACK (notify_mute_cb), playsink);
    }
    /* use the sink to control the volume and mute */
    if (playsink->volume_changed) {
      g_object_set (G_OBJECT (chain->volume), "volume", playsink->volume, NULL);
      playsink->volume_changed = FALSE;
    }
    if (playsink->mute_changed) {
      if (chain->mute) {
        g_object_set (chain->mute, "mute", playsink->mute, NULL);
      } else {
        if (playsink->mute)
          g_object_set (chain->volume, "volume", (gdouble) 0.0, NULL);
      }
      playsink->mute_changed = FALSE;
    }
  } else {
    /* no volume, we need to add a volume element when we can */
    GST_DEBUG_OBJECT (playsink, "the sink has no volume property");
    have_volume = FALSE;
    chain->sink_volume = FALSE;
  }

  if (raw && !(playsink->flags & GST_PLAY_FLAG_NATIVE_AUDIO)) {
    GST_DEBUG_OBJECT (playsink, "creating audioconvert");
    chain->conv = gst_element_factory_make ("audioconvert", "aconv");
    if (chain->conv == NULL) {
      post_missing_element_message (playsink, "audioconvert");
      GST_ELEMENT_WARNING (playsink, CORE, MISSING_PLUGIN,
          (_("Missing element '%s' - check your GStreamer installation."),
              "audioconvert"), ("possibly a liboil version mismatch?"));
    } else {
      gst_bin_add (bin, chain->conv);
      if (prev) {
        if (!gst_element_link_pads (prev, "src", chain->conv, "sink"))
          goto link_failed;
      } else {
        head = chain->conv;
      }
      prev = chain->conv;
    }

    GST_DEBUG_OBJECT (playsink, "creating audioresample");
    chain->resample = gst_element_factory_make ("audioresample", "aresample");
    if (chain->resample == NULL) {
      post_missing_element_message (playsink, "audioresample");
      GST_ELEMENT_WARNING (playsink, CORE, MISSING_PLUGIN,
          (_("Missing element '%s' - check your GStreamer installation."),
              "audioresample"), ("possibly a liboil version mismatch?"));
    } else {
      gst_bin_add (bin, chain->resample);
      if (prev) {
        if (!gst_element_link_pads (prev, "src", chain->resample, "sink"))
          goto link_failed;
      } else {
        head = chain->resample;
      }
      prev = chain->resample;
    }

    if (!have_volume && playsink->flags & GST_PLAY_FLAG_SOFT_VOLUME) {
      GST_DEBUG_OBJECT (playsink, "creating volume");
      chain->volume = gst_element_factory_make ("volume", "volume");
      if (chain->volume == NULL) {
        post_missing_element_message (playsink, "volume");
        GST_ELEMENT_WARNING (playsink, CORE, MISSING_PLUGIN,
            (_("Missing element '%s' - check your GStreamer installation."),
                "volume"), ("possibly a liboil version mismatch?"));
      } else {
        have_volume = TRUE;

        g_signal_connect (chain->volume, "notify::volume",
            G_CALLBACK (notify_volume_cb), playsink);

        /* volume also has the mute property */
        chain->mute = chain->volume;
        g_signal_connect (chain->mute, "notify::mute",
            G_CALLBACK (notify_mute_cb), playsink);

        /* configure with the latest volume and mute */
        g_object_set (G_OBJECT (chain->volume), "volume", playsink->volume,
            NULL);
        g_object_set (G_OBJECT (chain->mute), "mute", playsink->mute, NULL);
        gst_bin_add (bin, chain->volume);

        if (prev) {
          if (!gst_element_link_pads (prev, "src", chain->volume, "sink"))
            goto link_failed;
        } else {
          head = chain->volume;
        }
        prev = chain->volume;
      }
    }
  }

  if (prev) {
    /* we only have to link to the previous element if we have something in
     * front of the sink */
    GST_DEBUG_OBJECT (playsink, "linking to sink");
    if (!gst_element_link_pads (prev, "src", chain->sink, NULL))
      goto link_failed;
  }

  /* post a warning if we have no way to configure the volume */
  if (!have_volume) {
    GST_ELEMENT_WARNING (playsink, STREAM, NOT_IMPLEMENTED,
        (_("No volume control found")), ("Volume/mute is not available"));
  }

  /* and ghost the sinkpad of the headmost element */
  GST_DEBUG_OBJECT (playsink, "ghosting sink pad");
  pad = gst_element_get_static_pad (head, "sink");
  chain->sinkpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);
  gst_element_add_pad (chain->chain.bin, chain->sinkpad);

  return chain;

  /* ERRORS */
no_sinks:
  {
    if (!elem) {
      post_missing_element_message (playsink, "autoaudiosink");
      if (strcmp (DEFAULT_AUDIOSINK, "autoaudiosink")) {
        post_missing_element_message (playsink, DEFAULT_AUDIOSINK);
        GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
            (_("Both autoaudiosink and %s elements are missing."),
                DEFAULT_AUDIOSINK), (NULL));
      } else {
        GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
            (_("The autoaudiosink element is missing.")), (NULL));
      }
    } else {
      if (strcmp (DEFAULT_AUDIOSINK, "autoaudiosink")) {
        GST_ELEMENT_ERROR (playsink, CORE, STATE_CHANGE,
            (_("Both autoaudiosink and %s elements are not working."),
                DEFAULT_AUDIOSINK), (NULL));
      } else {
        GST_ELEMENT_ERROR (playsink, CORE, MISSING_PLUGIN,
            (_("The autoaudiosink element is not working.")), (NULL));
      }
    }
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

static gboolean
setup_audio_chain (GstPlaySink * playsink, gboolean raw, gboolean queue)
{
  GstElement *elem;
  GstPlayAudioChain *chain;
  GstStateChangeReturn ret;

  chain = playsink->audiochain;

  /* if the chain was active we don't do anything */
  if (GST_PLAY_CHAIN (chain)->activated == TRUE)
    return TRUE;

  if (chain->chain.raw != raw)
    return FALSE;

  /* try to set the sink element to READY again */
  ret = gst_element_set_state (chain->sink, GST_STATE_READY);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return FALSE;

  /* check if the sink, or something within the sink, has the volume property.
   * If it does we don't need to add a volume element.  */
  elem = gst_play_sink_find_property_sinks (playsink, chain->sink, "volume");
  if (elem) {
    chain->volume = elem;

    if (playsink->volume_changed) {
      GST_DEBUG_OBJECT (playsink, "the sink has a volume property, setting %f",
          playsink->volume);
      /* use the sink to control the volume */
      g_object_set (G_OBJECT (chain->volume), "volume", playsink->volume, NULL);
      playsink->volume_changed = FALSE;
    }

    g_signal_connect (chain->volume, "notify::volume",
        G_CALLBACK (notify_volume_cb), playsink);
    /* if the sink also has a mute property we can use this as well. We'll only
     * use the mute property if there is a volume property. We can simulate the
     * mute with the volume otherwise. */
    chain->mute =
        gst_play_sink_find_property_sinks (playsink, chain->sink, "mute");
    if (chain->mute) {
      GST_DEBUG_OBJECT (playsink, "the sink has a mute property");
      g_signal_connect (chain->mute, "notify::mute",
          G_CALLBACK (notify_mute_cb), playsink);
    }
  } else {
    /* no volume, we need to add a volume element when we can */
    GST_DEBUG_OBJECT (playsink, "the sink has no volume property");
    if (!raw) {
      GST_LOG_OBJECT (playsink, "non-raw format, can't do soft volume control");

      if (chain->volume)
        g_signal_handlers_disconnect_by_func (chain->volume, notify_volume_cb,
            playsink);
      if (chain->mute)
        g_signal_handlers_disconnect_by_func (chain->mute, notify_mute_cb,
            playsink);

      chain->volume = NULL;
      chain->mute = NULL;
    } else {
      /* both last and current chain are raw audio, there should be a volume
       * element already, unless the sink changed from one with a volume
       * property to one that hasn't got a volume property, in which case we
       * re-generate the chain */
      if (chain->volume == NULL) {
        GST_DEBUG_OBJECT (playsink, "no existing volume element to re-use");
        return FALSE;
      }

      GST_DEBUG_OBJECT (playsink, "reusing existing volume element");
    }
  }
  return TRUE;
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
  GstElement *elem;

  chain = g_new0 (GstPlayVisChain, 1);
  chain->chain.playsink = playsink;

  GST_DEBUG_OBJECT (playsink, "making vis chain %p", chain);

  chain->chain.bin = gst_bin_new ("visbin");
  bin = GST_BIN_CAST (chain->chain.bin);
  gst_object_ref_sink (bin);

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
    GST_DEBUG_OBJECT (playsink, "trying configure vis");
    chain->vis = try_element (playsink, playsink->visualisation, FALSE);
  }
  if (chain->vis == NULL) {
    GST_DEBUG_OBJECT (playsink, "trying goom");
    elem = gst_element_factory_make ("goom", "vis");
    chain->vis = try_element (playsink, elem, TRUE);
  }
  if (chain->vis == NULL)
    goto no_goom;

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
  gboolean need_audio, need_video, need_vis, need_text, need_subp;

  GST_DEBUG_OBJECT (playsink, "reconfiguring");

  /* assume we need nothing */
  need_audio = need_video = need_vis = need_text = need_subp = FALSE;

  GST_PLAY_SINK_LOCK (playsink);
  GST_OBJECT_LOCK (playsink);
  /* get flags, there are protected with the object lock */
  flags = playsink->flags;
  GST_OBJECT_UNLOCK (playsink);

  /* figure out which components we need */
  if (flags & GST_PLAY_FLAG_TEXT && (playsink->text_pad || playsink->subp_pad)) {
    /* we have a text_pad and we need text rendering, in this case we need a
     * video_pad to combine the video with the text */
    if (!playsink->video_pad)
      goto subs_but_no_video;

    /* we have subtitles and we are requested to show it, we also need to show
     * video in this case. */
    need_video = TRUE;
    need_text = (playsink->text_pad != NULL);
    need_subp = (playsink->subp_pad != NULL);

    /* we can't handle both of them yet (FIXME: wouldn't it be better to just
     * pick one and ignore the other then instead of erroring out?) */
    if (need_text && need_subp)
      goto subs_and_text;
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
    gboolean raw, async, queue;

    /* we need a raw sink when we do vis or when we have a raw pad */
    raw = need_vis ? TRUE : playsink->video_pad_raw;
    /* we try to set the sink async=FALSE when we need vis, this way we can
     * avoid a queue in the audio chain. */
    async = !need_vis;
    /* put a little queue in front of the video but only if we are not doing
     * subpictures because then we will add the queue in front of the subpicture
     * mixer to minimize latency. */
    queue = (need_subp == FALSE);

    GST_DEBUG_OBJECT (playsink, "adding video, raw %d",
        playsink->video_pad_raw);

    if (playsink->videochain) {
      /* try to reactivate the chain */
      if (!setup_video_chain (playsink, raw, async, queue)) {
        add_chain (GST_PLAY_CHAIN (playsink->videochain), FALSE);
        activate_chain (GST_PLAY_CHAIN (playsink->videochain), FALSE);
        free_chain ((GstPlayChain *) playsink->videochain);
        playsink->videochain = NULL;
      }
    }

    if (!playsink->videochain) {
      playsink->videochain = gen_video_chain (playsink, raw, async, queue);
    }
    if (playsink->videochain) {
      GST_DEBUG_OBJECT (playsink, "adding video chain");
      add_chain (GST_PLAY_CHAIN (playsink->videochain), TRUE);
      activate_chain (GST_PLAY_CHAIN (playsink->videochain), TRUE);
      /* if we are not part of vis or subtitles, set the ghostpad target */
      if (!need_vis && !need_text && playsink->text_pad == NULL) {
        GST_DEBUG_OBJECT (playsink, "ghosting video sinkpad");
        gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->video_pad),
            playsink->videochain->sinkpad);
      }
    }
  } else {
    GST_DEBUG_OBJECT (playsink, "no video needed");
    if (playsink->videochain) {
      GST_DEBUG_OBJECT (playsink, "removing video chain");
      if (playsink->vischain) {
        GstPad *srcpad;

        GST_DEBUG_OBJECT (playsink, "unlinking vis chain");

        /* also had visualisation, release the tee srcpad before we then
         * unlink the video from it */
        if (playsink->audio_tee_vissrc) {
          gst_element_release_request_pad (playsink->audio_tee,
              playsink->audio_tee_vissrc);
          gst_object_unref (playsink->audio_tee_vissrc);
          playsink->audio_tee_vissrc = NULL;
        }
        srcpad =
            gst_element_get_static_pad (playsink->vischain->chain.bin, "src");
        gst_pad_unlink (srcpad, playsink->videochain->sinkpad);
      }
      add_chain (GST_PLAY_CHAIN (playsink->videochain), FALSE);
      activate_chain (GST_PLAY_CHAIN (playsink->videochain), FALSE);
    }
    if (playsink->video_pad)
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->video_pad), NULL);
  }

  if (need_text) {
    GST_DEBUG_OBJECT (playsink, "adding text");
    if (!playsink->textchain) {
      GST_DEBUG_OBJECT (playsink, "creating text chain");
      playsink->textchain = gen_text_chain (playsink);
    }
    if (playsink->textchain) {
      GST_DEBUG_OBJECT (playsink, "adding text chain");
      add_chain (GST_PLAY_CHAIN (playsink->textchain), TRUE);
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->text_pad),
          playsink->textchain->textsinkpad);
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->video_pad),
          playsink->textchain->videosinkpad);
      gst_pad_link (playsink->textchain->srcpad, playsink->videochain->sinkpad);
      activate_chain (GST_PLAY_CHAIN (playsink->textchain), TRUE);
      if (playsink->textchain->overlay)
        g_object_set (playsink->textchain->overlay, "silent", FALSE, NULL);
    }
  } else {
    GST_DEBUG_OBJECT (playsink, "no text needed");
    /* we have no subtitles/text or we are requested to not show them */
    if (playsink->textchain) {
      if (playsink->text_pad == NULL) {
        /* no text pad, remove the chain entirely */
        GST_DEBUG_OBJECT (playsink, "removing text chain");
        add_chain (GST_PLAY_CHAIN (playsink->textchain), FALSE);
        activate_chain (GST_PLAY_CHAIN (playsink->textchain), FALSE);
      } else {
        /* we have a chain and a textpad, turn the subtitles off */
        GST_DEBUG_OBJECT (playsink, "turning off the text");
        if (playsink->textchain->overlay)
          g_object_set (playsink->textchain->overlay, "silent", TRUE, NULL);
      }
    }
    if (!need_video && playsink->video_pad)
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->video_pad), NULL);
    if (playsink->text_pad)
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->text_pad), NULL);
  }

  if (need_subp && playsink->videochain) {
    GST_DEBUG_OBJECT (playsink, "adding subpicture");
    if (!playsink->subpchain) {
      GST_DEBUG_OBJECT (playsink, "creating subpicture chain");
      playsink->subpchain = gen_subp_chain (playsink);
    }
    if (playsink->subpchain) {
      GST_DEBUG_OBJECT (playsink, "adding subp chain");
      add_chain (GST_PLAY_CHAIN (playsink->subpchain), TRUE);
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->subp_pad),
          playsink->subpchain->subpsinkpad);
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->video_pad),
          playsink->subpchain->videosinkpad);
      gst_pad_link (playsink->subpchain->srcpad, playsink->videochain->sinkpad);
      activate_chain (GST_PLAY_CHAIN (playsink->subpchain), TRUE);
    }
  } else {
    GST_DEBUG_OBJECT (playsink, "no subpicture needed");
    /* we have no subpicture or we are requested to not show them */
    if (playsink->subpchain) {
      if (playsink->subp_pad == NULL) {
        /* no subpicture pad, remove the chain entirely */
        GST_DEBUG_OBJECT (playsink, "removing subp chain");
        add_chain (GST_PLAY_CHAIN (playsink->subpchain), FALSE);
        activate_chain (GST_PLAY_CHAIN (playsink->subpchain), FALSE);
      } else {
        /* we have a chain and a subpicture pad, turn the subtitles off */
        GST_DEBUG_OBJECT (playsink, "turning off the subp");
      }
    }
    if (!need_video && playsink->video_pad)
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->video_pad), NULL);
    if (playsink->subp_pad)
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (playsink->subp_pad), NULL);
  }

  if (need_audio) {
    gboolean raw, queue;

    GST_DEBUG_OBJECT (playsink, "adding audio");

    /* get a raw sink if we are asked for a raw pad */
    raw = playsink->audio_pad_raw;
    if (need_vis && playsink->videochain) {
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

    if (playsink->audiochain) {
      /* try to reactivate the chain */
      if (!setup_audio_chain (playsink, raw, queue)) {
        GST_DEBUG_OBJECT (playsink, "removing current audio chain");
        if (playsink->audio_tee_asrc) {
          gst_element_release_request_pad (playsink->audio_tee,
              playsink->audio_tee_asrc);
          gst_object_unref (playsink->audio_tee_asrc);
          playsink->audio_tee_asrc = NULL;
        }
        add_chain (GST_PLAY_CHAIN (playsink->audiochain), FALSE);
        activate_chain (GST_PLAY_CHAIN (playsink->audiochain), FALSE);
        playsink->audiochain->volume = NULL;
        playsink->audiochain->mute = NULL;
        free_chain ((GstPlayChain *) playsink->audiochain);
        playsink->audiochain = NULL;
        playsink->volume_changed = playsink->mute_changed = FALSE;
      }
    }

    if (!playsink->audiochain) {
      GST_DEBUG_OBJECT (playsink, "creating new audio chain");
      playsink->audiochain = gen_audio_chain (playsink, raw, queue);
    }

    if (playsink->audiochain) {
      GST_DEBUG_OBJECT (playsink, "adding audio chain");
      if (playsink->audio_tee_asrc == NULL) {
        playsink->audio_tee_asrc =
            gst_element_get_request_pad (playsink->audio_tee, "src%d");
      }
      add_chain (GST_PLAY_CHAIN (playsink->audiochain), TRUE);
      activate_chain (GST_PLAY_CHAIN (playsink->audiochain), TRUE);
      gst_pad_link (playsink->audio_tee_asrc, playsink->audiochain->sinkpad);
    }
  } else {
    GST_DEBUG_OBJECT (playsink, "no audio needed");
    /* we have no audio or we are requested to not play audio */
    if (playsink->audiochain) {
      GST_DEBUG_OBJECT (playsink, "removing audio chain");
      /* release the audio pad */
      if (playsink->audio_tee_asrc) {
        gst_element_release_request_pad (playsink->audio_tee,
            playsink->audio_tee_asrc);
        gst_object_unref (playsink->audio_tee_asrc);
        playsink->audio_tee_asrc = NULL;
      }
      if (playsink->audiochain->sink_volume) {
        playsink->audiochain->volume = NULL;
        playsink->audiochain->mute = NULL;
      }
      add_chain (GST_PLAY_CHAIN (playsink->audiochain), FALSE);
      activate_chain (GST_PLAY_CHAIN (playsink->audiochain), FALSE);
    }
  }

  if (need_vis) {
    GstPad *srcpad;

    if (!playsink->vischain)
      playsink->vischain = gen_vis_chain (playsink);

    GST_DEBUG_OBJECT (playsink, "adding visualisation");

    if (playsink->vischain) {
      GST_DEBUG_OBJECT (playsink, "setting up vis chain");
      srcpad =
          gst_element_get_static_pad (playsink->vischain->chain.bin, "src");
      add_chain (GST_PLAY_CHAIN (playsink->vischain), TRUE);
      activate_chain (GST_PLAY_CHAIN (playsink->vischain), TRUE);
      if (playsink->audio_tee_vissrc == NULL) {
        playsink->audio_tee_vissrc =
            gst_element_get_request_pad (playsink->audio_tee, "src%d");
      }
      gst_pad_link (playsink->audio_tee_vissrc, playsink->vischain->sinkpad);
      gst_pad_link (srcpad, playsink->videochain->sinkpad);
      gst_object_unref (srcpad);
    }
  } else {
    GST_DEBUG_OBJECT (playsink, "no vis needed");
    if (playsink->vischain) {
      if (playsink->audio_tee_vissrc) {
        gst_element_release_request_pad (playsink->audio_tee,
            playsink->audio_tee_vissrc);
        gst_object_unref (playsink->audio_tee_vissrc);
        playsink->audio_tee_vissrc = NULL;
      }
      GST_DEBUG_OBJECT (playsink, "removing vis chain");
      add_chain (GST_PLAY_CHAIN (playsink->vischain), FALSE);
      activate_chain (GST_PLAY_CHAIN (playsink->vischain), FALSE);
    }
  }
  do_async_done (playsink);
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
subs_and_text:
  {
    GST_ELEMENT_ERROR (playsink, STREAM, FORMAT,
        (_("Can't display both text subtitles and subpictures.")),
        ("Have text pad and subpicture pad"));
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
  gboolean activate = TRUE;
  const gchar *pad_name = NULL;

  GST_DEBUG_OBJECT (playsink, "request pad type %d", type);

  GST_PLAY_SINK_LOCK (playsink);
  switch (type) {
    case GST_PLAY_SINK_TYPE_AUDIO_RAW:
      pad_name = "audio_raw_sink";
      raw = TRUE;
    case GST_PLAY_SINK_TYPE_AUDIO:
      if (pad_name == NULL)
        pad_name = "audio_sink";
      if (!playsink->audio_tee) {
        GST_LOG_OBJECT (playsink, "creating tee");
        /* create tee when needed. This element will feed the audio sink chain
         * and the vis chain. */
        playsink->audio_tee = gst_element_factory_make ("tee", "audiotee");
        playsink->audio_tee_sink =
            gst_element_get_static_pad (playsink->audio_tee, "sink");
        gst_bin_add (GST_BIN_CAST (playsink), playsink->audio_tee);
        gst_element_set_state (playsink->audio_tee, GST_STATE_PAUSED);
      } else {
        gst_element_set_state (playsink->audio_tee, GST_STATE_PAUSED);
      }
      if (!playsink->audio_pad) {
        GST_LOG_OBJECT (playsink, "ghosting tee sinkpad");
        playsink->audio_pad =
            gst_ghost_pad_new (pad_name, playsink->audio_tee_sink);
        created = TRUE;
      }
      playsink->audio_pad_raw = raw;
      res = playsink->audio_pad;
      break;
    case GST_PLAY_SINK_TYPE_VIDEO_RAW:
      pad_name = "video_raw_sink";
      raw = TRUE;
    case GST_PLAY_SINK_TYPE_VIDEO:
      if (pad_name == NULL)
        pad_name = "video_sink";
      if (!playsink->video_pad) {
        GST_LOG_OBJECT (playsink, "ghosting videosink");
        playsink->video_pad =
            gst_ghost_pad_new_no_target (pad_name, GST_PAD_SINK);
        created = TRUE;
      }
      playsink->video_pad_raw = raw;
      res = playsink->video_pad;
      break;
    case GST_PLAY_SINK_TYPE_TEXT:
      GST_LOG_OBJECT (playsink, "ghosting text");
      if (!playsink->text_pad) {
        playsink->text_pad =
            gst_ghost_pad_new_no_target ("text_sink", GST_PAD_SINK);
        created = TRUE;
      }
      res = playsink->text_pad;
      break;
    case GST_PLAY_SINK_TYPE_FLUSHING:
    {
      gchar *padname;

      /* we need a unique padname for the flushing pad. */
      padname = g_strdup_printf ("flushing_%d", playsink->count);
      res = gst_ghost_pad_new_no_target (padname, GST_PAD_SINK);
      g_free (padname);
      playsink->count++;
      activate = FALSE;
      created = TRUE;
      break;
    }
    case GST_PLAY_SINK_TYPE_SUBPIC:
      GST_LOG_OBJECT (playsink, "ghosting subpicture pad");
      if (!playsink->subp_pad) {
        playsink->subp_pad =
            gst_ghost_pad_new_no_target ("subp_sink", GST_PAD_SINK);
        created = TRUE;
      }
      res = playsink->subp_pad;
      break;
    default:
      res = NULL;
      break;
  }
  GST_PLAY_SINK_UNLOCK (playsink);

  if (created && res) {
    /* we have to add the pad when it's active or we get an error when the
     * element is 'running' */
    gst_pad_set_active (res, TRUE);
    gst_element_add_pad (GST_ELEMENT_CAST (playsink), res);
    if (!activate)
      gst_pad_set_active (res, activate);
  }

  return res;
}

static GstPad *
gst_play_sink_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name)
{
  GstPlaySink *psink;
  GstPad *pad;
  GstElementClass *klass;
  GstPlaySinkType type;
  const gchar *tplname;

  g_return_val_if_fail (templ != NULL, NULL);

  GST_DEBUG_OBJECT (element, "name:%s", name);

  psink = GST_PLAY_SINK (element);
  klass = GST_ELEMENT_GET_CLASS (element);
  tplname = GST_PAD_TEMPLATE_NAME_TEMPLATE (templ);

  /* Figure out the GstPlaySinkType based on the template */
  if (!strcmp (tplname, "audio_sink"))
    type = GST_PLAY_SINK_TYPE_AUDIO;
  else if (!strcmp (tplname, "aduio_raw_sink"))
    type = GST_PLAY_SINK_TYPE_AUDIO_RAW;
  else if (!strcmp (tplname, "video_sink"))
    type = GST_PLAY_SINK_TYPE_VIDEO;
  else if (!strcmp (tplname, "video_raw_sink"))
    type = GST_PLAY_SINK_TYPE_VIDEO_RAW;
  else if (!strcmp (tplname, "text_sink"))
    type = GST_PLAY_SINK_TYPE_TEXT;
  else if (!strcmp (tplname, "subpicsink"))
    type = GST_PLAY_SINK_TYPE_SUBPIC;
  else
    goto unknown_template;

  pad = gst_play_sink_request_pad (psink, type);
  return pad;

unknown_template:
  GST_WARNING_OBJECT (element, "Unknown pad template");
  return NULL;
}

void
gst_play_sink_release_pad (GstPlaySink * playsink, GstPad * pad)
{
  GstPad **res = NULL;
  gboolean untarget = TRUE;

  GST_DEBUG_OBJECT (playsink, "release pad %" GST_PTR_FORMAT, pad);

  GST_PLAY_SINK_LOCK (playsink);
  if (pad == playsink->video_pad) {
    res = &playsink->video_pad;
  } else if (pad == playsink->audio_pad) {
    res = &playsink->audio_pad;
  } else if (pad == playsink->text_pad) {
    res = &playsink->text_pad;
  } else if (pad == playsink->subp_pad) {
    res = &playsink->subp_pad;
  } else {
    /* try to release the given pad anyway, these could be the FLUSHING pads. */
    res = &pad;
    untarget = FALSE;
  }
  GST_PLAY_SINK_UNLOCK (playsink);

  if (*res) {
    GST_DEBUG_OBJECT (playsink, "deactivate pad %" GST_PTR_FORMAT, *res);
    gst_pad_set_active (*res, FALSE);
    if (untarget) {
      GST_DEBUG_OBJECT (playsink, "untargeting pad %" GST_PTR_FORMAT, *res);
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (*res), NULL);
    }
    GST_DEBUG_OBJECT (playsink, "remove pad %" GST_PTR_FORMAT, *res);
    gst_element_remove_pad (GST_ELEMENT_CAST (playsink), *res);
    *res = NULL;
  }
}

static void
gst_play_sink_release_request_pad (GstElement * element, GstPad * pad)
{
  GstPlaySink *psink = GST_PLAY_SINK (element);

  gst_play_sink_release_pad (psink, pad);
}

static void
gst_play_sink_handle_message (GstBin * bin, GstMessage * message)
{
  GstPlaySink *playsink;

  playsink = GST_PLAY_SINK_CAST (bin);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_STEP_DONE:
    {
      GstFormat format;
      guint64 amount;
      gdouble rate;
      gboolean flush, intermediate, eos;
      guint64 duration;

      GST_INFO_OBJECT (playsink, "Handling step-done message");
      gst_message_parse_step_done (message, &format, &amount, &rate, &flush,
          &intermediate, &duration, &eos);

      if (format == GST_FORMAT_BUFFERS) {
        /* for the buffer format, we align the other streams */
        if (playsink->audiochain) {
          GstEvent *event;

          event =
              gst_event_new_step (GST_FORMAT_TIME, duration, rate, flush,
              intermediate);

          if (!gst_element_send_event (playsink->audiochain->chain.bin, event)) {
            GST_DEBUG_OBJECT (playsink, "Event failed when sent to audio sink");
          }
        }
      }
      GST_BIN_CLASS (gst_play_sink_parent_class)->handle_message (bin, message);
      break;
    }
    default:
      GST_BIN_CLASS (gst_play_sink_parent_class)->handle_message (bin, message);
      break;
  }
}

/* Send an event to our sinks until one of them works; don't then send to the
 * remaining sinks (unlike GstBin)
 * Special case: If a subpicture or text sink is set we need to send the event
 * to them in case it's source is different from the a/v stream's source.
 */
static gboolean
gst_play_sink_send_event_to_sink (GstPlaySink * playsink, GstEvent * event)
{
  gboolean res = TRUE;

  if (playsink->textchain && playsink->textchain->sink) {
    gst_event_ref (event);
    if ((res = gst_element_send_event (playsink->textchain->chain.bin, event))) {
      GST_DEBUG_OBJECT (playsink, "Sent event succesfully to text sink");
    } else {
      GST_DEBUG_OBJECT (playsink, "Event failed when sent to text sink");
    }
  }

  if (playsink->subpchain && playsink->subpchain->sink) {
    gst_event_ref (event);
    if ((res = gst_element_send_event (playsink->subpchain->chain.bin, event))) {
      GST_DEBUG_OBJECT (playsink, "Sent event succesfully to subpicture sink");
    } else {
      GST_DEBUG_OBJECT (playsink, "Event failed when sent to subpicture sink");
    }
  }

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
  GstPlaySink *playsink;

  playsink = GST_PLAY_SINK_CAST (element);

  switch (event_type) {
    case GST_EVENT_SEEK:
      GST_DEBUG_OBJECT (element, "Sending event to a sink");
      res = gst_play_sink_send_event_to_sink (playsink, event);
      break;
    case GST_EVENT_STEP:
    {
      GstFormat format;
      guint64 amount;
      gdouble rate;
      gboolean flush, intermediate;

      gst_event_parse_step (event, &format, &amount, &rate, &flush,
          &intermediate);

      if (format == GST_FORMAT_BUFFERS) {
        /* for buffers, we will try to step video frames, for other formats we
         * send the step to all sinks */
        res = gst_play_sink_send_event_to_sink (playsink, event);
      } else {
        res =
            GST_ELEMENT_CLASS (gst_play_sink_parent_class)->send_event (element,
            event);
      }
      break;
    }
    default:
      res =
          GST_ELEMENT_CLASS (gst_play_sink_parent_class)->send_event (element,
          event);
      break;
  }
  return res;
}

static GstStateChangeReturn
gst_play_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstStateChangeReturn bret;

  GstPlaySink *playsink;

  playsink = GST_PLAY_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* we want to go async to PAUSED until we managed to configure and add the
       * sinks */
      do_async_start (playsink);
      ret = GST_STATE_CHANGE_ASYNC;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (playsink->audiochain && playsink->audiochain->sink_volume) {
        /* remove our links to the mute and volume elements when they were
         * provided by a sink */
        playsink->audiochain->volume = NULL;
        playsink->audiochain->mute = NULL;
      }
      ret = GST_STATE_CHANGE_SUCCESS;
      break;
    default:
      /* all other state changes return SUCCESS by default, this value can be
       * overridden by the result of the children */
      ret = GST_STATE_CHANGE_SUCCESS;
      break;
  }

  /* do the state change of the children */
  bret =
      GST_ELEMENT_CLASS (gst_play_sink_parent_class)->change_state (element,
      transition);
  /* now look at the result of our children and adjust the return value */
  switch (bret) {
    case GST_STATE_CHANGE_FAILURE:
      /* failure, we stop */
      goto activate_failed;
    case GST_STATE_CHANGE_NO_PREROLL:
      /* some child returned NO_PREROLL. This is strange but we never know. We
       * commit our async state change (if any) and return the NO_PREROLL */
      do_async_done (playsink);
      ret = bret;
      break;
    case GST_STATE_CHANGE_ASYNC:
      /* some child was async, return this */
      ret = bret;
      break;
    default:
      /* return our previously configured return value */
      break;
  }

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* FIXME Release audio device when we implement that */
      playsink->need_async_start = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* remove sinks we added */
      if (playsink->videochain) {
        activate_chain (GST_PLAY_CHAIN (playsink->videochain), FALSE);
        add_chain (GST_PLAY_CHAIN (playsink->videochain), FALSE);
      }
      if (playsink->audiochain) {
        activate_chain (GST_PLAY_CHAIN (playsink->audiochain), FALSE);
        add_chain (GST_PLAY_CHAIN (playsink->audiochain), FALSE);
      }
      if (playsink->vischain) {
        activate_chain (GST_PLAY_CHAIN (playsink->vischain), FALSE);
        add_chain (GST_PLAY_CHAIN (playsink->vischain), FALSE);
      }
      if (playsink->textchain) {
        activate_chain (GST_PLAY_CHAIN (playsink->textchain), FALSE);
        add_chain (GST_PLAY_CHAIN (playsink->textchain), FALSE);
      }
      if (playsink->subpchain) {
        activate_chain (GST_PLAY_CHAIN (playsink->subpchain), FALSE);
        add_chain (GST_PLAY_CHAIN (playsink->subpchain), FALSE);
      }
      do_async_done (playsink);
      break;
    default:
      break;
  }
  return ret;

  /* ERRORS */
activate_failed:
  {
    GST_DEBUG_OBJECT (element,
        "element failed to change states -- activation problem?");
    return GST_STATE_CHANGE_FAILURE;
  }
}

static void
gst_play_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec)
{
  GstPlaySink *playsink = GST_PLAY_SINK (object);

  switch (prop_id) {
    case PROP_FLAGS:
      gst_play_sink_set_flags (playsink, g_value_get_flags (value));
      break;
    case PROP_VOLUME:
      gst_play_sink_set_volume (playsink, g_value_get_double (value));
      break;
    case PROP_MUTE:
      gst_play_sink_set_mute (playsink, g_value_get_boolean (value));
      break;
    case PROP_FONT_DESC:
      gst_play_sink_set_font_desc (playsink, g_value_get_string (value));
      break;
    case PROP_VIS_PLUGIN:
      gst_play_sink_set_vis_plugin (playsink, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, spec);
      break;
  }
}

static void
gst_play_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec)
{
  GstPlaySink *playsink = GST_PLAY_SINK (object);

  switch (prop_id) {
    case PROP_FLAGS:
      g_value_set_flags (value, gst_play_sink_get_flags (playsink));
      break;
    case PROP_VOLUME:
      g_value_set_double (value, gst_play_sink_get_volume (playsink));
      break;
    case PROP_MUTE:
      g_value_set_boolean (value, gst_play_sink_get_mute (playsink));
      break;
    case PROP_FONT_DESC:
      g_value_take_string (value, gst_play_sink_get_font_desc (playsink));
      break;
    case PROP_VIS_PLUGIN:
      g_value_take_object (value, gst_play_sink_get_vis_plugin (playsink));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, spec);
      break;
  }
}


gboolean
gst_play_sink_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_play_sink_debug, "playsink", 0, "play bin");

  return gst_element_register (plugin, "playsink", GST_RANK_NONE,
      GST_TYPE_PLAY_SINK);
}
