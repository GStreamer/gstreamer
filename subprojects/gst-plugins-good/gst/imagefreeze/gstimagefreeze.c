/* GStreamer
 * Copyright (c) 2005 Edward Hervey <bilboed@bilboed.com>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2020 Sebastian Dröge <sebastian@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-imagefreeze
 * @title: imagefreeze
 *
 * The imagefreeze element generates a still frame video stream from
 * the input. It duplicates the first frame with the framerate requested
 * by downstream, allows seeking and answers queries.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v filesrc location=some.png ! decodebin ! videoconvert ! imagefreeze ! autovideosink
 * ]| This pipeline shows a still frame stream of a PNG file.
 *
 */

/* This is based on the imagefreeze element from PiTiVi:
 * http://git.gnome.org/browse/pitivi/tree/pitivi/elements/imagefreeze.py
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/glib-compat-private.h>

#include "gstimagefreeze.h"

#define DEFAULT_NUM_BUFFERS     -1
#define DEFAULT_ALLOW_REPLACE   FALSE
#define DEFAULT_IS_LIVE         FALSE

enum
{
  PROP_0,
  PROP_NUM_BUFFERS,
  PROP_ALLOW_REPLACE,
  PROP_IS_LIVE,
};

static void gst_image_freeze_finalize (GObject * object);

static void gst_image_freeze_reset (GstImageFreeze * self);

static GstStateChangeReturn gst_image_freeze_change_state (GstElement * element,
    GstStateChange transition);
static GstClock *gst_image_freeze_provide_clock (GstElement * element);

static void gst_image_freeze_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_image_freeze_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_image_freeze_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_image_freeze_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_image_freeze_sink_setcaps (GstImageFreeze * self,
    GstCaps * caps);
static GstCaps *gst_image_freeze_query_caps (GstImageFreeze * self,
    GstPad * pad, GstCaps * filter);
static gboolean gst_image_freeze_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static void gst_image_freeze_src_loop (GstPad * pad);
static gboolean gst_image_freeze_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_image_freeze_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static GstStaticPadTemplate sink_pad_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY); video/x-bayer(ANY)"));

static GstStaticPadTemplate src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY); video/x-bayer(ANY)"));

GST_DEBUG_CATEGORY_STATIC (gst_image_freeze_debug);
#define GST_CAT_DEFAULT gst_image_freeze_debug

#define gst_image_freeze_parent_class parent_class
G_DEFINE_TYPE (GstImageFreeze, gst_image_freeze, GST_TYPE_ELEMENT);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (imagefreeze, "imagefreeze",
    GST_RANK_NONE, GST_TYPE_IMAGE_FREEZE,
    GST_DEBUG_CATEGORY_INIT (gst_image_freeze_debug, "imagefreeze", 0,
        "imagefreeze element");
    );

static void
gst_image_freeze_class_init (GstImageFreezeClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_image_freeze_finalize;
  gobject_class->set_property = gst_image_freeze_set_property;
  gobject_class->get_property = gst_image_freeze_get_property;

  g_object_class_install_property (gobject_class, PROP_NUM_BUFFERS,
      g_param_spec_int ("num-buffers", "Number of buffers",
          "Number of buffers to output before sending EOS (-1 = unlimited)",
          -1, G_MAXINT, DEFAULT_NUM_BUFFERS, G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ALLOW_REPLACE,
      g_param_spec_boolean ("allow-replace", "Allow Replace",
          "Allow replacing the input buffer and always output the latest",
          DEFAULT_ALLOW_REPLACE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstImageFreeze:is-live
   *
   * Selects whether the output stream should be a non-live stream based on
   * the segment configured via a %GST_EVENT_SEEK, or whether the output
   * stream should be a live stream with the negotiated framerate.
   *
   * Since: 1.18
   */
  g_object_class_install_property (gobject_class, PROP_IS_LIVE,
      g_param_spec_boolean ("is-live", "Is Live",
          "Whether to output a live video stream",
          DEFAULT_IS_LIVE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_image_freeze_change_state);
  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_image_freeze_provide_clock);

  gst_element_class_set_static_metadata (gstelement_class,
      "Still frame stream generator",
      "Filter/Video",
      "Generates a still frame stream from an image",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &sink_pad_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &src_pad_template);
}

