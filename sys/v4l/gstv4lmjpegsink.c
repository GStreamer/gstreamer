/* G-Streamer hardware MJPEG video sink plugin
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <string.h>
#include "v4lmjpegsink_calls.h"

static GstElementDetails gst_v4lmjpegsink_details = {
  "Video (video4linux/MJPEG) sink",
  "Sink/Video",
  "Writes MJPEG-encoded frames to a zoran MJPEG/video4linux device",
  VERSION,
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  "(C) 2001",
};

/* v4lmjpegsink signals and args */
enum {
  SIGNAL_FRAME_DISPLAYED,
  SIGNAL_HAVE_SIZE,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_NUMBUFS,
  ARG_BUFSIZE,
  ARG_X_OFFSET,
  ARG_Y_OFFSET,
  ARG_FRAMES_DISPLAYED,
  ARG_FRAME_TIME,
};


/* init functions */
static void                  gst_v4lmjpegsink_class_init   (GstV4lMjpegSinkClass *klass);
static void                  gst_v4lmjpegsink_init         (GstV4lMjpegSink      *v4lmjpegsink);

/* the chain of buffers */
static GstPadConnectReturn   gst_v4lmjpegsink_sinkconnect  (GstPad               *pad,
                                                            GstCaps              *vscapslist);
static void                  gst_v4lmjpegsink_chain        (GstPad               *pad,
                                                            GstBuffer            *buf);

/* get/set gst object functions */
static void                  gst_v4lmjpegsink_set_property (GObject              *object,
                                                            guint                prop_id,
                                                            const GValue         *value,
                                                            GParamSpec           *pspec);
static void                  gst_v4lmjpegsink_get_property (GObject              *object,
                                                            guint                prop_id,
                                                            GValue               *value,
                                                            GParamSpec           *pspec);
static void                  gst_v4lmjpegsink_close        (GstV4lMjpegSink      *v4lmjpegsink);
static GstElementStateReturn gst_v4lmjpegsink_change_state (GstElement           *element);
static void		     gst_v4lmjpegsink_set_clock    (GstElement *element, GstClock *clock);


static GstCaps *capslist = NULL;
static GstPadTemplate *sink_template;

static GstElementClass *parent_class = NULL;
static guint gst_v4lmjpegsink_signals[LAST_SIGNAL] = { 0 };


