/* GStreamer
 * Copyright (C) <2011> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include "gstplaysinkvideoconvert.h"

#include <gst/pbutils/pbutils.h>
#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_STATIC (gst_play_sink_video_convert_debug);
#define GST_CAT_DEFAULT gst_play_sink_video_convert_debug

#define parent_class gst_play_sink_video_convert_parent_class

G_DEFINE_TYPE (GstPlaySinkVideoConvert, gst_play_sink_video_convert,
    GST_TYPE_BIN);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static gboolean
is_raw_caps (GstCaps * caps)
{
  gint i, n;
  GstStructure *s;
  const gchar *name;

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    s = gst_caps_get_structure (caps, i);
    name = gst_structure_get_name (s);
    if (!g_str_has_prefix (name, "video/x-raw"))
      return FALSE;
  }

  return TRUE;
}

static void
post_missing_element_message (GstPlaySinkVideoConvert * self,
    const gchar * name)
{
  GstMessage *msg;

  msg = gst_missing_element_message_new (GST_ELEMENT_CAST (self), name);
  gst_element_post_message (GST_ELEMENT_CAST (self), msg);
}

static void
distribute_running_time (GstElement * element, const GstSegment * segment)
{
  GstEvent *event;
  GstPad *pad;

  pad = gst_element_get_static_pad (element, "sink");

  if (segment->accum) {
    event = gst_event_new_new_segment_full (FALSE, segment->rate,
        segment->applied_rate, segment->format, 0, segment->accum, 0);
    gst_pad_push_event (pad, event);
  }

  event = gst_event_new_new_segment_full (FALSE, segment->rate,
      segment->applied_rate, segment->format,
      segment->start, segment->stop, segment->time);
  gst_pad_push_event (pad, event);

  gst_object_unref (pad);
}

static void
pad_blocked_cb (GstPad * pad, gboolean blocked, GstPlaySinkVideoConvert * self)
{
  GstPad *peer;
  GstCaps *caps;
  gboolean raw;

  GST_PLAY_SINK_VIDEO_CONVERT_LOCK (self);
  self->sink_proxypad_blocked = blocked;
  GST_DEBUG_OBJECT (self, "Pad blocked: %d", blocked);
  if (!blocked)
    goto done;

  /* There must be a peer at this point */
  peer = gst_pad_get_peer (self->sinkpad);
  caps = gst_pad_get_negotiated_caps (peer);
  if (!caps)
    caps = gst_pad_get_caps_reffed (peer);
  gst_object_unref (peer);

  raw = is_raw_caps (caps);
  GST_DEBUG_OBJECT (self, "Caps %" GST_PTR_FORMAT " are raw: %d", caps, raw);
  gst_caps_unref (caps);

  if (raw == self->raw)
    goto unblock;
  self->raw = raw;

  if (raw) {
    GstBin *bin = GST_BIN_CAST (self);
    GstElement *head = NULL, *prev = NULL;
    GstPad *pad;

    GST_DEBUG_OBJECT (self, "Creating raw conversion pipeline");

    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->sinkpad), NULL);
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->srcpad), NULL);

    self->conv = gst_element_factory_make ("ffmpegcolorspace", "conv");
    if (self->conv == NULL) {
      post_missing_element_message (self, "ffmpegcolorspace");
      GST_ELEMENT_WARNING (self, CORE, MISSING_PLUGIN,
          (_("Missing element '%s' - check your GStreamer installation."),
              "ffmpegcolorspace"), ("video rendering might fail"));
    } else {
      gst_bin_add (bin, self->conv);
      gst_element_sync_state_with_parent (self->conv);
      distribute_running_time (self->conv, &self->segment);
      prev = head = self->conv;
    }

    self->scale = gst_element_factory_make ("videoscale", "scale");
    if (self->scale == NULL) {
      post_missing_element_message (self, "videoscale");
      GST_ELEMENT_WARNING (self, CORE, MISSING_PLUGIN,
          (_("Missing element '%s' - check your GStreamer installation."),
              "videoscale"), ("possibly a liboil version mismatch?"));
    } else {
      /* Add black borders if necessary to keep the DAR */
      g_object_set (self->scale, "add-borders", TRUE, NULL);
      gst_bin_add (bin, self->scale);
      gst_element_sync_state_with_parent (self->scale);
      distribute_running_time (self->scale, &self->segment);
      if (prev) {
        if (!gst_element_link_pads_full (prev, "src", self->scale, "sink",
                GST_PAD_LINK_CHECK_TEMPLATE_CAPS))
          goto link_failed;
      } else {
        head = self->scale;
      }
      prev = self->scale;
    }

    if (head) {
      pad = gst_element_get_static_pad (head, "sink");
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->sinkpad), pad);
      gst_object_unref (pad);
    }

    if (prev) {
      pad = gst_element_get_static_pad (prev, "src");
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->srcpad), pad);
      gst_object_unref (pad);
    }

    if (!head && !prev) {
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->srcpad),
          self->sink_proxypad);
    }

    GST_DEBUG_OBJECT (self, "Raw conversion pipeline created");
  } else {
    GstBin *bin = GST_BIN_CAST (self);

    GST_DEBUG_OBJECT (self, "Removing raw conversion pipeline");

    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->sinkpad), NULL);
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->srcpad), NULL);

    if (self->conv) {
      gst_element_set_state (self->conv, GST_STATE_NULL);
      gst_bin_remove (bin, self->conv);
      self->conv = NULL;
    }
    if (self->scale) {
      gst_element_set_state (self->scale, GST_STATE_NULL);
      gst_bin_remove (bin, self->scale);
      self->scale = NULL;
    }

    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->srcpad),
        self->sink_proxypad);

    GST_DEBUG_OBJECT (self, "Raw conversion pipeline removed");
  }

