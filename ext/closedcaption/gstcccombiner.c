/*
 * GStreamer
 * Copyright (C) 2018 Sebastian Dröge <sebastian@centricular.com>
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


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>
#include <string.h>

#include "gstcccombiner.h"

GST_DEBUG_CATEGORY_STATIC (gst_cc_combiner_debug);
#define GST_CAT_DEFAULT gst_cc_combiner_debug

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate captiontemplate =
    GST_STATIC_PAD_TEMPLATE ("caption",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS
    ("closedcaption/x-cea-608,format={ (string) raw, (string) s334-1a}; "
        "closedcaption/x-cea-708,format={ (string) cc_data, (string) cdp }"));

G_DEFINE_TYPE (GstCCCombiner, gst_cc_combiner, GST_TYPE_AGGREGATOR);
#define parent_class gst_cc_combiner_parent_class

typedef struct
{
  GstVideoCaptionType caption_type;
  GstBuffer *buffer;
} CaptionData;

static void
caption_data_clear (CaptionData * data)
{
  gst_buffer_unref (data->buffer);
}

static void
gst_cc_combiner_finalize (GObject * object)
{
  GstCCCombiner *self = GST_CCCOMBINER (object);

  g_array_unref (self->current_frame_captions);
  self->current_frame_captions = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

#define GST_FLOW_NEED_DATA GST_FLOW_CUSTOM_SUCCESS

static GstFlowReturn
gst_cc_combiner_collect_captions (GstCCCombiner * self)
{
  GstAggregatorPad *caption_pad;
  GstBuffer *video_buf;

  g_assert (self->current_video_buffer != NULL);

  caption_pad =
      GST_AGGREGATOR_PAD_CAST (gst_element_get_static_pad (GST_ELEMENT_CAST
          (self), "caption"));

  /* No caption pad, forward buffer directly */
  if (!caption_pad) {
    GST_LOG_OBJECT (self, "No caption pad, passing through video");
    video_buf = self->current_video_buffer;
    self->current_video_buffer = NULL;
    return gst_aggregator_finish_buffer (GST_AGGREGATOR_CAST (self), video_buf);
  }

  GST_LOG_OBJECT (self, "Trying to collect captions for queued video buffer");
  do {
    GstBuffer *caption_buf;
    GstClockTime caption_time;
    CaptionData caption_data;

    caption_buf = gst_aggregator_pad_peek_buffer (caption_pad);
    if (!caption_buf) {
      if (gst_aggregator_pad_is_eos (caption_pad)) {
        GST_DEBUG_OBJECT (self, "Caption pad is EOS, we're done");
        break;
      } else {
        GST_DEBUG_OBJECT (self, "Need more caption data");
        gst_object_unref (caption_pad);
        return GST_FLOW_NEED_DATA;
      }
    }

    caption_time = GST_BUFFER_PTS (caption_buf);
    if (!GST_CLOCK_TIME_IS_VALID (caption_time)) {
      GST_ERROR_OBJECT (self, "Caption buffer without PTS");

      gst_buffer_unref (caption_buf);
      gst_object_unref (caption_pad);

      return GST_FLOW_ERROR;
    }

    caption_time =
        gst_segment_to_running_time (&caption_pad->segment, GST_FORMAT_TIME,
        caption_time);

    if (!GST_CLOCK_TIME_IS_VALID (caption_time)) {
      GST_DEBUG_OBJECT (self, "Caption buffer outside segment, dropping");

      gst_aggregator_pad_drop_buffer (caption_pad);
      gst_buffer_unref (caption_buf);

      continue;
    }

    /* Collected all caption buffers for this video buffer */
    if (caption_time >= self->current_video_running_time_end) {
      gst_buffer_unref (caption_buf);
      break;
    } else if (caption_time < self->current_video_running_time) {
      GST_DEBUG_OBJECT (self,
          "Caption buffer before current video frame, dropping");

      gst_aggregator_pad_drop_buffer (caption_pad);
      gst_buffer_unref (caption_buf);
      continue;
    }

    /* This caption buffer has to be collected */
    GST_LOG_OBJECT (self,
        "Collecting caption buffer %p %" GST_TIME_FORMAT " for video buffer %p",
        caption_buf, GST_TIME_ARGS (caption_time), self->current_video_buffer);
    caption_data.caption_type = self->current_caption_type;
    caption_data.buffer = caption_buf;
    g_array_append_val (self->current_frame_captions, caption_data);
    gst_aggregator_pad_drop_buffer (caption_pad);
  } while (TRUE);

  if (self->current_frame_captions->len > 0) {
    guint i;

    GST_LOG_OBJECT (self, "Attaching %u captions to buffer %p",
        self->current_frame_captions->len, self->current_video_buffer);
    video_buf = gst_buffer_make_writable (self->current_video_buffer);
    self->current_video_buffer = NULL;

    for (i = 0; i < self->current_frame_captions->len; i++) {
      CaptionData *caption_data =
          &g_array_index (self->current_frame_captions, CaptionData, i);
      GstMapInfo map;

      gst_buffer_map (caption_data->buffer, &map, GST_MAP_READ);
      gst_buffer_add_video_caption_meta (video_buf, caption_data->caption_type,
          map.data, map.size);
      gst_buffer_unmap (caption_data->buffer, &map);
    }

    g_array_set_size (self->current_frame_captions, 0);
  } else {
    GST_LOG_OBJECT (self, "No captions for buffer %p",
        self->current_video_buffer);
    video_buf = self->current_video_buffer;
    self->current_video_buffer = NULL;
  }

  gst_object_unref (caption_pad);

  return gst_aggregator_finish_buffer (GST_AGGREGATOR_CAST (self), video_buf);
}

