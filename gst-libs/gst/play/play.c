/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
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

#include "play.h"

#define TICK_INTERVAL_MSEC 200

enum
{
  TIME_TICK,
  STREAM_LENGTH,
  HAVE_VIDEO_SIZE,
  LAST_SIGNAL
};

struct _GstPlayPrivate {  
  char *location;
  
  GHashTable *elements;
  
  gint64 time_nanos;
  gint64 length_nanos;
  
  gint get_length_attempt;
  
  gint     tick_unblock_remaining; /* how many msecs left
                                      to unblock due to seeking */

  guint tick_id;
  guint length_id;
  
  gulong handoff_hid;

  /* error/debug handling */
  GError *error;
  gchar *debug;
};

static guint gst_play_signals[LAST_SIGNAL] = { 0 };

static GstPipelineClass *parent_class = NULL;

/* ======================================================= */
/*                                                         */
/*                    Private Methods                      */
/*                                                         */
/* ======================================================= */

static GQuark
gst_play_error_quark (void)
{
  static GQuark quark = 0;
  if (quark == 0)
    quark = g_quark_from_static_string ("gst-play-error-quark");
  return quark;
}

/* General GError creation */
static void
gst_play_error_create (GError ** error, const gchar *message)
{
  /* check if caller wanted an error reported */
  if (error == NULL)
    return;

  *error = g_error_new (GST_PLAY_ERROR, 0, message);
  return;
}

/* GError creation when plugin is missing */
/* FIXME: what if multiple elements could have been used and they're all
 * missing ? varargs ? */
static void
gst_play_error_plugin (const gchar *element, GError ** error)
{
  gchar *message;

  message = g_strdup_printf ("The %s element could not be found. "
                             "This element is essential for playback. "
                             "Please install the right plug-in and verify "
                             "that it works by running 'gst-inspect %s'",
                             element, element);
  gst_play_error_create (error, message);
  g_free (message);
  return;
}

#define GST_PLAY_MAKE_OR_ERROR(el, factory, name, error)	\
G_STMT_START {							\
  el = gst_element_factory_make (factory, name);		\
  if (!GST_IS_ELEMENT (el))					\
  {								\
    gst_play_error_plugin (factory, error);			\
    return FALSE;						\
  }								\
} G_STMT_END

#define GST_PLAY_ERROR_RETURN(error, message)			\
G_STMT_START {							\
  gst_play_error_create (error, message);			\
    return FALSE;						\
} G_STMT_END