unblock:
  gst_pad_set_blocked_async_full (self->sink_proxypad, FALSE,
      (GstPadBlockCallback) pad_blocked_cb, gst_object_ref (self),
      (GDestroyNotify) gst_object_unref);

done:
  GST_PLAY_SINK_VIDEO_CONVERT_UNLOCK (self);
  return;

link_failed:
  {
    GST_ELEMENT_ERROR (self, CORE, PAD,
        (NULL), ("Failed to configure the video converter."));
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->srcpad),
        self->sink_proxypad);
    gst_pad_set_blocked_async_full (self->sink_proxypad, FALSE,
        (GstPadBlockCallback) pad_blocked_cb, gst_object_ref (self),
        (GDestroyNotify) gst_object_unref);

    GST_PLAY_SINK_VIDEO_CONVERT_UNLOCK (self);
    return;
  }
}

static gboolean
gst_play_sink_video_convert_sink_event (GstPad * pad, GstEvent * event)
{
  GstPlaySinkVideoConvert *self =
      GST_PLAY_SINK_VIDEO_CONVERT (gst_pad_get_parent (pad));
  gboolean ret;

  ret = gst_proxy_pad_event_default (pad, gst_event_ref (event));

  if (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT) {
    gboolean update;
    gdouble rate, applied_rate;
    GstFormat format;
    gint64 start, stop, position;

    GST_PLAY_SINK_VIDEO_CONVERT_LOCK (self);
    gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
        &format, &start, &stop, &position);

    GST_DEBUG_OBJECT (self, "Segment before %" GST_SEGMENT_FORMAT,
        &self->segment);
    gst_segment_set_newsegment_full (&self->segment, update, rate, applied_rate,
        format, start, stop, position);
    GST_DEBUG_OBJECT (self, "Segment after %" GST_SEGMENT_FORMAT,
        &self->segment);
    GST_PLAY_SINK_VIDEO_CONVERT_UNLOCK (self);
  } else if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP) {
    GST_PLAY_SINK_VIDEO_CONVERT_LOCK (self);
    GST_DEBUG_OBJECT (self, "Resetting segment");
    gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
    GST_PLAY_SINK_VIDEO_CONVERT_UNLOCK (self);
  }

  gst_event_unref (event);
  gst_object_unref (self);

  return ret;
}

static gboolean
gst_play_sink_video_convert_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstPlaySinkVideoConvert *self =
      GST_PLAY_SINK_VIDEO_CONVERT (gst_pad_get_parent (pad));
  gboolean ret;
  GstStructure *s;
  const gchar *name;
  gboolean reconfigure = FALSE;

  GST_PLAY_SINK_VIDEO_CONVERT_LOCK (self);
  s = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (s);

  if (g_str_has_prefix (name, "video/x-raw-")) {
    if (!self->raw && !gst_pad_is_blocked (self->sink_proxypad)) {
      GST_DEBUG_OBJECT (self, "Changing caps from non-raw to raw");
      reconfigure = TRUE;
      gst_pad_set_blocked_async_full (self->sink_proxypad, TRUE,
          (GstPadBlockCallback) pad_blocked_cb, gst_object_ref (self),
          (GDestroyNotify) gst_object_unref);
    }
  } else {
    if (self->raw && !gst_pad_is_blocked (self->sink_proxypad)) {
      GST_DEBUG_OBJECT (self, "Changing caps from raw to non-raw");
      reconfigure = TRUE;
      gst_pad_set_blocked_async_full (self->sink_proxypad, TRUE,
          (GstPadBlockCallback) pad_blocked_cb, gst_object_ref (self),
          (GDestroyNotify) gst_object_unref);
    }
  }

  /* Otherwise the setcaps below fails */
  if (reconfigure) {
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->sinkpad), NULL);
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->srcpad), NULL);
  }
  GST_PLAY_SINK_VIDEO_CONVERT_UNLOCK (self);

  ret = gst_ghost_pad_setcaps_default (pad, caps);

  GST_DEBUG_OBJECT (self, "Setting sink caps %" GST_PTR_FORMAT ": %d", caps,
      ret);

  gst_object_unref (self);

  return ret;
}