static GstFlowReturn
gst_cc_combiner_aggregate (GstAggregator * aggregator, gboolean timeout)
{
  GstCCCombiner *self = GST_CCCOMBINER (aggregator);
  GstFlowReturn flow_ret = GST_FLOW_OK;

  /* If we have no current video buffer, queue one. If we have one but
   * its end running time is not known yet, try to determine it from the
   * next video buffer */
  if (!self->current_video_buffer
      || !GST_CLOCK_TIME_IS_VALID (self->current_video_running_time_end)) {
    GstAggregatorPad *video_pad;
    GstClockTime video_start;
    GstBuffer *video_buf;

    video_pad =
        GST_AGGREGATOR_PAD_CAST (gst_element_get_static_pad (GST_ELEMENT_CAST
            (aggregator), "sink"));
    video_buf = gst_aggregator_pad_peek_buffer (video_pad);
    if (!video_buf) {
      if (gst_aggregator_pad_is_eos (video_pad)) {
        GST_DEBUG_OBJECT (aggregator, "Video pad is EOS, we're done");

        /* Assume that this buffer ends where it started +50ms (25fps) and handle it */
        if (self->current_video_buffer) {
          self->current_video_running_time_end =
              self->current_video_running_time + 50 * GST_MSECOND;
          flow_ret = gst_cc_combiner_collect_captions (self);
        }

        /* If we collected all captions for the remaining video frame we're
         * done, otherwise get called another time and go directly into the
         * outer branch for finishing the current video frame */
        if (flow_ret == GST_FLOW_NEED_DATA)
          flow_ret = GST_FLOW_OK;
        else
          flow_ret = GST_FLOW_EOS;
      } else {
        GST_LOG_OBJECT (aggregator, "Video pad has no data, waiting");
        flow_ret = GST_FLOW_OK;
      }

      gst_object_unref (video_pad);
      return flow_ret;
    }

    video_start = GST_BUFFER_PTS (video_buf);
    if (!GST_CLOCK_TIME_IS_VALID (video_start)) {
      gst_buffer_unref (video_buf);
      gst_object_unref (video_pad);

      GST_ERROR_OBJECT (aggregator, "Video buffer without PTS");

      return GST_FLOW_ERROR;
    }

    video_start =
        gst_segment_to_running_time (&video_pad->segment, GST_FORMAT_TIME,
        video_start);
    if (!GST_CLOCK_TIME_IS_VALID (video_start)) {
      GST_DEBUG_OBJECT (aggregator, "Buffer outside segment, dropping");
      gst_aggregator_pad_drop_buffer (video_pad);
      gst_buffer_unref (video_buf);
      gst_object_unref (video_pad);
      return GST_FLOW_OK;
    }

    if (self->current_video_buffer) {
      /* If we already have a video buffer just update the current end running
       * time accordingly. That's what was missing and why we got here */
      self->current_video_running_time_end = video_start;
      gst_buffer_unref (video_buf);
      GST_LOG_OBJECT (self,
          "Determined end timestamp for video buffer: %p %" GST_TIME_FORMAT
          " - %" GST_TIME_FORMAT, self->current_video_buffer,
          GST_TIME_ARGS (self->current_video_running_time),
          GST_TIME_ARGS (self->current_video_running_time_end));
    } else {
      /* Otherwise we had no buffer queued currently. Let's do that now
       * so that we can collect captions for it */
      gst_buffer_replace (&self->current_video_buffer, video_buf);
      self->current_video_running_time = video_start;
      gst_aggregator_pad_drop_buffer (video_pad);
      gst_buffer_unref (video_buf);

      if (GST_BUFFER_DURATION_IS_VALID (video_buf)) {
        GstClockTime end_time =
            GST_BUFFER_PTS (video_buf) + GST_BUFFER_DURATION (video_buf);
        if (video_pad->segment.stop != -1 && end_time > video_pad->segment.stop)
          end_time = video_pad->segment.stop;
        self->current_video_running_time_end =
            gst_segment_to_running_time (&video_pad->segment, GST_FORMAT_TIME,
            end_time);
      } else if (self->video_fps_n != 0 && self->video_fps_d != 0) {
        GstClockTime end_time =
            GST_BUFFER_PTS (video_buf) + gst_util_uint64_scale_int (GST_SECOND,
            self->video_fps_d, self->video_fps_n);
        if (video_pad->segment.stop != -1 && end_time > video_pad->segment.stop)
          end_time = video_pad->segment.stop;
        self->current_video_running_time_end =
            gst_segment_to_running_time (&video_pad->segment, GST_FORMAT_TIME,
            end_time);
      } else {
        self->current_video_running_time_end = GST_CLOCK_TIME_NONE;
      }

      GST_LOG_OBJECT (self,
          "Queued new video buffer: %p %" GST_TIME_FORMAT " - %"
          GST_TIME_FORMAT, self->current_video_buffer,
          GST_TIME_ARGS (self->current_video_running_time),
          GST_TIME_ARGS (self->current_video_running_time_end));
    }

    gst_object_unref (video_pad);
  }

  /* At this point we have a video buffer queued and can start collecting
   * caption buffers for it */
  g_assert (self->current_video_buffer != NULL);
  g_assert (GST_CLOCK_TIME_IS_VALID (self->current_video_running_time));
  g_assert (GST_CLOCK_TIME_IS_VALID (self->current_video_running_time_end));

  flow_ret = gst_cc_combiner_collect_captions (self);

  /* Only if we collected all captions we replace the current video buffer
   * with NULL and continue with the next one on the next call */
  if (flow_ret == GST_FLOW_NEED_DATA) {
    flow_ret = GST_FLOW_OK;
  } else {
    gst_buffer_replace (&self->current_video_buffer, NULL);
    self->current_video_running_time = self->current_video_running_time_end =
        GST_CLOCK_TIME_NONE;
  }

  return flow_ret;
}

