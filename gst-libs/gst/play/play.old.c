/* GStreamer
 * Copyright (C) 1999,2000,2001,2002 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000,2001,2002 Wim Taymans <wtay@chello.be>
 *                              2002 Steve Baker <steve@stevebaker.org>
 *
 * play.c: GstPlay object code
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

#include "play.h"

enum {
	STREAM_END,
	INFORMATION,
	STATE_CHANGE,
	STREAM_LENGTH,
	TIME_TICK,
	HAVE_XID,
	HAVE_VIDEO_SIZE,
	LAST_SIGNAL,
};

/* this struct is used to decouple signals coming out of threaded pipelines */
typedef struct _GstPlaySignal    GstPlaySignal;
struct _GstPlaySignal
{
	gint signal_id;
	union {
		struct {
			gint width;
			gint height;
		} video_size;
		struct {
			gint xid;
		} video_xid;
		struct {
			GstElementState old_state;
			GstElementState new_state;
		} state;
		struct {
			GstElement* element;
			GParamSpec* param;
		} info;
	} signal_data;
};

enum
{
	ARG_0,
	ARG_LOCATION,
	ARG_VOLUME,
	ARG_MUTE,
	/* FILL ME */
};

static guint gst_play_signals [LAST_SIGNAL] = { 0 };

static void         gst_play_init		    (GstPlay *play);
static void         gst_play_class_init	            (GstPlayClass *klass);
static void         gst_play_dispose		    (GObject *object);

static guint         gst_play_default_timeout_add    (guint interval, GSourceFunc function, gpointer data);
static guint         gst_play_default_idle_add       (GSourceFunc function, gpointer data);

static void         gst_play_set_property	    (GObject *object, guint prop_id, 
						     const GValue *value, GParamSpec *pspec);
static void         gst_play_get_property	    (GObject *object, guint prop_id, 
						     GValue *value, GParamSpec *pspec);
static void         callback_pipeline_error         (GObject *object, GstObject *orig, gchar *error, GstPlay* play);
static void         callback_pipeline_state_change  (GstElement *element, GstElementState old, 
						     GstElementState state, GstPlay *play);
static void         callback_pipeline_deep_notify   (GstElement *element, GstElement *orig, 
						     GParamSpec *param, GstPlay *play);
static void         callback_audio_sink_eos         (GstElement *element, GstPlay *play);
static void         callback_video_have_xid         (GstElement *element, gint xid, GstPlay *play);
static void         callback_video_have_size        (GstElement *element, gint width, gint height, GstPlay *play);


static void         callback_bin_pre_iterate        (GstBin *bin, GMutex *mutex);
static void         callback_bin_post_iterate       (GstBin *bin, GMutex *mutex);

static gboolean     gst_play_idle_signal            (GstPlay *play);
static gboolean     gst_play_idle_callback	    (GstPlay *play);
static gboolean     gst_play_get_length_callback    (GstPlay *play);
static gboolean     gst_play_tick_callback          (GstPlay *play);

GQuark
gst_play_error_quark (void)
{
	static GQuark quark = 0;
	if (quark == 0) {
		quark = g_quark_from_static_string ("gst-play-error-quark");
	}

	return quark;
}

/* GError creation when plugin is missing */
/* If we want to make error messages less generic and have more errors
 * than only plug-ins, move the message creation to the switch */
static void
gst_play_error_plugin (GstPlayError type, GError **error)
{
        gchar *name;

        if (error == NULL) return;

        switch (type)
        {
                case GST_PLAY_ERROR_THREAD:
                        name = g_strdup ("thread");
                        break;
                case GST_PLAY_ERROR_QUEUE:
                        name = g_strdup ("queue");
                        break;
                case GST_PLAY_ERROR_FAKESINK:
                        name = g_strdup ("fakesink");
                        break;
                case GST_PLAY_ERROR_VOLUME:
                        name = g_strdup ("volume");
                        break;
                case GST_PLAY_ERROR_COLORSPACE:
                        name = g_strdup ("colorspace");
                        break;
                case GST_PLAY_ERROR_GNOMEVFSSRC:
                        name = g_strdup ("gnomevfssrc");
                        break;
                default:
                        name = g_strdup ("unknown");
                        break;
        }
        *error = g_error_new (GST_PLAY_ERROR, type,
                              "The %s plug-in could not be found. "
                              "This plug-in is essential for gst-player. "
                              "Please install it and verify that it works "
                              "by running 'gst-inspect %s'",
                              name, name);
        g_free (name);
        return;
}

