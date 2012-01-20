/* GStreamer
 * Copyright (c) 2005 Edward Hervey <bilboed@bilboed.com>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-imagefreeze
 *
 * The imagefreeze element generates a still frame video stream from
 * the input. It duplicates the first frame with the framerate requested
 * by downstream, allows seeking and answers queries.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=some.png ! decodebin2 ! imagefreeze ! autovideosink
 * ]| This pipeline shows a still frame stream of a PNG file.
 * </refsect2>
 */

/* This is based on the imagefreeze element from PiTiVi:
 * http://git.gnome.org/browse/pitivi/tree/pitivi/elements/imagefreeze.py
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/glib-compat-private.h>

#include "gstimagefreeze.h"

static void gst_image_freeze_finalize (GObject * object);

static void gst_image_freeze_reset (GstImageFreeze * self);

static GstStateChangeReturn gst_image_freeze_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_image_freeze_sink_chain (GstPad * pad,
    GstBuffer * buffer);
static gboolean gst_image_freeze_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_image_freeze_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_image_freeze_sink_getcaps (GstPad * pad);
static gboolean gst_image_freeze_sink_query (GstPad * pad, GstQuery * query);
static GstFlowReturn gst_image_freeze_sink_bufferalloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static void gst_image_freeze_src_loop (GstPad * pad);
static gboolean gst_image_freeze_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_image_freeze_src_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_image_freeze_src_query_type (GstPad * pad);

static GstStaticPadTemplate sink_pad_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv; video/x-raw-rgb; video/x-raw-gray"));

static GstStaticPadTemplate src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv; video/x-raw-rgb; video/x-raw-gray"));

GST_DEBUG_CATEGORY_STATIC (gst_image_freeze_debug);
#define GST_CAT_DEFAULT gst_image_freeze_debug

GST_BOILERPLATE (GstImageFreeze, gst_image_freeze, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_image_freeze_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (gstelement_class,
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
gst_image_freeze_class_init (GstImageFreezeClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_image_freeze_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_image_freeze_change_state);
}

static void
gst_image_freeze_init (GstImageFreeze * self, GstImageFreezeClass * g_class)
{
  self->sinkpad = gst_pad_new_from_static_template (&sink_pad_template, "sink");
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_sink_chain));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_sink_event));
  gst_pad_set_query_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_sink_query));
  gst_pad_set_setcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_sink_setcaps));
  gst_pad_set_getcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_sink_getcaps));
  gst_pad_set_bufferalloc_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_sink_bufferalloc));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&src_pad_template, "src");
  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_src_event));
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_src_query));
  gst_pad_set_query_type_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_image_freeze_src_query_type));
  gst_pad_use_fixed_caps (self->srcpad);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->lock = g_mutex_new ();

  gst_image_freeze_reset (self);
}

static void
gst_image_freeze_finalize (GObject * object)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (object);

  gst_image_freeze_reset (self);

  if (self->lock)
    g_mutex_free (self->lock);
  self->lock = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_image_freeze_reset (GstImageFreeze * self)
{
  GST_DEBUG_OBJECT (self, "Resetting internal state");

  g_mutex_lock (self->lock);
  gst_buffer_replace (&self->buffer, NULL);

  gst_segment_init (&self->segment, GST_FORMAT_TIME);
  self->need_segment = TRUE;
  gst_event_replace (&self->close_segment, NULL);

  self->fps_n = self->fps_d = 0;
  self->offset = 0;
  g_mutex_unlock (self->lock);

  g_atomic_int_set (&self->seeking, 0);
}

static gboolean
gst_image_freeze_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (gst_pad_get_parent (pad));
  gboolean ret = FALSE;
  GstStructure *s;
  gint fps_n, fps_d;
  GstCaps *othercaps, *intersection;
  guint i, n;

  caps = gst_caps_make_writable (gst_caps_ref (caps));

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
  caps = intersection;
  intersection = othercaps = NULL;

  /* 3. Intersect with downstream peer caps */
  othercaps = gst_pad_peer_get_caps (self->srcpad);
  if (othercaps) {
    intersection = gst_caps_intersect (caps, othercaps);
    GST_DEBUG_OBJECT (pad, "Intersecting: %" GST_PTR_FORMAT, caps);
    GST_DEBUG_OBJECT (pad, "with: %" GST_PTR_FORMAT, othercaps);
    GST_DEBUG_OBJECT (pad, "gave: %" GST_PTR_FORMAT, intersection);
    gst_caps_unref (othercaps);
    gst_caps_unref (caps);
    caps = intersection;
    intersection = othercaps = NULL;
  }

  /* 4. For every candidate check if it's accepted downstream
   * and fixate framerate to nearest 25/1 */
  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstCaps *candidate = gst_caps_new_empty ();
    GstStructure *s = gst_structure_copy (gst_caps_get_structure (caps, i));

    gst_caps_append_structure (candidate, s);
    if (gst_pad_peer_accept_caps (self->srcpad, candidate)) {
      if (gst_structure_has_field_typed (s, "framerate", GST_TYPE_FRACTION) ||
          gst_structure_fixate_field_nearest_fraction (s, "framerate", 25, 1)) {
        gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d);
        if (fps_d != 0) {
          g_mutex_lock (self->lock);
          self->fps_n = fps_n;
          self->fps_d = fps_d;
          g_mutex_unlock (self->lock);
          GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, candidate);
          gst_pad_set_caps (self->srcpad, candidate);
          gst_caps_unref (candidate);
          ret = TRUE;
          goto done;
        } else {
          GST_WARNING_OBJECT (pad, "Invalid caps with framerate %d/%d", fps_n,
              fps_d);
        }
      }
    }
    gst_caps_unref (candidate);
  }

