#include <config.h>

#include <string.h>

#include "gstplay.h"
#include "gstplayprivate.h"

static void gst_play_class_init		(GstPlayClass *klass);
static void gst_play_init		(GstPlay *play);

static void gst_play_set_arg		(GtkObject *object, GtkArg *arg, guint id);
static void gst_play_get_arg		(GtkObject *object, GtkArg *arg, guint id);

static void gst_play_realize		(GtkWidget *play);

static void gst_play_frame_displayed	(GstElement *element, GstPlay *play);
static void gst_play_have_size 		(GstElement *element, guint width, guint height, GstPlay *play);
static void gst_play_audio_handoff	(GstElement *element, GstPlay *play);

/* signals and args */
enum {
	SIGNAL_STATE_CHANGED,
	SIGNAL_FRAME_DISPLAYED,
	SIGNAL_AUDIO_PLAYED,
	LAST_SIGNAL
};

enum {
	ARG_0,
	ARG_URI,
	ARG_MUTE,
	ARG_STATE,
	ARG_MEDIA_SIZE,
	ARG_MEDIA_OFFSET,
	ARG_MEDIA_TOTAL_TIME,
	ARG_MEDIA_CURRENT_TIME,
};

static GtkObject *parent_class = NULL;
static guint gst_play_signals[LAST_SIGNAL] = {0};

GtkType
gst_play_get_type (void)
{
	static GtkType play_type = 0;

	if (!play_type) {
		static const GtkTypeInfo play_info = {
			"GstPlay",
			sizeof (GstPlay),
			sizeof (GstPlayClass),
			(GtkClassInitFunc) gst_play_class_init,
			(GtkObjectInitFunc) gst_play_init,
			NULL,
			NULL,
			(GtkClassInitFunc)NULL,
		};
		play_type = gtk_type_unique (gtk_hbox_get_type (), &play_info);
	}
	return play_type;
}

