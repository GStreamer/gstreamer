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

#include "gstplay.h"

#include <gst/xoverlay/xoverlay.h>
#include <gst/navigation/navigation.h>
#include <gst/mixer/mixer.h>

enum
{
  TIME_TICK,
  STREAM_LENGTH,
  HAVE_VIDEO_SIZE,
  LAST_SIGNAL
};

static guint gst_play_signals[LAST_SIGNAL] = { 0 };

static GstPipelineClass *parent_class = NULL;

/* ======================================================= */
/*                                                         */
/*                    Private Methods                      */
/*                                                         */
/* ======================================================= */

static gboolean
gst_play_pipeline_setup (GstPlay *play)
{
  GstElement *work_thread, *video_thread;
  GstElement *video_queue, *video_colorspace, *video_scaler, *video_sink;
  GstElement *audio_thread, *audio_queue, *audio_volume, *audio_sink;
  GstElement *audio_tee, *vis_thread, *vis_queue;
  GstPad *audio_tee_pad1, *audio_tee_pad2, *vis_thread_pad, *audio_sink_pad;
  
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  
  work_thread = gst_thread_new ("work_thread");
  if (!GST_IS_THREAD (work_thread))
    return FALSE;
  
  g_hash_table_insert (play->elements, "work_thread", work_thread);
  gst_bin_add (GST_BIN (play), work_thread);
  
  /* Creating our video output bin
     { queue ! colorspace ! videoscale ! fakesink } */
  video_thread = gst_thread_new ("video_thread");
  if (!GST_IS_THREAD (video_thread))
    return FALSE;
  
  g_hash_table_insert (play->elements, "video_thread", video_thread);
  gst_bin_add (GST_BIN (work_thread), video_thread);
  
  /* Buffer queue for our video thread */
  video_queue = gst_element_factory_make ("queue", "video_queue");
  if (!GST_IS_ELEMENT (video_queue))
    return FALSE;
  
  g_hash_table_insert (play->elements, "video_queue", video_queue);
  
  /* Colorspace conversion */
  /* FIXME: Use ffcolorspace and fallback to Hermes on failure ?*/
  video_colorspace = gst_element_factory_make ("colorspace",
                                               "video_colorspace");
  if (!GST_IS_ELEMENT (video_colorspace))
    return FALSE;
  
  g_hash_table_insert (play->elements, "video_colorspace",
                       video_colorspace);
  
  /* Software scaling of video stream */
  video_scaler = gst_element_factory_make ("videoscale", "video_scaler");
  if (!GST_IS_ELEMENT (video_scaler))
    return FALSE;
  
  g_hash_table_insert (play->elements, "video_scaler", video_scaler);
  
  /* Placeholder for future video sink bin */
  video_sink = gst_element_factory_make ("fakesink", "video_sink");
  if (!GST_IS_ELEMENT (video_sink))
    return FALSE;
  
  g_hash_table_insert (play->elements, "video_sink", video_sink);
  
  /* Linking, Adding, Ghosting */
  gst_element_link_many (video_queue, video_colorspace,
                         video_scaler, video_sink, NULL);
  gst_bin_add_many (GST_BIN (video_thread), video_queue, video_colorspace,
                    video_scaler, video_sink, NULL);
  gst_element_add_ghost_pad (video_thread,
                             gst_element_get_pad (video_queue, "sink"),
			     "sink");
  
  /* Creating our audio output bin 
     { queue ! volume ! tee ! { queue ! goom } ! fakesink } */
  audio_thread = gst_thread_new ("audio_thread");
  if (!GST_IS_THREAD (audio_thread))
    return FALSE;
  
  g_hash_table_insert (play->elements, "audio_thread", audio_thread);
  gst_bin_add (GST_BIN (work_thread), audio_thread);
  
  /* Buffer queue for our audio thread */
  audio_queue = gst_element_factory_make ("queue", "audio_queue");
  if (!GST_IS_ELEMENT (audio_queue))
    return FALSE;
  
  g_hash_table_insert (play->elements, "audio_queue", audio_queue);
  
  /* Volume control */
  audio_volume = gst_element_factory_make ("volume", "audio_volume");
  if (!GST_IS_ELEMENT (audio_volume))
    return FALSE;
  
  g_hash_table_insert (play->elements, "audio_volume", audio_volume);
  
  /* Duplicate audio signal to sink and visualization thread */
  audio_tee = gst_element_factory_make ("tee", "audio_tee");
  if (!GST_IS_ELEMENT (audio_tee))
    return FALSE;
  
  audio_tee_pad1 = gst_element_get_request_pad (audio_tee, "src%d");
  audio_tee_pad2 = gst_element_get_request_pad (audio_tee, "src%d");
  g_hash_table_insert (play->elements, "audio_tee_pad1", audio_tee_pad1);
  g_hash_table_insert (play->elements, "audio_tee_pad2", audio_tee_pad2);
  g_hash_table_insert (play->elements, "audio_tee", audio_tee);
  
  /* Placeholder for future audio sink bin */
  audio_sink = gst_element_factory_make ("fakesink", "audio_sink");
  if (!GST_IS_ELEMENT (audio_sink))
    return FALSE;
  
  audio_sink_pad = gst_element_get_pad (audio_sink, "sink");
  g_hash_table_insert (play->elements, "audio_sink_pad", audio_sink_pad);
  g_hash_table_insert (play->elements, "audio_sink", audio_sink);
  
  /* Visualization thread */
  vis_thread = gst_thread_new ("vis_thread");
  if (!GST_IS_THREAD (vis_thread))
    return FALSE;
  
  g_hash_table_insert (play->elements, "vis_thread", vis_thread);
  
  /* Buffer queue for our visualization thread */
  vis_queue = gst_element_factory_make ("queue", "vis_queue");
  if (!GST_IS_ELEMENT (vis_queue))
    return FALSE;
  
  g_hash_table_insert (play->elements, "vis_queue", vis_queue);
  
  /* Adding, Ghosting in visualization */
  gst_bin_add (GST_BIN (vis_thread), vis_queue);
  vis_thread_pad = gst_element_add_ghost_pad (vis_thread,
                                     gst_element_get_pad (vis_queue, "sink"),
			             "sink");
  g_hash_table_insert (play->elements, "vis_thread_pad", vis_thread_pad);
  
  
  /* Linking, Adding, Ghosting in audio */
  gst_element_link_many (audio_queue, audio_volume, audio_tee, NULL);
  gst_pad_link (audio_tee_pad1, audio_sink_pad);
  gst_bin_add_many (GST_BIN (audio_thread), audio_queue, audio_volume,
                    audio_tee, vis_thread, audio_sink);
  gst_element_add_ghost_pad (audio_thread,
                             gst_element_get_pad (audio_queue, "sink"),
			     "sink");
  
  return TRUE;
}