static void
gst_image_freeze_init (GstImageFreeze * self)
{
  self->sinkpad = gst_pad_new_from_static_template (&sink_pad_template, "sink");
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_sink_chain));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_sink_event));
  gst_pad_set_query_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_sink_query));
  GST_PAD_SET_PROXY_ALLOCATION (self->sinkpad);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&src_pad_template, "src");
  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_src_event));
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_src_query));
  gst_pad_use_fixed_caps (self->srcpad);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  g_mutex_init (&self->lock);
  g_cond_init (&self->blocked_cond);

  self->num_buffers = DEFAULT_NUM_BUFFERS;
  self->allow_replace = DEFAULT_ALLOW_REPLACE;
  self->is_live = DEFAULT_IS_LIVE;

  gst_image_freeze_reset (self);
}

static void
gst_image_freeze_finalize (GObject * object)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (object);

  self->num_buffers = DEFAULT_NUM_BUFFERS;

  gst_image_freeze_reset (self);

  g_mutex_clear (&self->lock);
  g_cond_clear (&self->blocked_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_image_freeze_reset (GstImageFreeze * self)
{
  GST_DEBUG_OBJECT (self, "Resetting internal state");

  g_mutex_lock (&self->lock);
  gst_buffer_replace (&self->buffer, NULL);
  gst_caps_replace (&self->buffer_caps, NULL);
  gst_caps_replace (&self->current_caps, NULL);
  self->num_buffers_left = self->num_buffers;

  gst_segment_init (&self->segment, GST_FORMAT_TIME);
  self->need_segment = TRUE;
  self->flushing = TRUE;

  self->negotiated_framerate = FALSE;
  self->fps_n = self->fps_d = 0;
  self->offset = 0;
  self->seqnum = 0;
  g_mutex_unlock (&self->lock);
}

static gboolean
gst_image_freeze_sink_setcaps (GstImageFreeze * self, GstCaps * caps)
{
  gboolean ret = FALSE;
  GstStructure *s;
  gint fps_n, fps_d;
  GstCaps *othercaps, *intersection, *pad_current_caps;
  guint i, n;
  GstPad *pad;

  pad = self->sinkpad;

  caps = gst_caps_copy (caps);

  /* If we already negotiated a framerate then only update for the
   * caps of the new buffer */
  if (self->negotiated_framerate) {
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, self->fps_n,
        self->fps_d, NULL);
    pad_current_caps = gst_pad_get_current_caps (self->srcpad);
    if (pad_current_caps && !gst_caps_is_equal (caps, pad_current_caps)) {
      GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);
      gst_pad_set_caps (self->srcpad, caps);
    }
    gst_caps_unref (pad_current_caps);
    gst_caps_unref (caps);
    return TRUE;
  }

  /* Else negotiate a framerate with downstream */

  GST_DEBUG_OBJECT (pad, "Setting caps: %" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);

  /* 1. Remove framerate */
  gst_structure_remove_field (s, "framerate");
  gst_structure_set (s, "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
      NULL);

  /* 2. Intersect with template caps */
  othercaps = (GstCaps *) gst_pad_get_pad_template_caps (pad);
  intersection = gst_caps_intersect (caps, othercaps);
  GST_DEBUG_OBJECT (pad, "Intersecting: %" GST_PTR_FORMAT, caps);
  GST_DEBUG_OBJECT (pad, "with: %" GST_PTR_FORMAT, othercaps);
  GST_DEBUG_OBJECT (pad, "gave: %" GST_PTR_FORMAT, intersection);
  gst_caps_unref (caps);
  gst_caps_unref (othercaps);
  caps = intersection;
  intersection = othercaps = NULL;

  /* 3. Intersect with downstream peer caps */
  othercaps = gst_pad_peer_query_caps (self->srcpad, caps);
  GST_DEBUG_OBJECT (pad, "Peer query resulted: %" GST_PTR_FORMAT, othercaps);
  gst_caps_unref (caps);
  caps = othercaps;
  othercaps = NULL;

  /* 4. For every candidate try to use it downstream with framerate as
   * near as possible to 25/1 */
  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstCaps *candidate = gst_caps_new_empty ();
    GstStructure *s = gst_structure_copy (gst_caps_get_structure (caps, i));
    GstCapsFeatures *f =
        gst_caps_features_copy (gst_caps_get_features (caps, i));

    gst_caps_append_structure_full (candidate, s, f);
    if (gst_structure_has_field_typed (s, "framerate", GST_TYPE_FRACTION) ||
        gst_structure_fixate_field_nearest_fraction (s, "framerate", 25, 1)) {
      gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d);
      if (fps_d != 0) {
        gst_pad_set_caps (self->srcpad, candidate);
        g_mutex_lock (&self->lock);
        self->fps_n = fps_n;
        self->fps_d = fps_d;
        g_mutex_unlock (&self->lock);
        self->negotiated_framerate = TRUE;
        GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, candidate);
        ret = TRUE;
        gst_caps_unref (candidate);
        break;
      } else {
        GST_WARNING_OBJECT (pad, "Invalid caps with framerate %d/%d", fps_n,
            fps_d);
      }
    }
    gst_caps_unref (candidate);
  }

  if (!ret)
    GST_ERROR_OBJECT (pad, "No usable caps found");

  gst_caps_unref (caps);

  return ret;
}