/* split static pipeline functions to a seperate file */
#include "playpipelines.c"

static GstElementClass * parent_class = NULL;

GType
gst_play_get_type (void)
{
	static GType play_type = 0;
	
	if (!play_type)
	{
		static const GTypeInfo play_info = {
			sizeof (GstPlayClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gst_play_class_init,
			NULL, NULL, sizeof (GstPlay),
			0, (GInstanceInitFunc) gst_play_init,
			NULL
		};
      
		play_type = g_type_register_static (G_TYPE_OBJECT, "GstPlay", &play_info, 0);
	}

	return play_type;
}

static void
gst_play_class_init (GstPlayClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  
	parent_class = g_type_class_ref(GST_TYPE_OBJECT);

	klass->information = NULL;
	klass->state_changed = NULL;
	klass->stream_end = NULL;
  
	gobject_class->dispose = gst_play_dispose;
	gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_play_set_property);
	gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_play_get_property);

	g_object_class_install_property (gobject_class, ARG_LOCATION,
		g_param_spec_string ("location", "location of file", 
			             "location of the file to play",
				     NULL, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, ARG_VOLUME,
		g_param_spec_float ("volume", "Playing volume", 
			            "Playing volume", 
							     0, 1.0, 0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, ARG_MUTE,
					 g_param_spec_boolean ("mute", "Volume muted", "Playing volume muted",
							       FALSE, G_PARAM_READWRITE));
  
	gst_play_signals [INFORMATION] = 
		g_signal_new ("information", 
			      G_TYPE_FROM_CLASS (klass), 
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GstPlayClass, information), 
			      NULL, NULL,
			      gst_marshal_VOID__OBJECT_PARAM, 
			      G_TYPE_NONE, 2, 
			      G_TYPE_OBJECT, G_TYPE_PARAM);
	
	gst_play_signals [STATE_CHANGE] = 
		g_signal_new ("state_change", 
			      G_TYPE_FROM_CLASS (klass), 
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GstPlayClass, state_changed), 
			      NULL, NULL,
			      gst_marshal_VOID__INT_INT,
			      G_TYPE_NONE, 2, 
			      G_TYPE_INT, G_TYPE_INT);
	
	gst_play_signals [STREAM_END] =
		g_signal_new ("stream_end",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GstPlayClass, stream_end),
			      NULL, NULL,
			      gst_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gst_play_signals [TIME_TICK] = 
		g_signal_new ("time_tick", 
			      G_TYPE_FROM_CLASS (klass), 
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GstPlayClass, time_tick), 
			      NULL, NULL,
			      gst_marshal_VOID__INT64,
			      G_TYPE_NONE, 1, 
			      G_TYPE_INT64);

	gst_play_signals [STREAM_LENGTH] = 
		g_signal_new ("stream_length", 
			      G_TYPE_FROM_CLASS (klass), 
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GstPlayClass, stream_length), 
			      NULL, NULL,
			      gst_marshal_VOID__INT64,
			      G_TYPE_NONE, 1, 
			      G_TYPE_INT64);

	gst_play_signals [HAVE_XID] = 
		g_signal_new ("have_xid", 
			      G_TYPE_FROM_CLASS (klass), 
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GstPlayClass, have_xid), 
			      NULL, NULL,
			      gst_marshal_VOID__INT,
			      G_TYPE_NONE, 1, 
			      G_TYPE_INT);

	gst_play_signals [HAVE_VIDEO_SIZE] = 
		g_signal_new ("have_video_size", 
			      G_TYPE_FROM_CLASS (klass), 
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GstPlayClass, have_video_size), 
			      NULL, NULL,
			      gst_marshal_VOID__INT_INT,
			      G_TYPE_NONE, 2, 
			      G_TYPE_INT, G_TYPE_INT);

	gst_control_init(NULL,NULL);
}