static void
gst_play_class_init (GstPlayClass *klass)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;

	parent_class = gtk_type_class (gtk_hbox_get_type ());

	object_class = (GtkObjectClass*)klass;
	widget_class = (GtkWidgetClass*)klass;

	gst_play_signals[SIGNAL_STATE_CHANGED] =
		gtk_signal_new ("playing_state_changed", GTK_RUN_FIRST, object_class->type,
				GTK_SIGNAL_OFFSET (GstPlayClass, state_changed),
				gtk_marshal_NONE__INT, GTK_TYPE_NONE, 1,
				GTK_TYPE_INT);

	gst_play_signals[SIGNAL_FRAME_DISPLAYED] =
		gtk_signal_new ("frame_displayed",GTK_RUN_FIRST, object_class->type,
				GTK_SIGNAL_OFFSET (GstPlayClass, frame_displayed),
				gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

	gst_play_signals[SIGNAL_AUDIO_PLAYED] =
		gtk_signal_new ("audio_played",GTK_RUN_FIRST, object_class->type,
				GTK_SIGNAL_OFFSET (GstPlayClass, audio_played),
				gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

	gtk_object_class_add_signals (object_class, gst_play_signals, LAST_SIGNAL);
	gtk_object_add_arg_type ("GstPlay::uri", GTK_TYPE_STRING,
				 GTK_ARG_READABLE, ARG_URI);
	gtk_object_add_arg_type ("GstPlay::mute", GTK_TYPE_BOOL,
				 GTK_ARG_READWRITE, ARG_MUTE);
	gtk_object_add_arg_type ("GstPlay::state", GTK_TYPE_INT,
				 GTK_ARG_READABLE, ARG_STATE);
	gtk_object_add_arg_type ("GstPlay::media_size", GTK_TYPE_ULONG,
				 GTK_ARG_READABLE, ARG_MEDIA_SIZE);
	gtk_object_add_arg_type ("GstPlay::media_offset", GTK_TYPE_ULONG,
				 GTK_ARG_READABLE, ARG_MEDIA_OFFSET);
	gtk_object_add_arg_type ("GstPlay::media_total_time", GTK_TYPE_ULONG,
				 GTK_ARG_READABLE, ARG_MEDIA_TOTAL_TIME);
	gtk_object_add_arg_type ("GstPlay::media_current_time", GTK_TYPE_ULONG,
				 GTK_ARG_READABLE, ARG_MEDIA_CURRENT_TIME);

	object_class->set_arg = gst_play_set_arg;
	object_class->get_arg = gst_play_get_arg;

	widget_class->realize = gst_play_realize;

}

static void
gst_play_init (GstPlay *play)
{
	GstPlayPrivate *priv = g_new0 (GstPlayPrivate, 1);
	GstElement *colorspace;

	play->priv = priv;

	/* create a new bin to hold the elements */
	priv->pipeline = gst_pipeline_new ("main_pipeline");
	g_assert (priv->pipeline != NULL);

	priv->audio_element = gst_elementfactory_make ("osssink", "play_audio");
	g_return_if_fail (priv->audio_element != NULL);
	gtk_signal_connect (GTK_OBJECT (priv->audio_element), "handoff",
			    GTK_SIGNAL_FUNC (gst_play_audio_handoff), play);

	priv->video_element = gst_elementfactory_make ("bin", "video_bin");
  
	priv->video_show = gst_elementfactory_make ("xvideosink", "show");
	g_return_if_fail (priv->video_show != NULL);
	//gtk_object_set (GTK_OBJECT (priv->video_element), "xv_enabled", FALSE, NULL);
	gtk_signal_connect (GTK_OBJECT (priv->video_show), "frame_displayed",
			    GTK_SIGNAL_FUNC (gst_play_frame_displayed), play);
	gtk_signal_connect (GTK_OBJECT (priv->video_show), "have_size",
			    GTK_SIGNAL_FUNC (gst_play_have_size), play);

	gst_bin_add (GST_BIN (priv->video_element), priv->video_show);

	colorspace = gst_elementfactory_make ("colorspace", "colorspace");
	if (colorspace == NULL) {
	  g_warning ("could not create the 'colorspace' element, doing without");
	  gst_element_add_ghost_pad (priv->video_element, 
				   gst_element_get_pad (priv->video_show, "sink"),
				   "sink");
	}
	else {
	  gst_bin_add (GST_BIN (priv->video_element), colorspace);
	  gst_element_connect (colorspace, "src", priv->video_show, "sink");
	  gst_element_add_ghost_pad (priv->video_element, 
				   gst_element_get_pad (colorspace, "sink"),
				   "sink");
	}

	play->state = GST_PLAY_STOPPED;
	play->flags = 0;

	priv->src = NULL;
	priv->muted = FALSE;
	priv->can_seek = TRUE;
	priv->uri = NULL;
	priv->offset_element = NULL;
	priv->bit_rate_element = NULL;
	priv->media_time_element = NULL;

	priv->source_width = 0;
	priv->source_height = 0;
}

GstPlay *
gst_play_new ()
{
	return GST_PLAY (gtk_type_new (GST_TYPE_PLAY));
}

static gboolean
gst_play_idle_func (gpointer data)
{
	return gst_bin_iterate (GST_BIN (data));
}

static void
gst_play_eos (GstElement *element,
	      GstPlay *play)
{
	GST_DEBUG(0, "gstplay: eos reached\n");
	gst_play_stop (play);
}

static void
gst_play_have_size (GstElement *element, guint width, guint height,
		    GstPlay *play)
{
	GstPlayPrivate *priv;

	priv = (GstPlayPrivate *) play->priv;

	priv->source_width = width;
	priv->source_height = height;

	gtk_widget_set_usize (priv->video_widget, width, height);
}

static void
gst_play_frame_displayed (GstElement *element, GstPlay *play)
{
	GstPlayPrivate *priv;
	static int stolen = FALSE;

	priv = (GstPlayPrivate *)play->priv;

	gdk_threads_enter ();
	if (!stolen) {
		gtk_widget_realize (priv->video_widget);
		gtk_socket_steal (GTK_SOCKET (priv->video_widget),
				  gst_util_get_int_arg (G_OBJECT (priv->video_show), "xid"));
		gtk_widget_show (priv->video_widget);
		stolen = TRUE;
	}
	gdk_threads_leave ();

	gtk_signal_emit (GTK_OBJECT (play), gst_play_signals[SIGNAL_FRAME_DISPLAYED],
			 NULL);
}

static void
gst_play_audio_handoff (GstElement *element, GstPlay *play)
{
	gtk_signal_emit (GTK_OBJECT (play), gst_play_signals[SIGNAL_AUDIO_PLAYED],
			 NULL);
}

static void
gst_play_object_introspect (GstObject *object, const gchar *property, GstElement **target)
{
	gchar *info;
	GtkArgInfo *arg;
	GstElement *element;

	if (!GST_IS_ELEMENT (object) && !GST_IS_BIN (object))
		return;

	element = GST_ELEMENT (object);

	info = gtk_object_arg_get_info (GTK_OBJECT_TYPE (element), property, &arg);

	if (info) {
		g_free(info);
	}
	else {
		*target = element;
		GST_DEBUG(0, "gstplay: using element \"%s\" for %s property\n", 
			  gst_element_get_name(element), property);
	}
}

/* Dumb introspection of the interface...
 * this will change with glib 1.4
 * */
static void
gst_play_object_added (GstAutoplug* autoplug, GstObject *object, GstPlay *play)
{
	GstPlayPrivate *priv;

	g_return_if_fail (play != NULL);

	priv = (GstPlayPrivate *)play->priv;

	if (GST_FLAG_IS_SET (object, GST_ELEMENT_NO_SEEK)) {
		priv->can_seek = FALSE;
	}

	// first come first serve here...
	if (!priv->offset_element)
		gst_play_object_introspect (object, "offset", &priv->offset_element);
	if (!priv->bit_rate_element)
		gst_play_object_introspect (object, "bit_rate", &priv->bit_rate_element);
	if (!priv->media_time_element)
		gst_play_object_introspect (object, "media_time", &priv->media_time_element);
	if (!priv->current_time_element)
		gst_play_object_introspect (object, "current_time", &priv->current_time_element);

}

static void
gst_play_cache_empty (GstElement *element, GstPlay *play)
{
	GstPlayPrivate *priv;
	GstElement *new_element;

	priv = (GstPlayPrivate *)play->priv;

	gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);

	new_element = gst_bin_get_by_name (GST_BIN (priv->pipeline), "new_element");

	gst_element_disconnect (priv->src, "src", priv->cache, "sink");
	gst_element_disconnect (priv->cache, "src", new_element, "sink");
	gst_bin_remove (GST_BIN (priv->pipeline), priv->cache);
	gst_element_connect (priv->src, "src", new_element, "sink");

	gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
}

