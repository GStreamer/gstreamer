/*
 *  GStreamer Video sink.
 *
 *  Copyright (C) <2003> Julien Moutte <julien@moutte.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideosink.h"

/* VideoSink signals and args */

enum {
  HAVE_VIDEO_OUT,
  HAVE_SIZE,
  FRAME_DISPLAYED,
  LAST_SIGNAL
};


enum {
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_FRAMES_DISPLAYED,
  ARG_FRAME_TIME,
};

static GstElementClass *parent_class = NULL;
static guint gst_videosink_signals[LAST_SIGNAL] = { 0 };

/* Private methods */

static void
gst_videosink_set_property (GObject *object, guint prop_id,
                            const GValue *value, GParamSpec *pspec)
{
  GstVideoSink *videosink;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_VIDEOSINK (object));
  
  videosink = GST_VIDEOSINK (object);
  
  switch (prop_id)
    {
      case ARG_WIDTH:
        gst_video_sink_set_geometry (videosink, g_value_get_int (value),
                                     videosink->height);
        break;
      case ARG_HEIGHT:
        gst_video_sink_set_geometry (videosink, videosink->width,
                                     g_value_get_int (value));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gst_videosink_get_property (GObject *object, guint prop_id,
                            GValue *value, GParamSpec *pspec)
{
  GstVideoSink *videosink;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_VIDEOSINK (object));
  
  videosink = GST_VIDEOSINK (object);
  
  switch (prop_id)
    {
      case ARG_WIDTH:
        g_value_set_int (value, videosink->width);
        break;
      case ARG_HEIGHT:
        g_value_set_int (value, videosink->height);
        break;
      case ARG_FRAMES_DISPLAYED:
        g_value_set_int (value, videosink->frames_displayed);
        break;
      case ARG_FRAME_TIME:
        g_value_set_int64 (value, videosink->frame_time);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gst_videosink_set_clock (GstElement *element, GstClock *clock)
{
  GstVideoSink *videosink;

  videosink = GST_VIDEOSINK (element);
  
  videosink->clock = clock;
}

/* Initing stuff */

static void
gst_videosink_init (GstVideoSink *videosink)
{
  videosink->video_out = NULL;
  videosink->width = -1;
  videosink->height = -1;
  videosink->frames_displayed = 0;
  videosink->frame_time = 0;

  videosink->clock = NULL;
  videosink->formats = NULL;
}

static void
gst_videosink_class_init (GstVideoSinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WIDTH,
    g_param_spec_int ("width", "Width", "Width of the video output",
                      G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));
  
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HEIGHT,
    g_param_spec_int ("height", "Height", "Height of the video output",
                      G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));
  
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAMES_DISPLAYED,
    g_param_spec_int ("frames_displayed", "Frames displayed",
                      "The number of frames displayed so far",
                      G_MININT,G_MAXINT, 0, G_PARAM_READABLE));
  
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAME_TIME,
    g_param_spec_int ("frame_time", "Frame time", "The interval between frames",
                      G_MININT, G_MAXINT, 0, G_PARAM_READABLE));

  gobject_class->set_property = gst_videosink_set_property;
  gobject_class->get_property = gst_videosink_get_property;

  gst_videosink_signals[FRAME_DISPLAYED] =
    g_signal_new ("frame_displayed",
                  G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstVideoSinkClass, frame_displayed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
                  
  gst_videosink_signals[HAVE_SIZE] =
    g_signal_new ("have_size",
                  G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstVideoSinkClass, have_size),
                  NULL, NULL,
                  gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2,
		  G_TYPE_UINT, G_TYPE_UINT);

  gst_videosink_signals[HAVE_VIDEO_OUT] =
    g_signal_new ("have_video_out",
                  G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstVideoSinkClass, have_video_out),
                  NULL, NULL,
                  gst_marshal_VOID__POINTER, G_TYPE_NONE, 1,
		  G_TYPE_POINTER);

  gstelement_class->set_clock = gst_videosink_set_clock;
}

/* Public virtual methods */

/**
 * gst_video_sink_set_video_out:
 * @videosink: a #GstVideoSink to set the video out on.
 * @video_out: the #gpointer linking to video out.
 *
 * This will call the video sink's set_video_out method. You should use this
 * method to tell to a video sink to display video output to a specific
 * video out ressource.
 */
void
gst_video_sink_set_video_out (GstVideoSink *videosink, gpointer video_out)
{
  GstVideoSinkClass *class;
  
  g_return_if_fail (videosink != NULL);
  g_return_if_fail (GST_IS_VIDEOSINK (videosink));
  
  class = GST_VIDEOSINK_GET_CLASS (videosink);
  
  if (class->set_video_out)
    class->set_video_out (videosink, video_out);
}