static gboolean
gst_play_pipeline_setup (GstPlay *play, GError **error)
{
  /* Threads */
  GstElement *work_thread, *audio_thread, *video_thread;
  /* Main Thread elements */
  GstElement *source, *autoplugger, *audioconvert, *volume, *tee, *identity;
  GstElement *identity_cs;
  /* Visualization bin */
  GstElement *vis_bin, *vis_queue, *vis_element, *vis_cs;
  /* Video Thread elements */
  GstElement *video_queue, *video_switch, *video_cs, *video_balance;
  GstElement *balance_cs, *video_scaler, *video_sink;
  /* Audio Thread elements */
  GstElement *audio_queue, *audio_sink;
  /* Some useful pads */
  GstPad *tee_pad1, *tee_pad2;
  
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  
  /* Creating main thread and its elements */
  {
  GST_PLAY_MAKE_OR_ERROR (work_thread, "thread", "work_thread", error);
  g_hash_table_insert (play->priv->elements, "work_thread", work_thread);
  gst_bin_add (GST_BIN (play), work_thread);

  /* Placeholder for datasrc */
  GST_PLAY_MAKE_OR_ERROR (source, "fakesrc", "source", error);
  g_hash_table_insert (play->priv->elements, "source", source);
  
  /* Autoplugger */
  GST_PLAY_MAKE_OR_ERROR (autoplugger, "spider", "autoplugger", error);
  g_hash_table_insert (play->priv->elements, "autoplugger", autoplugger);
  
  /* Make sure we convert audio to the needed format */
  GST_PLAY_MAKE_OR_ERROR (audioconvert, "audioconvert", "audioconvert", error);
  g_hash_table_insert (play->priv->elements, "audioconvert", audioconvert);
  
  /* Duplicate audio signal to audio sink and visualization thread */
  GST_PLAY_MAKE_OR_ERROR (tee, "tee", "tee", error);
  tee_pad1 = gst_element_get_request_pad (tee, "src%d");
  tee_pad2 = gst_element_get_request_pad (tee, "src%d");
  g_hash_table_insert (play->priv->elements, "tee_pad1", tee_pad1);
  g_hash_table_insert (play->priv->elements, "tee_pad2", tee_pad2);
  g_hash_table_insert (play->priv->elements, "tee", tee);
  
  gst_bin_add_many (GST_BIN (work_thread), source, autoplugger, audioconvert, tee, NULL);
  if (!gst_element_link_many (source, autoplugger, audioconvert, tee, NULL))
    GST_PLAY_ERROR_RETURN (error, "Could not link source thread elements");
  
  /* identity ! colorspace ! switch  */
  GST_PLAY_MAKE_OR_ERROR (identity, "identity", "identity", error);
  g_hash_table_insert (play->priv->elements, "identity", identity);
  
  identity_cs = gst_element_factory_make ("ffcolorspace", "identity_cs");
  if (!GST_IS_ELEMENT (identity_cs)) {
    identity_cs = gst_element_factory_make ("colorspace", "identity_cs");
    if (!GST_IS_ELEMENT (identity_cs))
    {
      gst_play_error_plugin ("colorspace", error);
      return FALSE;
    }
  }
  g_hash_table_insert (play->priv->elements, "identity_cs", identity_cs);
  gst_bin_add_many (GST_BIN (work_thread), identity, identity_cs,  NULL);
  if (!gst_element_link_many (autoplugger, identity, identity_cs, NULL))
    GST_PLAY_ERROR_RETURN (error, "Could not link work thread elements");
  }
  
  /* Visualization bin (note: it s not added to the pipeline yet) */
  {
  vis_bin = gst_bin_new ("vis_bin");
  if (!GST_IS_ELEMENT (vis_bin))
  {
    gst_play_error_plugin ("bin", error);
    return FALSE;
  }
  
  g_hash_table_insert (play->priv->elements, "vis_bin", vis_bin);
  
  /* Buffer queue for video data */
  GST_PLAY_MAKE_OR_ERROR (vis_queue, "queue", "vis_queue", error);
  g_hash_table_insert (play->priv->elements, "vis_queue", vis_queue);
  
  /* Visualization element placeholder */
  GST_PLAY_MAKE_OR_ERROR (vis_element, "identity", "vis_element", error);
  g_hash_table_insert (play->priv->elements, "vis_element", vis_element);
  
  /* Colorspace conversion */
  vis_cs = gst_element_factory_make ("ffcolorspace", "vis_cs");
  if (!GST_IS_ELEMENT (vis_cs)) {
    vis_cs = gst_element_factory_make ("colorspace", "vis_cs");
    if (!GST_IS_ELEMENT (vis_cs))
    {
      gst_play_error_plugin ("colorspace", error);
      return FALSE;
    }
  }
  
  g_hash_table_insert (play->priv->elements, "vis_cs", vis_cs);
  
  gst_bin_add_many (GST_BIN (vis_bin), vis_queue, vis_element, vis_cs, NULL);
  if (!gst_element_link_many (vis_queue, vis_element, vis_cs, NULL))
    GST_PLAY_ERROR_RETURN (error, "Could not link visualisation thread elements");
  gst_element_add_ghost_pad (vis_bin,
                             gst_element_get_pad (vis_cs, "src"), "src");
  }
  /* Creating our video output bin */
  {
  GST_PLAY_MAKE_OR_ERROR (video_thread, "thread", "video_thread", error);
  g_hash_table_insert (play->priv->elements, "video_thread", video_thread);
  gst_bin_add (GST_BIN (work_thread), video_thread);
  
  /* Buffer queue for video data */
  GST_PLAY_MAKE_OR_ERROR (video_queue, "queue", "video_queue", error);
  g_hash_table_insert (play->priv->elements, "video_queue", video_queue);
  
  GST_PLAY_MAKE_OR_ERROR (video_switch, "switch", "video_switch", error);
  g_hash_table_insert (play->priv->elements, "video_switch", video_switch);
  
  /* Colorspace conversion */
  video_cs = gst_element_factory_make ("ffcolorspace", "video_cs");
  if (!GST_IS_ELEMENT (video_cs)) {
    video_cs = gst_element_factory_make ("colorspace", "video_cs");
    if (!GST_IS_ELEMENT (video_cs))
    {
      gst_play_error_plugin ("colorspace", error);
      return FALSE;
    }
  }
  g_hash_table_insert (play->priv->elements, "video_cs", video_cs);
  
  /* Software colorbalance */
  GST_PLAY_MAKE_OR_ERROR (video_balance, "videobalance", "video_balance", error);
  g_hash_table_insert (play->priv->elements, "video_balance", video_balance);
  
  /* Colorspace conversion */
  balance_cs = gst_element_factory_make ("ffcolorspace", "balance_cs");
  if (!GST_IS_ELEMENT (balance_cs)) {
    balance_cs = gst_element_factory_make ("colorspace", "balance_cs");
    if (!GST_IS_ELEMENT (balance_cs))
    {
      gst_play_error_plugin ("colorspace", error);
      return FALSE;
    }
  }
  g_hash_table_insert (play->priv->elements, "balance_cs", balance_cs);
  
  /* Software scaling of video stream */
  GST_PLAY_MAKE_OR_ERROR (video_scaler, "videoscale", "video_scaler", error);
  g_hash_table_insert (play->priv->elements, "video_scaler", video_scaler);
  
  /* Placeholder for future video sink bin */
  GST_PLAY_MAKE_OR_ERROR (video_sink, "fakesink", "video_sink", error);
  g_hash_table_insert (play->priv->elements, "video_sink", video_sink);
  
  gst_bin_add_many (GST_BIN (video_thread), video_queue, video_switch, video_cs,
                    video_balance, balance_cs, video_scaler, video_sink, NULL);
  if (!gst_element_link_many (video_queue, video_switch, video_cs,
                              video_balance, balance_cs, video_scaler,
                              video_sink, NULL))
    GST_PLAY_ERROR_RETURN (error, "Could not link video output thread elements");
  gst_element_add_ghost_pad (video_thread,
                             gst_element_get_pad (video_queue, "sink"),
                             "sink");
  if (!gst_element_link (identity_cs, video_thread))
    GST_PLAY_ERROR_RETURN (error, "Could not link video output thread elements");
  }
  /* Creating our audio output bin 
     { queue ! fakesink } */
  {
  GST_PLAY_MAKE_OR_ERROR (audio_thread, "thread", "audio_thread", error);
  g_hash_table_insert (play->priv->elements, "audio_thread", audio_thread);
  gst_bin_add (GST_BIN (work_thread), audio_thread);
  
  /* Buffer queue for our audio thread */
  GST_PLAY_MAKE_OR_ERROR (audio_queue, "queue", "audio_queue", error);
  g_hash_table_insert (play->priv->elements, "audio_queue", audio_queue);
  
  /* Volume control */
  GST_PLAY_MAKE_OR_ERROR (volume, "volume", "volume", error);
  g_hash_table_insert (play->priv->elements, "volume", volume);
    
  /* Placeholder for future audio sink bin */
  GST_PLAY_MAKE_OR_ERROR (audio_sink, "fakesink", "audio_sink", error);
  g_hash_table_insert (play->priv->elements, "audio_sink", audio_sink);
  
  gst_bin_add_many (GST_BIN (audio_thread), audio_queue, volume, audio_sink, NULL);
  if (!gst_element_link_many (audio_queue, volume, audio_sink, NULL))
    GST_PLAY_ERROR_RETURN (error, "Could not link audio output thread elements");
  gst_element_add_ghost_pad (audio_thread,
                             gst_element_get_pad (audio_queue, "sink"),
                             "sink");
  gst_pad_link (tee_pad2, gst_element_get_pad (audio_queue, "sink"));
  }
  
  return TRUE;
}

