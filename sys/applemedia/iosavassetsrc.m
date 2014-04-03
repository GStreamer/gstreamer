/*
 * GStreamer
 * Copyright (C) 2013 Fluendo S.L. <support@fluendo.com>
 *    Authors: Andoni Morales Alastruey <amorales@fluendo.com>
 * Copyright (C) 2014 Collabora Ltd.
 *    Authors: Matthieu Bouron <matthieu.bouron@collabora.com>
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

/**
 * SECTION:element-plugin
 *
 * Read and decode samples from iOS assets using the AVAssetReader API.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m iosavassetsrc uri="file://movie.mp4" ! autovideosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "iosavassetsrc.h"
#include "coremediabuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_avasset_src_debug);
#define GST_CAT_DEFAULT gst_avasset_src_debug

#define CMTIME_TO_GST_TIME(x) \
    (x.value == 0 ? 0 : (guint64)(x.value * GST_SECOND / x.timescale));
#define GST_AVASSET_SRC_LOCK(x) (g_mutex_lock (&x->lock));
#define GST_AVASSET_SRC_UNLOCK(x) (g_mutex_unlock (&x->lock));
#define MEDIA_TYPE_TO_STR(x) \
    (x == GST_AVASSET_READER_MEDIA_TYPE_AUDIO ? "audio" : "video")
#define AVASSET_READER_HAS_AUDIO(x) \
    ([self->reader hasMediaType:GST_AVASSET_READER_MEDIA_TYPE_AUDIO])
#define AVASSET_READER_HAS_VIDEO(x) \
    ([self->reader hasMediaType:GST_AVASSET_READER_MEDIA_TYPE_VIDEO])
#define OBJC_CALLOUT_BEGIN() \
   NSAutoreleasePool *pool; \
   \
   pool = [[NSAutoreleasePool alloc] init]
#define OBJC_CALLOUT_END() \
  [pool release]

enum
{
  PROP_0,
  PROP_URI
};

static GstStaticPadTemplate audio_factory = GST_STATIC_PAD_TEMPLATE ("audio",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) F32LE, "
        "rate = " GST_AUDIO_RATE_RANGE ", "
        "channels = (int) [1, 2], "
        "layout = (string) interleaved"
    )
);

static GstStaticPadTemplate video_factory = GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) NV12, "
        "framerate = " GST_VIDEO_FPS_RANGE ", "
        "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE
    )
);

static void gst_avasset_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_avasset_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_avasset_src_dispose (GObject *object);

static GstStateChangeReturn gst_avasset_src_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_avasset_src_query (GstPad *pad, GstObject * parent, GstQuery *query);
static gboolean gst_avasset_src_event (GstPad *pad, GstObject * parent, GstEvent *event);
static gboolean gst_avasset_src_send_event (GstAVAssetSrc *self,
    GstEvent *event);

static void gst_avasset_src_read_audio (GstAVAssetSrc *self);
static void gst_avasset_src_read_video (GstAVAssetSrc *self);
static void gst_avasset_src_start (GstAVAssetSrc *self);
static void gst_avasset_src_stop (GstAVAssetSrc *self);
static gboolean gst_avasset_src_start_reading (GstAVAssetSrc *self);
static void gst_avasset_src_stop_reading (GstAVAssetSrc *self);
static void gst_avasset_src_stop_all (GstAVAssetSrc *self);
static void gst_avasset_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void
_do_init (GType avassetsrc_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_avasset_src_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (avassetsrc_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);
  GST_DEBUG_CATEGORY_INIT (gst_avasset_src_debug, "iosavassetsrc",
      0, "iosavassetsrc element");
}

G_DEFINE_TYPE_WITH_CODE (GstAVAssetSrc, gst_avasset_src, GST_TYPE_ELEMENT,
    _do_init (g_define_type_id));


/* GObject vmethod implementations */

static void
gst_avasset_src_class_init (GstAVAssetSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
    "Source and decoder for iOS assets",
    "Source/Codec",
    "Read and decode samples from iOS assets using the AVAssetReader API",
    "Andoni Morales Alastruey amorales@fluendo.com");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&audio_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&video_factory));

  gobject_class->set_property = gst_avasset_src_set_property;
  gobject_class->get_property = gst_avasset_src_get_property;
  gobject_class->dispose = gst_avasset_src_dispose;

  /**
   * GstAVAssetSrc:uri
   *
   * URI of the asset to read
   *
   **/
  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "Asset URI",
          "URI of the asset to read", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gstelement_class->change_state = gst_avasset_src_change_state;

}