static void 
gst_play_init (GstPlay *play) 
{
	play->pipeline     = NULL;
	play->source       = NULL;
	play->autoplugger  = NULL;
	play->audio_sink   = NULL;
	play->audio_sink_element   = NULL;
	play->video_sink   = NULL;
	play->video_sink_element   = NULL;
	play->volume       = NULL;
	play->other_elements = g_hash_table_new(g_str_hash, g_str_equal);
	play->audio_bin_mutex = g_mutex_new();
	play->video_bin_mutex = g_mutex_new();
	gst_play_set_idle_timeout_funcs(play, gst_play_default_timeout_add, gst_play_default_idle_add);
	play->tick_timeout_id = play->idle_timeout_id = play->idle_signal_id = 0;
}

GstPlay *
gst_play_new (GstPlayPipeType pipe_type, GError **error)
{
	GstPlay *play;

	play = g_object_new (GST_TYPE_PLAY, NULL);

	/* FIXME: looks like only VIDEO ever gets used ! */
	switch (pipe_type){
		case GST_PLAY_PIPE_VIDEO:
			play->setup_pipeline = gst_play_video_setup;
			play->teardown_pipeline = NULL;
			play->set_data_src = gst_play_video_set_data_src;
			play->set_autoplugger = gst_play_video_set_auto;
			play->set_video_sink = gst_play_video_set_video;
			play->set_audio_sink = gst_play_video_set_audio;
			break;
		case GST_PLAY_PIPE_AUDIO:
			/* we can reuse the threaded set functions */
			play->setup_pipeline = gst_play_audio_setup;
			play->teardown_pipeline = NULL;
			play->set_data_src = gst_play_simple_set_data_src;
			play->set_autoplugger = gst_play_audiot_set_auto;
			play->set_video_sink = NULL;
			play->set_audio_sink = gst_play_audiot_set_audio;
			break;
		case GST_PLAY_PIPE_AUDIO_THREADED:
			play->setup_pipeline = gst_play_audiot_setup;
			play->teardown_pipeline = NULL;
			play->set_data_src = gst_play_simple_set_data_src;
			play->set_autoplugger = gst_play_audiot_set_auto;
			play->set_video_sink = NULL;
			play->set_audio_sink = gst_play_audiot_set_audio;
			break;
		case GST_PLAY_PIPE_AUDIO_HYPER_THREADED:
			play->setup_pipeline = gst_play_audioht_setup;
			play->teardown_pipeline = NULL;
			play->set_data_src = gst_play_simple_set_data_src;
			play->set_autoplugger = gst_play_audioht_set_auto;
			play->set_video_sink = NULL;
			play->set_audio_sink = gst_play_audioht_set_audio;
			break;
		default:
			g_warning("unknown pipeline type: %d\n", pipe_type);
	}

	/* init pipeline */
	if ((play->setup_pipeline) &&
	    (! play->setup_pipeline (play, error)))
	{
		g_object_unref (play);
		return NULL;
	}


	if (play->pipeline){
		/* connect to pipeline events */
		g_signal_connect (G_OBJECT (play->pipeline), "deep_notify", G_CALLBACK (callback_pipeline_deep_notify), play);
		g_signal_connect (G_OBJECT (play->pipeline), "state_change", G_CALLBACK (callback_pipeline_state_change), play);
		g_signal_connect (G_OBJECT (play->pipeline), "error", G_CALLBACK (callback_pipeline_error), play);
	}

	if (play->volume){
		play->vol_dpman =  gst_dpman_get_manager(play->volume);
		play->vol_dparam = gst_dpsmooth_new(G_TYPE_FLOAT);
  
		g_object_set(G_OBJECT(play->vol_dparam), "update_period", 2000000LL, NULL);
  
		g_object_set(G_OBJECT(play->vol_dparam), "slope_delta_float", 0.1F, NULL);
		g_object_set(G_OBJECT(play->vol_dparam), "slope_time", 10000000LL, NULL); 
 
		if (!gst_dpman_attach_dparam (play->vol_dpman, "volume", play->vol_dparam)){
			g_warning("could not attach dparam to volume element\n");
		}
		gst_dpman_set_mode(play->vol_dpman, "asynchronous");
		gst_play_set_volume(play, 0.9);
	}
	
	play->signal_queue = g_async_queue_new();

	return play;
}

