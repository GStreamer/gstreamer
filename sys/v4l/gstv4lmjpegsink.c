/* GStreamer
 *
 * gstv4lmjpegsink.c: hardware MJPEG video sink plugin
 *
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "v4lmjpegsink_calls.h"

GST_DEBUG_CATEGORY (v4lmjpegsink_debug);
#define GST_CAT_DEFAULT v4lmjpegsink_debug

/* elementfactory information */
static GstElementDetails gst_v4lmjpegsink_details = {
  "Video (video4linux/MJPEG) sink",
  "Sink/Video",
  "Writes MJPEG-encoded frames to a zoran MJPEG/video4linux device",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>"
};

/* v4lmjpegsink signals and args */
enum
{
  SIGNAL_FRAME_DISPLAYED,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_NUMBUFS,
  ARG_BUFSIZE,
  ARG_X_OFFSET,
  ARG_Y_OFFSET,
  ARG_FRAMES_DISPLAYED,
  ARG_FRAME_TIME
};


/* init functions */
static void gst_v4lmjpegsink_base_init (gpointer g_class);
static void gst_v4lmjpegsink_class_init (GstV4lMjpegSinkClass * klass);
static void gst_v4lmjpegsink_init (GstV4lMjpegSink * v4lmjpegsink);

/* the chain of buffers */
static GstPadLinkReturn gst_v4lmjpegsink_sinkconnect (GstPad * pad,
    const GstCaps * vscapslist);
static void gst_v4lmjpegsink_chain (GstPad * pad, GstData * _data);

/* get/set gst object functions */
static void gst_v4lmjpegsink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_v4lmjpegsink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_v4lmjpegsink_change_state (GstElement *
    element);
static void gst_v4lmjpegsink_set_clock (GstElement * element, GstClock * clock);


static GstElementClass *parent_class = NULL;
static guint gst_v4lmjpegsink_signals[LAST_SIGNAL] = { 0 };


GType
gst_v4lmjpegsink_get_type (void)
{
  static GType v4lmjpegsink_type = 0;

  if (!v4lmjpegsink_type) {
    static const GTypeInfo v4lmjpegsink_info = {
      sizeof (GstV4lMjpegSinkClass),
      gst_v4lmjpegsink_base_init,
      NULL,
      (GClassInitFunc) gst_v4lmjpegsink_class_init,
      NULL,
      NULL,
      sizeof (GstV4lMjpegSink),
      0,
      (GInstanceInitFunc) gst_v4lmjpegsink_init,
    };

    v4lmjpegsink_type =
        g_type_register_static (GST_TYPE_V4LELEMENT, "GstV4lMjpegSink",
        &v4lmjpegsink_info, 0);
  }
  return v4lmjpegsink_type;
}