/* =========================================== */
/*                                             */
/*                 Interfaces                  */
/*                                             */
/* =========================================== */

static gboolean
gst_play_interface_supported (GstInterface *iface, GType type)
{
  g_assert (type == GST_TYPE_NAVIGATION ||
            type == GST_TYPE_X_OVERLAY ||
            type == GST_TYPE_MIXER);
  return TRUE;
}

static void
gst_play_interface_init (GstInterfaceClass *klass)
{
  klass->supported = gst_play_interface_supported;
}

static void
gst_play_navigation_send_event (GstNavigation *navigation,
                                GstStructure *structure)
{
  
}

static void
gst_play_navigation_init (GstNavigationInterface *iface)
{
  iface->send_event = gst_play_navigation_send_event;
}

static void
gst_play_set_xwindow_id (GstXOverlay *overlay, XID xwindow_id)
{
  
}

static void
gst_play_xoverlay_init (GstXOverlayClass *iface)
{
  iface->set_xwindow_id = gst_play_set_xwindow_id;
}

static void
gst_play_mixer_init (GstMixerClass *klass)
{
  klass->list_tracks = NULL;
  klass->set_volume = NULL;
  klass->get_volume = NULL;
  klass->set_mute = NULL;
  klass->set_record = NULL;
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
    
  if (play->location)
    {
      g_free (play->location);
      play->location = NULL;
    }
  
  if (play->elements)
    {
      g_hash_table_destroy (play->elements);
      play->elements = NULL;
    }
    
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_play_init (GstPlay *play)
{
  play->location = NULL;
  play->elements = g_hash_table_new (g_str_hash, g_str_equal);
  if (!gst_play_pipeline_setup (play))
    g_warning ("libgstplay: failed initializing pipeline");
}

static void
gst_play_class_init (GstPlayClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_play_dispose;

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
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  
  if (play->location)
    g_free (play->location);
  
  play->location = g_strdup (location);
  
  if (GST_STATE (GST_ELEMENT (play)) != GST_STATE_READY)
    gst_element_set_state (GST_ELEMENT (play), GST_STATE_READY);
  
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
  return g_strdup (play->location);
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
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  
  return TRUE;
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

  if (!play_type)
    {
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
      static const GInterfaceInfo iface_info = {
        (GInterfaceInitFunc) gst_play_interface_init,
        NULL,
        NULL,
      };
      static const GInterfaceInfo navigation_info = {
        (GInterfaceInitFunc) gst_play_navigation_init,
        NULL,
        NULL,
      };
      static const GInterfaceInfo overlay_info = {
        (GInterfaceInitFunc) gst_play_xoverlay_init,
        NULL,
        NULL,
      };
      static const GInterfaceInfo mixer_info = {
        (GInterfaceInitFunc) gst_play_mixer_init,
        NULL,
        NULL,
      };
      
      play_type = g_type_register_static (GST_TYPE_PIPELINE, "GstPlay",
                                          &play_info, 0);
      
      g_type_add_interface_static (play_type, GST_TYPE_INTERFACE,
                                   &iface_info);
      g_type_add_interface_static (play_type, GST_TYPE_NAVIGATION,
                                   &navigation_info);
      g_type_add_interface_static (play_type, GST_TYPE_X_OVERLAY,
                                   &overlay_info);
      g_type_add_interface_static (play_type, GST_TYPE_MIXER,
                                   &mixer_info);
    }

  return play_type;
}