static void
gst_avasset_src_init (GstAVAssetSrc * self)
{
  self->selected_audio_track = 0;
  self->selected_video_track = 0;
  self->last_audio_pad_ret = GST_FLOW_OK;
  self->last_video_pad_ret = GST_FLOW_OK;
  g_mutex_init (&self->lock);
}

static void
gst_avasset_src_dispose (GObject *object)
{
  GstAVAssetSrc *self = GST_AVASSET_SRC (object);

  if (self->uri != NULL) {
    g_free (self->uri);
    self->uri = NULL;
  }

  if (self->seek_event) {
    gst_event_unref (self->seek_event);
    self->seek_event = NULL;
  }
}

static void
gst_avasset_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAVAssetSrc *self = GST_AVASSET_SRC (object);

  switch (prop_id) {
    case PROP_URI:
      if (self->uri) {
        g_free (self->uri);
      }
      self->uri = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_avasset_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAVAssetSrc *self = GST_AVASSET_SRC (object);

  switch (prop_id) {
    case PROP_URI:
      g_value_set_string (value, self->uri);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_avasset_src_change_state (GstElement * element, GstStateChange transition)
{
  GstAVAssetSrc *self = GST_AVASSET_SRC (element);
  GstStateChangeReturn ret;
  GError *error;

  GST_DEBUG ("%s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  OBJC_CALLOUT_BEGIN ();
  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY: {
      self->state = GST_AVASSET_SRC_STATE_STOPPED;
      self->reader = [[GstAVAssetReader alloc] initWithURI:self->uri:&error];
      if (error) {
        GST_ELEMENT_ERROR (element, RESOURCE, FAILED, ("AVAssetReader error"),
            ("%s", error->message));
        g_error_free (error);
        gst_avasset_src_stop_all (self);
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_avasset_src_start (self);
      gst_avasset_src_start_reading (self);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_avasset_src_parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_avasset_src_stop_reading (self);
      gst_avasset_src_stop (self);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      [self->reader release];
      break;
    default:
      break;
  }
  OBJC_CALLOUT_END ();
  return ret;
}

static GstCaps *
gst_avasset_src_get_caps(GstAVAssetSrc * self, GstPad * pad, GstCaps * filter)
{
  GstCaps * caps;

  caps = gst_pad_get_current_caps (pad);
  if (!caps) {
    caps = gst_pad_get_pad_template_caps (pad);
  }

  if (filter) {
    GstCaps *intersection = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }

  return caps;
}

static gboolean
gst_avasset_src_query (GstPad *pad, GstObject * parent, GstQuery *query)
{
    gboolean ret = FALSE;
    GstCaps *caps;
    GstAVAssetSrc *self = GST_AVASSET_SRC (parent);

    switch (GST_QUERY_TYPE (query)) {
      case GST_QUERY_URI:
        gst_query_set_uri (query, self->uri);
        ret = TRUE;
        break;
      case GST_QUERY_DURATION:
        gst_query_set_duration (query, GST_FORMAT_TIME, self->reader.duration);
        ret = TRUE;
        break;
      case GST_QUERY_POSITION:
        gst_query_set_position (query, GST_FORMAT_TIME, self->reader.position);
        ret = TRUE;
        break;
      case GST_QUERY_SEEKING: {
        GstFormat fmt;
        gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
        if (fmt == GST_FORMAT_TIME) {
          gst_query_set_seeking (query, GST_FORMAT_TIME, TRUE, 0, self->reader.duration);
          ret = TRUE;
        }
        break;
      }
      case GST_QUERY_CAPS: {
        GstCaps *filter = NULL;
        gst_query_parse_caps (query, &filter);
        caps = gst_avasset_src_get_caps (self, pad, filter);
        gst_query_set_caps_result (query, caps);
        ret = TRUE;
        break;
      }
      default:
        ret = FALSE;
        break;
    }

    return ret;
}

static gboolean
gst_avasset_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstAVAssetSrc *self;
  gboolean res = TRUE;
  GError *error = NULL;

  OBJC_CALLOUT_BEGIN ();
  self = GST_AVASSET_SRC (gst_pad_get_parent_element (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK: {
      GstFormat format;
      GstSeekFlags flags;
      gdouble rate;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      GstSegment segment;

      GST_DEBUG ("Processing SEEK event");

      GST_AVF_ASSET_SRC_LOCK (self);
      if (self->seek_event && gst_event_get_seqnum (event) ==
          gst_event_get_seqnum (self->seek_event)) {
        GST_AVF_ASSET_SRC_UNLOCK (self);
        break;
      }
      self->seek_event = gst_event_ref (event);
      GST_AVF_ASSET_SRC_UNLOCK (self);

      /* pause tasks before re-acquiring the object's lock */
      gst_avf_asset_src_stop_reading (self);
      GST_AVF_ASSET_SRC_LOCK (self);

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type,
          &start, &stop_type, &stop);

      if (rate < 0) {
        GST_WARNING ("Negative rates are not supported yet");
        GST_AVASSET_SRC_UNLOCK (self);
        res = FALSE;
        break;
      }

      if (format != GST_FORMAT_TIME || start_type == GST_SEEK_TYPE_NONE) {
        GST_AVASSET_SRC_UNLOCK(self);
        gst_avasset_src_start_reading (self);
        res = FALSE;
        break;
      }
      if (stop_type == GST_SEEK_TYPE_NONE) {
        stop = GST_CLOCK_TIME_NONE;
      }
      gst_avasset_src_send_event (self, gst_event_new_flush_start ());
      [self->reader seekTo: start: stop: &error];

      gst_segment_init (&segment, GST_FORMAT_TIME);
      segment.rate = rate;
      segment.start = start;
      segment.stop = stop;
      segment.position = start;

      gst_avasset_src_send_event (self, gst_event_new_flush_stop (TRUE));
      gst_avasset_src_send_event (self, gst_event_new_segment (&segment));

      if (error != NULL) {
        GST_ELEMENT_ERROR (self, RESOURCE, SEEK,
            ("AVAssetReader seek failed"), ("%s", error->message));
        g_error_free(error);
        res = FALSE;
      }
      GST_AVASSET_SRC_UNLOCK (self);
      gst_event_unref (event);

      /* start tasks after releasing the object's lock */
      gst_avasset_src_start_reading (self);
      break;
    }
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  gst_object_unref (self);
  OBJC_CALLOUT_END ();
  return res;
}

static GstFlowReturn
gst_avasset_src_send_start_stream (GstAVAssetSrc * self, GstPad * pad)
{
  GstEvent *event;
  gchar *stream_id;
  GstFlowReturn ret;

  stream_id = gst_pad_create_stream_id (pad, GST_ELEMENT_CAST (self), NULL);
  GST_DEBUG_OBJECT (self, "Pushing STREAM START");
  event = gst_event_new_stream_start (stream_id);
  gst_event_set_group_id (event, gst_util_group_id_next ());

  ret = gst_pad_push_event (pad, event);
  g_free (stream_id);

  return ret;
}

static GstFlowReturn
gst_avasset_src_combine_flows (GstAVAssetSrc * self, GstAVAssetReaderMediaType type,
    GstFlowReturn ret)
{
  gboolean has_other_pad;
  GstFlowReturn last_other_pad_ret;

  GST_AVASSET_SRC_LOCK (self);
  if (type == GST_AVASSET_READER_MEDIA_TYPE_AUDIO) {
    self->last_audio_pad_ret = ret;
    has_other_pad = AVASSET_READER_HAS_VIDEO (ret);
    last_other_pad_ret = self->last_video_pad_ret;
  } else if (type == GST_AVASSET_READER_MEDIA_TYPE_VIDEO) {
    self->last_video_pad_ret = ret;
    has_other_pad = AVASSET_READER_HAS_AUDIO (ret);
    last_other_pad_ret = self->last_audio_pad_ret;
  } else {
    GST_ERROR ("Unsupported media type");
    ret = GST_FLOW_ERROR;
    goto exit;
  }

  if (!has_other_pad || ret != GST_FLOW_NOT_LINKED)
    goto exit;

  ret = last_other_pad_ret;

exit:
  GST_AVASSET_SRC_UNLOCK (self);
  return ret;
}

static void
gst_avasset_src_read_data (GstAVAssetSrc *self, GstPad *pad,
    GstAVAssetReaderMediaType type)
{
  GstBuffer *buf;
  GstFlowReturn ret, combined_ret;
  GError *error;

  OBJC_CALLOUT_BEGIN ();

  GST_AVASSET_SRC_LOCK (self);
  if (self->state != GST_AVASSET_SRC_STATE_READING) {
    GST_AVASSET_SRC_UNLOCK (self);
    goto exit;
  }

  buf = [self->reader nextBuffer:type:&error];
  GST_AVASSET_SRC_UNLOCK (self);

  if (buf == NULL) {
    if (error != NULL) {
      GST_ELEMENT_ERROR (self, RESOURCE, READ, ("Error reading next buffer"),
          ("%s", error->message));
      g_error_free (error);

      gst_avasset_src_combine_flows (self, type, GST_FLOW_ERROR);
      gst_pad_pause_task (pad);
      goto exit;
    }

    gst_pad_push_event (pad, gst_event_new_eos ());
    gst_avasset_src_combine_flows (self, type, GST_FLOW_EOS);
    gst_pad_pause_task (pad);
    goto exit;
  }

  ret = gst_pad_push (pad, buf);
  combined_ret = gst_avasset_src_combine_flows (self, type, ret);

  if (ret != GST_FLOW_OK) {
    GST_WARNING ("Error pushing %s buffer on pad %" GST_PTR_FORMAT
        ", reason %s", MEDIA_TYPE_TO_STR (type), pad, gst_flow_get_name (ret));

    if (ret == GST_FLOW_EOS) {
      gst_pad_push_event (pad, gst_event_new_eos ());
    }

    if (combined_ret != GST_FLOW_OK) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("Internal data stream error."),
          ("stream stopped reason %s", gst_flow_get_name (ret)));
    }

    gst_pad_pause_task (pad);
  }

exit:
  OBJC_CALLOUT_END ();
}

static void
gst_avasset_src_read_audio (GstAVAssetSrc *self)
{
  gst_avasset_src_read_data (self, self->audiopad,
      GST_AVASSET_READER_MEDIA_TYPE_AUDIO);
}

static void
gst_avasset_src_read_video (GstAVAssetSrc *self)
{
  gst_avasset_src_read_data (self, self->videopad,
      GST_AVASSET_READER_MEDIA_TYPE_VIDEO);
}

static gboolean
gst_avasset_src_start_reader (GstAVAssetSrc * self)
{
  GError *error = NULL;
  gboolean ret = TRUE;

  OBJC_CALLOUT_BEGIN ();

  [self->reader start: &error];
  if (error != NULL) {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("AVAssetReader could not start reading"), ("%s", error->message));
    g_error_free (error);
    ret = FALSE;
    goto exit;
  }

exit:
  OBJC_CALLOUT_END ();
  return ret;
}

static gboolean
gst_avasset_src_send_event (GstAVAssetSrc *self, GstEvent *event)
{
  gboolean ret = TRUE;

  OBJC_CALLOUT_BEGIN ();

  if (AVASSET_READER_HAS_VIDEO (self)) {
    ret |= gst_pad_push_event (self->videopad, gst_event_ref (event));
  }
  if (AVASSET_READER_HAS_AUDIO (self)) {
    ret |= gst_pad_push_event (self->audiopad, gst_event_ref (event));
  }

  gst_event_unref (event);
  OBJC_CALLOUT_END ();
  return ret;
}

static void
gst_avasset_src_start (GstAVAssetSrc *self)
{
  GstSegment segment;

  OBJC_CALLOUT_BEGIN ();
  if (self->state == GST_AVASSET_SRC_STATE_STARTED) {
    goto exit;
  }

  GST_DEBUG_OBJECT (self, "Creating pads and starting reader");

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.duration = self->reader.duration;

  /* We call AVAssetReader's startReading when the pads are linked
   * and no outputs can be added afterwards, so the tracks must be
   * selected before adding any of the new pads */
  if (AVASSET_READER_HAS_AUDIO (self)) {
    [self->reader selectTrack: GST_AVASSET_READER_MEDIA_TYPE_AUDIO:
        self->selected_audio_track];
  }
  if (AVASSET_READER_HAS_VIDEO (self)) {
    [self->reader selectTrack: GST_AVASSET_READER_MEDIA_TYPE_VIDEO:
         self->selected_video_track];
  }

  if (AVASSET_READER_HAS_AUDIO (self)) {
    self->audiopad = gst_pad_new_from_static_template (&audio_factory, "audio");
    gst_pad_set_query_function (self->audiopad,
        gst_avasset_src_query);
    gst_pad_set_event_function(self->audiopad,
        gst_avasset_src_event);
    gst_pad_use_fixed_caps (self->audiopad);
    gst_pad_set_active (self->audiopad, TRUE);
    gst_avasset_src_send_start_stream (self, self->audiopad);
    gst_pad_set_caps (self->audiopad,
        [self->reader getCaps: GST_AVASSET_READER_MEDIA_TYPE_AUDIO]);
    gst_pad_push_event (self->audiopad, gst_event_new_caps (
        [self->reader getCaps: GST_AVASSET_READER_MEDIA_TYPE_AUDIO]));
    gst_pad_push_event (self->audiopad, gst_event_new_segment (&segment));
    gst_element_add_pad (GST_ELEMENT (self), self->audiopad);
  }
  if (AVASSET_READER_HAS_VIDEO (self)) {
    self->videopad = gst_pad_new_from_static_template (&video_factory, "video");
    gst_pad_set_query_function (self->videopad,
        gst_avasset_src_query);
    gst_pad_set_event_function(self->videopad,
        gst_avasset_src_event);
    gst_pad_use_fixed_caps (self->videopad);
    gst_pad_set_active (self->videopad, TRUE);
    gst_avasset_src_send_start_stream (self, self->videopad);
    gst_pad_set_caps (self->videopad,
        [self->reader getCaps: GST_AVASSET_READER_MEDIA_TYPE_VIDEO]);
    gst_pad_push_event (self->videopad, gst_event_new_caps (
        [self->reader getCaps: GST_AVASSET_READER_MEDIA_TYPE_VIDEO]));
    gst_pad_push_event (self->videopad, gst_event_new_segment (&segment));
    gst_element_add_pad (GST_ELEMENT (self), self->videopad);
  }
  gst_element_no_more_pads (GST_ELEMENT (self));

  self->state = GST_AVASSET_SRC_STATE_STARTED;

exit:
  OBJC_CALLOUT_END ();
}

static void
gst_avasset_src_stop (GstAVAssetSrc *self)
{
  gboolean has_audio, has_video;
  OBJC_CALLOUT_BEGIN();

  if (self->state == GST_AVASSET_SRC_STATE_STOPPED) {
    goto exit;
  }

  GST_DEBUG ("Stopping tasks and removing pads");

  has_audio = AVASSET_READER_HAS_AUDIO (self);
  has_video = AVASSET_READER_HAS_VIDEO (self);
  [self->reader stop];

  if (has_audio) {
    gst_pad_stop_task (self->audiopad);
    gst_element_remove_pad (GST_ELEMENT (self), self->audiopad);
  }
  if (has_video) {
    gst_pad_stop_task (self->videopad);
    gst_element_remove_pad (GST_ELEMENT (self), self->videopad);
  }

  self->state = GST_AVASSET_SRC_STATE_STOPPED;

exit:
  OBJC_CALLOUT_END ();
}

static gboolean
gst_avasset_src_start_reading (GstAVAssetSrc *self)
{
  gboolean ret = TRUE;

  if (self->state != GST_AVASSET_SRC_STATE_STARTED) {
    goto exit;
  }

  GST_DEBUG_OBJECT (self, "Start reading");

  if ((ret = gst_avasset_src_start_reader (self)) != TRUE) {
    goto exit;
  }

  if (AVASSET_READER_HAS_AUDIO (self)) {
    ret = gst_pad_start_task (self->audiopad, (GstTaskFunction)gst_avasset_src_read_audio, self, NULL);
    if (!ret) {
      GST_ERROR ("Failed to start audio task");
      goto exit;
    }
  }

  if (AVASSET_READER_HAS_VIDEO (self)) {
    ret = gst_pad_start_task (self->videopad, (GstTaskFunction)gst_avasset_src_read_video, self, NULL);
    if (!ret) {
      GST_ERROR ("Failed to start video task");
      goto exit;
    }
  }

  self->state = GST_AVASSET_SRC_STATE_READING;

exit:
  return ret;
}

static void
gst_avasset_src_stop_reading (GstAVAssetSrc * self)
{
  if (self->state != GST_AVASSET_SRC_STATE_READING) {
    return;
  }

  GST_DEBUG_OBJECT (self, "Stop reading");

  if (AVASSET_READER_HAS_AUDIO (self)) {
    gst_pad_pause_task (self->audiopad);
  }
  if (AVASSET_READER_HAS_VIDEO (self)) {
    gst_pad_pause_task (self->videopad);
  }

  self->state = GST_AVASSET_SRC_STATE_STARTED;
}

static void
gst_avasset_src_stop_all (GstAVAssetSrc *self)
{
  GST_AVASSET_SRC_LOCK (self);
  gst_avasset_src_stop_reading (self);
  gst_avasset_src_stop (self);
  GST_AVASSET_SRC_UNLOCK (self);
}

static GQuark
gst_avasset_src_error_quark (void)
{
  static GQuark q;              /* 0 */

  if (G_UNLIKELY (q == 0)) {
      q = g_quark_from_static_string ("avasset-src-error-quark");
  }
  return q;
}

static GstURIType
gst_avasset_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar * const *
gst_avasset_src_uri_get_protocols (GType type)
{
  static const gchar * const protocols[] = { "file", "ipod-library", NULL };

  return protocols;
}

static gchar *
gst_avasset_src_uri_get_uri (GstURIHandler * handler)
{
  GstAVAssetSrc *self = GST_AVASSET_SRC (handler);

  return g_strdup (self->uri);
}

static gboolean
gst_avasset_src_uri_set_uri (GstURIHandler * handler, const gchar * uri, GError **error)
{
  GstAVAssetSrc *self = GST_AVASSET_SRC (handler);
  NSString *str;
  NSURL *url;
  AVAsset *asset;
  gchar *escaped_uri;
  gboolean ret = FALSE;

  OBJC_CALLOUT_BEGIN ();
  escaped_uri = g_uri_escape_string (uri, ":/", TRUE);
  str = [NSString stringWithUTF8String: escaped_uri];
  url = [[NSURL alloc] initWithString: str];
  asset = [AVAsset assetWithURL: url];
  g_free (escaped_uri);

  if (asset.playable) {
    ret = TRUE;
    if (self->uri) {
      g_free (self->uri);
    }
    self->uri = g_strdup (uri);
  } else {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Invalid URI '%s' for ios_assetsrc", self->uri);
  }
  OBJC_CALLOUT_END ();
  return ret;
}

static void
gst_avasset_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_avasset_src_uri_get_type;
  iface->get_protocols = gst_avasset_src_uri_get_protocols;
  iface->get_uri = gst_avasset_src_uri_get_uri;
  iface->set_uri = gst_avasset_src_uri_set_uri;
}