static void
gst_play_have_type (GstElement *sink, GstCaps *caps, GstPlay *play)
{
	GstPlayPrivate *priv;
	GstElement *new_element;
	GstAutoplug *autoplug;

	GST_DEBUG (0,"GstPipeline: play have type\n");

	priv = (GstPlayPrivate *)play->priv;

	gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);

	gst_element_disconnect (priv->cache, "src", priv->typefind, "sink");
	gst_bin_remove (GST_BIN (priv->pipeline), priv->typefind);

	autoplug = gst_autoplugfactory_make ("staticrender");
	g_assert (autoplug != NULL);

	gtk_signal_connect (GTK_OBJECT (autoplug), "new_object", gst_play_object_added, play);

	new_element = gst_autoplug_to_renderers (autoplug,
						 caps,
						 priv->video_element,
						 priv->audio_element,
						 NULL);

	if (!new_element) {
		// FIXME, signal a suitable error
		return;
	}

	gst_element_set_name (new_element, "new_element");

	gst_bin_add (GST_BIN (priv->pipeline), new_element);

	gtk_object_set (G_OBJECT (priv->cache), "reset", TRUE, NULL);

	gst_element_connect (priv->cache, "src", new_element, "sink");

	gtk_signal_connect (GTK_OBJECT (priv->pipeline), "eos", GTK_SIGNAL_FUNC (gst_play_eos), play);

	gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
}