static void
gst_play_dispose (GObject *object)
{
	GstPlay *play = GST_PLAY (object);
	/* Before disposing, removing any callback that could
	be called : time ticks, signal poller, etc...*/
	if (play->tick_timeout_id)
		if (!g_source_remove(play->tick_timeout_id))
			g_warning("failed to remove timetick timer %d", play->tick_timeout_id);
	if (play->idle_timeout_id)
		if (!g_source_remove(play->idle_timeout_id))
			g_warning("failed to remove idle timer %d", play->idle_timeout_id);
	if (play->idle_signal_id)
		if (!g_source_remove(play->idle_signal_id))
			g_warning("failed to remove signal idle timer %d", play->idle_signal_id);
		
	G_OBJECT_CLASS (parent_class)->dispose (object);
	g_mutex_free(play->audio_bin_mutex);
	g_mutex_free(play->video_bin_mutex);
}

static void
callback_pipeline_error (GObject *object, GstObject *orig, gchar *error, GstPlay* play)
{ 
	g_print ("Pipeline error: %s\n", error);
	if (gst_element_get_state(play->pipeline) == GST_STATE_PLAYING){
		gst_element_set_state(play->pipeline, GST_STATE_READY);
	}
} 

static void
callback_pipeline_deep_notify (GstElement *element, GstElement *orig, GParamSpec *param, GstPlay* play)
{
	GstPlaySignal *signal;
	signal = g_new0(GstPlaySignal, 1);
	signal->signal_id = INFORMATION;
	signal->signal_data.info.element = orig;
	signal->signal_data.info.param = param;
	g_async_queue_push(play->signal_queue, signal);
	/* If an idle callback has been added for signal polling
	no need to put a new one */
	if (!play->idle_signal_id)
		play->idle_signal_id = play->idle_add_func ((GSourceFunc) gst_play_idle_signal, play);
}

static void
callback_pipeline_state_change (GstElement *element, GstElementState old, GstElementState state, GstPlay* play)
{
	GstPlaySignal *signal;

	g_return_if_fail (GST_IS_ELEMENT (element));
	g_return_if_fail (GST_IS_PLAY (play));
	g_return_if_fail (element == play->pipeline);

	/*g_print ("got state change %s to %s\n", gst_element_state_get_name (old), gst_element_state_get_name (state));*/

	/* do additional stuff depending on state */
	if (GST_IS_PIPELINE (play->pipeline)){
		switch (state) {
		case GST_STATE_PLAYING:
			play->idle_timeout_id = play->idle_add_func ((GSourceFunc) gst_play_idle_callback, play);
			play->tick_timeout_id = play->timeout_add_func (200, (GSourceFunc) gst_play_tick_callback, play);
			if (play->length_nanos == 0LL){
				/* try to get the length up to 16 times */
				play->get_length_attempt = 16;
				play->timeout_add_func (200, (GSourceFunc) gst_play_get_length_callback, play); 
			}
			break;
		default:
			break;
		}
	}	
	signal = g_new0(GstPlaySignal, 1);
	signal->signal_id = STATE_CHANGE;
	signal->signal_data.state.old_state = old;
	signal->signal_data.state.new_state = state;
	g_async_queue_push(play->signal_queue, signal);
	/* If an idle callback has been added for signal polling
	no need to put a new one */
	if (!play->idle_signal_id)
		play->idle_signal_id = play->idle_add_func ((GSourceFunc) gst_play_idle_signal, play);
}