@implementation GstAVAssetReader

@synthesize duration;
@synthesize position;

- (NSDictionary *) capsToAudioSettings
{
  gint depth;
  gboolean isFloat;
  GstAudioInfo info;

  if (!gst_caps_is_fixed (audio_caps))
    return NULL;

  gst_audio_info_from_caps (&info, audio_caps);
  isFloat = GST_AUDIO_INFO_IS_FLOAT(&info);
  depth = GST_AUDIO_INFO_DEPTH(&info);

  return [NSDictionary dictionaryWithObjectsAndKeys:
      [NSNumber numberWithInt:kAudioFormatLinearPCM], AVFormatIDKey,
      [NSNumber numberWithFloat:info.rate], AVSampleRateKey,
      [NSNumber numberWithInt:info.channels], AVNumberOfChannelsKey,
      //[NSData dataWithBytes:&channelLayout length:sizeof(AudioChannelLayout)],
      //AVChannelLayoutKey,
      [NSNumber numberWithInt:depth], AVLinearPCMBitDepthKey,
      [NSNumber numberWithBool:isFloat],AVLinearPCMIsFloatKey,
      [NSNumber numberWithBool:NO], AVLinearPCMIsNonInterleaved,
      [NSNumber numberWithBool:NO], AVLinearPCMIsBigEndianKey,
      nil];
}