static void
gst_play_have_video_size (GstElement *element, gint width,
                          gint height, GstPlay *play)
{
  g_return_if_fail (play != NULL);
  g_return_if_fail (GST_IS_PLAY (play));
  g_signal_emit (G_OBJECT (play), gst_play_signals[HAVE_VIDEO_SIZE],
                 0, width, height);
}

static gboolean
gst_play_tick_callback (GstPlay *play)
{
  GstFormat format = GST_FORMAT_TIME;
  gboolean q = FALSE;
  GstElement *audio_sink_element = NULL;
  
  g_return_val_if_fail (play != NULL, FALSE);
  /* just return without updating the UI when we are in the middle of seeking */
  if (play->priv->tick_unblock_remaining > 0)
  {
    play->priv->tick_unblock_remaining -= TICK_INTERVAL_MSEC;
    return TRUE;
  }
  
  if (!GST_IS_PLAY (play)) {
    play->priv->tick_id = 0;
    return FALSE;
  }
  
  audio_sink_element = g_hash_table_lookup (play->priv->elements,
                                            "audio_sink_element");
  
  if (!GST_IS_ELEMENT (audio_sink_element)) {
    play->priv->tick_id = 0;
    return FALSE;
  }
  
  q = gst_element_query (audio_sink_element, GST_QUERY_POSITION, &format,
                         &(play->priv->time_nanos));
  
  if (q)
    g_signal_emit (G_OBJECT (play), gst_play_signals[TIME_TICK],
                   0,play->priv->time_nanos);
  
  if (GST_STATE (GST_ELEMENT (play)) == GST_STATE_PLAYING)
    return TRUE;
  else {
    play->priv->tick_id = 0;
    return FALSE;
  }
}

static gboolean
gst_play_get_length_callback (GstPlay *play)
{
  GstElement *audio_sink_element, *video_sink_element;
  GstFormat format = GST_FORMAT_TIME;
  gint64 value;
  gboolean q = FALSE;
  
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  
  /* We try to get length from all real sink elements */
  audio_sink_element = g_hash_table_lookup (play->priv->elements,
                                            "audio_sink_element");
  video_sink_element = g_hash_table_lookup (play->priv->elements,
                                            "video_sink_element");
  if (!GST_IS_ELEMENT (audio_sink_element) &&
      !GST_IS_ELEMENT (video_sink_element)) {
    play->priv->length_id = 0;
    return FALSE;
  }
  
  /* Audio first and then Video */
  if (GST_IS_ELEMENT (audio_sink_element))
    q = gst_element_query (audio_sink_element, GST_QUERY_TOTAL, &format,
                           &value);
  if ( (!q) && (GST_IS_ELEMENT (video_sink_element)) )
    q = gst_element_query (video_sink_element, GST_QUERY_TOTAL, &format,
                           &value);
   
  if (q) {
    play->priv->length_nanos = value;
    g_signal_emit (G_OBJECT (play), gst_play_signals[STREAM_LENGTH],
                   0,play->priv->length_nanos);
    play->priv->length_id = 0;
    return FALSE;
  }
  
  play->priv->get_length_attempt++;
  
  /* We try 16 times */
  if (play->priv->get_length_attempt > 15) {
    play->priv->length_id = 0;
    return FALSE;
  }
  else
    return TRUE;
}