static gboolean
gst_cc_combiner_sink_event (GstAggregator * aggregator,
    GstAggregatorPad * agg_pad, GstEvent * event)
{
  GstCCCombiner *self = GST_CCCOMBINER (aggregator);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      GstCaps *caps;
      GstStructure *s;

      gst_event_parse_caps (event, &caps);
      s = gst_caps_get_structure (caps, 0);

      if (strcmp (GST_OBJECT_NAME (agg_pad), "caption") == 0) {
        self->current_caption_type = gst_video_caption_type_from_caps (caps);
      } else {
        if (!gst_structure_get_fraction (s, "framerate", &self->video_fps_n,
                &self->video_fps_d))
          self->video_fps_n = self->video_fps_d = 0;

        gst_aggregator_set_src_caps (aggregator, caps);
      }

      break;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_event (aggregator, agg_pad,
      event);
}

static gboolean
gst_cc_combiner_stop (GstAggregator * aggregator)
{
  GstCCCombiner *self = GST_CCCOMBINER (aggregator);

  self->video_fps_n = self->video_fps_d = 0;
  self->current_video_running_time = self->current_video_running_time_end =
      GST_CLOCK_TIME_NONE;
  gst_buffer_replace (&self->current_video_buffer, NULL);

  g_array_set_size (self->current_frame_captions, 0);
  self->current_caption_type = GST_VIDEO_CAPTION_TYPE_UNKNOWN;

  return TRUE;
}