- (void) releaseReader
{
  [video_track release];
  [audio_track release];
  [video_tracks release];
  [audio_tracks release];
  [reader release];
}

- (void) initReader: (GError **) error
{
  NSError *nserror;

  reader = [[AVAssetReader alloc] initWithAsset:asset error:&nserror];
  if (nserror != NULL) {
    GST_ERROR ("Error initializing reader: %s",
        [nserror.description UTF8String]);
    *error = g_error_new (GST_AVASSET_SRC_ERROR, GST_AVASSET_ERROR_INIT, "%s",
        [nserror.description UTF8String]);
    [asset release];
    [reader release];
    return;
  }

  audio_tracks = [[asset tracksWithMediaType:AVMediaTypeAudio] retain];
  video_tracks = [[asset tracksWithMediaType:AVMediaTypeVideo] retain];
  reader.timeRange = CMTimeRangeMake(kCMTimeZero, asset.duration);
  GST_INFO ("Found %lu video tracks and %lu audio tracks",
      (unsigned long)[video_tracks count], (unsigned long)[audio_tracks count]);
}

- (id) initWithURI:(gchar*)uri : (GError **)error;
{
  NSString *str;
  NSURL *url;
  gchar *escaped_uri;

  GST_INFO ("Initializing AVAssetReader with uri:%s", uri);
  *error = NULL;

  escaped_uri = g_uri_escape_string (uri, ":/", TRUE);
  str = [NSString stringWithUTF8String: escaped_uri];
  url = [[NSURL alloc] initWithString: str];
  asset = [[AVAsset assetWithURL: url] retain];
  g_free (escaped_uri);

  if (!asset.playable) {
    *error = g_error_new (GST_AVASSET_SRC_ERROR, GST_AVASSET_ERROR_NOT_PLAYABLE,
        "Media is not playable");
    [asset release];
    return nil;
  }

  selected_audio_track = -1;
  selected_video_track = -1;
  reading = FALSE;
  position = 0;
  duration = CMTIME_TO_GST_TIME (asset.duration);

  /* FIXME: use fixed caps here until we found a way to determine
   * the native audio format */
  audio_caps = gst_caps_from_string ("audio/x-raw, "
      "format=F32LE, rate=44100, channels=2, layout=interleaved");

  [self initReader: error];
  if (*error) {
    return nil;
  }

  self = [super init];
  return self;
}