static gboolean
gst_play_idle_signal (GstPlay *play)
{
	GstPlaySignal *signal;
	gint queue_length;
	
	g_return_val_if_fail(GST_IS_PLAY(play), FALSE);
	
	signal = g_async_queue_try_pop(play->signal_queue);
	if (signal == NULL){
		return FALSE;
	}

	switch (signal->signal_id){
	case HAVE_XID:
		g_signal_emit (G_OBJECT (play), gst_play_signals[HAVE_XID], 0,
		               signal->signal_data.video_xid.xid);
		break;
	case HAVE_VIDEO_SIZE:
		g_signal_emit (G_OBJECT (play), gst_play_signals[HAVE_VIDEO_SIZE], 0, 
		               signal->signal_data.video_size.width, signal->signal_data.video_size.height);
		break;
	case STATE_CHANGE:
		g_signal_emit (G_OBJECT (play), gst_play_signals[STATE_CHANGE], 0, 
		               signal->signal_data.state.old_state, signal->signal_data.state.new_state);
		break;
	case INFORMATION:
		g_signal_emit (G_OBJECT (play), gst_play_signals[INFORMATION], 0, 
		               signal->signal_data.info.element, signal->signal_data.info.param);
		break;
	default:
		break;
	}

	g_free(signal);
	queue_length = g_async_queue_length (play->signal_queue);
	
	/* If queue length is zero the idle callback will be
	destroyed */
	
	if (!queue_length)
		play->idle_signal_id = 0;
	
	return (queue_length > 0);
}

static gboolean
gst_play_idle_eos (GstPlay* play)
{
	g_signal_emit (G_OBJECT (play), gst_play_signals [STREAM_END], 0);
	return FALSE;
}

static void
callback_audio_sink_eos (GstElement *element, GstPlay *play)
{
	play->idle_add_func ((GSourceFunc) gst_play_idle_eos, play);
}

static void
callback_video_have_xid (GstElement *element, gint xid, GstPlay *play)
{
	GstPlaySignal *signal;
	signal = g_new0(GstPlaySignal, 1);
	signal->signal_id = HAVE_XID;
	signal->signal_data.video_xid.xid = xid;
	g_async_queue_push(play->signal_queue, signal);
	/* If an idle callback has been added for signal polling
	no need to put a new one */
	if (!play->idle_signal_id)
		play->idle_signal_id = play->idle_add_func ((GSourceFunc) gst_play_idle_signal, play);
	/*g_print("have xid %d\n", xid);*/
}

static void
callback_video_have_size (GstElement *element, gint width, gint height, GstPlay *play)
{
	GstPlaySignal *signal;
	signal = g_new0(GstPlaySignal, 1);
	signal->signal_id = HAVE_VIDEO_SIZE;
	signal->signal_data.video_size.width = width;
	signal->signal_data.video_size.height = height;
	g_async_queue_push(play->signal_queue, signal);
	/* If an idle callback has been added for signal polling
	no need to put a new one */
	if (!play->idle_signal_id)
		play->idle_signal_id = play->idle_add_func ((GSourceFunc) gst_play_idle_signal, play);
	/*g_print("have size %d x %d\n", width, height);*/
}

static void 
callback_bin_pre_iterate (GstBin *bin, GMutex *mutex)
{
	g_mutex_lock(mutex);
}

static void 
callback_bin_post_iterate (GstBin *bin, GMutex *mutex)
{
	g_mutex_unlock(mutex);
}