/* remove framerate in writable @caps */
static void
gst_image_freeze_remove_fps (GstImageFreeze * self, GstCaps * caps)
{
  gint i, n;

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    gst_structure_remove_field (s, "framerate");
    gst_structure_set (s, "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT,
        1, NULL);
  }
}

static GstCaps *
gst_image_freeze_query_caps (GstImageFreeze * self, GstPad * pad,
    GstCaps * filter)
{
  GstCaps *ret, *tmp, *templ;
  GstPad *otherpad;

  otherpad = (pad == self->srcpad) ? self->sinkpad : self->srcpad;

  if (filter) {
    filter = gst_caps_copy (filter);
    gst_image_freeze_remove_fps (self, filter);
  }
  templ = gst_pad_get_pad_template_caps (pad);
  tmp = gst_pad_peer_query_caps (otherpad, filter);
  if (tmp) {
    GST_LOG_OBJECT (otherpad, "peer caps %" GST_PTR_FORMAT, tmp);
    ret = gst_caps_intersect (tmp, templ);
    gst_caps_unref (tmp);
  } else {
    GST_LOG_OBJECT (otherpad, "going to copy");
    ret = gst_caps_copy (templ);
  }
  if (templ)
    gst_caps_unref (templ);
  if (filter)
    gst_caps_unref (filter);

  ret = gst_caps_make_writable (ret);
  gst_image_freeze_remove_fps (self, ret);

  GST_LOG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_image_freeze_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (parent);
  gboolean ret;

  GST_LOG_OBJECT (pad, "Handling query of type '%s'",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps;

      gst_query_parse_caps (query, &caps);
      caps = gst_image_freeze_query_caps (self, pad, caps);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
  }

  return ret;
}

static gboolean
gst_image_freeze_convert (GstImageFreeze * self,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean ret = FALSE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  if (src_value == -1) {
    *dest_value = -1;
    return TRUE;
  }

  switch (src_format) {
    case GST_FORMAT_DEFAULT:{
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          g_mutex_lock (&self->lock);
          if (self->fps_n == 0)
            *dest_value = -1;
          else
            *dest_value =
                gst_util_uint64_scale (src_value, GST_SECOND * self->fps_d,
                self->fps_n);
          g_mutex_unlock (&self->lock);
          ret = TRUE;
          break;
        default:
          break;
      }
      break;
    }
    case GST_FORMAT_TIME:{
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          g_mutex_lock (&self->lock);
          *dest_value =
              gst_util_uint64_scale (src_value, self->fps_n,
              self->fps_d * GST_SECOND);
          g_mutex_unlock (&self->lock);
          ret = TRUE;
          break;
        default:
          break;
      }
      break;
    }
    default:
      break;
  }

  return ret;
}