static void
gst_v4lmjpegsink_base_init (gpointer g_class)
{
  static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-jpeg, "
          "width = (int) [ 1, MAX ], "
          "height = (int) [ 1, MAX ], " "framerate = (double) [ 0, MAX ]")
      );
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_v4lmjpegsink_details);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));
}
static void
gst_v4lmjpegsink_class_init (GstV4lMjpegSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_V4LELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NUMBUFS,
      g_param_spec_int ("num-buffers", "num-buffers", "num-buffers",
          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFSIZE,
      g_param_spec_int ("buffer-size", "buffer-size", "buffer-size",
          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_X_OFFSET,
      g_param_spec_int ("x-offset", "x-offset", "x-offset",
          G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_Y_OFFSET,
      g_param_spec_int ("y-offset", "y-offset", "y-offset",
          G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAMES_DISPLAYED,
      g_param_spec_int ("frames-displayed", "frames-displayed",
          "frames-displayed", G_MININT, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAME_TIME,
      g_param_spec_int ("frame-time", "frame-time", "frame-time", G_MININT,
          G_MAXINT, 0, G_PARAM_READABLE));

  GST_DEBUG_CATEGORY_INIT (v4lmjpegsink_debug, "v4lmjpegsink", 0,
      "V4L MJPEG sink element");
  gobject_class->set_property = gst_v4lmjpegsink_set_property;
  gobject_class->get_property = gst_v4lmjpegsink_get_property;

  gst_v4lmjpegsink_signals[SIGNAL_FRAME_DISPLAYED] =
      g_signal_new ("frame-displayed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstV4lMjpegSinkClass,
          frame_displayed), NULL, NULL, g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  gstelement_class->change_state = gst_v4lmjpegsink_change_state;
  gstelement_class->set_clock = gst_v4lmjpegsink_set_clock;
}


static void
gst_v4lmjpegsink_init (GstV4lMjpegSink * v4lmjpegsink)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (v4lmjpegsink);

  v4lmjpegsink->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_element_add_pad (GST_ELEMENT (v4lmjpegsink), v4lmjpegsink->sinkpad);

  gst_pad_set_chain_function (v4lmjpegsink->sinkpad, gst_v4lmjpegsink_chain);
  gst_pad_set_link_function (v4lmjpegsink->sinkpad,
      gst_v4lmjpegsink_sinkconnect);

  v4lmjpegsink->clock = NULL;

  v4lmjpegsink->width = -1;
  v4lmjpegsink->height = -1;

  v4lmjpegsink->x_offset = -1;
  v4lmjpegsink->y_offset = -1;

  v4lmjpegsink->numbufs = 64;
  v4lmjpegsink->bufsize = 256;

  GST_FLAG_SET (v4lmjpegsink, GST_ELEMENT_THREAD_SUGGESTED);
}


static GstPadLinkReturn
gst_v4lmjpegsink_sinkconnect (GstPad * pad, const GstCaps * vscapslist)
{
  GstV4lMjpegSink *v4lmjpegsink;
  GstStructure *structure;

  v4lmjpegsink = GST_V4LMJPEGSINK (gst_pad_get_parent (pad));

  /* in case the buffers are active (which means that we already
   * did capsnego before and didn't clean up), clean up anyways */
  if (GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lmjpegsink)))
    if (!gst_v4lmjpegsink_playback_deinit (v4lmjpegsink))
      return GST_PAD_LINK_REFUSED;

  structure = gst_caps_get_structure (vscapslist, 0);

  gst_structure_get_int (structure, "width", &v4lmjpegsink->width);
  gst_structure_get_int (structure, "height", &v4lmjpegsink->height);

  if (!gst_v4lmjpegsink_set_playback (v4lmjpegsink, v4lmjpegsink->width, v4lmjpegsink->height, v4lmjpegsink->x_offset, v4lmjpegsink->y_offset, GST_V4LELEMENT (v4lmjpegsink)->vchan.norm, 0))   /* TODO: interlacing */
    return GST_PAD_LINK_REFUSED;

  /* set buffer info */
  if (!gst_v4lmjpegsink_set_buffer (v4lmjpegsink,
          v4lmjpegsink->numbufs, v4lmjpegsink->bufsize))
    return GST_PAD_LINK_REFUSED;
  if (!gst_v4lmjpegsink_playback_init (v4lmjpegsink))
    return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;

}


static void
gst_v4lmjpegsink_set_clock (GstElement * element, GstClock * clock)
{
  GstV4lMjpegSink *v4mjpegsink = GST_V4LMJPEGSINK (element);

  v4mjpegsink->clock = clock;
}


static void
gst_v4lmjpegsink_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstV4lMjpegSink *v4lmjpegsink;
  gint num;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  v4lmjpegsink = GST_V4LMJPEGSINK (gst_pad_get_parent (pad));

  if (v4lmjpegsink->clock) {
    GST_DEBUG ("videosink: clock wait: %" G_GUINT64_FORMAT,
        GST_BUFFER_TIMESTAMP (buf));

    gst_element_wait (GST_ELEMENT (v4lmjpegsink), GST_BUFFER_TIMESTAMP (buf));
  }
#if 0
  if (GST_BUFFER_POOL (buf) == v4lmjpegsink->bufferpool) {
    num = GPOINTER_TO_INT (GST_BUFFER_POOL_PRIVATE (buf));
    gst_v4lmjpegsink_play_frame (v4lmjpegsink, num);
  } else {
#endif
    /* check size */
    if (GST_BUFFER_SIZE (buf) > v4lmjpegsink->breq.size) {
      GST_ELEMENT_ERROR (v4lmjpegsink, RESOURCE, WRITE, (NULL),
          ("Buffer too big (%d KB), max. buffersize is %ld KB",
              GST_BUFFER_SIZE (buf) / 1024, v4lmjpegsink->breq.size / 1024));
      return;
    }

    /* put JPEG data to the device */
    gst_v4lmjpegsink_wait_frame (v4lmjpegsink, &num);
    memcpy (gst_v4lmjpegsink_get_buffer (v4lmjpegsink, num),
        GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
    gst_v4lmjpegsink_play_frame (v4lmjpegsink, num);
#if 0
  }
#endif

  g_signal_emit (G_OBJECT (v4lmjpegsink),
      gst_v4lmjpegsink_signals[SIGNAL_FRAME_DISPLAYED], 0);

  gst_buffer_unref (buf);
}


#if 0
static GstBuffer *
gst_v4lmjpegsink_buffer_new (GstBufferPool * pool,
    guint64 offset, guint size, gpointer user_data)
{
  GstV4lMjpegSink *v4lmjpegsink = GST_V4LMJPEGSINK (user_data);
  GstBuffer *buffer = NULL;
  guint8 *data;
  gint num;

  if (!GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lmjpegsink)))
    return NULL;
  if (v4lmjpegsink->breq.size < size) {
    GST_DEBUG ("Requested buffer size is too large (%d > %ld)",
        size, v4lmjpegsink->breq.size);
    return NULL;
  }
  if (!gst_v4lmjpegsink_wait_frame (v4lmjpegsink, &num))
    return NULL;
  data = gst_v4lmjpegsink_get_buffer (v4lmjpegsink, num);
  if (!data)
    return NULL;
  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = data;
  GST_BUFFER_MAXSIZE (buffer) = v4lmjpegsink->breq.size;
  GST_BUFFER_SIZE (buffer) = size;
  GST_BUFFER_POOL (buffer) = pool;
  GST_BUFFER_POOL_PRIVATE (buffer) = GINT_TO_POINTER (num);

  /* with this flag set, we don't need our own buffer_free() function */
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_DONTFREE);

  return buffer;
}
#endif