static void
gst_play_state_change (GstElement *element, GstElementState old,
                       GstElementState state)
{
  GstPlay *play;
  
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_PLAY (element));
  
  play = GST_PLAY (element);
  
  if (state == GST_STATE_PLAYING) {
    if (play->priv->tick_id) {
      g_source_remove (play->priv->tick_id);
      play->priv->tick_id = 0;
    }
        
    play->priv->tick_id = g_timeout_add (TICK_INTERVAL_MSEC,
                                         (GSourceFunc) gst_play_tick_callback,
                                         play);
      
    play->priv->get_length_attempt = 0;
    
    if (play->priv->length_id) {
      g_source_remove (play->priv->length_id);
      play->priv->length_id = 0;
    }
        
    play->priv->length_id = g_timeout_add (TICK_INTERVAL_MSEC,
                                   (GSourceFunc) gst_play_get_length_callback,
                                   play);
  }
  else {
    if (play->priv->tick_id) {
      g_source_remove (play->priv->tick_id);
      play->priv->tick_id = 0;
    }
    if (play->priv->length_id) {
      g_source_remove (play->priv->length_id);
      play->priv->length_id = 0;
    }
  }
    
  if (GST_ELEMENT_CLASS (parent_class)->state_change)
    GST_ELEMENT_CLASS (parent_class)->state_change (element, old, state);
}

static void
gst_play_identity_handoff (GstElement *identity, GstBuffer *buf, GstPlay *play)
{
  g_signal_handler_disconnect (G_OBJECT (identity), play->priv->handoff_hid);
  play->priv->handoff_hid = 0;
  gst_play_connect_visualization (play, FALSE);
}

/* =========================================== */
/*                                             */
/*         Init & Dispose & Class init         */
/*                                             */
/* =========================================== */

static void
gst_play_dispose (GObject *object)
{
  GstPlay *play;
  
  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_PLAY (object));
  
  play = GST_PLAY (object);
  
  if (play->priv->length_id) {
    g_source_remove (play->priv->length_id);
    play->priv->length_id = 0;
  }
    
  if (play->priv->tick_id) {
    g_source_remove (play->priv->tick_id);
    play->priv->tick_id = 0;
  }
    
  if (play->priv->location) {
    g_free (play->priv->location);
    play->priv->location = NULL;
  }
  
  if (play->priv->elements) {
    g_hash_table_destroy (play->priv->elements);
    play->priv->elements = NULL;
  }
    
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_play_init (GstPlay *play)
{
  play->priv = g_new0 (GstPlayPrivate, 1);
  play->priv->location = NULL;
  play->priv->length_nanos = 0;
  play->priv->time_nanos = 0;
  play->priv->elements = g_hash_table_new (g_str_hash, g_str_equal);
  play->priv->error = NULL;
  play->priv->debug = NULL;
  
  if (!gst_play_pipeline_setup (play, &play->priv->error))
  {
    g_warning ("libgstplay: failed initializing pipeline, error: %s",
               play->priv->error->message);
  }
}

static void
gst_play_class_init (GstPlayClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  
  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_play_dispose;

  element_class->state_change = gst_play_state_change;
  
  gst_play_signals[TIME_TICK] =
    g_signal_new ("time_tick", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (GstPlayClass, time_tick), NULL, NULL,
                  gst_marshal_VOID__INT64, G_TYPE_NONE, 1, G_TYPE_INT64);
  gst_play_signals[STREAM_LENGTH] =
    g_signal_new ("stream_length", G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (GstPlayClass, stream_length), NULL, NULL,
                  gst_marshal_VOID__INT64, G_TYPE_NONE, 1, G_TYPE_INT64);
  gst_play_signals[HAVE_VIDEO_SIZE] =
    g_signal_new ("have_video_size", G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (GstPlayClass, have_video_size), NULL, NULL,
                  gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2,
                  G_TYPE_INT, G_TYPE_INT);
}

/* ======================================================= */
/*                                                         */
/*                     Public Methods                      */
/*                                                         */
/* ======================================================= */

/**
 * gst_play_set_location:
 * @play: a #GstPlay.
 * @location: a const #char* indicating location to play
 *
 * Set location of @play to @location.
 *
 * Returns: TRUE if location was set successfully.
 */
