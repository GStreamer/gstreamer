/* GStreamer
 * Copyright (C) 1999,2000,2001,2002 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000,2001,2002 Wim Taymans <wtay@chello.be>
 *                              2002 Steve Baker <steve@stevebaker.org>
 *                              2003 Julien Moutte <julien@moutte.net>
 *
 * playpipelines.c: Set up pipelines for playback
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

static gboolean
gst_play_default_set_data_src (GstPlay * play,
			       GstElement * datasrc, GstElement * parent)
{
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (datasrc != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (datasrc), FALSE);

  if (GST_IS_ELEMENT (play->source))
    {
      /* we need to remove the existing data
         source before creating a new one */
      if (GST_IS_ELEMENT (play->autoplugger))
	{
	  gst_element_unlink (play->autoplugger, play->source);
	}
      gst_bin_remove (GST_BIN (parent), play->source);
    }

  play->source = datasrc;
  g_return_val_if_fail (play->source != NULL, FALSE);

  gst_bin_add (GST_BIN (parent), play->source);
  if (GST_IS_ELEMENT (play->autoplugger))
    {
      gst_element_link (play->autoplugger, play->source);
    }
  return TRUE;
}

/*  
 *  GST_PLAY_PIPE_AUDIO
 *  gnomevfssrc ! spider ! volume ! osssink
 */

static gboolean
gst_play_audio_setup (GstPlay * play, GError ** error)
{

  /* creating gst_bin */
  play->pipeline = gst_pipeline_new ("main_pipeline");
  g_return_val_if_fail (GST_IS_PIPELINE (play->pipeline), FALSE);

  /* create source element */
  play->source = gst_element_factory_make ("gnomevfssrc", "source");
  if (!play->source)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_GNOMEVFSSRC, error);
      return FALSE;
    }

  /* Adding element to bin */
  gst_bin_add (GST_BIN (play->pipeline), play->source);

  /* create audio elements */
  play->volume = gst_element_factory_make ("volume", "volume");
  if (!play->volume)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_VOLUME, error);
      return FALSE;
    }

  /* creating fake audio_sink */
  play->audio_sink = gst_element_factory_make ("fakesink", "fake_audio");
  if (play->audio_sink == NULL)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_FAKESINK, error);
      return FALSE;
    }

  gst_bin_add_many (GST_BIN (play->pipeline), play->volume,
		    play->audio_sink, NULL);

  gst_element_link (play->volume, play->audio_sink);

  return TRUE;
}

static gboolean
gst_play_simple_set_data_src (GstPlay * play, GstElement * datasrc)
{
  return gst_play_default_set_data_src (play, datasrc, play->pipeline);
}

/*
 *  GST_PLAY_PIPE_AUDIO_THREADED
 *  { gnomevfssrc ! spider ! volume ! osssink }
 */

static gboolean
gst_play_audiot_setup (GstPlay * play, GError ** error)
{
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);

  /* creating gst_thread */
  play->pipeline = gst_thread_new ("main_pipeline");
  g_return_val_if_fail (GST_IS_THREAD (play->pipeline), FALSE);

  /* create source element */
  play->source = gst_element_factory_make ("gnomevfssrc", "source");
  if (!play->source)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_GNOMEVFSSRC, error);
      return FALSE;
    }

  /* Adding element to bin */
  gst_bin_add (GST_BIN (play->pipeline), play->source);

  /* create audio elements */
  play->volume = gst_element_factory_make ("volume", "volume");
  if (!play->volume)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_VOLUME, error);
      return FALSE;
    }

  /* creating fake audiosink */
  play->audio_sink = gst_element_factory_make ("fakesink", "fake_audio");
  if (play->audio_sink == NULL)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_FAKESINK, error);
      return FALSE;
    }

  gst_bin_add_many (GST_BIN (play->pipeline), play->volume,
		    play->audio_sink, NULL);

  gst_element_link (play->volume, play->audio_sink);

  return TRUE;
}