static gboolean
gst_image_freeze_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (parent);
  gboolean ret = FALSE;

  GST_LOG_OBJECT (pad, "Handling query of type '%s'",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:{
      GstFormat src_format, dest_format;
      gint64 src_value, dest_value;

      gst_query_parse_convert (query, &src_format, &src_value, &dest_format,
          &dest_value);
      ret =
          gst_image_freeze_convert (self, src_format, src_value, &dest_format,
          &dest_value);
      if (ret)
        gst_query_set_convert (query, src_format, src_value, dest_format,
            dest_value);
      break;
    }
    case GST_QUERY_POSITION:{
      GstFormat format;
      gint64 position;

      gst_query_parse_position (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_DEFAULT:{
          g_mutex_lock (&self->lock);
          position = self->offset;
          g_mutex_unlock (&self->lock);
          ret = TRUE;
          break;
        }
        case GST_FORMAT_TIME:{
          g_mutex_lock (&self->lock);
          position = self->segment.position;
          g_mutex_unlock (&self->lock);
          ret = TRUE;
          break;
        }
        default:
          break;
      }

      if (ret) {
        gst_query_set_position (query, format, position);
        GST_DEBUG_OBJECT (pad,
            "Returning position %" G_GINT64_FORMAT " in format %s", position,
            gst_format_get_name (format));
      } else {
        GST_DEBUG_OBJECT (pad, "Position query failed");
      }
      break;
    }
    case GST_QUERY_DURATION:{
      GstFormat format;
      gint64 duration;

      gst_query_parse_duration (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_TIME:{
          g_mutex_lock (&self->lock);
          duration = self->segment.stop;
          g_mutex_unlock (&self->lock);
          ret = TRUE;
          break;
        }
        case GST_FORMAT_DEFAULT:{
          g_mutex_lock (&self->lock);
          duration = self->segment.stop;
          if (duration != -1)
            duration =
                gst_util_uint64_scale (duration, self->fps_n,
                GST_SECOND * self->fps_d);
          g_mutex_unlock (&self->lock);
          ret = TRUE;
          break;
        }
        default:
          break;
      }

      if (ret) {
        gst_query_set_duration (query, format, duration);
        GST_DEBUG_OBJECT (pad,
            "Returning duration %" G_GINT64_FORMAT " in format %s", duration,
            gst_format_get_name (format));
      } else {
        GST_DEBUG_OBJECT (pad, "Duration query failed");
      }
      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat format;
      gboolean seekable;

      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      seekable = !self->is_live && (format == GST_FORMAT_TIME
          || format == GST_FORMAT_DEFAULT);

      gst_query_set_seeking (query, format, seekable, (seekable ? 0 : -1), -1);
      ret = TRUE;
      break;
    }
    case GST_QUERY_LATENCY:
      if (self->is_live) {
        /* If we run live, we output the buffer without any latency but allow
         * for at most one frame of latency. If downstream takes longer to
         * consume out frame we would skip ahead */
        if (self->fps_n > 0 && self->fps_d > 0)
          gst_query_set_latency (query, TRUE, 0,
              gst_util_uint64_scale_ceil (GST_SECOND, self->fps_d,
                  self->fps_n));
        else
          gst_query_set_latency (query, TRUE, 0, GST_CLOCK_TIME_NONE);
      } else {
        /* If we don't run live, even if upstream is live, we never output any
         * buffers with latency but immediately generate buffers as fast as we
         * can according to the negotiated framerate */
        gst_query_set_latency (query, FALSE, 0, GST_CLOCK_TIME_NONE);
      }
      ret = TRUE;
      break;
    case GST_QUERY_CAPS:
    {
      GstCaps *caps;
      gst_query_parse_caps (query, &caps);
      caps = gst_image_freeze_query_caps (self, pad, caps);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}


static gboolean
gst_image_freeze_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (parent);
  gboolean ret;

  GST_LOG_OBJECT (pad, "Got %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      g_mutex_lock (&self->lock);
      gst_event_parse_caps (event, &caps);
      gst_caps_replace (&self->current_caps, caps);
      g_mutex_unlock (&self->lock);
      gst_event_unref (event);
      ret = TRUE;
      break;
    }
    case GST_EVENT_EOS:
      if (!self->buffer) {
        /* if we receive EOS before a buffer arrives, then let it pass */
        GST_DEBUG_OBJECT (self, "EOS without input buffer, passing on");
        ret = gst_pad_push_event (self->srcpad, event);
        break;
      }
      /* fall-through */
    case GST_EVENT_SEGMENT:
      GST_DEBUG_OBJECT (pad, "Dropping event");
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_FLUSH_START:
      gst_image_freeze_reset (self);
      /* fall through */
    default:
      ret = gst_pad_push_event (self->srcpad, gst_event_ref (event));
      if (GST_EVENT_IS_STICKY (event))
        ret = TRUE;
      gst_event_unref (event);
      break;
  }

  return ret;
}