gboolean
gst_play_set_location (GstPlay *play, const char *location)
{
  GstElement *work_thread, *source, *autoplugger;
  GstElement *audioconvert, *identity;
  
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  
  if (play->priv->location)
    g_free (play->priv->location);
  
  play->priv->location = g_strdup (location);
  
  if (GST_STATE (GST_ELEMENT (play)) != GST_STATE_READY)
    gst_element_set_state (GST_ELEMENT (play), GST_STATE_READY);
  
  work_thread = g_hash_table_lookup (play->priv->elements, "work_thread");
  if (!GST_IS_ELEMENT (work_thread))
    return FALSE;
  source = g_hash_table_lookup (play->priv->elements, "source");
  if (!GST_IS_ELEMENT (source))
    return FALSE;
  autoplugger = g_hash_table_lookup (play->priv->elements, "autoplugger");
  if (!GST_IS_ELEMENT (autoplugger))
    return FALSE;
  audioconvert = g_hash_table_lookup (play->priv->elements, "audioconvert");
  if (!GST_IS_ELEMENT (audioconvert))
    return FALSE;
  identity = g_hash_table_lookup (play->priv->elements, "identity");
  if (!GST_IS_ELEMENT (identity))
    return FALSE;
  
  /* Spider can autoplugg only once. We remove the actual one and put a new
     autoplugger */
  gst_element_unlink (source, autoplugger);
  gst_element_unlink (autoplugger, identity);
  gst_element_unlink (autoplugger, audioconvert);
  gst_bin_remove (GST_BIN (work_thread), autoplugger);
  
  autoplugger = gst_element_factory_make ("spider", "autoplugger");
  if (!GST_IS_ELEMENT (autoplugger))
    return FALSE;
  
  gst_bin_add (GST_BIN (work_thread), autoplugger);
  gst_element_link (source, autoplugger);
  gst_element_link (autoplugger, audioconvert);
  gst_element_link (autoplugger, identity);
  
  g_hash_table_replace (play->priv->elements, "autoplugger", autoplugger);
  
  /* FIXME: Why don't we have an interface to do that kind of stuff ? */
  g_object_set (G_OBJECT (source), "location", play->priv->location, NULL);
  
  play->priv->length_nanos = 0LL;
  play->priv->time_nanos = 0LL;
  
  g_signal_emit (G_OBJECT (play), gst_play_signals[STREAM_LENGTH], 0, 0LL);
  g_signal_emit (G_OBJECT (play), gst_play_signals[TIME_TICK], 0, 0LL);
  
  return TRUE;
}

/**
 * gst_play_get_location:
 * @play: a #GstPlay.
 *
 * Get current location of @play.
 *
 * Returns: a const #char* pointer to current location.
 */
char *
gst_play_get_location (GstPlay *play)
{
  g_return_val_if_fail (play != NULL, NULL);
  g_return_val_if_fail (GST_IS_PLAY (play), NULL);
  return g_strdup (play->priv->location);
}

/**
 * gst_play_seek_to_time:
 * @play: a #GstPlay.
 * @time_nanos: a #gint64 indicating a time position.
 *
 * Performs a seek on @play until @time_nanos.
 */
gboolean
gst_play_seek_to_time (GstPlay * play, gint64 time_nanos)
{
  GstElement *audio_seek_element, *video_seek_element, *audio_sink_element;
  
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  
  if (time_nanos < 0LL)
    time_nanos = 0LL;
  
  audio_seek_element = g_hash_table_lookup (play->priv->elements,
                                            "audioconvert");
  audio_sink_element = g_hash_table_lookup (play->priv->elements,
                                            "audio_sink_element");
  video_seek_element = g_hash_table_lookup (play->priv->elements,
                                            "identity");
  
  if (GST_IS_ELEMENT (audio_seek_element) &&
      GST_IS_ELEMENT (video_seek_element) &&
      GST_IS_ELEMENT (audio_sink_element)) {
    gboolean s = FALSE;
   
    /* HACK: block tick signal from idler for 500 msec */
    /* GStreamer can't currently report when seeking is finished,
       so we just chose a .5 sec default block time */
    play->priv->tick_unblock_remaining = 500;
    
    s = gst_element_seek (video_seek_element, GST_FORMAT_TIME |
                          GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH,
                          time_nanos);
    if (!s) {
      s = gst_element_seek (audio_seek_element, GST_FORMAT_TIME |
                            GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH,
                            time_nanos);
    }
    
    if (s) {
      GstFormat format = GST_FORMAT_TIME;
      gboolean q = FALSE;
      
      q = gst_element_query (audio_sink_element, GST_QUERY_POSITION, &format,
                             &(play->priv->time_nanos));

      if (q)
        g_signal_emit (G_OBJECT (play), gst_play_signals[TIME_TICK],
                       0,play->priv->time_nanos);
    }
  }
  
  return TRUE;
}

/**
 * gst_play_set_data_src:
 * @play: a #GstPlay.
 * @data_src: a #GstElement.
 *
 * Set @data_src as the source element of @play.
 *
 * Returns: TRUE if call succeeded.
 */
