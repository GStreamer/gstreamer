/* GStreamer
 * Copyright (C) 1999,2000,2001,2002 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000,2001,2002 Wim Taymans <wtay@chello.be>
 *                              2002 Steve Baker <steve@stevebaker.org>
 *                              2003 Julien Moutte <julien@moutte.net>
 *
 * play.h: GstPlay object code
 *
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

#ifndef __GSTPLAY_H__
#define __GSTPLAY_H__

#include <gst/gst.h>
#include <gst/control/control.h>

/*
 * GstPlay is a simple class for audio and video playback.
 * It's job is to get the media (supplied by a URI) played.  
 * More specific it should get the media from source to the output elements.
 * How that is done should not be relevant for developers using this class. 
 * A user using this class should not have to know very much about how
 * GStreamer works, other than that it plays back media.
 * Additionally it supplies signals to get information about the current
 * playing state.
 */

typedef enum
{
  GST_PLAY_OK,
  GST_PLAY_UNKNOWN_MEDIA,
  GST_PLAY_CANNOT_PLAY,
  GST_PLAY_ERROR,
} GstPlayReturn;

typedef enum
{
  GST_PLAY_PIPE_AUDIO,
  GST_PLAY_PIPE_AUDIO_THREADED,
  GST_PLAY_PIPE_AUDIO_HYPER_THREADED,
  GST_PLAY_PIPE_VIDEO,
  GST_PLAY_PIPE_VIDEO_VISUALISATION,
} GstPlayPipeType;

typedef enum
{
  GST_PLAY_ERROR_FAKESINK,
  GST_PLAY_ERROR_THREAD,
  GST_PLAY_ERROR_QUEUE,
  GST_PLAY_ERROR_GNOMEVFSSRC,
  GST_PLAY_ERROR_VOLUME,
  GST_PLAY_ERROR_COLORSPACE,
  GST_PLAY_ERROR_LAST,
} GstPlayError;

typedef enum
{
  GST_PLAY_SINK_TYPE_AUDIO,
  GST_PLAY_SINK_TYPE_VIDEO,
  GST_PLAY_SINK_TYPE_ANY,
} GstPlaySinkType;

#define GST_PLAY_ERROR 		gst_play_error_quark ()

#define GST_TYPE_PLAY            (gst_play_get_type())
#define GST_PLAY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PLAY, GstPlay))
#define GST_PLAY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PLAY, GstPlayClass))
#define GST_IS_PLAY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PLAY))
#define GST_IS_PLAY_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PLAY))
#define GST_PLAY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_PLAY, GstPlayClass))

typedef struct _GstPlay GstPlay;
typedef struct _GstPlayClass GstPlayClass;
typedef struct _GstPlayIdleData GstPlayIdleData;

typedef guint (*GstPlayTimeoutAdd) (guint interval,
				    GSourceFunc function, gpointer data);
typedef guint (*GstPlayIdleAdd) (GSourceFunc function, gpointer data);

struct _GstPlay
{
  GObject parent;

    gboolean (*setup_pipeline) (GstPlay * play, GError ** error);
  void (*teardown_pipeline) (GstPlay * play);
    gboolean (*set_data_src) (GstPlay * play, GstElement * datasrc);
    gboolean (*set_autoplugger) (GstPlay * play, GstElement * autoplugger);
    gboolean (*set_video_sink) (GstPlay * play, GstElement * videosink);
    gboolean (*set_audio_sink) (GstPlay * play, GstElement * audiosink);

  /* core elements */
  GstElement *pipeline;
  GstElement *volume;
  GstElement *source;
  GstElement *autoplugger;
  GstElement *video_sink;
  GstElement *video_sink_element;
  GstElement *audio_sink;
  GstElement *audio_sink_element;
  GstElement *visualization_sink_element;

  GstDParamManager *vol_dpman;
  GstDParam *vol_dparam;
  GHashTable *other_elements;

  GstClock *clock;

  gboolean need_stream_length;
  gboolean need_seek;
  gint time_seconds;
  gint get_length_attempt;
  gint64 seek_time;
  gint64 time_nanos;
  gint64 length_nanos;

  GAsyncQueue *signal_queue;

  GstPlayTimeoutAdd timeout_add_func;
  GstPlayIdleAdd idle_add_func;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstPlayClass
{
  GObjectClass parent_class;

  /* signals */
  void (*information) (GstPlay * play,
		       GstObject * element, GParamSpec * param);
  void (*pipeline_error) (GstPlay * play, GstElement * element, char *error);
  void (*state_changed) (GstPlay * play,
			 GstElementState old_state,
			 GstElementState new_state);
  void (*stream_end) (GstPlay * play);
  void (*time_tick) (GstPlay * play, gint64 time_nanos);
  void (*stream_length) (GstPlay * play, gint64 length_nanos);
  void (*have_video_out) (GstPlay * play, gpointer video_out);
  void (*have_vis_video_out) (GstPlay * play, gpointer video_out);
  void (*have_video_size) (GstPlay * play, gint width, gint height);
  void (*have_vis_size) (GstPlay * play, gint width, gint height);

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstPlayIdleData
{
  GSourceFunc func;
  gpointer data;
};


void gst_play_seek_to_time (GstPlay * play, gint64 time_nanos);

void gst_play_need_new_video_window (GstPlay * play);

void
gst_play_set_idle_timeout_funcs (GstPlay * play,
				 GstPlayTimeoutAdd timeout_add_func,
				 GstPlayIdleAdd idle_add_func);
GstElement *gst_play_get_sink_element (GstPlay * play,
				       GstElement * element,
				       GstPlaySinkType sink_type);

/* Set/Get state */

GstElementStateReturn
gst_play_set_state (GstPlay * play, GstElementState state);
GstElementState gst_play_get_state (GstPlay * play);

/* Set/Get location */

gboolean gst_play_set_location (GstPlay * play, const gchar * location);
gchar *gst_play_get_location (GstPlay * play);

/* Set/Get volume */

void gst_play_set_volume (GstPlay * play, gfloat volume);
gfloat gst_play_get_volume (GstPlay * play);

/* Set/Get mute */

void gst_play_set_mute (GstPlay * play, gboolean mute);
gboolean gst_play_get_mute (GstPlay * play);

/* Set sinks and data src */

gboolean gst_play_set_data_src (GstPlay * play, GstElement * data_src);
gboolean gst_play_set_video_sink (GstPlay * play, GstElement * video_sink);
gboolean
gst_play_set_visualization_video_sink (GstPlay * play,
				       GstElement * video_sink);
gboolean gst_play_set_audio_sink (GstPlay * play, GstElement * audio_sink);

gboolean
gst_play_set_visualization_element (GstPlay * play, GstElement * element);

gboolean gst_play_connect_visualization (GstPlay * play, gboolean connect);

GType gst_play_get_type (void);

GstPlay *gst_play_new (GstPlayPipeType pipe_type, GError ** error);

#endif /* __GSTPLAY_H__ */