static gboolean
gst_image_freeze_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (parent);
  gboolean ret;

  GST_LOG_OBJECT (pad, "Got %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_QOS:
    case GST_EVENT_LATENCY:
    case GST_EVENT_STEP:
      GST_DEBUG_OBJECT (pad, "Dropping event");
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_SEEK:{
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      gint64 last_stop;
      gboolean start_task;
      gboolean flush;
      guint32 seqnum;

      if (self->is_live) {
        GST_ERROR_OBJECT (pad, "Can't seek in live mode");
        ret = FALSE;
        gst_event_unref (event);
        break;
      }

      seqnum = gst_event_get_seqnum (event);
      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);
      gst_event_unref (event);

      flush = !!(flags & GST_SEEK_FLAG_FLUSH);

      if (format != GST_FORMAT_TIME && format != GST_FORMAT_DEFAULT) {
        GST_ERROR_OBJECT (pad, "Seek in invalid format: %s",
            gst_format_get_name (format));
        ret = FALSE;
        break;
      }

      if (format == GST_FORMAT_DEFAULT) {
        format = GST_FORMAT_TIME;
        if (!gst_image_freeze_convert (self, GST_FORMAT_DEFAULT, start, &format,
                &start)
            || !gst_image_freeze_convert (self, GST_FORMAT_DEFAULT, stop,
                &format, &stop)
            || start == -1 || stop == -1) {
          GST_ERROR_OBJECT (pad,
              "Failed to convert seek from DEFAULT format into TIME format");
          ret = FALSE;
          break;
        }
      }

      if (flush) {
        GstEvent *e;

        g_mutex_lock (&self->lock);
        self->flushing = TRUE;
        g_mutex_unlock (&self->lock);

        e = gst_event_new_flush_start ();
        gst_event_set_seqnum (e, seqnum);
        gst_pad_push_event (self->srcpad, e);
      } else {
        gst_pad_pause_task (self->srcpad);
      }

      GST_PAD_STREAM_LOCK (self->srcpad);

      g_mutex_lock (&self->lock);

      gst_segment_do_seek (&self->segment, rate, format, flags, start_type,
          start, stop_type, stop, NULL);
      self->need_segment = TRUE;
      last_stop = self->segment.position;

      start_task = self->buffer != NULL;
      self->flushing = FALSE;
      g_mutex_unlock (&self->lock);

      if (flush) {
        GstEvent *e;

        e = gst_event_new_flush_stop (TRUE);
        gst_event_set_seqnum (e, seqnum);
        gst_pad_push_event (self->srcpad, e);
      }

      if (flags & GST_SEEK_FLAG_SEGMENT) {
        GstMessage *m;

        m = gst_message_new_segment_start (GST_OBJECT (self),
            format, last_stop);
        gst_element_post_message (GST_ELEMENT (self), m);
      }

      self->seqnum = seqnum;
      GST_PAD_STREAM_UNLOCK (self->srcpad);

      GST_DEBUG_OBJECT (pad, "Seek successful");

      if (start_task) {
        g_mutex_lock (&self->lock);

        if (self->buffer != NULL)
          gst_pad_start_task (self->srcpad,
              (GstTaskFunction) gst_image_freeze_src_loop, self->srcpad, NULL);

        g_mutex_unlock (&self->lock);
      }

      ret = TRUE;
      break;
    }
    case GST_EVENT_FLUSH_START:
      g_mutex_lock (&self->lock);
      self->flushing = TRUE;
      g_mutex_unlock (&self->lock);
      ret = gst_pad_push_event (self->sinkpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_image_freeze_reset (self);
      g_mutex_lock (&self->lock);
      self->flushing = FALSE;
      g_mutex_unlock (&self->lock);
      ret = gst_pad_push_event (self->sinkpad, event);
      break;
    default:
      ret = gst_pad_push_event (self->sinkpad, event);
      break;
  }

  return ret;
}