static gboolean
connect_pads (GstElement *new_element, GstElement *target, gboolean add)
{
	GList *pads = gst_element_get_pad_list (new_element);
	GstPad *targetpad = gst_element_get_pad (target, "sink");

	while (pads) {
		GstPad *pad = GST_PAD (pads->data);

		if (gst_pad_check_compatibility (pad, targetpad)) {
			if (add) {
				gst_bin_add (GST_BIN (gst_element_get_parent (
					GST_ELEMENT (gst_pad_get_real_parent (pad)))),
					     target);
			}
			gst_pad_connect (pad, targetpad);
			return TRUE;
		}
		pads = g_list_next (pads);
	}
	return FALSE;
}

GstPlayReturn
gst_play_set_uri (GstPlay *play, const guchar *uri)
{
	GstPlayPrivate *priv;
	FILE *file;
	gchar* uriloc;

	g_return_val_if_fail (play != NULL, GST_PLAY_ERROR);
	g_return_val_if_fail (GST_IS_PLAY (play), GST_PLAY_ERROR);
	g_return_val_if_fail (uri != NULL, GST_PLAY_ERROR);
	
	priv = (GstPlayPrivate *)play->priv;

	if (priv->uri)
		g_free (priv->uri);
	
	/* see if it looks like an URI */
	if ((uriloc = strstr (uri, ":/"))) {
		priv->src = gst_elementfactory_make ("gnomevfssrc", "srcelement");
		
		if (!priv->src) {
			if (strstr (uri, "file:/")) {
	      uri += strlen ("file:/");
			}
			else
				return GST_PLAY_CANNOT_PLAY;
		}
	}
	
	if (priv->src == NULL) {
		priv->src = gst_elementfactory_make ("filesrc", "srcelement");
	}
	
	priv->uri = g_strdup (uri);
	
	//priv->src = gst_elementfactory_make ("dvdsrc", "disk_src");
	priv->offset_element = priv->src;
	g_return_val_if_fail (priv->src != NULL, GST_PLAY_CANNOT_PLAY);
	
	gtk_object_set (G_OBJECT (priv->src), "location", priv->uri, NULL);
	
	priv->cache = gst_elementfactory_make ("autoplugcache", "cache");
	g_return_val_if_fail (priv->cache != NULL, GST_PLAY_CANNOT_PLAY);
	
	gtk_signal_connect (GTK_OBJECT (priv->cache), "cache_empty", 
			    GTK_SIGNAL_FUNC (gst_play_cache_empty), play);
	
	priv->typefind = gst_elementfactory_make ("typefind", "typefind");
	g_return_val_if_fail (priv->typefind != NULL, GST_PLAY_CANNOT_PLAY);
	gtk_signal_connect (GTK_OBJECT (priv->typefind), "have_type", 
			    GTK_SIGNAL_FUNC (gst_play_have_type), play);
	
	
	gst_bin_add (GST_BIN (priv->pipeline), priv->src);
	gst_bin_add (GST_BIN (priv->pipeline), priv->cache);
	gst_bin_add (GST_BIN (priv->pipeline), priv->typefind);
	
	gst_element_connect (priv->src, "src", priv->cache, "sink");
	gst_element_connect (priv->cache, "src", priv->typefind, "sink");
	
	return GST_PLAY_OK;
}

static void
gst_play_realize (GtkWidget *widget)
{
	GstPlay *play;
	GstPlayPrivate *priv;

	g_return_if_fail (GST_IS_PLAY (widget));

	//g_print ("realize\n");

	play = GST_PLAY (widget);
	priv = (GstPlayPrivate *)play->priv;

	priv->video_widget = gtk_socket_new ();

	gtk_container_add (GTK_CONTAINER (widget), priv->video_widget);
	
	if (GTK_WIDGET_CLASS (parent_class)->realize) {
		GTK_WIDGET_CLASS (parent_class)->realize (widget);
	}

	//gtk_socket_steal (GTK_SOCKET (priv->video_widget),
	//             gst_util_get_int_arg (GTK_OBJECT(priv->video_element), "xid"));

	//gtk_widget_realize (priv->video_widget);
	//gtk_socket_steal (GTK_SOCKET (priv->video_widget),
	//             gst_util_get_int_arg (GTK_OBJECT(priv->video_element), "xid"));
}