static void
gst_v4lmjpegsink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4lMjpegSink *v4lmjpegsink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_V4LMJPEGSINK (object));

  v4lmjpegsink = GST_V4LMJPEGSINK (object);

  switch (prop_id) {
    case ARG_NUMBUFS:
      v4lmjpegsink->numbufs = g_value_get_int (value);
      break;
    case ARG_BUFSIZE:
      v4lmjpegsink->bufsize = g_value_get_int (value);
      break;
    case ARG_X_OFFSET:
      v4lmjpegsink->x_offset = g_value_get_int (value);
      break;
    case ARG_Y_OFFSET:
      v4lmjpegsink->y_offset = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_v4lmjpegsink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4lMjpegSink *v4lmjpegsink;

  /* it's not null if we got it, but it might not be ours */
  v4lmjpegsink = GST_V4LMJPEGSINK (object);

  switch (prop_id) {
    case ARG_FRAMES_DISPLAYED:
      g_value_set_int (value, v4lmjpegsink->frames_displayed);
      break;
    case ARG_FRAME_TIME:
      g_value_set_int (value, v4lmjpegsink->frame_time / 1000000);
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
gst_v4lmjpegsink_change_state (GstElement * element)
{
  GstV4lMjpegSink *v4lmjpegsink;
  GstElementStateReturn parent_value;

  g_return_val_if_fail (GST_IS_V4LMJPEGSINK (element), GST_STATE_FAILURE);
  v4lmjpegsink = GST_V4LMJPEGSINK (element);

  /* set up change state */
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      /* we used to do buffer setup here, but that's now done
       * right after capsnego */
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* start */
      if (!gst_v4lmjpegsink_playback_start (v4lmjpegsink))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      /* de-queue all queued buffers */
      if (!gst_v4lmjpegsink_playback_stop (v4lmjpegsink))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* stop playback, unmap all buffers */
      if (!gst_v4lmjpegsink_playback_deinit (v4lmjpegsink))
        return GST_STATE_FAILURE;
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    parent_value = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  } else {
    parent_value = GST_STATE_FAILURE;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return parent_value;

  return GST_STATE_SUCCESS;
}