static gboolean
gst_play_get_length_callback (GstPlay *play)
{
	gint64 value;
	GstFormat format = GST_FORMAT_TIME;
	gboolean query_worked = FALSE;

	g_print("trying to get length\n");
	if (play->audio_sink_element != NULL){
		g_mutex_lock(play->audio_bin_mutex);
		query_worked = gst_element_query (play->audio_sink_element, GST_QUERY_TOTAL, &format, &value);
		g_mutex_unlock(play->audio_bin_mutex);
	}
	else if (play->video_sink_element != NULL){
		g_mutex_lock(play->video_bin_mutex);
		query_worked = gst_element_query (play->video_sink_element, GST_QUERY_TOTAL, &format, &value);
		g_mutex_unlock(play->video_bin_mutex);
	}
	if (query_worked){
		g_print("got length %" G_GINT64_FORMAT "\n", value);
		g_signal_emit (G_OBJECT (play), gst_play_signals [STREAM_LENGTH], 0, value);
		play->length_nanos = value;
		return FALSE;
	}
	else {
		if (play->get_length_attempt-- < 1){
			/* we've tried enough times, give up */
			return FALSE;
		}
	}
	return (gst_element_get_state(play->pipeline) == GST_STATE_PLAYING);
}

static gboolean
gst_play_tick_callback (GstPlay *play)
{
	gint secs;
	
	g_return_val_if_fail(GST_IS_PLAY(play), FALSE);
	
	play->clock = gst_bin_get_clock (GST_BIN (play->pipeline));
	play->time_nanos = gst_clock_get_time(play->clock);
	secs = (gint) (play->time_nanos / GST_SECOND);
	if (secs != play->time_seconds){
		play->time_seconds = secs;
		g_signal_emit (G_OBJECT (play), gst_play_signals [TIME_TICK], 0, play->time_nanos);
	}

	return (gst_element_get_state(play->pipeline) == GST_STATE_PLAYING);
}

static gboolean
gst_play_idle_callback (GstPlay *play)
{
	g_return_val_if_fail(GST_IS_PLAY(play), FALSE);
	
	return gst_bin_iterate (GST_BIN (play->pipeline));
}