GType
gst_v4lmjpegsink_get_type (void)
{
  static GType v4lmjpegsink_type = 0;

  if (!v4lmjpegsink_type) {
    static const GTypeInfo v4lmjpegsink_info = {
      sizeof(GstV4lMjpegSinkClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_v4lmjpegsink_class_init,
      NULL,
      NULL,
      sizeof(GstV4lMjpegSink),
      0,
      (GInstanceInitFunc)gst_v4lmjpegsink_init,
    };
    v4lmjpegsink_type = g_type_register_static(GST_TYPE_V4LELEMENT, "GstV4lMjpegSink", &v4lmjpegsink_info, 0);
  }
  return v4lmjpegsink_type;
}


static void
gst_v4lmjpegsink_class_init (GstV4lMjpegSinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_V4LELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NUMBUFS,
    g_param_spec_int("num_buffers","num_buffers","num_buffers",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFSIZE,
    g_param_spec_int("buffer_size","buffer_size","buffer_size",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_X_OFFSET,
    g_param_spec_int("x_offset","x_offset","x_offset",
    G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_Y_OFFSET,
    g_param_spec_int("y_offset","y_offset","y_offset",
    G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FRAMES_DISPLAYED,
    g_param_spec_int("frames_displayed","frames_displayed","frames_displayed",
    G_MININT,G_MAXINT,0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FRAME_TIME,
    g_param_spec_int("frame_time","frame_time","frame_time",
    G_MININT,G_MAXINT,0,G_PARAM_READABLE));

  gobject_class->set_property = gst_v4lmjpegsink_set_property;
  gobject_class->get_property = gst_v4lmjpegsink_get_property;

  gst_v4lmjpegsink_signals[SIGNAL_FRAME_DISPLAYED] =
    g_signal_new ("frame_displayed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstV4lMjpegSinkClass, frame_displayed), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_v4lmjpegsink_signals[SIGNAL_HAVE_SIZE] =
    g_signal_new ("have_size", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstV4lMjpegSinkClass, have_size), NULL, NULL,
                   gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2,
		   G_TYPE_UINT, G_TYPE_UINT);


  gstelement_class->change_state = gst_v4lmjpegsink_change_state;
}


static void
gst_v4lmjpegsink_init (GstV4lMjpegSink *v4lmjpegsink)
{
  v4lmjpegsink->sinkpad = gst_pad_new_from_template (sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (v4lmjpegsink), v4lmjpegsink->sinkpad);

  gst_pad_set_chain_function (v4lmjpegsink->sinkpad, gst_v4lmjpegsink_chain);
  gst_pad_set_connect_function (v4lmjpegsink->sinkpad, gst_v4lmjpegsink_sinkconnect);

  v4lmjpegsink->clock = NULL;
  GST_ELEMENT (v4lmjpegsink)->setclockfunc    = gst_v4lmjpegsink_set_clock;

  v4lmjpegsink->width = -1;
  v4lmjpegsink->height = -1;

  v4lmjpegsink->x_offset = -1;
  v4lmjpegsink->y_offset = -1;

  v4lmjpegsink->numbufs = 64;
  v4lmjpegsink->bufsize = 256;

  GST_FLAG_SET(v4lmjpegsink, GST_ELEMENT_THREAD_SUGGESTED);
}


static GstPadConnectReturn
gst_v4lmjpegsink_sinkconnect (GstPad  *pad,
                              GstCaps *vscapslist)
{
  GstV4lMjpegSink *v4lmjpegsink;
  GstCaps *caps;

  v4lmjpegsink = GST_V4LMJPEGSINK (gst_pad_get_parent (pad));

  /* we are not going to act on variable caps */
  if (!GST_CAPS_IS_FIXED (vscapslist) || !GST_V4L_IS_OPEN(GST_V4LELEMENT(v4lmjpegsink)))
    return GST_PAD_CONNECT_DELAYED;

  for (caps = vscapslist; caps != NULL; caps = vscapslist = vscapslist->next)
  {
    v4lmjpegsink->width = gst_caps_get_int (caps, "width");
    v4lmjpegsink->height = gst_caps_get_int (caps, "height");

    if (!gst_v4lmjpegsink_set_playback(v4lmjpegsink,
         v4lmjpegsink->width, v4lmjpegsink->height,
         v4lmjpegsink->x_offset, v4lmjpegsink->y_offset,
         GST_V4LELEMENT(v4lmjpegsink)->norm, 0)) /* TODO: interlacing */
      continue;

    g_signal_emit (G_OBJECT (v4lmjpegsink), gst_v4lmjpegsink_signals[SIGNAL_HAVE_SIZE], 0,
      v4lmjpegsink->width, v4lmjpegsink->height);

    return GST_PAD_CONNECT_OK;
  }

  /* if we got here - it's not good */
  return GST_PAD_CONNECT_REFUSED;
}


static void
gst_v4lmjpegsink_set_clock (GstElement *element, GstClock *clock)
{
  GstV4lMjpegSink *v4mjpegsink = GST_V4LMJPEGSINK (element);

  v4mjpegsink->clock = clock;
}


static void
gst_v4lmjpegsink_chain (GstPad    *pad,
                        GstBuffer *buf)
{
  GstV4lMjpegSink *v4lmjpegsink;
  GstClockTimeDiff jitter;
  gint num;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  v4lmjpegsink = GST_V4LMJPEGSINK (gst_pad_get_parent (pad));

  if (v4lmjpegsink->clock) {
    GST_DEBUG (0,"videosink: clock wait: %llu\n", GST_BUFFER_TIMESTAMP(buf));

    jitter = gst_clock_current_diff(v4lmjpegsink->clock, GST_BUFFER_TIMESTAMP (buf));

    if (jitter > 500000 || jitter < -500000)
      GST_DEBUG (0, "jitter: %lld\n", jitter);

    gst_element_clock_wait(GST_ELEMENT(v4lmjpegsink), v4lmjpegsink->clock, GST_BUFFER_TIMESTAMP(buf));
  }
  
  /* check size */
  if (GST_BUFFER_SIZE(buf) > v4lmjpegsink->breq.size)
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsink),
      "Buffer too big (%d KB), max. buffersize is %d KB",
      GST_BUFFER_SIZE(buf)/1024, v4lmjpegsink->breq.size/1024);
    return;
  }

  /* put JPEG data to the device */
  gst_v4lmjpegsink_wait_frame(v4lmjpegsink, &num);
  memcpy(gst_v4lmjpegsink_get_buffer(v4lmjpegsink, num),
    GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
  gst_v4lmjpegsink_play_frame(v4lmjpegsink, num);

  g_signal_emit(G_OBJECT(v4lmjpegsink),gst_v4lmjpegsink_signals[SIGNAL_FRAME_DISPLAYED],0);

  gst_buffer_unref(buf);
}


static void
gst_v4lmjpegsink_set_property (GObject      *object,
                               guint        prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  GstV4lMjpegSink *v4lmjpegsink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_V4LMJPEGSINK (object));

  v4lmjpegsink = GST_V4LMJPEGSINK (object);

  switch (prop_id)
  {
    case ARG_NUMBUFS:
      v4lmjpegsink->numbufs = g_value_get_int(value);
      break;
    case ARG_BUFSIZE:
      v4lmjpegsink->bufsize = g_value_get_int(value);
      break;
    case ARG_X_OFFSET:
      v4lmjpegsink->x_offset = g_value_get_int(value);
      break;
    case ARG_Y_OFFSET:
      v4lmjpegsink->y_offset = g_value_get_int(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_v4lmjpegsink_get_property (GObject    *object,
                               guint      prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  GstV4lMjpegSink *v4lmjpegsink;

  /* it's not null if we got it, but it might not be ours */
  v4lmjpegsink = GST_V4LMJPEGSINK(object);

  switch (prop_id) {
    case ARG_FRAMES_DISPLAYED:
      g_value_set_int (value, v4lmjpegsink->frames_displayed);
      break;
    case ARG_FRAME_TIME:
      g_value_set_int (value, v4lmjpegsink->frame_time/1000000);
      break;
    case ARG_NUMBUFS:
      g_value_set_int (value, v4lmjpegsink->numbufs);
      break;
    case ARG_BUFSIZE:
      g_value_set_int (value, v4lmjpegsink->bufsize);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstElementStateReturn
gst_v4lmjpegsink_change_state (GstElement *element)
{
  GstV4lMjpegSink *v4lmjpegsink;
  GstElementStateReturn parent_value;

  g_return_val_if_fail (GST_IS_V4LMJPEGSINK (element), GST_STATE_FAILURE);
  v4lmjpegsink = GST_V4LMJPEGSINK(element);

  /* set up change state */
  switch (GST_STATE_TRANSITION(element)) {
    case GST_STATE_READY_TO_PAUSED:
      /* set buffer info */
      if (!gst_v4lmjpegsink_set_buffer(v4lmjpegsink,
           v4lmjpegsink->numbufs, v4lmjpegsink->bufsize))
        return GST_STATE_FAILURE;
      if (!gst_v4lmjpegsink_playback_init(v4lmjpegsink))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* start */
      if (!gst_v4lmjpegsink_playback_start(v4lmjpegsink))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      /* de-queue all queued buffers */
      if (!gst_v4lmjpegsink_playback_stop(v4lmjpegsink))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* stop playback, unmap all buffers */
      if (!gst_v4lmjpegsink_playback_deinit(v4lmjpegsink))
        return GST_STATE_FAILURE;
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    parent_value = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  if (GST_STATE_TRANSITION(element) == GST_STATE_NULL_TO_READY)
  {
    if ((GST_V4LELEMENT(v4lmjpegsink)->norm >= VIDEO_MODE_PAL ||
         GST_V4LELEMENT(v4lmjpegsink)->norm < VIDEO_MODE_AUTO) ||
        GST_V4LELEMENT(v4lmjpegsink)->channel < 0)
      if (!gst_v4l_set_chan_norm(GST_V4LELEMENT(v4lmjpegsink),
           0, GST_V4LELEMENT(v4lmjpegsink)->norm))
        return GST_STATE_FAILURE;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return parent_value;

  return GST_STATE_SUCCESS;
}


static gboolean
plugin_init (GModule   *module,
             GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstCaps *caps;

  /* create an elementfactory for the v4lmjpegsink element */
  factory = gst_elementfactory_new("v4lmjpegsink",GST_TYPE_V4LMJPEGSINK,
                                   &gst_v4lmjpegsink_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  caps = gst_caps_new ("v4lmjpegsink_caps",
                       "video/jpeg",
                       gst_props_new (
                          "width",  GST_PROPS_INT_RANGE (0, G_MAXINT),
                          "height", GST_PROPS_INT_RANGE (0, G_MAXINT),
                          NULL       )
                      );
  capslist = gst_caps_append(capslist, caps);

  sink_template = gst_padtemplate_new (
		  "sink",
                  GST_PAD_SINK,
  		  GST_PAD_ALWAYS,
		  capslist, NULL);

  gst_elementfactory_add_padtemplate (factory, sink_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "v4lmjpegsink",
  plugin_init
};