- (bool) selectTrack: (GstAVAssetReaderMediaType) type : (gint) index
{
  NSArray *tracks;
  AVAssetTrack *track;
  AVAssetReaderOutput **output;
  NSDictionary *settings;
  NSString *mediaType;
  gint *selected_track;

  GST_INFO ("Selecting %s track %d", MEDIA_TYPE_TO_STR(type), index);

  if (type == GST_AVASSET_READER_MEDIA_TYPE_AUDIO) {
    mediaType = AVMediaTypeAudio;
    selected_track = &selected_audio_track;
    output = &audio_track;
    settings = [self capsToAudioSettings];
  } else if (type == GST_AVASSET_READER_MEDIA_TYPE_VIDEO) {
    mediaType = AVMediaTypeVideo;
    selected_track = &selected_video_track;
    output = &video_track;
    settings = [NSDictionary dictionaryWithObjectsAndKeys:
        [NSNumber numberWithInt:
        kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange],
        kCVPixelBufferPixelFormatTypeKey, nil];
  } else {
    return FALSE;
  }

  tracks = [asset tracksWithMediaType:mediaType];
  if ([tracks count] == 0 || [tracks count] < index + 1) {
    return FALSE;
  }

  track = [tracks objectAtIndex:index];
  *selected_track = index;
  *output  = [AVAssetReaderTrackOutput
      assetReaderTrackOutputWithTrack:track
      outputSettings:settings];
  [*output retain];
  [reader addOutput:*output];
  return TRUE;
}

