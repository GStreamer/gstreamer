/* GStreamer
 * Copyright (C) 1999,2000,2001,2002 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000,2001,2002 Wim Taymans <wtay@chello.be>
 *                              2002 Steve Baker <steve@stevebaker.org>
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
 
/*
 *  GST_PLAY_PIPE_AUDIO_THREADED
 *  { gnomevfssrc ! spider ! volume ! osssink }
 */
 
static gboolean 
gst_play_audiot_setup (GstPlay *play, GError **error)
{
	
	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	
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

	/* create audiosink.
	FIXME : Should use gconf to choose the right one */
	play->audio_sink = gst_element_factory_make ("osssink", "play_audio");
	if (!play->audio_sink)
	  g_warning ("You need the osssink element to use this program.");
	
	g_object_set (
			G_OBJECT (play->audio_sink),
			"fragment", 0x00180008, NULL);
	
	g_signal_connect (
			G_OBJECT (play->audio_sink), "eos",
			G_CALLBACK (callback_audio_sink_eos), play);

	gst_bin_add_many (
			GST_BIN (play->pipeline), play->volume,
			play->audio_sink, NULL);
	
	gst_element_connect (play->volume, play->audio_sink);
	
	gst_bin_set_pre_iterate_function(
			GST_BIN (play->pipeline), 
			(GstBinPrePostIterateFunction) callback_bin_pre_iterate,
			play->audio_bin_mutex);
			
	gst_bin_set_post_iterate_function(
			GST_BIN (play->pipeline), 
			(GstBinPrePostIterateFunction) callback_bin_post_iterate,
			play->audio_bin_mutex);

	return TRUE;
}


static gboolean
gst_play_audiot_set_audio (GstPlay *play, GstElement *audio_sink)
{
	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	g_return_val_if_fail (GST_IS_ELEMENT (audio_sink), FALSE);
	
	if (play->audio_sink)
	{
		gst_element_disconnect (play->volume, play->audio_sink);
		gst_bin_remove (GST_BIN (play->pipeline), play->audio_sink);
	}

	play->audio_sink = audio_sink;
	gst_bin_add (GST_BIN (play->pipeline), play->audio_sink);
	gst_element_connect (play->volume, play->audio_sink);

	return TRUE;
}


static gboolean
gst_play_audiot_set_auto (GstPlay *play, GstElement *autoplugger)
{

	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	g_return_val_if_fail (GST_IS_ELEMENT (autoplugger), FALSE);
	
	if (play->autoplugger){
		/* we need to remove the existing autoplugger before creating a new one */
		gst_element_disconnect (play->autoplugger, play->volume);
		gst_element_disconnect (play->autoplugger, play->source);
		gst_bin_remove (GST_BIN (play->pipeline), play->autoplugger);
	}
	
	play->autoplugger = autoplugger;
	g_return_val_if_fail (play->autoplugger != NULL, FALSE);

	gst_bin_add (GST_BIN (play->pipeline), play->autoplugger);
	gst_element_connect (play->source, play->autoplugger);
	gst_element_connect (play->autoplugger, play->volume);
	return TRUE;
}

/*
 *  GST_PLAY_PIPE_AUDIO_HYPER_THREADED
 *  { gnomevfssrc ! spider ! { queue ! volume ! osssink } }
 */