static gboolean
gst_play_audiot_set_audio (GstPlay * play, GstElement * audio_sink)
{
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (audio_sink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (audio_sink), FALSE);

  if (play->audio_sink)
    {
      gst_element_unlink (play->volume, play->audio_sink);
      gst_bin_remove (GST_BIN (play->pipeline), play->audio_sink);
    }

  play->audio_sink = audio_sink;
  gst_bin_add (GST_BIN (play->pipeline), play->audio_sink);
  gst_element_link (play->volume, play->audio_sink);

  play->audio_sink_element = gst_play_get_sink_element (play,
							audio_sink,
							GST_PLAY_SINK_TYPE_AUDIO);

  play->visualisation_sink_element = gst_play_get_sink_element (play,
								audio_sink,
								GST_PLAY_SINK_TYPE_VIDEO);

  if (play->audio_sink_element != NULL)
    {
      g_signal_connect (G_OBJECT (play->audio_sink_element), "eos",
			G_CALLBACK (callback_audio_sink_eos), play);
    }

  return TRUE;
}


static gboolean
gst_play_audiot_set_auto (GstPlay * play, GstElement * autoplugger)
{
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (autoplugger != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (autoplugger), FALSE);

  if (play->autoplugger)
    {
      /* we need to remove the existing autoplugger
         before creating a new one */
      gst_element_unlink (play->autoplugger, play->volume);
      gst_element_unlink (play->autoplugger, play->source);
      gst_bin_remove (GST_BIN (play->pipeline), play->autoplugger);
    }

  play->autoplugger = autoplugger;
  g_return_val_if_fail (play->autoplugger != NULL, FALSE);

  gst_bin_add (GST_BIN (play->pipeline), play->autoplugger);
  gst_element_link (play->source, play->autoplugger);
  gst_element_link (play->autoplugger, play->volume);
  return TRUE;
}

/*
 *  GST_PLAY_PIPE_AUDIO_HYPER_THREADED
 *  { gnomevfssrc ! spider ! { queue ! volume ! osssink } }
 */

static gboolean
gst_play_audioht_setup (GstPlay * play, GError ** error)
{
  GstElement *audio_thread, *audio_queue;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);

/*
	play->pipeline = gst_thread_new ("main_pipeline");
	g_return_val_if_fail (GST_IS_THREAD (play->pipeline), FALSE);
*/

  /* creating pipeline */
  play->pipeline = gst_pipeline_new ("main_pipeline");
  g_return_val_if_fail (GST_IS_PIPELINE (play->pipeline), FALSE);

  /* create source element */
  play->source = gst_element_factory_make ("gnomevfssrc", "source");
  if (!play->source)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_GNOMEVFSSRC, error);
      return FALSE;
    }

  /* Adding element to bin */
  gst_bin_add (GST_BIN (play->pipeline), play->source);

  /* create audio thread */
  audio_thread = gst_thread_new ("audio_thread");
  g_return_val_if_fail (GST_IS_THREAD (audio_thread), FALSE);

  g_hash_table_insert (play->other_elements, "audio_thread", audio_thread);

  /* create audio queue */
  audio_queue = gst_element_factory_make ("queue", "audio_queue");
  if (!audio_queue)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_QUEUE, error);
      return FALSE;
    }

  g_hash_table_insert (play->other_elements, "audio_queue", audio_queue);

  /* create source element */
  play->volume = gst_element_factory_make ("volume", "volume");
  if (!play->volume)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_VOLUME, error);
      return FALSE;
    }

  /* create audiosink. */
  play->audio_sink = gst_element_factory_make ("fakesink", "play_audio");
  if (play->audio_sink == NULL)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_FAKESINK, error);
      return FALSE;
    }

  gst_bin_add_many (GST_BIN (audio_thread), audio_queue, play->volume,
		    play->audio_sink, NULL);

  gst_element_link_many (audio_queue, play->volume, play->audio_sink);

  gst_element_add_ghost_pad (audio_thread,
			     gst_element_get_pad (audio_queue, "sink"),
			     "sink");

  gst_bin_add (GST_BIN (play->pipeline), audio_thread);

  return TRUE;
}