void
gst_play_play (GstPlay *play)
{
	GstPlayPrivate *priv;

	g_return_if_fail (play != NULL);
	g_return_if_fail (GST_IS_PLAY (play));

	priv = (GstPlayPrivate *)play->priv;

	if (play->state == GST_PLAY_PLAYING) return;

	if (play->state == GST_PLAY_STOPPED)
		gst_element_set_state (GST_ELEMENT (priv->pipeline),GST_STATE_READY);
	gst_element_set_state (GST_ELEMENT (priv->pipeline),GST_STATE_PLAYING);

	play->state = GST_PLAY_PLAYING;
	gtk_idle_add (gst_play_idle_func, priv->pipeline);

	gtk_signal_emit (GTK_OBJECT (play), gst_play_signals[SIGNAL_STATE_CHANGED],
			 play->state);
}

void
gst_play_pause (GstPlay *play)
{
	GstPlayPrivate *priv;

	g_return_if_fail (play != NULL);
	g_return_if_fail (GST_IS_PLAY (play));

	priv = (GstPlayPrivate *)play->priv;

	if (play->state != GST_PLAY_PLAYING) return;

	gst_element_set_state (GST_ELEMENT (priv->pipeline), GST_STATE_PAUSED);

	play->state = GST_PLAY_PAUSED;
	g_idle_remove_by_data (priv->pipeline);

	gtk_signal_emit (GTK_OBJECT (play), gst_play_signals[SIGNAL_STATE_CHANGED],
			 play->state);
}

void
gst_play_stop (GstPlay *play)
{
	GstPlayPrivate *priv;

	g_return_if_fail (play != NULL);
	g_return_if_fail (GST_IS_PLAY (play));

	priv = (GstPlayPrivate *)play->priv;

	if (play->state == GST_PLAY_STOPPED) return;

	// FIXME until state changes are handled properly
	gst_element_set_state (GST_ELEMENT (priv->pipeline), GST_STATE_READY);
	gtk_object_set (GTK_OBJECT (priv->src), "offset", 0, NULL);
	//gst_element_set_state (GST_ELEMENT (priv->pipeline),GST_STATE_NULL);

	play->state = GST_PLAY_STOPPED;
	g_idle_remove_by_data (priv->pipeline);

	gtk_signal_emit (GTK_OBJECT (play), gst_play_signals[SIGNAL_STATE_CHANGED],
			 play->state);
}

GtkWidget *
gst_play_get_video_widget (GstPlay *play)
{
	GstPlayPrivate *priv;

	g_return_val_if_fail (play != NULL, 0);
	g_return_val_if_fail (GST_IS_PLAY (play), 0);

	priv = (GstPlayPrivate *)play->priv;

	return priv->video_widget;
}

gint
gst_play_get_source_width (GstPlay *play)
{
	GstPlayPrivate *priv;

	g_return_val_if_fail (play != NULL, 0);
	g_return_val_if_fail (GST_IS_PLAY (play), 0);

	priv = (GstPlayPrivate *)play->priv;

	return priv->source_width;
}

gint
gst_play_get_source_height (GstPlay *play)
{
	GstPlayPrivate *priv;

	g_return_val_if_fail (play != NULL, 0);
	g_return_val_if_fail (GST_IS_PLAY (play), 0);

	priv = (GstPlayPrivate *)play->priv;

	return priv->source_height;
}

gulong
gst_play_get_media_size (GstPlay *play)
{
	GstPlayPrivate *priv;

	g_return_val_if_fail (play != NULL, 0);
	g_return_val_if_fail (GST_IS_PLAY (play), 0);

	priv = (GstPlayPrivate *)play->priv;

	return gst_util_get_long_arg (G_OBJECT (priv->src), "filesize");
}

gulong
gst_play_get_media_offset (GstPlay *play)
{
	GstPlayPrivate *priv;

	g_return_val_if_fail (play != NULL, 0);
	g_return_val_if_fail (GST_IS_PLAY (play), 0);

	priv = (GstPlayPrivate *)play->priv;

	if (priv->offset_element)
		return gst_util_get_long_arg (G_OBJECT (priv->offset_element), "offset");
	else
		return 0;
}