gboolean
gst_play_set_data_src (GstPlay *play, GstElement *data_src)
{
  GstElement *work_thread, *old_data_src, *autoplugger;
  
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  
  /* We bring back the pipeline to READY */
  if (GST_STATE (GST_ELEMENT (play)) != GST_STATE_READY)
    gst_element_set_state (GST_ELEMENT (play), GST_STATE_READY);
  
  /* Getting needed objects */
  work_thread = g_hash_table_lookup (play->priv->elements, "work_thread");
  if (!GST_IS_ELEMENT (work_thread))
    return FALSE;
  old_data_src = g_hash_table_lookup (play->priv->elements, "source");
  if (!GST_IS_ELEMENT (old_data_src))
    return FALSE;
  autoplugger = g_hash_table_lookup (play->priv->elements, "autoplugger");
  if (!GST_IS_ELEMENT (autoplugger))
    return FALSE;
  
  /* Unlinking old source from autoplugger, removing it from pipeline, adding
     the new one and connecting it to autoplugger FIXME: we should put a new
     autoplugger here as spider can autoplugg only once */
  gst_element_unlink (old_data_src, autoplugger);
  gst_bin_remove (GST_BIN (work_thread), old_data_src);
  gst_bin_add (GST_BIN (work_thread), data_src);
  gst_element_link (data_src, autoplugger);
  
  g_hash_table_replace (play->priv->elements, "source", data_src);
  
  return TRUE;
}

/**
 * gst_play_set_video_sink:
 * @play: a #GstPlay.
 * @video_sink: a #GstElement.
 *
 * Set @video_sink as the video sink element of @play.
 *
 * Returns: TRUE if call succeeded.
 */
gboolean
gst_play_set_video_sink (GstPlay *play, GstElement *video_sink)
{
  GstElement *video_thread, *old_video_sink, *video_scaler, *video_sink_element;
  
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (video_sink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (video_sink), FALSE);
  
  /* We bring back the pipeline to READY */
  if (GST_STATE (GST_ELEMENT (play)) != GST_STATE_READY)
    gst_element_set_state (GST_ELEMENT (play), GST_STATE_READY);
  
  /* Getting needed objects */
  video_thread = g_hash_table_lookup (play->priv->elements, "video_thread");
  if (!GST_IS_ELEMENT (video_thread))
    return FALSE;
  old_video_sink = g_hash_table_lookup (play->priv->elements, "video_sink");
  if (!GST_IS_ELEMENT (old_video_sink))
    return FALSE;
  video_scaler = g_hash_table_lookup (play->priv->elements, "video_scaler");
  if (!GST_IS_ELEMENT (video_scaler))
    return FALSE;
  
  /* Unlinking old video sink from video scaler, removing it from pipeline,
     adding the new one and linking it */
  gst_element_unlink (video_scaler, old_video_sink);
  gst_bin_remove (GST_BIN (video_thread), old_video_sink);
  gst_bin_add (GST_BIN (video_thread), video_sink);
  gst_element_link (video_scaler, video_sink);
  
  g_hash_table_replace (play->priv->elements, "video_sink", video_sink);
  
  video_sink_element = gst_play_get_sink_element (play, video_sink,
                                                  GST_PLAY_SINK_TYPE_VIDEO);
  if (GST_IS_ELEMENT (video_sink_element)) {
    g_hash_table_replace (play->priv->elements, "video_sink_element",
                          video_sink_element);
    if (GST_IS_X_OVERLAY (video_sink_element)) {
      g_signal_connect (G_OBJECT (video_sink_element),
                        "desired_size_changed",
                         G_CALLBACK (gst_play_have_video_size), play);
    }
  } 
  
  gst_element_set_state (video_sink, GST_STATE (GST_ELEMENT(play)));
  
  return TRUE;
}

/**
 * gst_play_set_audio_sink:
 * @play: a #GstPlay.
 * @audio_sink: a #GstElement.
 *
 * Set @audio_sink as the audio sink element of @play.
 *
 * Returns: TRUE if call succeeded.
 */
gboolean
gst_play_set_audio_sink (GstPlay *play, GstElement *audio_sink)
{
  GstElement *old_audio_sink, *audio_thread, *volume, *audio_sink_element;
  
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (audio_sink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (audio_sink), FALSE);
  
  /* We bring back the pipeline to READY */
  if (GST_STATE (GST_ELEMENT (play)) != GST_STATE_READY)
    gst_element_set_state (GST_ELEMENT (play), GST_STATE_READY);
  
  /* Getting needed objects */
  old_audio_sink = g_hash_table_lookup (play->priv->elements, "audio_sink");
  if (!GST_IS_ELEMENT (old_audio_sink))
    return FALSE;
  audio_thread = g_hash_table_lookup (play->priv->elements, "audio_thread");
  if (!GST_IS_ELEMENT (audio_thread))
    return FALSE;
  volume = g_hash_table_lookup (play->priv->elements, "volume");
  if (!GST_IS_ELEMENT (volume))
    return FALSE;
  
  /* Unlinking old audiosink, removing it from pipeline, putting the new one
     and linking it */
  gst_element_unlink (volume, old_audio_sink);
  gst_bin_remove (GST_BIN (audio_thread), old_audio_sink);
  gst_bin_add (GST_BIN (audio_thread), audio_sink);
  gst_element_link (volume, audio_sink);
  
  g_hash_table_replace (play->priv->elements, "audio_sink", audio_sink);
  
  audio_sink_element = gst_play_get_sink_element (play, audio_sink,
                                                  GST_PLAY_SINK_TYPE_AUDIO);
  if (GST_IS_ELEMENT (audio_sink_element)) {
    g_hash_table_replace (play->priv->elements, "audio_sink_element",
                          audio_sink_element);
  }
  
  gst_element_set_state (audio_sink, GST_STATE (GST_ELEMENT(play)));
  
  return TRUE;
}