static gboolean
gst_play_audioht_set_audio (GstPlay * play, GstElement * audio_sink)
{
  GstElement *audio_thread;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (audio_sink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (audio_sink), FALSE);

  audio_thread = g_hash_table_lookup (play->other_elements, "audio_thread");

  if (play->audio_sink)
    {
      gst_element_unlink (play->volume, play->audio_sink);
      gst_bin_remove (GST_BIN (audio_thread), play->audio_sink);
    }

  play->audio_sink = audio_sink;
  gst_bin_add (GST_BIN (audio_thread), play->audio_sink);
  gst_element_link (play->volume, play->audio_sink);

  play->audio_sink_element = gst_play_get_sink_element (play,
							audio_sink,
							GST_PLAY_SINK_TYPE_AUDIO);

  play->visualisation_sink_element = gst_play_get_sink_element (play,
								audio_sink,
								GST_PLAY_SINK_TYPE_VIDEO);

  if (play->audio_sink_element != NULL)
    {
      g_signal_connect (G_OBJECT (play->audio_sink_element), "eos",
			G_CALLBACK (callback_audio_sink_eos), play);
    }

  return TRUE;
}


static gboolean
gst_play_audioht_set_auto (GstPlay * play, GstElement * autoplugger)
{
  GstElement *audio_thread;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (autoplugger != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (autoplugger), FALSE);

  audio_thread = g_hash_table_lookup (play->other_elements, "audio_thread");

  if (play->autoplugger)
    {
      /* we need to remove the existing autoplugger
         before creating a new one */
      gst_element_unlink (play->autoplugger, audio_thread);
      gst_element_unlink (play->autoplugger, play->source);
      gst_bin_remove (GST_BIN (play->pipeline), play->autoplugger);
    }

  play->autoplugger = autoplugger;
  g_return_val_if_fail (play->autoplugger != NULL, FALSE);

  gst_bin_add (GST_BIN (play->pipeline), play->autoplugger);
  gst_element_link (play->source, play->autoplugger);
  gst_element_link (play->autoplugger, audio_thread);
  return TRUE;
}

/*
 * GST_PLAY_PIPE_VIDEO
 * { gnomevfssrc ! spider ! { queue ! volume ! (audiosink) }
 * spider0.src2 ! { queue ! colorspace ! (videosink) } }
 */