- (void) start: (GError **)error
{
  if (reading)
    return;

  if (![reader startReading]) {
    *error = g_error_new (GST_AVASSET_SRC_ERROR, GST_AVASSET_ERROR_START,
        "%s", [reader.error.description UTF8String]);
    reading = FALSE;
    return;
  }
  reading = TRUE;
}

- (void) stop
{
  [self->reader cancelReading];
  reading = FALSE;
}

- (bool) hasMediaType: (GstAVAssetReaderMediaType) type
{
  if (type == GST_AVASSET_READER_MEDIA_TYPE_AUDIO) {
    return [audio_tracks count] != 0;
  }
  if (type == GST_AVASSET_READER_MEDIA_TYPE_VIDEO) {
    return [video_tracks count] != 0;
  }
  return FALSE;
}

- (void) seekTo: (guint64) startTS : (guint64) stopTS : (GError **) error
{
  CMTime startTime = kCMTimeZero, stopTime = kCMTimePositiveInfinity;

  if (startTS != GST_CLOCK_TIME_NONE) {
    startTime = CMTimeMake (startTS, GST_SECOND);
  }
  if (stopTS != GST_CLOCK_TIME_NONE) {
    stopTime = CMTimeMake (stopTS, GST_SECOND);
  }

  /* AVAssetReader needs to be recreated before changing the
   * timerange property */
  [self stop];
  [self releaseReader];
  [self initReader: error];
  if (*error) {
    return;
  }

  GST_DEBUG ("Seeking to start:%" GST_TIME_FORMAT " stop:%" GST_TIME_FORMAT,
      GST_TIME_ARGS(startTS), GST_TIME_ARGS(stopTS));

  reader.timeRange = CMTimeRangeMake(startTime, stopTime);
  [self selectTrack: GST_AVASSET_READER_MEDIA_TYPE_AUDIO:selected_audio_track];
  [self selectTrack: GST_AVASSET_READER_MEDIA_TYPE_VIDEO:selected_video_track];
  [self start: error];
}