/**
 * gst_video_sink_push_ui_event:
 * @videosink: a #GstVideoSink to push the event to.
 * @event: the #GstEvent to be pushed.
 *
 * This will push an event to the video sink. That event is supposed to be
 * a user interface event and will be forwarded upstream to provide
 * interactivity support.
 */
void
gst_video_sink_push_ui_event (GstVideoSink *videosink, GstEvent *event)
{
  GstVideoSinkClass *class;
  
  g_return_if_fail (videosink != NULL);
  g_return_if_fail (GST_IS_VIDEOSINK (videosink));
  
  class = GST_VIDEOSINK_GET_CLASS (videosink);
  
  if (class->push_ui_event)
    class->push_ui_event (videosink, event);
}

/**
 * gst_video_sink_set_geometry:
 * @videosink: a #GstVideoSink which geometry will be set.
 * @width: a width as a #gint.
 * @height: a height as a #gint.
 *
 * Set video sink's geometry to @width x @height. If that succeed you should
 * get the have_size signal being fired.
 */
void
gst_video_sink_set_geometry (GstVideoSink *videosink, gint width, gint height)
{
  GstVideoSinkClass *class;
  
  g_return_if_fail (videosink != NULL);
  g_return_if_fail (GST_IS_VIDEOSINK (videosink));
  
  class = GST_VIDEOSINK_GET_CLASS (videosink);
  
  if (class->set_geometry)
    class->set_geometry (videosink, width, height);
}

/* Public methods */

/**
 * gst_video_sink_got_video_out:
 * @videosink: a #GstVideoSink which got a video out ressource.
 * @video_out: a #gpointer linking to the video out ressource.
 *
 * This will fire an have_video_out signal and update the internal object's
 * #gpointer.
 *
 * This function should be used by video sink developpers.
 */
void
gst_video_sink_got_video_out (GstVideoSink *videosink, gpointer video_out)
{
  g_return_if_fail (videosink != NULL);
  g_return_if_fail (GST_IS_VIDEOSINK (videosink));
  
  videosink->video_out = video_out;
  
  g_signal_emit (G_OBJECT (videosink), gst_videosink_signals[HAVE_VIDEO_OUT],
                 0, video_out);
}

/**
 * gst_video_sink_got_video_size:
 * @videosink: a #GstVideoSink which received video geometry.
 * @width: a width as a #gint.
 * @height: a height as a #gint.
 *
 * This will fire an have_size signal and update the internal object's
 * geometry.
 *
 * This function should be used by video sink developpers.
 */
void
gst_video_sink_got_video_size (GstVideoSink *videosink, gint width, gint height)
{
  g_return_if_fail (videosink != NULL);
  g_return_if_fail (GST_IS_VIDEOSINK (videosink));
  
  videosink->width = width;
  videosink->height = height;
  
  g_signal_emit (G_OBJECT (videosink), gst_videosink_signals[HAVE_SIZE],
                 0, width, height);
}

/**
 * gst_video_sink_frame_displayed:
 * @videosink: a #GstVideoSink which displayed a frame.
 *
 * This will fire an frame_displayed signal and update the internal object's
 * counter.
 *
 * This function should be used by video sink developpers.
 */
void
gst_video_sink_frame_displayed (GstVideoSink *videosink)
{
  g_return_if_fail (videosink != NULL);
  g_return_if_fail (GST_IS_VIDEOSINK (videosink));
  
  videosink->frames_displayed++;
  
  g_signal_emit (G_OBJECT (videosink),
                 gst_videosink_signals[FRAME_DISPLAYED], 0);
}

/**
 * gst_video_sink_get_geometry:
 * @videosink: a #GstVideoSink which displayed a frame.
 * @width: a #gint pointer where the width will be set.
 * @height: a #gint pointer where the height will be set.
 *
 * This will fill set @width and @height with the video sink's current geometry.
 */
void
gst_video_sink_get_geometry (GstVideoSink *videosink, gint *width, gint *height)
{
  g_return_if_fail (videosink != NULL);
  g_return_if_fail (GST_IS_VIDEOSINK (videosink));
  *width = videosink->width;
  *height = videosink->height;
}

GType
gst_videosink_get_type (void)
{
  static GType videosink_type = 0;

  if (!videosink_type)
    {
      static const GTypeInfo videosink_info = {
        sizeof (GstVideoSinkClass),
        NULL,
        NULL,
        (GClassInitFunc) gst_videosink_class_init,
        NULL,
        NULL,
        sizeof (GstVideoSink),
        0,
        (GInstanceInitFunc) gst_videosink_init,
      };
    
      videosink_type = g_type_register_static (GST_TYPE_ELEMENT,
                                               "GstVideoSink",
                                               &videosink_info, 0);
    }
    
  return videosink_type;
}