static gboolean
gst_play_video_setup (GstPlay * play, GError ** error)
{
  GstElement *audio_bin, *audio_queue;
  GstElement *video_queue, *video_bin;
  GstElement *work_thread, *colorspace;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);

  /* creating pipeline */
  play->pipeline = gst_pipeline_new ("main_pipeline");
  g_return_val_if_fail (GST_IS_PIPELINE (play->pipeline), FALSE);

  /* creating work thread */
  work_thread = gst_thread_new ("work_thread");
  g_return_val_if_fail (GST_IS_THREAD (work_thread), FALSE);
  g_hash_table_insert (play->other_elements, "work_thread", work_thread);

  gst_bin_add (GST_BIN (play->pipeline), work_thread);

  /* create source element */
  play->source = gst_element_factory_make ("gnomevfssrc", "source");
  if (!play->source)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_GNOMEVFSSRC, error);
      return FALSE;
    }
  gst_bin_add (GST_BIN (work_thread), play->source);

  /* creating volume element */
  play->volume = gst_element_factory_make ("volume", "volume");
  if (!play->volume)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_VOLUME, error);
      return FALSE;
    }

  /* creating audio_sink element */
  play->audio_sink = gst_element_factory_make ("fakesink", "fake_audio");
  if (!play->audio_sink)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_FAKESINK, error);
      return FALSE;
    }
  play->audio_sink_element = NULL;

  /* creating audio_queue element */
  audio_queue = gst_element_factory_make ("queue", "audio_queue");
  if (!audio_queue)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_QUEUE, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements, "audio_queue", audio_queue);

  /* creating audio thread */
  audio_bin = gst_thread_new ("audio_bin");
  if (!audio_bin)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_THREAD, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements, "audio_bin", audio_bin);

  /* adding all that stuff to bin */
  gst_bin_add_many (GST_BIN (audio_bin), audio_queue, play->volume,
		    play->audio_sink, NULL);
  gst_element_link_many (audio_queue, play->volume, play->audio_sink, NULL);

  gst_element_add_ghost_pad (audio_bin,
			     gst_element_get_pad (audio_queue, "sink"),
			     "sink");

  gst_bin_add (GST_BIN (work_thread), audio_bin);

  /* create video elements */
  play->video_sink = gst_element_factory_make ("fakesink", "fake_show");
  if (!play->video_sink)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_FAKESINK, error);
      return FALSE;
    }
  play->video_sink_element = NULL;

  video_queue = gst_element_factory_make ("queue", "video_queue");
  if (!video_queue)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_QUEUE, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements, "video_queue", video_queue);

  colorspace = gst_element_factory_make ("colorspace", "colorspace");
  if (!colorspace)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_COLORSPACE, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements, "colorspace", colorspace);

  video_bin = gst_thread_new ("video_bin");
  if (!video_bin)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_THREAD, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements, "video_bin", video_bin);

  /* adding all that stuff to bin */
  gst_bin_add_many (GST_BIN (video_bin), video_queue, colorspace,
		    play->video_sink, NULL);

  gst_element_link_many (video_queue, colorspace, play->video_sink, NULL);

  gst_element_add_ghost_pad (video_bin,
			     gst_element_get_pad (video_queue, "sink"),
			     "sink");

  gst_bin_add (GST_BIN (work_thread), video_bin);

  return TRUE;
}

static gboolean
gst_play_video_set_data_src (GstPlay * play, GstElement * datasrc)
{
  GstElement *work_thread;
  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (datasrc != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (datasrc), FALSE);

  work_thread = g_hash_table_lookup (play->other_elements, "work_thread");
  return gst_play_default_set_data_src (play, datasrc, work_thread);
}

static gboolean
gst_play_video_set_auto (GstPlay * play, GstElement * autoplugger)
{

  GstElement *audio_bin, *video_bin, *work_thread;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (autoplugger != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (autoplugger), FALSE);

  audio_bin = g_hash_table_lookup (play->other_elements, "audio_bin");
  video_bin = g_hash_table_lookup (play->other_elements, "video_bin");
  work_thread = g_hash_table_lookup (play->other_elements, "work_thread");

  if (play->autoplugger)
    {
      /* we need to remove the existing autoplugger
         before creating a new one */
      gst_element_unlink (play->autoplugger, audio_bin);
      gst_element_unlink (play->autoplugger, play->source);
      gst_element_unlink (play->autoplugger, video_bin);

      gst_bin_remove (GST_BIN (work_thread), play->autoplugger);
    }

  play->autoplugger = autoplugger;
  g_return_val_if_fail (play->autoplugger != NULL, FALSE);

  gst_bin_add (GST_BIN (work_thread), play->autoplugger);
  gst_element_link (play->source, play->autoplugger);
  gst_element_link (play->autoplugger, audio_bin);
  gst_element_link (play->autoplugger, video_bin);

  return TRUE;
}