static gboolean 
gst_play_audioht_setup (GstPlay *play, GError **error)
{
	GstElement *audio_thread, *audio_queue;

	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	
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
	
	g_hash_table_insert(play->other_elements, "audio_thread", audio_thread);
	
	/* create audio queue */
	audio_queue = gst_element_factory_make ("queue", "audio_queue");
	if (!audio_queue)
	{
		gst_play_error_plugin (GST_PLAY_ERROR_QUEUE, error);
		return FALSE;
	}
	
	g_hash_table_insert(play->other_elements, "audio_queue", audio_queue);
	
	/* create source element */
	play->volume = gst_element_factory_make ("volume", "volume");
	if (!play->volume)
	{
		gst_play_error_plugin (GST_PLAY_ERROR_VOLUME, error);
		return FALSE;
	}

	/* create audiosink.
	FIXME : Should use gconf to choose the right one */
	play->audio_sink = gst_element_factory_make ("osssink", "play_audio");
	if (!play->audio_sink)
		g_warning ("You need the osssink element to use this program.\n");

	g_object_set (G_OBJECT (play->audio_sink), "fragment", 0x00180008, NULL);
	
	g_signal_connect (G_OBJECT (play->audio_sink), "eos",
			  G_CALLBACK (callback_audio_sink_eos), play);

	gst_bin_add_many (
				GST_BIN (audio_thread), audio_queue, play->volume,
				play->audio_sink, NULL);
	
	gst_element_connect_many (audio_queue, play->volume, play->audio_sink);
	
	gst_element_add_ghost_pad (
				audio_thread, gst_element_get_pad (audio_queue, "sink"),
			   "sink");

	gst_bin_add (GST_BIN (play->pipeline), audio_thread);

	gst_bin_set_pre_iterate_function(
				GST_BIN (audio_thread), 
				(GstBinPrePostIterateFunction) callback_bin_pre_iterate,
				play->audio_bin_mutex);
	
	gst_bin_set_post_iterate_function(
				GST_BIN (audio_thread), 
				(GstBinPrePostIterateFunction) callback_bin_post_iterate,
				play->audio_bin_mutex);

	return TRUE;
}


static gboolean
gst_play_audioht_set_audio (GstPlay *play, GstElement *audio_sink)
{
	GstElement *audio_thread;
	
	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	g_return_val_if_fail (GST_IS_ELEMENT (audio_sink), FALSE);

	audio_thread = g_hash_table_lookup(play->other_elements, "audio_thread");

	if (play->audio_sink)
	{
		gst_element_disconnect (play->volume, play->audio_sink);
		gst_bin_remove (GST_BIN (audio_thread), play->audio_sink);
	}

	play->audio_sink = audio_sink;
	gst_bin_add (GST_BIN (audio_thread), play->audio_sink);
	gst_element_connect (play->volume, play->audio_sink);

	return TRUE;
}


static gboolean
gst_play_audioht_set_auto (GstPlay *play, GstElement *autoplugger)
{
	GstElement *audio_thread;
	
	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	g_return_val_if_fail (GST_IS_ELEMENT (autoplugger), FALSE);

	audio_thread = g_hash_table_lookup(play->other_elements, "audio_thread");

	if (play->autoplugger){
		/* we need to remove the existing autoplugger before creating a new one */
		gst_element_disconnect (play->autoplugger, audio_thread);
		gst_element_disconnect (play->autoplugger, play->source);
		gst_bin_remove (GST_BIN (play->pipeline), play->autoplugger);
	}
	
	play->autoplugger = autoplugger;
	g_return_val_if_fail (play->autoplugger != NULL, FALSE);

	gst_bin_add (GST_BIN (play->pipeline), play->autoplugger);
	gst_element_connect (play->source, play->autoplugger);
	gst_element_connect (play->autoplugger, audio_thread);
	return TRUE;
}

/*
 * GST_PLAY_PIPE_VIDEO
 * { gnomevfssrc ! spider ! { queue ! volume ! osssink }
 * spider0.src2 ! { queue ! colorspace ! (videosink) } }
 */