/**
 * gst_play_set_visualization:
 * @play: a #GstPlay.
 * @element: a #GstElement.
 *
 * Set @video_sink as the video sink element of @play.
 *
 * Returns: TRUE if call succeeded.
 */
gboolean
gst_play_set_visualization (GstPlay *play, GstElement *vis_element)
{
  GstElement *vis_bin, *vis_queue, *old_vis_element, *vis_cs;
  gboolean was_playing = FALSE;
  
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (vis_element != NULL, FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (vis_element), FALSE);
  
  /* Getting needed objects */
  vis_bin = g_hash_table_lookup (play->priv->elements, "vis_bin");
  if (!GST_IS_ELEMENT (vis_bin))
    return FALSE;
  vis_queue = g_hash_table_lookup (play->priv->elements, "vis_queue");
  if (!GST_IS_ELEMENT (vis_queue))
    return FALSE;
  old_vis_element = g_hash_table_lookup (play->priv->elements,
                                         "vis_element");
  if (!GST_IS_ELEMENT (old_vis_element))
    return FALSE;
  vis_cs = g_hash_table_lookup (play->priv->elements, "vis_cs");
  if (!GST_IS_ELEMENT (vis_cs))
    return FALSE;
  
  /* We bring back the pipeline to PAUSED */
  if (GST_STATE (GST_ELEMENT (play)) == GST_STATE_PLAYING) {
    gst_element_set_state (GST_ELEMENT (play), GST_STATE_PAUSED);
    was_playing = TRUE;
  }

  gst_element_unlink_many (vis_queue, old_vis_element, vis_cs, NULL);
  gst_bin_remove (GST_BIN (vis_bin), old_vis_element);
  gst_bin_add (GST_BIN (vis_bin), vis_element);
  gst_element_link_many (vis_queue, vis_element, vis_cs, NULL);
  
  g_hash_table_replace (play->priv->elements, "vis_element", vis_element);
  
  if (was_playing)
    gst_element_set_state (GST_ELEMENT (play), GST_STATE_PLAYING);
  
  return TRUE;
}

/**
 * gst_play_connect_visualization:
 * @play: a #GstPlay.
 * @connect: a #gboolean indicating wether or not
 * visualization should be connected.
 *
 * Connect or disconnect visualization bin in @play.
 *
 * Returns: TRUE if call succeeded.
 */
gboolean
gst_play_connect_visualization (GstPlay * play, gboolean connect)
{
  GstElement *video_thread, *vis_queue, *vis_bin, *video_switch, *identity;
  GstPad *tee_pad1, *vis_queue_pad, *vis_bin_pad, *switch_pad;
  gboolean was_playing = FALSE;
  
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  
  /* Until i fix the switch */
  return TRUE;
  
  /* Getting needed objects */
  video_thread = g_hash_table_lookup (play->priv->elements, "video_thread");
  if (!GST_IS_ELEMENT (video_thread))
    return FALSE;
  vis_bin = g_hash_table_lookup (play->priv->elements, "vis_bin");
  if (!GST_IS_ELEMENT (vis_bin))
    return FALSE;
  vis_queue = g_hash_table_lookup (play->priv->elements, "vis_queue");
  if (!GST_IS_ELEMENT (vis_queue))
    return FALSE;
  video_switch = g_hash_table_lookup (play->priv->elements, "video_switch");
  if (!GST_IS_ELEMENT (video_switch))
    return FALSE;
  identity = g_hash_table_lookup (play->priv->elements, "identity");
  if (!GST_IS_ELEMENT (identity))
    return FALSE;
  tee_pad1 = g_hash_table_lookup (play->priv->elements, "tee_pad1");
  if (!GST_IS_PAD (tee_pad1))
    return FALSE;
  
  vis_queue_pad = gst_element_get_pad (vis_queue, "sink");
  
  /* Check if the vis element is in the pipeline. That means visualization is
     connected already */
  if (gst_element_get_managing_bin (vis_bin)) {
    
    /* If we are supposed to connect then nothing to do we return */
    if (connect) {
      return TRUE;
    }
    
    /* Disconnecting visualization */
    
    /* We bring back the pipeline to PAUSED */
    if (GST_STATE (GST_ELEMENT (play)) == GST_STATE_PLAYING) {
      gst_element_set_state (GST_ELEMENT (play), GST_STATE_PAUSED);
      was_playing = TRUE;
    }
    
    /* Unlinking, removing */
    gst_pad_unlink (tee_pad1, vis_queue_pad);
    vis_bin_pad = gst_element_get_pad (vis_bin, "src");
    switch_pad = gst_pad_get_peer (vis_bin_pad);
    gst_pad_unlink (vis_bin_pad, switch_pad);
    gst_element_release_request_pad (video_switch, switch_pad);
    gst_object_ref (GST_OBJECT (vis_bin));
    gst_bin_remove (GST_BIN (video_thread), vis_bin);
  }
  else {
    
    /* If we are supposed to disconnect then nothing to do we return */
    if (!connect) {
      return TRUE;
    }
    
    /* Connecting visualization */
    
    /* We bring back the pipeline to PAUSED */
    if (GST_STATE (GST_ELEMENT (play)) == GST_STATE_PLAYING) {
      gst_element_set_state (GST_ELEMENT (play), GST_STATE_PAUSED);
      was_playing = TRUE;
    }
    
    /* Adding, linking */
    play->priv->handoff_hid = g_signal_connect (G_OBJECT (identity),
                                "handoff",
                                G_CALLBACK (gst_play_identity_handoff), play);
    gst_bin_add (GST_BIN (video_thread), vis_bin);
    gst_pad_link (tee_pad1, vis_queue_pad);
    gst_element_link (vis_bin, video_switch);
  }

  if (was_playing)
    gst_element_set_state (GST_ELEMENT (play), GST_STATE_PLAYING);
  
  return TRUE;
}