static gboolean
gst_play_video_set_video (GstPlay * play, GstElement * video_sink)
{
  GstElement *video_mate, *video_bin;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (video_sink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (video_sink), FALSE);

  video_bin = g_hash_table_lookup (play->other_elements, "video_bin");
  video_mate = g_hash_table_lookup (play->other_elements, "colorspace");

  if (play->video_sink)
    {
      gst_element_unlink (video_mate, play->video_sink);
      gst_bin_remove (GST_BIN (video_bin), play->video_sink);
    }
  play->video_sink = video_sink;
  gst_bin_add (GST_BIN (video_bin), play->video_sink);
  gst_element_link (video_mate, play->video_sink);

  play->video_sink_element = gst_play_get_sink_element (play,
							video_sink,
							GST_PLAY_SINK_TYPE_VIDEO);

  if (play->video_sink_element != NULL)
    {
      g_signal_connect (G_OBJECT (play->video_sink_element),
			"have_xid",
			G_CALLBACK (callback_video_have_xid), play);
      g_signal_connect (G_OBJECT (play->video_sink_element),
			"have_size",
			G_CALLBACK (callback_video_have_size), play);
      g_object_set (G_OBJECT (play->video_sink_element),
		    "need_new_window", TRUE, "toplevel", FALSE, NULL);
    }
  return TRUE;
}


static gboolean
gst_play_video_set_audio (GstPlay * play, GstElement * audio_sink)
{
  GstElement *audio_bin;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (audio_sink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (audio_sink), FALSE);

  audio_bin = g_hash_table_lookup (play->other_elements, "audio_bin");

  if (play->audio_sink)
    {
      gst_element_unlink (play->volume, play->audio_sink);
      gst_bin_remove (GST_BIN (audio_bin), play->audio_sink);
    }

  play->audio_sink = audio_sink;
  gst_bin_add (GST_BIN (audio_bin), play->audio_sink);
  gst_element_link (play->volume, play->audio_sink);

  play->audio_sink_element = gst_play_get_sink_element (play,
							audio_sink,
							GST_PLAY_SINK_TYPE_AUDIO);

  play->visualisation_sink_element = gst_play_get_sink_element (play,
								audio_sink,
								GST_PLAY_SINK_TYPE_VIDEO);

  if (play->audio_sink_element != NULL)
    {
      g_signal_connect (G_OBJECT (play->audio_sink_element), "eos",
			G_CALLBACK (callback_audio_sink_eos), play);
    }

  return TRUE;
}

/*
 * GST_PLAY_PIPE_VIDEO_VISUALISATION
 * { gnomevfssrc ! spider ! { queue ! volume ! (audiosink) }
 * spider0.src2 ! { queue ! colorspace ! (videosink) } }
 */