done:
  if (!ret)
    GST_ERROR_OBJECT (pad, "No usable caps found");

  gst_caps_unref (caps);
  gst_object_unref (self);

  return ret;
}

static GstCaps *
gst_image_freeze_sink_getcaps (GstPad * pad)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (gst_pad_get_parent (pad));
  GstCaps *ret, *tmp;
  guint i, n;

  if (GST_PAD_CAPS (pad)) {
    ret = gst_caps_copy (GST_PAD_CAPS (pad));
    goto done;
  }

  tmp = gst_pad_peer_get_caps (self->srcpad);
  if (tmp) {
    ret = gst_caps_intersect (tmp, gst_pad_get_pad_template_caps (pad));
    gst_caps_unref (tmp);
  } else {
    ret = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  n = gst_caps_get_size (ret);
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (ret, i);

    gst_structure_remove_field (s, "framerate");
    gst_structure_set (s, "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT,
        1, NULL);
  }

done:
  gst_object_unref (self);

  GST_LOG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_image_freeze_sink_query (GstPad * pad, GstQuery * query)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (gst_pad_get_parent (pad));
  gboolean ret;
  GstPad *peer = gst_pad_get_peer (self->srcpad);

  GST_LOG_OBJECT (pad, "Handling query of type '%s'",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  if (!peer) {
    GST_INFO_OBJECT (pad, "No peer yet, dropping query");
    ret = FALSE;
  } else {
    ret = gst_pad_query (peer, query);
    gst_object_unref (peer);
  }

  gst_object_unref (self);
  return ret;
}

static GstFlowReturn
gst_image_freeze_sink_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (gst_pad_get_parent (pad));
  GstFlowReturn ret;
  gboolean do_alloc;

  GST_LOG_OBJECT (pad, "Allocating buffer with offset 0x%" G_GINT64_MODIFIER
      "x and size %u with caps: %" GST_PTR_FORMAT, offset, size, caps);

  *buf = NULL;

  g_mutex_lock (self->lock);
  do_alloc = self->buffer == NULL;
  g_mutex_unlock (self->lock);

  if (do_alloc) {
    gboolean seeking = FALSE;

    do {
      GST_PAD_STREAM_LOCK (self->srcpad);
      ret = gst_pad_alloc_buffer (self->srcpad, offset, size, caps, buf);

      seeking = ret == GST_FLOW_WRONG_STATE
          && g_atomic_int_get (&self->seeking);
      GST_PAD_STREAM_UNLOCK (self->srcpad);
    } while (seeking);

    if (G_UNLIKELY (ret != GST_FLOW_OK))
      GST_ERROR_OBJECT (pad, "Allocating buffer failed: %s",
          gst_flow_get_name (ret));
  } else {
    /* Let upstream go EOS if we already have a buffer */
    ret = GST_FLOW_UNEXPECTED;
  }

  gst_object_unref (self);

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
          g_mutex_lock (self->lock);
          if (self->fps_n == 0)
            *dest_value = -1;
          else
            *dest_value =
                gst_util_uint64_scale (src_value, GST_SECOND * self->fps_d,
                self->fps_n);
          g_mutex_unlock (self->lock);
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
          g_mutex_lock (self->lock);
          *dest_value =
              gst_util_uint64_scale (src_value, self->fps_n,
              self->fps_d * GST_SECOND);
          g_mutex_unlock (self->lock);
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

static const GstQueryType *
gst_image_freeze_src_query_type (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_SEEKING,
    GST_QUERY_CONVERT,
    0
  };

  return types;
}