static void
gst_image_freeze_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstImageFreeze *self;

  self = GST_IMAGE_FREEZE (object);

  switch (prop_id) {
    case PROP_NUM_BUFFERS:
      self->num_buffers = g_value_get_int (value);
      break;
    case PROP_ALLOW_REPLACE:
      self->allow_replace = g_value_get_boolean (value);
      break;
    case PROP_IS_LIVE:
      self->is_live = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_image_freeze_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstImageFreeze *self;

  self = GST_IMAGE_FREEZE (object);

  switch (prop_id) {
    case PROP_NUM_BUFFERS:
      g_value_set_int (value, self->num_buffers);
      break;
    case PROP_ALLOW_REPLACE:
      g_value_set_boolean (value, self->allow_replace);
      break;
    case PROP_IS_LIVE:
      g_value_set_boolean (value, self->is_live);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_image_freeze_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (parent);
  GstFlowReturn flow_ret;

  g_mutex_lock (&self->lock);
  if (self->buffer && !self->allow_replace) {
    GST_DEBUG_OBJECT (pad, "Already have a buffer, dropping");
    gst_buffer_unref (buffer);
    g_mutex_unlock (&self->lock);
    return GST_FLOW_EOS;
  }

  if (!self->current_caps) {
    GST_ERROR_OBJECT (pad, "Not negotiated yet");
    g_mutex_unlock (&self->lock);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  gst_buffer_replace (&self->buffer, buffer);
  if (!self->buffer_caps
      || !gst_caps_is_equal (self->buffer_caps, self->current_caps))
    gst_pad_mark_reconfigure (self->srcpad);
  gst_caps_replace (&self->buffer_caps, self->current_caps);
  gst_buffer_unref (buffer);

  gst_pad_start_task (self->srcpad, (GstTaskFunction) gst_image_freeze_src_loop,
      self->srcpad, NULL);
  flow_ret = self->allow_replace ? GST_FLOW_OK : GST_FLOW_EOS;
  g_mutex_unlock (&self->lock);
  return flow_ret;
}

static void
gst_image_freeze_src_loop (GstPad * pad)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (GST_PAD_PARENT (pad));
  GstBuffer *buffer;
  guint64 offset;
  GstClockTime timestamp, timestamp_end;
  guint64 cstart, cstop;
  gboolean in_seg, eos;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  gboolean first = FALSE;

  g_mutex_lock (&self->lock);
  if (self->flushing) {
    GST_DEBUG_OBJECT (pad, "Flushing");
    flow_ret = GST_FLOW_FLUSHING;
    g_mutex_unlock (&self->lock);
    goto pause_task;
  } else if (!self->buffer) {
    GST_ERROR_OBJECT (pad, "Have no buffer yet");
    flow_ret = GST_FLOW_ERROR;
    g_mutex_unlock (&self->lock);
    goto pause_task;
  }

  g_assert (self->buffer);

  /* Take a new reference of the buffer here so we're guaranteed to have one
   * in all the following code even if it disappears while we temporarily
   * unlock the mutex */
  buffer = gst_buffer_ref (self->buffer);

  if (gst_pad_check_reconfigure (self->srcpad)) {
    GstCaps *buffer_caps = gst_caps_ref (self->buffer_caps);
    g_mutex_unlock (&self->lock);
    if (!gst_image_freeze_sink_setcaps (self, buffer_caps)) {
      gst_caps_unref (buffer_caps);
      gst_buffer_unref (buffer);
      gst_pad_mark_reconfigure (self->srcpad);
      flow_ret = GST_FLOW_NOT_NEGOTIATED;
      goto pause_task;
    }
    gst_caps_unref (buffer_caps);
    g_mutex_lock (&self->lock);
  }

  /* normally we don't count buffers */
  if (G_UNLIKELY (self->num_buffers_left >= 0)) {
    GST_DEBUG_OBJECT (pad, "Buffers left %d", self->num_buffers_left);
    if (self->num_buffers_left == 0) {
      flow_ret = GST_FLOW_EOS;
      gst_buffer_unref (buffer);
      g_mutex_unlock (&self->lock);
      goto pause_task;
    } else {
      self->num_buffers_left--;
    }
  }
  buffer = gst_buffer_make_writable (buffer);
  g_mutex_unlock (&self->lock);

  if (self->need_segment) {
    GstEvent *e;

    GST_DEBUG_OBJECT (pad, "Pushing SEGMENT event: %" GST_SEGMENT_FORMAT,
        &self->segment);
    e = gst_event_new_segment (&self->segment);

    if (self->seqnum)
      gst_event_set_seqnum (e, self->seqnum);

    g_mutex_lock (&self->lock);
    if (self->segment.rate >= 0) {
      self->offset =
          gst_util_uint64_scale (self->segment.start, self->fps_n,
          self->fps_d * GST_SECOND);
    } else {
      self->offset =
          gst_util_uint64_scale (self->segment.stop, self->fps_n,
          self->fps_d * GST_SECOND);
    }
    g_mutex_unlock (&self->lock);

    self->need_segment = FALSE;
    first = TRUE;

    gst_pad_push_event (self->srcpad, e);
  }

  g_mutex_lock (&self->lock);
  offset = self->offset;
  if (self->is_live) {
    GstClockTime base_time, clock_time;
    GstClockTimeDiff jitter;
    GstClockReturn clock_ret;
    GstClock *clock;

    clock = gst_element_get_clock (GST_ELEMENT (self));

    /* Wait until the element went to PLAYING or flushing */
    while ((!clock || self->blocked) && !self->flushing) {
      g_cond_wait (&self->blocked_cond, &self->lock);
      gst_clear_object (&clock);
      clock = gst_element_get_clock (GST_ELEMENT (self));
    }

    if (self->flushing) {
      g_mutex_unlock (&self->lock);
      gst_buffer_unref (buffer);
      flow_ret = GST_FLOW_FLUSHING;
      gst_clear_object (&clock);
      goto pause_task;
    }

    /* Wait on the clock until the time for our current frame is reached */
    base_time = gst_element_get_base_time (GST_ELEMENT (self));
    if (self->fps_n != 0) {
      clock_time =
          base_time + gst_util_uint64_scale (offset, self->fps_d * GST_SECOND,
          self->fps_n);
    } else {
      clock_time = base_time;
    }

    self->clock_id = gst_clock_new_single_shot_id (clock, clock_time);
    g_mutex_unlock (&self->lock);
    GST_TRACE_OBJECT (self,
        "Waiting for %" GST_TIME_FORMAT ", now %" GST_TIME_FORMAT,
        GST_TIME_ARGS (clock_time), GST_TIME_ARGS (gst_clock_get_time (clock)));
    clock_ret = gst_clock_id_wait (self->clock_id, &jitter);
    GST_TRACE_OBJECT (self,
        "Waited for %" GST_TIME_FORMAT ", clock ret %d, jitter %"
        GST_STIME_FORMAT, GST_TIME_ARGS (clock_time), clock_ret,
        GST_STIME_ARGS (jitter));
    g_mutex_lock (&self->lock);
    gst_clock_id_unref (self->clock_id);
    self->clock_id = NULL;
    gst_object_unref (clock);

    if (self->flushing || clock_ret == GST_CLOCK_UNSCHEDULED) {
      g_mutex_unlock (&self->lock);
      gst_buffer_unref (buffer);
      flow_ret = GST_FLOW_FLUSHING;
      goto pause_task;
    }

    /* If we were late, adjust our offset and jump ahead if needed */
    if (self->fps_n != 0) {
      if (jitter > 0) {
        guint64 new_offset =
            gst_util_uint64_scale (clock_time + jitter - base_time, self->fps_n,
            self->fps_d * GST_SECOND);

        if (new_offset != offset) {
          GST_INFO_OBJECT (self,
              "Late by %" GST_TIME_FORMAT ", old offset %" G_GUINT64_FORMAT
              ", new offset %" G_GUINT64_FORMAT, GST_TIME_ARGS (jitter), offset,
              new_offset);
          self->offset = offset = new_offset;
        }
      }

      timestamp =
          gst_util_uint64_scale (offset, self->fps_d * GST_SECOND, self->fps_n);
      timestamp_end =
          gst_util_uint64_scale (offset + 1, self->fps_d * GST_SECOND,
          self->fps_n);
    } else {
      /* If we have no framerate then we output a single frame now */
      if (jitter > 0)
        timestamp = jitter;
      else
        timestamp = 0;

      timestamp_end = GST_CLOCK_TIME_NONE;
    }
  } else {
    if (self->fps_n != 0) {
      timestamp =
          gst_util_uint64_scale (offset, self->fps_d * GST_SECOND, self->fps_n);
      timestamp_end =
          gst_util_uint64_scale (offset + 1, self->fps_d * GST_SECOND,
          self->fps_n);
    } else {
      timestamp = self->segment.start;
      timestamp_end = GST_CLOCK_TIME_NONE;
    }
  }

  eos = (self->fps_n == 0 && offset > 0) ||
      (self->segment.rate >= 0 && self->segment.stop != -1
      && timestamp > self->segment.stop) || (self->segment.rate < 0
      && offset == 0) || (self->segment.rate < 0
      && self->segment.start != -1 && timestamp_end < self->segment.start);

  if (self->fps_n == 0 && offset > 0)
    in_seg = FALSE;
  else
    in_seg =
        gst_segment_clip (&self->segment, GST_FORMAT_TIME, timestamp,
        timestamp_end, &cstart, &cstop);

  if (in_seg) {
    self->segment.position = cstart;
    if (self->segment.rate >= 0)
      self->segment.position = cstop;
  }

  if (self->segment.rate >= 0)
    self->offset++;
  else
    self->offset--;
  g_mutex_unlock (&self->lock);

  GST_DEBUG_OBJECT (pad, "Handling buffer with timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (in_seg) {
    GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_PTS (buffer) = cstart;
    GST_BUFFER_DURATION (buffer) = cstop - cstart;
    GST_BUFFER_OFFSET (buffer) = offset;
    GST_BUFFER_OFFSET_END (buffer) = offset + 1;
    if (first)
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    else
      GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
    flow_ret = gst_pad_push (self->srcpad, buffer);
    GST_DEBUG_OBJECT (pad, "Pushing buffer resulted in %s",
        gst_flow_get_name (flow_ret));
    if (flow_ret != GST_FLOW_OK)
      goto pause_task;
  } else {
    gst_buffer_unref (buffer);
  }

  if (eos) {
    flow_ret = GST_FLOW_EOS;
    goto pause_task;
  }

  return;

pause_task:
  {
    const gchar *reason = gst_flow_get_name (flow_ret);

    GST_LOG_OBJECT (self, "pausing task, reason %s", reason);
    gst_pad_pause_task (pad);

    if (flow_ret == GST_FLOW_EOS) {
      if ((self->segment.flags & GST_SEEK_FLAG_SEGMENT)) {
        GstMessage *m;
        GstEvent *e;

        GST_DEBUG_OBJECT (pad, "Sending segment done at end of segment");
        if (self->segment.rate >= 0) {
          m = gst_message_new_segment_done (GST_OBJECT_CAST (self),
              GST_FORMAT_TIME, self->segment.stop);
          e = gst_event_new_segment_done (GST_FORMAT_TIME, self->segment.stop);
        } else {
          m = gst_message_new_segment_done (GST_OBJECT_CAST (self),
              GST_FORMAT_TIME, self->segment.start);
          e = gst_event_new_segment_done (GST_FORMAT_TIME, self->segment.start);
        }
        gst_element_post_message (GST_ELEMENT_CAST (self), m);
        gst_pad_push_event (self->srcpad, e);
      } else {
        GstEvent *e = gst_event_new_eos ();

        GST_DEBUG_OBJECT (pad, "Sending EOS at end of segment");

        if (self->seqnum)
          gst_event_set_seqnum (e, self->seqnum);
        gst_pad_push_event (self->srcpad, e);
      }
    } else if (flow_ret == GST_FLOW_NOT_LINKED || flow_ret < GST_FLOW_EOS) {
      GstEvent *e = gst_event_new_eos ();

      GST_ELEMENT_FLOW_ERROR (self, flow_ret);

      if (self->seqnum)
        gst_event_set_seqnum (e, self->seqnum);

      gst_pad_push_event (self->srcpad, e);
    }
    return;
  }
}

static GstStateChangeReturn
gst_image_freeze_change_state (GstElement * element, GstStateChange transition)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  gboolean no_preroll = FALSE;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_image_freeze_reset (self);
      g_mutex_lock (&self->lock);
      self->flushing = FALSE;
      self->blocked = TRUE;
      g_mutex_unlock (&self->lock);
      if (self->is_live)
        no_preroll = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      g_mutex_lock (&self->lock);
      self->blocked = FALSE;
      g_cond_signal (&self->blocked_cond);
      g_mutex_unlock (&self->lock);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_mutex_lock (&self->lock);
      self->flushing = TRUE;
      if (self->clock_id) {
        GST_DEBUG_OBJECT (self, "unlock clock wait");
        gst_clock_id_unschedule (self->clock_id);
      }
      self->blocked = FALSE;
      g_cond_signal (&self->blocked_cond);
      g_mutex_unlock (&self->lock);
      gst_image_freeze_reset (self);
      gst_pad_stop_task (self->srcpad);
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      g_mutex_lock (&self->lock);
      self->blocked = TRUE;
      g_mutex_unlock (&self->lock);
      if (self->is_live)
        no_preroll = TRUE;
      break;
    default:
      break;
  }

  if (no_preroll && ret == GST_STATE_CHANGE_SUCCESS)
    ret = GST_STATE_CHANGE_NO_PREROLL;

  return ret;
}

/* FIXME: GStreamer 2.0 */
static GstClock *
gst_image_freeze_provide_clock (GstElement * element)
{
  return gst_system_clock_obtain ();
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return GST_ELEMENT_REGISTER (imagefreeze, plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    imagefreeze,
    "Still frame stream generator",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