static gboolean
gst_play_video_vis_setup (GstPlay * play, GError ** error)
{

  GstElement *work_thread, *tee_element;
  GstPad *tee_vis_pad, *tee_audio_pad;
  GstPad *audio_sink_pad, *vis_video_thread_pad;
  GstElement *vis_video_thread;
  GstElement *audio_queue, *audio_sink;
  GstElement *vis_colorspace, *vis_video_queue, *vis_video_sink;
  GstElement *video_queue, *video_bin, *colorspace;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);

  /* creating pipeline */
  play->pipeline = gst_pipeline_new ("main_pipeline");
  g_return_val_if_fail (GST_IS_PIPELINE (play->pipeline), FALSE);

  /* creating work thread */
  work_thread = gst_thread_new ("work_thread");
  g_return_val_if_fail (GST_IS_THREAD (work_thread), FALSE);
  g_hash_table_insert (play->other_elements, "work_thread", work_thread);

  gst_bin_add (GST_BIN (play->pipeline), work_thread);

  /* create source element */
  play->source = gst_element_factory_make ("gnomevfssrc", "source");
  if (!play->source)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_GNOMEVFSSRC, error);
      return FALSE;
    }
  gst_bin_add (GST_BIN (work_thread), play->source);

  /* BEGIN VIS STUFF */

  /* Creating here the audio vis bin */

  play->audio_sink = gst_thread_new ("audio_thread");
  if (!play->audio_sink)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_THREAD, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements, "audio_bin", play->audio_sink);

  audio_queue = gst_element_factory_make ("queue", "audio_queue");
  if (!audio_queue)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_QUEUE, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements, "audio_queue", audio_queue);

  play->volume = gst_element_factory_make ("volume", "audio_volume");
  if (!play->volume)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_VOLUME, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements, "audio_volume", play->volume);

  tee_element = gst_element_factory_make ("tee", "audio_tee");
  g_return_val_if_fail (GST_IS_ELEMENT (tee_element), FALSE);
  g_hash_table_insert (play->other_elements, "vis_tee", play->audio_sink);

  tee_vis_pad = gst_element_get_request_pad (tee_element, "src%d");
  tee_audio_pad = gst_element_get_request_pad (tee_element, "src%d");
  g_hash_table_insert (play->other_elements, "tee_vis_pad", tee_vis_pad);
  g_hash_table_insert (play->other_elements, "tee_audio_pad", tee_audio_pad);

  audio_sink = gst_element_factory_make ("fakesink", "audio_sink");
  if (!audio_sink)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_FAKESINK, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements, "audio_sink", audio_sink);

  audio_sink_pad = gst_element_get_pad (audio_sink, "sink");
  g_hash_table_insert (play->other_elements,
		       "audio_sink_pad", audio_sink_pad);

  play->audio_sink_element = NULL;


  gst_bin_add_many (GST_BIN (play->audio_sink),
		    audio_queue, play->volume, tee_element, audio_sink, NULL);
  gst_element_link_many (audio_queue, play->volume, tee_element, NULL);

  gst_pad_link (tee_audio_pad, audio_sink_pad);

  gst_element_add_ghost_pad (play->audio_sink,
			     gst_element_get_pad (audio_queue, "sink"),
			     "sink");

  /* Creating video part of the visualisation bin
     { queue ! (visualisation) ! colorspace ! (videosink) }
   */

  vis_video_thread = gst_thread_new ("vis_video_thread");
  if (!vis_video_thread)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_THREAD, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements,
		       "vis_video_thread", vis_video_thread);

  vis_video_queue = gst_element_factory_make ("queue", "vis_video_queue");
  if (!vis_video_queue)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_QUEUE, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements,
		       "vis_video_queue", vis_video_queue);

  vis_colorspace = gst_element_factory_make ("colorspace", "vis_colorspace");
  if (!vis_colorspace)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_COLORSPACE, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements,
		       "vis_colorspace", vis_colorspace);

  vis_video_sink = gst_element_factory_make ("fakesink", "vis_video_sink");
  if (!vis_video_sink)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_FAKESINK, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements,
		       "vis_video_sink", vis_video_sink);
  play->video_sink_element = NULL;

  gst_bin_add_many (GST_BIN (vis_video_thread), vis_video_queue,
		    vis_colorspace, vis_video_sink, NULL);


  vis_video_thread_pad = gst_element_add_ghost_pad (vis_video_thread,
						    gst_element_get_pad
						    (vis_video_queue, "sink"),
						    "sink");

  g_hash_table_insert (play->other_elements,
		       "vis_video_thread_pad", vis_video_thread_pad);

  gst_bin_add (GST_BIN (play->audio_sink), vis_video_thread);

  /* END VIS STUFF */

  gst_bin_add (GST_BIN (work_thread), play->audio_sink);

  /* create video elements */
  play->video_sink = gst_element_factory_make ("fakesink", "fake_show");
  if (!play->video_sink)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_FAKESINK, error);
      return FALSE;
    }
  play->video_sink_element = NULL;

  video_queue = gst_element_factory_make ("queue", "video_queue");
  if (!video_queue)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_QUEUE, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements, "video_queue", video_queue);

  colorspace = gst_element_factory_make ("colorspace", "colorspace");
  if (!colorspace)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_COLORSPACE, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements, "colorspace", colorspace);

  video_bin = gst_thread_new ("video_thread");
  if (!video_bin)
    {
      gst_play_error_plugin (GST_PLAY_ERROR_THREAD, error);
      return FALSE;
    }
  g_hash_table_insert (play->other_elements, "video_bin", video_bin);

  /* adding all that stuff to bin */
  gst_bin_add_many (GST_BIN (video_bin), video_queue, colorspace,
		    play->video_sink, NULL);

  gst_element_link_many (video_queue, colorspace, play->video_sink, NULL);

  gst_element_add_ghost_pad (video_bin,
			     gst_element_get_pad (video_queue, "sink"),
			     "sink");

  gst_bin_add (GST_BIN (work_thread), video_bin);

  return TRUE;
}