static void
gst_play_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GstPlay *play = GST_PLAY (object);

	g_return_if_fail (GST_IS_PLAY (play));
	
	switch (prop_id) {
	case ARG_LOCATION:
		gst_play_set_location(play, g_value_get_string (value));
		break;
	case ARG_VOLUME:
		gst_play_set_volume(play, g_value_get_float (value));
		break;
	case ARG_MUTE:
		gst_play_set_mute(play, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gst_play_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstPlay *play = GST_PLAY (object);

	g_return_if_fail (GST_IS_PLAY (play));

	switch (prop_id) {
	case ARG_LOCATION:
		g_value_set_string (value, gst_play_get_location(play));
		break;
	case ARG_VOLUME:
		g_value_set_float(value, gst_play_get_volume(play));
		break;
	case ARG_MUTE:
		g_value_set_boolean (value, gst_play_get_mute(play));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
gst_play_seek_to_time (GstPlay *play, gint64 time_nanos)
{
	GstEvent *s_event;
        guint8   prev_state;
	gboolean audio_seek_worked = FALSE;
	gboolean video_seek_worked = FALSE;

	g_return_if_fail (GST_IS_PLAY (play));
	if (time_nanos < 0LL){
		play->seek_time = 0LL;
	}
	else if (time_nanos < 0LL){
		play->seek_time = play->length_nanos;
	}
	else {
		play->seek_time = time_nanos;
	}

	/*g_print("doing seek to %lld\n", play->seek_time);*/
        prev_state = GST_STATE(play->pipeline);
	gst_element_set_state(play->pipeline, GST_STATE_PAUSED);

	s_event = gst_event_new_seek (GST_FORMAT_TIME |
	                              GST_SEEK_METHOD_SET |
	                              GST_SEEK_FLAG_FLUSH, play->seek_time);
	if (play->audio_sink_element != NULL){
		gst_event_ref (s_event);
		audio_seek_worked = gst_element_send_event (play->audio_sink_element, s_event);
	}
	if (play->video_sink_element != NULL){
		gst_event_ref (s_event);
		video_seek_worked = gst_element_send_event (play->video_sink_element, s_event);
	}
	gst_event_unref (s_event);

	if (audio_seek_worked || video_seek_worked){
		play->time_nanos = gst_clock_get_time(play->clock);
		g_signal_emit (G_OBJECT (play), gst_play_signals [TIME_TICK], 0, play->time_nanos);
	}
	gst_element_set_state(play->pipeline, prev_state);
}

void
gst_play_need_new_video_window(GstPlay *play)
{
	g_return_if_fail (GST_IS_PLAY (play));
	if (GST_IS_ELEMENT(play->video_sink_element)){
		g_object_set(G_OBJECT(play->video_sink_element), "need_new_window", TRUE, NULL);
	}
}

static gboolean
gst_play_default_idle (GstPlayIdleData *idle_data)
{
	if(idle_data->func(idle_data->data)){
		/* call this function again in the future */
		return TRUE;
	}
	/* this function should no longer be called */
	g_free(idle_data);
	return FALSE;
}

static guint
gst_play_default_timeout_add (guint interval, GSourceFunc function, gpointer data)
{
	GstPlayIdleData *idle_data = g_new0(GstPlayIdleData, 1);
	idle_data->func = function;
	idle_data->data = data;

	return g_timeout_add (interval, (GSourceFunc)gst_play_default_idle, idle_data);
}

static guint
gst_play_default_idle_add (GSourceFunc function, gpointer data)
{
	GstPlayIdleData *idle_data = g_new0(GstPlayIdleData, 1);
	idle_data->func = function;
	idle_data->data = data;
	
	return g_idle_add ((GSourceFunc)gst_play_default_idle, idle_data);
}

void
gst_play_set_idle_timeout_funcs (GstPlay *play, GstPlayTimeoutAdd timeout_add_func, GstPlayIdleAdd idle_add_func)
{
	g_return_if_fail (GST_IS_PLAY (play));
	play->timeout_add_func = timeout_add_func;
	play->idle_add_func = idle_add_func;
}

GstElement*
gst_play_get_sink_element (GstPlay *play, GstElement *element){
	GstPad *pad = NULL;
	GList *elements = NULL;
	const GList *pads = NULL;
	gboolean has_src;

	g_return_val_if_fail (GST_IS_PLAY (play), NULL);
	g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

	if (!GST_IS_BIN(element)){
		/* since its not a bin, we'll presume this 
		 * element is a sink element */		
		return element;
	}

	elements = (GList *) gst_bin_get_list (GST_BIN(element));
	/* traverse all elements looking for a src pad */
	while (elements && pad == NULL) {	
		element = GST_ELEMENT (elements->data);
		pads = gst_element_get_pad_list (element);
		has_src = FALSE;
		while (pads) {
			/* check for src pad */
			if (GST_PAD_DIRECTION (GST_PAD (pads->data)) == GST_PAD_SRC) {
				has_src = TRUE;
				break;
			}
			pads = g_list_next (pads);
		}
		if (!has_src){
			return element;
		}
		elements = g_list_next (elements);
	}
	/* we didn't find a sink element */
	return NULL;
}

gboolean
gst_play_set_data_src (GstPlay *play, GstElement *data_src)
{
	g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
	g_return_val_if_fail (GST_IS_ELEMENT (data_src), FALSE);

	if (gst_play_get_state (play) != GST_STATE_READY){
		gst_play_set_state (play, GST_STATE_READY);
	}

	if (play->set_data_src){
		return play->set_data_src(play, data_src);
	}

	/* if there is no set_data_src func, fail quietly */
	return FALSE;
}

gboolean
gst_play_set_video_sink (GstPlay *play, GstElement *video_sink)
{
	g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
	g_return_val_if_fail (GST_IS_ELEMENT (video_sink), FALSE);

	if (gst_play_get_state (play) != GST_STATE_READY){
		gst_play_set_state (play, GST_STATE_READY);
	}

	if (play->set_video_sink){
		return play->set_video_sink(play, video_sink);
	}

	/* if there is no set_video_sink func, fail quietly */
	return FALSE;
}

gboolean
gst_play_set_audio_sink (GstPlay *play, GstElement *audio_sink)
{
	g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
	g_return_val_if_fail (GST_IS_ELEMENT (audio_sink), FALSE);

	if (gst_play_get_state (play) != GST_STATE_READY){
		gst_play_set_state (play, GST_STATE_READY);
	}

	if (play->set_audio_sink){
		return play->set_audio_sink(play, audio_sink);
	}

	/* if there is no set_audio_sink func, fail quietly */
	return FALSE;
}

GstElementStateReturn
gst_play_set_state (GstPlay *play, GstElementState state)
{
	g_return_val_if_fail (GST_IS_PLAY (play), GST_STATE_FAILURE);
	g_return_val_if_fail (GST_IS_ELEMENT(play->pipeline), GST_STATE_FAILURE);
	/*g_print("setting state to %d\n", state);*/

	return gst_element_set_state(play->pipeline, state);
}

GstElementState
gst_play_get_state (GstPlay *play)
{
	g_return_val_if_fail (GST_IS_PLAY (play), GST_STATE_FAILURE);
	g_return_val_if_fail (play->pipeline, GST_STATE_FAILURE);

	return gst_element_get_state(play->pipeline);
}

gboolean
gst_play_set_location (GstPlay *play, const gchar *location)
{
	GstElementState current_state;
	g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
	g_return_val_if_fail (location != NULL, FALSE);

	current_state = gst_play_get_state (play);
	if (current_state != GST_STATE_READY){
		gst_play_set_state (play, GST_STATE_READY);
	}

	if (play->set_autoplugger){
		if (! play->set_autoplugger(play, gst_element_factory_make ("spider", "autoplugger"))){
			g_warning ("couldn't replace autoplugger\n");
			return FALSE;
		}
	}

	/* FIXME check for valid location (somehow) */
	g_object_set (G_OBJECT (play->source), "location", location, NULL);

	/* reset time/length values */
	play->time_seconds = 0;
	play->length_nanos = 0LL;
	play->time_nanos = 0LL;
	g_signal_emit (G_OBJECT (play), gst_play_signals [STREAM_LENGTH], 0, 0LL);
	g_signal_emit (G_OBJECT (play), gst_play_signals [TIME_TICK], 0, 0LL);
	play->need_stream_length = TRUE;

	return TRUE;
}

gchar*
gst_play_get_location (GstPlay *play)
{
	gchar* location;
	g_return_val_if_fail (GST_IS_PLAY (play), NULL);
	g_return_val_if_fail (GST_IS_ELEMENT(play->source), NULL);
	g_object_get (G_OBJECT (play->source), "location", &location, NULL);
	return location;
}


void
gst_play_set_volume (GstPlay *play, gfloat volume)
{
	g_return_if_fail (GST_IS_PLAY (play));

	g_object_set(G_OBJECT(play->vol_dparam), "value_float", volume, NULL);
}

gfloat
gst_play_get_volume (GstPlay *play)
{
	gfloat volume;

	g_return_val_if_fail (GST_IS_PLAY (play), 0);

	g_object_get(G_OBJECT(play->vol_dparam), "value_float", &volume, NULL);

	return volume;
}

void
gst_play_set_mute (GstPlay *play, gboolean mute)
{
	g_return_if_fail (GST_IS_PLAY (play));

	g_object_set (G_OBJECT (play->volume), "mute", mute, NULL);
}
	
gboolean
gst_play_get_mute (GstPlay *play)
{
	gboolean mute;

	g_return_val_if_fail (GST_IS_PLAY (play), 0);

	g_object_get (G_OBJECT (play->volume), "mute", &mute, NULL);

	return mute;
}

/* modelines */
/* vim:set ts=8:sw=8:noet */