static GstFlowReturn
gst_cc_combiner_flush (GstAggregator * aggregator)
{
  GstCCCombiner *self = GST_CCCOMBINER (aggregator);

  self->current_video_running_time = self->current_video_running_time_end =
      GST_CLOCK_TIME_NONE;
  gst_buffer_replace (&self->current_video_buffer, NULL);

  g_array_set_size (self->current_frame_captions, 0);

  return GST_FLOW_OK;
}

static GstAggregatorPad *
gst_cc_combiner_create_new_pad (GstAggregator * aggregator,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstCCCombiner *self = GST_CCCOMBINER (aggregator);
  GstAggregatorPad *agg_pad;

  if (templ->direction != GST_PAD_SINK)
    return NULL;

  if (templ->presence != GST_PAD_REQUEST)
    return NULL;

  if (strcmp (templ->name_template, "caption") != 0)
    return NULL;

  GST_OBJECT_LOCK (self);
  agg_pad = g_object_new (GST_TYPE_AGGREGATOR_PAD,
      "name", "caption", "direction", GST_PAD_SINK, "template", templ, NULL);
  self->current_caption_type = GST_VIDEO_CAPTION_TYPE_UNKNOWN;
  GST_OBJECT_UNLOCK (self);

  return agg_pad;
}

static void
gst_cc_combiner_class_init (GstCCCombinerClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstAggregatorClass *aggregator_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  aggregator_class = (GstAggregatorClass *) klass;

  gobject_class->finalize = gst_cc_combiner_finalize;

  gst_element_class_set_static_metadata (gstelement_class,
      "Closed Caption Combiner",
      "Filter",
      "Combines GstVideoCaptionMeta with video input stream",
      "Sebastian Dröge <sebastian@centricular.com>");

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &sinktemplate, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &srctemplate, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &captiontemplate, GST_TYPE_AGGREGATOR_PAD);

  aggregator_class->aggregate = gst_cc_combiner_aggregate;
  aggregator_class->stop = gst_cc_combiner_stop;
  aggregator_class->flush = gst_cc_combiner_flush;
  aggregator_class->create_new_pad = gst_cc_combiner_create_new_pad;
  aggregator_class->sink_event = gst_cc_combiner_sink_event;

  GST_DEBUG_CATEGORY_INIT (gst_cc_combiner_debug, "cccombiner",
      0, "Closed Caption combiner");
}

static void
gst_cc_combiner_init (GstCCCombiner * self)
{
  GstPadTemplate *templ;
  GstAggregatorPad *agg_pad;

  templ = gst_static_pad_template_get (&sinktemplate);
  agg_pad = g_object_new (GST_TYPE_AGGREGATOR_PAD,
      "name", "sink", "direction", GST_PAD_SINK, "template", templ, NULL);
  gst_object_unref (templ);
  gst_element_add_pad (GST_ELEMENT_CAST (self), GST_PAD_CAST (agg_pad));

  self->current_frame_captions =
      g_array_new (FALSE, FALSE, sizeof (CaptionData));
  g_array_set_clear_func (self->current_frame_captions,
      (GDestroyNotify) caption_data_clear);

  self->current_video_running_time = self->current_video_running_time_end =
      GST_CLOCK_TIME_NONE;

  self->current_caption_type = GST_VIDEO_CAPTION_TYPE_UNKNOWN;
}