static gboolean
gst_image_freeze_src_query (GstPad * pad, GstQuery * query)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (gst_pad_get_parent (pad));
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
          g_mutex_lock (self->lock);
          position = self->offset;
          g_mutex_unlock (self->lock);
          ret = TRUE;
          break;
        }
        case GST_FORMAT_TIME:{
          g_mutex_lock (self->lock);
          position = self->segment.last_stop;
          g_mutex_unlock (self->lock);
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
          g_mutex_lock (self->lock);
          duration = self->segment.stop;
          g_mutex_unlock (self->lock);
          ret = TRUE;
          break;
        }
        case GST_FORMAT_DEFAULT:{
          g_mutex_lock (self->lock);
          duration = self->segment.stop;
          if (duration != -1)
            duration =
                gst_util_uint64_scale (duration, self->fps_n,
                GST_SECOND * self->fps_d);
          g_mutex_unlock (self->lock);
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
      seekable = (format == GST_FORMAT_TIME || format == GST_FORMAT_DEFAULT);

      gst_query_set_seeking (query, format, seekable, (seekable ? 0 : -1), -1);
      ret = TRUE;
      break;
    }
    default:
      ret = FALSE;
      break;
  }

  gst_object_unref (self);
  return ret;
}


static gboolean
gst_image_freeze_sink_event (GstPad * pad, GstEvent * event)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (gst_pad_get_parent (pad));
  gboolean ret;

  GST_LOG_OBJECT (pad, "Got %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (!self->buffer) {
        /* if we receive EOS before a buffer arrives, then let it pass */
        GST_DEBUG_OBJECT (self, "EOS without input buffer, passing on");
        ret = gst_pad_push_event (self->srcpad, event);
        break;
      }
      /* fall-through */
    case GST_EVENT_NEWSEGMENT:
      GST_DEBUG_OBJECT (pad, "Dropping event");
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_FLUSH_START:
      gst_image_freeze_reset (self);
      /* fall through */
    default:
      ret = gst_pad_push_event (self->srcpad, event);
      break;
  }

  gst_object_unref (self);
  return ret;
}

static gboolean
gst_image_freeze_src_event (GstPad * pad, GstEvent * event)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (gst_pad_get_parent (pad));
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

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);
      gst_event_unref (event);

      flush = ! !(flags & GST_SEEK_FLAG_FLUSH);

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

        g_atomic_int_set (&self->seeking, 1);
        e = gst_event_new_flush_start ();
        gst_pad_push_event (self->srcpad, e);
      } else {
        gst_pad_pause_task (self->srcpad);
      }

      GST_PAD_STREAM_LOCK (self->srcpad);

      g_mutex_lock (self->lock);
      gst_event_replace (&self->close_segment, NULL);
      if (!flush) {
        if (!self->need_segment && self->segment.rate >= 0) {
          self->close_segment =
              gst_event_new_new_segment_full (TRUE, self->segment.rate,
              self->segment.applied_rate, self->segment.format,
              self->segment.start, self->segment.last_stop, self->segment.time);
        } else if (!self->need_segment) {
          gint64 stop;

          if ((stop = self->segment.stop) == -1)
            stop = self->segment.duration;

          self->close_segment =
              gst_event_new_new_segment_full (TRUE, self->segment.rate,
              self->segment.applied_rate, self->segment.format,
              self->segment.last_stop, stop, self->segment.last_stop);
        }
      }

      gst_segment_set_seek (&self->segment, rate, format, flags, start_type,
          start, stop_type, stop, NULL);
      self->need_segment = TRUE;
      last_stop = self->segment.last_stop;

      start_task = self->buffer != NULL;
      g_mutex_unlock (self->lock);

      if (flush) {
        GstEvent *e;

        e = gst_event_new_flush_stop ();
        gst_pad_push_event (self->srcpad, e);
        g_atomic_int_set (&self->seeking, 0);
      }

      if (flags & GST_SEEK_FLAG_SEGMENT) {
        GstMessage *m;

        m = gst_message_new_segment_start (GST_OBJECT (self),
            format, last_stop);
        gst_element_post_message (GST_ELEMENT (self), m);
      }

      GST_PAD_STREAM_UNLOCK (self->srcpad);

      GST_DEBUG_OBJECT (pad, "Seek successful");

      if (start_task) {
        g_mutex_lock (self->lock);

        if (self->buffer != NULL)
          gst_pad_start_task (self->srcpad,
              (GstTaskFunction) gst_image_freeze_src_loop, self->srcpad);

        g_mutex_unlock (self->lock);
      }

      ret = TRUE;
      break;
    }
    case GST_EVENT_FLUSH_START:
      gst_image_freeze_reset (self);
      /* fall through */
    default:
      ret = gst_pad_push_event (self->sinkpad, event);
      break;
  }

  gst_object_unref (self);
  return ret;
}