- (GstBuffer *) nextBuffer: (GstAVAssetReaderMediaType) type : (GError **) error
{
  CMSampleBufferRef cmbuf;
  AVAssetReaderTrackOutput *areader = NULL;
  GstCaps *caps;
  GstBuffer *buf;
  CMTime dur, ts;

  GST_LOG ("Reading %s next buffer", MEDIA_TYPE_TO_STR (type));
  if (type == GST_AVASSET_READER_MEDIA_TYPE_AUDIO && audio_track != NULL) {
    areader = audio_track;
    caps = audio_caps;
  } else if (type == GST_AVASSET_READER_MEDIA_TYPE_VIDEO &&
      video_track != NULL) {
    areader = video_track;
    caps = video_caps;
  }

  if (areader == NULL) {
    return NULL;
  }

  *error = NULL;
  cmbuf = [areader copyNextSampleBuffer];
  if (cmbuf == NULL) {
    if (reader.error != NULL) {
      *error = g_error_new (GST_AVASSET_SRC_ERROR, GST_AVASSET_ERROR_READ,
          "%s", [reader.error.description UTF8String]);
    }
    /* EOS */
    return NULL;
  }

  buf = gst_core_media_buffer_new (cmbuf, FALSE);
  dur = CMSampleBufferGetDuration (cmbuf);
  ts = CMSampleBufferGetPresentationTimeStamp (cmbuf);
  if (dur.value != 0) {
    GST_BUFFER_DURATION (buf) = CMTIME_TO_GST_TIME (dur);
  }
  GST_BUFFER_TIMESTAMP (buf) = CMTIME_TO_GST_TIME (ts);
  GST_LOG ("Copying next %s buffer ts:%" GST_TIME_FORMAT " dur:%"
      GST_TIME_FORMAT, MEDIA_TYPE_TO_STR (type),
      GST_TIME_ARGS(GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS(GST_BUFFER_DURATION (buf)));
  if (GST_BUFFER_TIMESTAMP (buf) > position) {
    position = GST_BUFFER_TIMESTAMP (buf);
  }
  return buf;
}

- (GstCaps *) getCaps: (GstAVAssetReaderMediaType) type
{
  GstCaps *caps = NULL;
  AVAssetTrack *track;

  if (type == GST_AVASSET_READER_MEDIA_TYPE_AUDIO) {
    caps = gst_caps_ref (audio_caps);
    GST_INFO ("Using audio caps: %" GST_PTR_FORMAT, caps);
  } else if (type == GST_AVASSET_READER_MEDIA_TYPE_VIDEO) {
    gint fr_n, fr_d;

    track = [video_tracks objectAtIndex: selected_video_track];
    gst_util_double_to_fraction(track.nominalFrameRate, &fr_n, &fr_d);
    caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, (int) track.naturalSize.width,
        "height", G_TYPE_INT, (int) track.naturalSize.height,
        "framerate", GST_TYPE_FRACTION, fr_n, fr_d, NULL);
    GST_INFO ("Using video caps: %" GST_PTR_FORMAT, caps);
    video_caps = gst_caps_ref (caps);
  }

  return caps;
}

- (oneway void) release
{
  [asset release];

  [self releaseReader];

  if (audio_caps != NULL) {
    gst_caps_unref (audio_caps);
  }

  if (video_caps != NULL) {
    gst_caps_unref (audio_caps);
  }
}

@end