static gboolean
gst_play_video_vis_set_audio (GstPlay * play, GstElement * audio_sink)
{
  GstElement *audio_bin, *vis_audio_sink;
  GstPad *audio_sink_pad, *tee_audio_pad;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (audio_sink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (audio_sink), FALSE);

  audio_bin = g_hash_table_lookup (play->other_elements, "audio_bin");
  vis_audio_sink = g_hash_table_lookup (play->other_elements, "audio_sink");
  audio_sink_pad = g_hash_table_lookup (play->other_elements,
					"audio_sink_pad");
  tee_audio_pad = g_hash_table_lookup (play->other_elements, "tee_audio_pad");

  if ((vis_audio_sink) && GST_IS_ELEMENT (vis_audio_sink))
    {
      gst_pad_unlink (tee_audio_pad, audio_sink_pad);
      gst_bin_remove (GST_BIN (audio_bin), vis_audio_sink);
    }

  gst_bin_add (GST_BIN (audio_bin), audio_sink);

  audio_sink_pad = gst_element_get_pad (audio_sink, "sink");

  gst_pad_link (tee_audio_pad, audio_sink_pad);

  g_hash_table_replace (play->other_elements, "audio_sink", audio_sink);

  g_hash_table_replace (play->other_elements,
			"audio_sink_pad", audio_sink_pad);

  play->audio_sink_element = gst_play_get_sink_element (play,
							audio_sink,
							GST_PLAY_SINK_TYPE_AUDIO);

  if (play->audio_sink_element != NULL)
    {
      g_signal_connect (G_OBJECT (play->audio_sink_element), "eos",
			G_CALLBACK (callback_audio_sink_eos), play);
    }

  return TRUE;
}

static gboolean
gst_play_video_vis_set_video (GstPlay * play, GstElement * video_sink)
{
  GstElement *video_mate, *video_bin;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (video_sink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (video_sink), FALSE);

  video_bin = g_hash_table_lookup (play->other_elements, "video_bin");
  video_mate = g_hash_table_lookup (play->other_elements, "colorspace");

  if (play->video_sink)
    {
      gst_element_unlink (video_mate, play->video_sink);
      gst_bin_remove (GST_BIN (video_bin), play->video_sink);
    }
  play->video_sink = video_sink;
  gst_bin_add (GST_BIN (video_bin), play->video_sink);
  gst_element_link (video_mate, play->video_sink);

  play->video_sink_element = gst_play_get_sink_element (play,
							video_sink,
							GST_PLAY_SINK_TYPE_VIDEO);

  if (play->video_sink_element != NULL)
    {
      g_signal_connect (G_OBJECT (play->video_sink_element),
			"have_xid",
			G_CALLBACK (callback_video_have_xid), play);
      g_signal_connect (G_OBJECT (play->video_sink_element),
			"have_size",
			G_CALLBACK (callback_video_have_size), play);
      g_object_set (G_OBJECT (play->video_sink_element),
		    "need_new_window", TRUE, "toplevel", FALSE, NULL);
    }
  return TRUE;
}

/**
 * gst_play_set_visualisation_video_sink:
 * @play: a #GstPlay.
 * @video_sink: a #GstElement.
 *
 * Set @video_sink as the visualisation video sink element of @play.
 *
 * Returns: TRUE if call succeeded.
 */