static GstCaps *
gst_play_sink_video_convert_getcaps (GstPad * pad)
{
  GstPlaySinkVideoConvert *self =
      GST_PLAY_SINK_VIDEO_CONVERT (gst_pad_get_parent (pad));
  GstCaps *ret;
  GstPad *otherpad, *peer;

  GST_PLAY_SINK_VIDEO_CONVERT_LOCK (self);
  if (pad == self->srcpad)
    otherpad = gst_object_ref (self->sinkpad);
  else
    otherpad = gst_object_ref (self->srcpad);
  GST_PLAY_SINK_VIDEO_CONVERT_UNLOCK (self);

  peer = gst_pad_get_peer (otherpad);
  if (peer) {
    ret = gst_pad_get_caps_reffed (peer);
    gst_object_unref (peer);
  } else {
    ret = gst_caps_new_any ();
  }

  gst_object_unref (otherpad);
  gst_object_unref (self);

  return ret;
}

static void
gst_play_sink_video_convert_finalize (GObject * object)
{
  GstPlaySinkVideoConvert *self = GST_PLAY_SINK_VIDEO_CONVERT_CAST (object);

  gst_object_unref (self->sink_proxypad);
  g_mutex_free (self->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_play_sink_video_convert_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstPlaySinkVideoConvert *self = GST_PLAY_SINK_VIDEO_CONVERT_CAST (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_PLAY_SINK_VIDEO_CONVERT_LOCK (self);
      if (gst_pad_is_blocked (self->sink_proxypad))
        gst_pad_set_blocked_async_full (self->sink_proxypad, FALSE,
            (GstPadBlockCallback) pad_blocked_cb, gst_object_ref (self),
            (GDestroyNotify) gst_object_unref);
      GST_PLAY_SINK_VIDEO_CONVERT_UNLOCK (self);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_PLAY_SINK_VIDEO_CONVERT_LOCK (self);
      gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
      if (self->conv) {
        gst_element_set_state (self->conv, GST_STATE_NULL);
        gst_bin_remove (GST_BIN_CAST (self), self->conv);
        self->conv = NULL;
      }
      if (self->scale) {
        gst_element_set_state (self->scale, GST_STATE_NULL);
        gst_bin_remove (GST_BIN_CAST (self), self->scale);
        self->scale = NULL;
      }
      gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->srcpad),
          self->sink_proxypad);
      self->raw = FALSE;
      GST_PLAY_SINK_VIDEO_CONVERT_UNLOCK (self);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_PLAY_SINK_VIDEO_CONVERT_LOCK (self);
      if (!gst_pad_is_blocked (self->sink_proxypad))
        gst_pad_set_blocked_async_full (self->sink_proxypad, TRUE,
            (GstPadBlockCallback) pad_blocked_cb, gst_object_ref (self),
            (GDestroyNotify) gst_object_unref);
      GST_PLAY_SINK_VIDEO_CONVERT_UNLOCK (self);
    default:
      break;
  }

  return ret;
}

static void
gst_play_sink_video_convert_class_init (GstPlaySinkVideoConvertClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  GST_DEBUG_CATEGORY_INIT (gst_play_sink_video_convert_debug,
      "playsinkvideoconvert", 0, "play bin");

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_play_sink_video_convert_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details_simple (gstelement_class,
      "Player Sink Video Converter", "Video/Bin/Converter",
      "Convenience bin for video conversion",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_play_sink_video_convert_change_state);
}

static void
gst_play_sink_video_convert_init (GstPlaySinkVideoConvert * self)
{
  GstPadTemplate *templ;

  self->lock = g_mutex_new ();
  gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);

  templ = gst_static_pad_template_get (&sinktemplate);
  self->sinkpad = gst_ghost_pad_new_no_target_from_template ("sink", templ);
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_play_sink_video_convert_sink_event));
  gst_pad_set_setcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_play_sink_video_convert_sink_setcaps));
  gst_pad_set_getcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_play_sink_video_convert_getcaps));

  self->sink_proxypad =
      GST_PAD_CAST (gst_proxy_pad_get_internal (GST_PROXY_PAD (self->sinkpad)));

  gst_element_add_pad (GST_ELEMENT_CAST (self), self->sinkpad);
  gst_object_unref (templ);

  templ = gst_static_pad_template_get (&srctemplate);
  self->srcpad = gst_ghost_pad_new_no_target_from_template ("src", templ);
  gst_pad_set_getcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_play_sink_video_convert_getcaps));
  gst_element_add_pad (GST_ELEMENT_CAST (self), self->srcpad);
  gst_object_unref (templ);

  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->srcpad),
      self->sink_proxypad);
}