static gboolean 
gst_play_video_setup (GstPlay *play, GError **error)
{
	GstElement *audio_bin, *audio_queue;
	GstElement *video_queue, *video_bin;
	GstElement *work_thread, *colorspace;

	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	
	/* creating pipeline */	
	play->pipeline = gst_pipeline_new ("main_pipeline");
	g_return_val_if_fail (GST_IS_PIPELINE (play->pipeline), FALSE);

	/* creating work thread */
	work_thread = gst_thread_new ("work_thread");
	g_return_val_if_fail (GST_IS_THREAD (work_thread), FALSE);
	g_hash_table_insert(play->other_elements, "work_thread", work_thread);
	
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

	/* setting up iterate functions */	
	gst_bin_set_pre_iterate_function (
		GST_BIN (audio_bin), 
		(GstBinPrePostIterateFunction) callback_bin_pre_iterate, 
		play->audio_bin_mutex);
	gst_bin_set_post_iterate_function (
		GST_BIN (audio_bin), 
		(GstBinPrePostIterateFunction) callback_bin_post_iterate, 
		play->audio_bin_mutex);

	/* adding all that stuff to bin */
	gst_bin_add_many (
		GST_BIN (audio_bin), audio_queue, play->volume, 
		play->audio_sink, NULL);
	gst_element_connect_many (audio_queue, play->volume,
		play->audio_sink, NULL);
	
	gst_element_add_ghost_pad (
		audio_bin, 
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
	
	gst_element_connect_many (video_queue, colorspace,
			play->video_sink, NULL);
	
	/* setting up iterate functions */
	gst_bin_set_pre_iterate_function (
			GST_BIN (video_bin), 
			(GstBinPrePostIterateFunction) callback_bin_pre_iterate, 
			play->video_bin_mutex);
	gst_bin_set_post_iterate_function (
			GST_BIN (video_bin), 
			(GstBinPrePostIterateFunction) callback_bin_post_iterate,
			play->video_bin_mutex);
	
	gst_element_add_ghost_pad (
			video_bin, gst_element_get_pad (video_queue, "sink"),
			"sink");
			
	gst_bin_add (GST_BIN (work_thread), video_bin);

	return TRUE;
}


static gboolean
gst_play_video_set_auto (GstPlay *play, GstElement *autoplugger){

	GstElement *audio_bin, *video_bin, *work_thread;

	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	g_return_val_if_fail (GST_IS_ELEMENT (autoplugger), FALSE);

	audio_bin = g_hash_table_lookup(play->other_elements, "audio_bin");
	video_bin = g_hash_table_lookup(play->other_elements, "video_bin");
	work_thread = g_hash_table_lookup(play->other_elements, "work_thread");

	if (play->autoplugger){
		/* we need to remove the existing autoplugger before creating a new one */
		gst_element_disconnect (play->autoplugger, audio_bin);
		gst_element_disconnect (play->autoplugger, play->source);
		gst_element_disconnect (play->autoplugger, video_bin);

		gst_bin_remove (GST_BIN (work_thread), play->autoplugger);
	}
	
	play->autoplugger = autoplugger;
	g_return_val_if_fail (play->autoplugger != NULL, FALSE);

	gst_bin_add (GST_BIN (work_thread), play->autoplugger);
	gst_element_connect (play->source, play->autoplugger);
	gst_element_connect (play->autoplugger, audio_bin);
	gst_element_connect (play->autoplugger, video_bin);

	return TRUE;
}


static gboolean
gst_play_video_set_video (GstPlay *play, GstElement *video_sink)
{
	GstElement *video_mate, *video_bin;
	
	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	g_return_val_if_fail (GST_IS_ELEMENT (video_sink), FALSE);

	video_bin = g_hash_table_lookup(play->other_elements, "video_bin");
	video_mate = g_hash_table_lookup(play->other_elements, "colorspace");

	if (play->video_sink){
		gst_element_disconnect (video_mate, play->video_sink);
		gst_bin_remove (GST_BIN (video_bin), play->video_sink);
	}
	play->video_sink = video_sink;
	gst_bin_add (GST_BIN (video_bin), play->video_sink);
	gst_element_connect (video_mate, play->video_sink);

	play->video_sink_element = gst_play_get_sink_element (play, video_sink);

	if (play->video_sink_element != NULL){
		g_signal_connect (G_OBJECT (play->video_sink_element), "have_xid",
				  G_CALLBACK (callback_video_have_xid), play);
		g_signal_connect (G_OBJECT (play->video_sink_element), "have_size",
				  G_CALLBACK (callback_video_have_size), play);
		g_object_set(G_OBJECT(play->video_sink_element), "need_new_window", TRUE, "toplevel", FALSE, NULL);
	}
	return TRUE;
}


static gboolean
gst_play_video_set_audio (GstPlay *play, GstElement *audio_sink)
{
	GstElement *audio_bin;
	
	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	g_return_val_if_fail (GST_IS_ELEMENT (audio_sink), FALSE);
	
	audio_bin = g_hash_table_lookup(play->other_elements, "audio_bin");
	
	if (play->audio_sink)
	{
		gst_element_disconnect (play->volume, play->audio_sink);
		gst_bin_remove (GST_BIN (audio_bin), play->audio_sink);
	}

	play->audio_sink = audio_sink;
	gst_bin_add (GST_BIN (audio_bin), play->audio_sink);
	gst_element_connect (play->volume, play->audio_sink);

	play->audio_sink_element = gst_play_get_sink_element (play, audio_sink);

	if (play->audio_sink_element != NULL){
		g_signal_connect (G_OBJECT (play->audio_sink), "eos",
				  G_CALLBACK (callback_audio_sink_eos), play);
	}

	return TRUE;
}

/*
 * GST_PLAY_PIPE_VIDEO_THREADSAFE
 * { gnomevfssrc ! spider ! { queue ! volume ! osssink } } 
 * spider0.src2 ! queue ! videosink
 * (note that the xvideosink is not contained by a thread)
 */

static gboolean 
gst_play_videots_setup (GstPlay *play, GError **error)
{
	GstElement *audio_bin, *audio_queue, *video_queue, *auto_identity, *work_thread;

	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	
	/* creating pipeline */
	play->pipeline = gst_pipeline_new ("main_pipeline");
	g_return_val_if_fail (GST_IS_PIPELINE (play->pipeline), FALSE);

	/* creating work thread */	
	work_thread = gst_thread_new ("work_thread");
	g_return_val_if_fail (GST_IS_THREAD (work_thread), FALSE);
	g_hash_table_insert(play->other_elements, "work_thread", work_thread);
	
	gst_bin_add (GST_BIN (play->pipeline), work_thread);

	/* create source element */
	play->source = gst_element_factory_make ("gnomevfssrc", "source");
	if (!play->source)
	{
		gst_play_error_plugin (GST_PLAY_ERROR_GNOMEVFSSRC, error);
		return FALSE;
	}
	gst_bin_add (GST_BIN (work_thread), play->source);
	
	auto_identity = gst_element_factory_make ("identity", "auto_identity");
	g_return_val_if_fail (auto_identity != NULL, FALSE);
	g_hash_table_insert(play->other_elements, "auto_identity", auto_identity);

	gst_bin_add (GST_BIN (work_thread), auto_identity);
	gst_element_add_ghost_pad (work_thread, 
				   gst_element_get_pad (auto_identity, "src"),
				   "src");
	
	/* create volume elements */
	play->volume = gst_element_factory_make ("volume", "volume");
	if (!play->volume)
	{
		gst_play_error_plugin (GST_PLAY_ERROR_VOLUME, error);
		return FALSE;
	}

	/* create audiosink.
	FIXME : Should use gconf to choose the right one */
	play->audio_sink = gst_element_factory_make ("osssink", "play_audio");
	if (!play->audio_sink)
		g_warning ("You need the osssink element to use this program.\n");
	
	g_object_set (G_OBJECT (play->audio_sink), "fragment", 0x00180008, NULL);
	g_signal_connect (
			G_OBJECT (play->audio_sink), "eos",
			G_CALLBACK (callback_audio_sink_eos), play);

	audio_queue = gst_element_factory_make ("queue", "audio_queue");
	if (!audio_queue)
	{
		gst_play_error_plugin (GST_PLAY_ERROR_QUEUE, error);
		return FALSE;
	}
	g_hash_table_insert(play->other_elements, "audio_queue", audio_queue);
	
	audio_bin = gst_thread_new ("audio_bin");
	if (!audio_bin)
	{
		gst_play_error_plugin (GST_PLAY_ERROR_THREAD, error);
		return FALSE;
	}
	g_hash_table_insert(play->other_elements, "audio_bin", audio_bin);

	gst_bin_set_pre_iterate_function(
			GST_BIN (audio_bin), 
			(GstBinPrePostIterateFunction) callback_bin_pre_iterate,
			play->audio_bin_mutex);
	
	gst_bin_set_post_iterate_function(
			GST_BIN (audio_bin), 
			(GstBinPrePostIterateFunction) callback_bin_post_iterate,
			play->audio_bin_mutex);

	gst_bin_add_many (
			GST_BIN (audio_bin), audio_queue,
			play->volume, play->audio_sink, NULL);
	
	gst_element_connect_many (
			audio_queue, play->volume,
			play->audio_sink, NULL);
	
	gst_element_add_ghost_pad (
			audio_bin, 
			gst_element_get_pad (audio_queue, "sink"),
			"sink");

	gst_bin_add (GST_BIN (work_thread), audio_bin);

	/* create video elements */
	play->video_sink = gst_element_factory_make ("xvideosink", "show");
	
	g_object_set (G_OBJECT (play->video_sink), "toplevel", FALSE, NULL);
	
	g_signal_connect (
			G_OBJECT (play->video_sink), "have_xid",
			G_CALLBACK (callback_video_have_xid), play);
	
	g_signal_connect (
			G_OBJECT (play->video_sink), "have_size",
			G_CALLBACK (callback_video_have_size), play);

	g_return_val_if_fail (play->video_sink != NULL, FALSE);
	
	video_queue = gst_element_factory_make ("queue", "video_queue");
	g_return_val_if_fail (video_queue != NULL, FALSE);
	g_hash_table_insert(play->other_elements, "video_queue", video_queue);
	g_object_set (G_OBJECT (video_queue), "block_timeout", 1000, NULL);

	gst_bin_add_many (
			GST_BIN (play->pipeline), video_queue,
			play->video_sink, NULL);
			
	gst_element_connect (video_queue, play->video_sink);
	
	gst_bin_set_pre_iterate_function(
			GST_BIN (play->pipeline), 
			(GstBinPrePostIterateFunction) callback_bin_pre_iterate,
			play->video_bin_mutex);
			
	gst_bin_set_post_iterate_function(
			GST_BIN (play->pipeline), 
			(GstBinPrePostIterateFunction) callback_bin_post_iterate,
			play->video_bin_mutex);
	
	gst_element_connect (work_thread, video_queue);

	return TRUE;
}


static gboolean
gst_play_videots_set_auto (GstPlay *play, GstElement *autoplugger){

	GstElement *audio_bin, *auto_identity, *work_thread;

	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	g_return_val_if_fail (GST_IS_ELEMENT (autoplugger), FALSE);
	
	audio_bin = g_hash_table_lookup(play->other_elements, "audio_bin");
	auto_identity = g_hash_table_lookup(play->other_elements, "auto_identity");
	work_thread = g_hash_table_lookup(play->other_elements, "work_thread");

	if (play->autoplugger){
		/* we need to remove the existing autoplugger before creating a new one */
		gst_element_disconnect (play->autoplugger, audio_bin);
		gst_element_disconnect (play->autoplugger, play->source);
		gst_element_disconnect (play->autoplugger, auto_identity);

		gst_bin_remove (GST_BIN (work_thread), play->autoplugger);
	}
	
	play->autoplugger = autoplugger;
	g_return_val_if_fail (play->autoplugger != NULL, FALSE);

	gst_bin_add (GST_BIN (work_thread), play->autoplugger);
	gst_element_connect (play->source, play->autoplugger);
	gst_element_connect (play->autoplugger, audio_bin);
	gst_element_connect (play->autoplugger, auto_identity);

	return TRUE;
}


static gboolean
gst_play_videots_set_video (GstPlay *play, GstElement *video_sink)
{
	GstElement *video_mate;
	
	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	g_return_val_if_fail (GST_IS_ELEMENT (video_sink), FALSE);
	
	video_mate = g_hash_table_lookup(play->other_elements, "video_queue");

	if (play->video_sink){
		gst_element_disconnect (video_mate, play->video_sink);
		gst_bin_remove (GST_BIN (play->pipeline), play->video_sink);
	}
	play->video_sink = video_sink;
	gst_bin_add (GST_BIN (play->pipeline), play->video_sink);
	gst_element_connect (video_mate, play->video_sink);

	return TRUE;
}


static gboolean
gst_play_videots_set_audio (GstPlay *play, GstElement *audio_sink)
{
	GstElement *audio_bin;
	
	g_return_val_if_fail (GST_IS_PLAY(play), FALSE);
	g_return_val_if_fail (GST_IS_ELEMENT (audio_sink), FALSE);
	
	audio_bin = g_hash_table_lookup(play->other_elements, "audio_bin");
	
	if (play->audio_sink)
	{
		gst_element_disconnect (play->volume, play->audio_sink);
		gst_bin_remove (GST_BIN (audio_bin), play->audio_sink);
	}

	play->audio_sink = audio_sink;
	gst_bin_add (GST_BIN (audio_bin), play->audio_sink);
	gst_element_connect (play->volume, play->audio_sink);


	return TRUE;
}

/* modelines */
/* vim:set ts=8:sw=8:noet */