static GstFlowReturn
gst_image_freeze_sink_chain (GstPad * pad, GstBuffer * buffer)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (GST_PAD_PARENT (pad));

  g_mutex_lock (self->lock);
  if (self->buffer) {
    GST_DEBUG_OBJECT (pad, "Already have a buffer, dropping");
    gst_buffer_unref (buffer);
    g_mutex_unlock (self->lock);
    return GST_FLOW_UNEXPECTED;
  }

  self->buffer = buffer;

  gst_pad_start_task (self->srcpad, (GstTaskFunction) gst_image_freeze_src_loop,
      self->srcpad);
  g_mutex_unlock (self->lock);
  return GST_FLOW_OK;
}

static void
gst_image_freeze_src_loop (GstPad * pad)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (GST_PAD_PARENT (pad));
  GstBuffer *buffer;
  guint64 offset;
  GstClockTime timestamp, timestamp_end;
  gint64 cstart, cstop;
  gboolean in_seg, eos;

  g_mutex_lock (self->lock);
  if (!self->buffer) {
    GST_ERROR_OBJECT (pad, "Have no buffer yet");
    g_mutex_unlock (self->lock);
    gst_pad_pause_task (self->srcpad);
    return;
  }
  buffer = gst_buffer_ref (self->buffer);
  buffer = gst_buffer_make_metadata_writable (buffer);
  g_mutex_unlock (self->lock);

  if (self->close_segment) {
    GST_DEBUG_OBJECT (pad, "Closing previous segment");
    gst_pad_push_event (self->srcpad, self->close_segment);
    self->close_segment = NULL;
  }

  if (self->need_segment) {
    GstEvent *e;

    GST_DEBUG_OBJECT (pad, "Pushing NEWSEGMENT event: %" GST_SEGMENT_FORMAT,
        &self->segment);
    e = gst_event_new_new_segment_full (FALSE, self->segment.rate,
        self->segment.applied_rate, self->segment.format, self->segment.start,
        self->segment.stop, self->segment.start);

    g_mutex_lock (self->lock);
    if (self->segment.rate >= 0) {
      self->offset =
          gst_util_uint64_scale (self->segment.start, self->fps_n,
          self->fps_d * GST_SECOND);
    } else {
      self->offset =
          gst_util_uint64_scale (self->segment.stop, self->fps_n,
          self->fps_d * GST_SECOND);
    }
    g_mutex_unlock (self->lock);

    self->need_segment = FALSE;

    gst_pad_push_event (self->srcpad, e);
  }

  g_mutex_lock (self->lock);
  offset = self->offset;

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

  if (in_seg)
    gst_segment_set_last_stop (&self->segment, GST_FORMAT_TIME, cstart);

  if (self->segment.rate >= 0)
    self->offset++;
  else
    self->offset--;
  g_mutex_unlock (self->lock);

  GST_DEBUG_OBJECT (pad, "Handling buffer with timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (in_seg) {
    GstFlowReturn ret;

    GST_BUFFER_TIMESTAMP (buffer) = cstart;
    GST_BUFFER_DURATION (buffer) = cstop - cstart;
    GST_BUFFER_OFFSET (buffer) = offset;
    GST_BUFFER_OFFSET_END (buffer) = offset + 1;
    gst_buffer_set_caps (buffer, GST_PAD_CAPS (self->srcpad));
    ret = gst_pad_push (self->srcpad, buffer);
    GST_DEBUG_OBJECT (pad, "Pushing buffer resulted in %s",
        gst_flow_get_name (ret));
    if (ret != GST_FLOW_OK)
      gst_pad_pause_task (self->srcpad);
  } else {
    gst_buffer_unref (buffer);
  }

  if (eos) {
    if ((self->segment.flags & GST_SEEK_FLAG_SEGMENT)) {
      GstMessage *m;

      GST_DEBUG_OBJECT (pad, "Sending segment done at end of segment");
      if (self->segment.rate >= 0)
        m = gst_message_new_segment_done (GST_OBJECT_CAST (self),
            GST_FORMAT_TIME, self->segment.stop);
      else
        m = gst_message_new_segment_done (GST_OBJECT_CAST (self),
            GST_FORMAT_TIME, self->segment.start);
      gst_element_post_message (GST_ELEMENT_CAST (self), m);
    } else {
      GST_DEBUG_OBJECT (pad, "Sending EOS at end of segment");
      gst_pad_push_event (self->srcpad, gst_event_new_eos ());
    }
    gst_pad_pause_task (self->srcpad);
  }
}

static GstStateChangeReturn
gst_image_freeze_change_state (GstElement * element, GstStateChange transition)
{
  GstImageFreeze *self = GST_IMAGE_FREEZE (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_image_freeze_reset (self);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_pad_stop_task (self->srcpad);
      gst_image_freeze_reset (self);
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_image_freeze_debug, "imagefreeze", 0,
      "imagefreeze element");

  if (!gst_element_register (plugin, "imagefreeze", GST_RANK_NONE,
          GST_TYPE_IMAGE_FREEZE))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "imagefreeze",
    "Still frame stream generator",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