gulong
gst_play_get_media_total_time (GstPlay *play)
{
	gulong total_time, bit_rate;
	GstPlayPrivate *priv;

	g_return_val_if_fail (play != NULL, 0);
	g_return_val_if_fail (GST_IS_PLAY (play), 0);

	priv = (GstPlayPrivate *)play->priv;

	if (priv->media_time_element) {
		return gst_util_get_long_arg (G_OBJECT (priv->media_time_element), "media_time");
	}

	if (priv->bit_rate_element == NULL) return 0;

	bit_rate = gst_util_get_long_arg (G_OBJECT (priv->bit_rate_element), "bit_rate");

	if (bit_rate)
		total_time = (gst_play_get_media_size (play) * 8) / bit_rate;
	else
		total_time = 0;

	return total_time;
}

gulong
gst_play_get_media_current_time (GstPlay *play)
{
	gulong current_time, bit_rate;
	GstPlayPrivate *priv;

	g_return_val_if_fail (play != NULL, 0);
	g_return_val_if_fail (GST_IS_PLAY (play), 0);

	priv = (GstPlayPrivate *)play->priv;

	if (priv->current_time_element) {
		return gst_util_get_long_arg (G_OBJECT (priv->current_time_element), "current_time");
	}

	if (priv->bit_rate_element == NULL) return 0;

	bit_rate = gst_util_get_long_arg (G_OBJECT (priv->bit_rate_element), "bit_rate");

	if (bit_rate)
		current_time = (gst_play_get_media_offset (play) * 8) / bit_rate;
	else
		current_time = 0;

	return current_time;
}

gboolean
gst_play_media_can_seek (GstPlay *play)
{
	GstPlayPrivate *priv;

	g_return_val_if_fail (play != NULL, FALSE);
	g_return_val_if_fail (GST_IS_PLAY (play), FALSE);

	priv = (GstPlayPrivate *)play->priv;

	return priv->can_seek;
}

void
gst_play_media_seek (GstPlay *play, gulong offset)
{
	GstPlayPrivate *priv;

	g_return_if_fail (play != NULL);
	g_return_if_fail (GST_IS_PLAY (play));

	priv = (GstPlayPrivate *)play->priv;

	gtk_object_set (GTK_OBJECT (priv->src), "offset", offset, NULL);
}

GstElement*
gst_play_get_pipeline (GstPlay *play)
{
	GstPlayPrivate *priv;

	g_return_val_if_fail (play != NULL, NULL);
	g_return_val_if_fail (GST_IS_PLAY (play), NULL);

	priv = (GstPlayPrivate *)play->priv;

	return GST_ELEMENT (priv->pipeline);
}

static void
gst_play_set_arg (GtkObject *object, GtkArg *arg, guint id)
{
	GstPlay *play;

	g_return_if_fail (object != NULL);
	g_return_if_fail (arg != NULL);

	play = GST_PLAY (object);

	switch (id) {
	case ARG_MUTE:
		break;
	default:
		g_warning ("GstPlay: unknown arg!");
		break;
	}
}

static void
gst_play_get_arg (GtkObject *object, GtkArg *arg, guint id)
{
	GstPlay *play;
	GstPlayPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (arg != NULL);

	play = GST_PLAY (object);
	priv = (GstPlayPrivate *)play->priv;

	switch (id) {
	case ARG_URI:
		GTK_VALUE_STRING (*arg) = priv->uri;
		break;
	case ARG_MUTE:
		GTK_VALUE_BOOL (*arg) = priv->muted;
		break;
	case ARG_STATE:
		GTK_VALUE_INT (*arg) = play->state;
		break;
	case ARG_MEDIA_SIZE:
		GTK_VALUE_LONG (*arg) = gst_play_get_media_size(play);
		break;
	case ARG_MEDIA_OFFSET:
		GTK_VALUE_LONG (*arg) = gst_play_get_media_offset(play);
		break;
	case ARG_MEDIA_TOTAL_TIME:
		break;
	case ARG_MEDIA_CURRENT_TIME:
		break;
	default:
		arg->type = GTK_TYPE_INVALID;
		break;
	}
}