gboolean
gst_play_set_visualisation_video_sink (GstPlay * play,
				       GstElement * video_sink)
{
  GstElement *video_mate, *video_bin, *vis_video_sink;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (video_sink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (video_sink), FALSE);

  video_bin = g_hash_table_lookup (play->other_elements, "vis_video_thread");
  video_mate = g_hash_table_lookup (play->other_elements, "vis_colorspace");
  vis_video_sink = g_hash_table_lookup (play->other_elements,
					"vis_video_sink");

  if (vis_video_sink)
    {
      gst_element_unlink (video_mate, vis_video_sink);
      gst_bin_remove (GST_BIN (video_bin), vis_video_sink);
    }

  gst_bin_add (GST_BIN (video_bin), video_sink);
  gst_element_link (video_mate, video_sink);

  g_hash_table_replace (play->other_elements, "vis_video_sink", video_sink);

  play->visualisation_sink_element = gst_play_get_sink_element (play,
								video_sink,
								GST_PLAY_SINK_TYPE_VIDEO);

  if (play->visualisation_sink_element != NULL)
    {
      g_signal_connect (G_OBJECT (play->visualisation_sink_element),
			"have_xid",
			G_CALLBACK (callback_video_have_vis_xid), play);
      /*g_signal_connect (    G_OBJECT (play->visualisation_sink_element),
         "have_size",
         G_CALLBACK (callback_video_have_vis_size),
         play); */
      g_object_set (G_OBJECT (play->visualisation_sink_element),
		    "need_new_window", TRUE, "toplevel", FALSE, NULL);
    }
  return TRUE;
}

/**
 * gst_play_set_visualisation_element:
 * @play: a #GstPlay.
 * @element: a #GstElement.
 *
 * Set @video_sink as the video sink element of @play.
 *
 * Returns: TRUE if call succeeded.
 */
gboolean
gst_play_set_visualisation_element (GstPlay * play, GstElement * element)
{
  GstElement *video_queue, *video_colorspace;
  GstElement *vis_element, *vis_video_bin;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  vis_video_bin = g_hash_table_lookup (play->other_elements,
				       "vis_video_thread");
  video_queue = g_hash_table_lookup (play->other_elements, "vis_video_queue");
  video_colorspace = g_hash_table_lookup (play->other_elements,
					  "vis_colorspace");
  vis_element = g_hash_table_lookup (play->other_elements, "vis_element");
  if (vis_element)
    {
      gst_element_unlink (video_queue, vis_element);
      gst_element_unlink (vis_element, video_colorspace);
      gst_bin_remove (GST_BIN (vis_video_bin), vis_element);
    }

  gst_bin_add (GST_BIN (vis_video_bin), element);
  gst_element_link_many (video_queue, element, video_colorspace, NULL);

  g_hash_table_replace (play->other_elements, "vis_element", element);

  return TRUE;
}

/**
 * gst_play_connect_visualisation:
 * @play: a #GstPlay.
 * @connect: a #gboolean indicating wether or not
 * visualisation should be connected.
 *
 * Connect or disconnect visualisation bin in @play.
 *
 * Returns: TRUE if call succeeded.
 */
gboolean
gst_play_connect_visualisation (GstPlay * play, gboolean connect)
{
  GstPad *tee_vis_pad, *vis_video_thread_pad;
  GstElement *vis_video_thread;
  gboolean connected = FALSE;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);

  tee_vis_pad = g_hash_table_lookup (play->other_elements, "tee_vis_pad");
  vis_video_thread_pad = g_hash_table_lookup (play->other_elements,
					      "vis_video_thread_pad");
  vis_video_thread = g_hash_table_lookup (play->other_elements,
					  "vis_video_thread");

  if (gst_pad_get_peer (vis_video_thread_pad) != NULL)
    connected = TRUE;
  else
    connected = FALSE;

  if ((connect) && (!connected))
    {
      gst_pad_link (tee_vis_pad, vis_video_thread_pad);
    }
  else if ((!connect) && (connected))
    {
      gst_pad_unlink (tee_vis_pad, vis_video_thread_pad);
    }

  if (GST_IS_ELEMENT (vis_video_thread) && GST_IS_ELEMENT (play->pipeline))
    {
      gst_element_set_state (vis_video_thread,
			     gst_element_get_state (play->pipeline));
    }

  return TRUE;
}

/* modelines */
/* vim:set ts=8:sw=8:noet */