/**
 * gst_play_get_sink_element:
 * @play: a #GstPlay.
 * @element: a #GstElement.
 * @sink_type: a #GstPlaySinkType.
 *
 * Searches recursively for a sink #GstElement with
 * type @sink_type in @element which is supposed to be a #GstBin.
 *
 * Returns: the sink #GstElement of @element.
 */
GstElement *
gst_play_get_sink_element (GstPlay *play,
                           GstElement *element, GstPlaySinkType sink_type)
{
  GList *elements = NULL;
  const GList *pads = NULL;
  gboolean has_src, has_correct_type;

  g_return_val_if_fail (play != NULL, NULL);
  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_PLAY (play), NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  if (!GST_IS_BIN (element)) {
    /* since its not a bin, we'll presume this 
     * element is a sink element */
    return element;
  }

  elements = (GList *) gst_bin_get_list (GST_BIN (element));

  /* traverse all elements looking for a src pad */

  while (elements) {
    element = GST_ELEMENT (elements->data);

    /* Recursivity :) */

    if (GST_IS_BIN (element)) {
      element = gst_play_get_sink_element (play, element, sink_type);
      if (GST_IS_ELEMENT (element))
        return element;
    }
    else {
      pads = gst_element_get_pad_list (element);
      has_src = FALSE;
      has_correct_type = FALSE;
      while (pads) {
        /* check for src pad */
        if (GST_PAD_DIRECTION (GST_PAD (pads->data)) == GST_PAD_SRC) {
          has_src = TRUE;
          break;
        }
        else {
          /* If not a src pad checking caps */
          GstCaps *caps;
          GstStructure *structure;
          gboolean has_video_cap = FALSE;
          gboolean has_audio_cap = FALSE;

          caps = gst_pad_get_caps (GST_PAD (pads->data));
          structure = gst_caps_get_structure (caps, 0);
          
          if (strcmp (gst_structure_get_name (structure),
                                  "audio/x-raw-int") == 0) {
            has_audio_cap = TRUE;
          }
          
          if (strcmp (gst_structure_get_name (structure),
                                   "video/x-raw-yuv") == 0 ||
              strcmp (gst_structure_get_name (structure),
                                   "video/x-raw-rgb") == 0) {
            has_video_cap = TRUE;
          }

          gst_caps_free (caps);
          
          switch (sink_type) {
            case GST_PLAY_SINK_TYPE_AUDIO:
              if (has_audio_cap)
                has_correct_type = TRUE;
              break;
            case GST_PLAY_SINK_TYPE_VIDEO:
              if (has_video_cap)
                has_correct_type = TRUE;
              break;
            case GST_PLAY_SINK_TYPE_ANY:
              if ((has_video_cap) || (has_audio_cap))
                has_correct_type = TRUE;
              break;
            default:
              has_correct_type = FALSE;
          }
        }
        
        pads = g_list_next (pads);
        
      }
      
      if ((!has_src) && (has_correct_type))
        return element;
    }
    
    elements = g_list_next (elements);
  }
  
  /* we didn't find a sink element */
  
  return NULL;
}

GstPlay *
gst_play_new (GError **error)
{
  GstPlay *play = g_object_new (GST_TYPE_PLAY, NULL);

  if (play->priv->error)
  {
    if (error)
    {
      *error = play->priv->error;
      play->priv->error = NULL;
    }
    else
    {
      g_warning ("Error creating GstPlay object.\n%s", play->priv->error->message);
      g_error_free (play->priv->error);
    }
  }
  return play;
}

/* =========================================== */
/*                                             */
/*          Object typing & Creation           */
/*                                             */
/* =========================================== */

GType
gst_play_get_type (void)
{
  static GType play_type = 0;

  if (!play_type) {
    static const GTypeInfo play_info = {
      sizeof (GstPlayClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_play_class_init,
      NULL,
      NULL,
      sizeof (GstPlay),
      0,
      (GInstanceInitFunc) gst_play_init,
      NULL
    };
      
    play_type = g_type_register_static (GST_TYPE_PIPELINE, "GstPlay",
                                        &play_info, 0);
  }

  return play_type;
}
