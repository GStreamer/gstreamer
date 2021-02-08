/* GStreamer
 * Copyright (C) 2021 Sebastian Dröge <sebastian@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/audio/audio.h>

#include "gstajacommon.h"
#include "gstajasrcdemux.h"

GST_DEBUG_CATEGORY_STATIC(gst_aja_src_demux_debug);
#define GST_CAT_DEFAULT gst_aja_src_demux_debug

static GstStaticPadTemplate video_src_template = GST_STATIC_PAD_TEMPLATE(
    "video", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static GstStaticPadTemplate audio_src_template = GST_STATIC_PAD_TEMPLATE(
    "audio", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("audio/x-raw"));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static GstFlowReturn gst_aja_src_demux_sink_chain(GstPad *pad,
                                                  GstObject *parent,
                                                  GstBuffer *buffer);
static gboolean gst_aja_src_demux_sink_event(GstPad *pad, GstObject *parent,
                                             GstEvent *event);

#define parent_class gst_aja_src_demux_parent_class
G_DEFINE_TYPE(GstAjaSrcDemux, gst_aja_src_demux, GST_TYPE_ELEMENT);

static void gst_aja_src_demux_class_init(GstAjaSrcDemuxClass *klass) {
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

  gst_element_class_add_static_pad_template(element_class, &sink_template);
  gst_element_class_add_static_pad_template(element_class, &video_src_template);
  gst_element_class_add_static_pad_template(element_class, &audio_src_template);

  gst_element_class_set_static_metadata(
      element_class, "AJA audio/video source demuxer", "Audio/Video/Demux",
      "Demuxes audio/video from video buffers",
      "Sebastian Dröge <sebastian@centricular.com>");

  GST_DEBUG_CATEGORY_INIT(gst_aja_src_demux_debug, "ajasrcdemux", 0,
                          "AJA source demuxer");
}

static void gst_aja_src_demux_init(GstAjaSrcDemux *self) {
  self->sink = gst_pad_new_from_static_template(&sink_template, "sink");
  gst_pad_set_chain_function(self->sink,
                             GST_DEBUG_FUNCPTR(gst_aja_src_demux_sink_chain));
  gst_pad_set_event_function(self->sink,
                             GST_DEBUG_FUNCPTR(gst_aja_src_demux_sink_event));
  gst_element_add_pad(GST_ELEMENT(self), self->sink);

  self->audio_src =
      gst_pad_new_from_static_template(&audio_src_template, "audio");
  gst_pad_use_fixed_caps(self->audio_src);
  gst_element_add_pad(GST_ELEMENT(self), self->audio_src);

  self->video_src =
      gst_pad_new_from_static_template(&video_src_template, "video");
  gst_pad_use_fixed_caps(self->video_src);
  gst_element_add_pad(GST_ELEMENT(self), self->video_src);
}

static GstFlowReturn gst_aja_src_demux_sink_chain(GstPad *pad,
                                                  GstObject *parent,
                                                  GstBuffer *buffer) {
  GstAjaSrcDemux *self = GST_AJA_SRC_DEMUX(parent);
  GstAjaAudioMeta *meta = gst_buffer_get_aja_audio_meta(buffer);
  GstFlowReturn audio_flow_ret = GST_FLOW_OK;
  GstFlowReturn video_flow_ret = GST_FLOW_OK;

  if (meta) {
    GstBuffer *audio_buffer;
    buffer = gst_buffer_make_writable(buffer);
    meta = gst_buffer_get_aja_audio_meta(buffer);
    audio_buffer = gst_buffer_ref(meta->buffer);
    gst_buffer_remove_meta(buffer, GST_META_CAST(meta));

    audio_flow_ret = gst_pad_push(self->audio_src, audio_buffer);
  } else {
    GstEvent *event =
        gst_event_new_gap(GST_BUFFER_PTS(buffer), GST_BUFFER_DURATION(buffer));
    gst_pad_push_event(self->audio_src, event);
  }

  video_flow_ret = gst_pad_push(self->video_src, buffer);

  // Combine flows the way it makes sense
  if (video_flow_ret == GST_FLOW_NOT_LINKED &&
      audio_flow_ret == GST_FLOW_NOT_LINKED)
    return GST_FLOW_NOT_LINKED;
  if (video_flow_ret == GST_FLOW_EOS && audio_flow_ret == GST_FLOW_EOS)
    return GST_FLOW_EOS;
  if (video_flow_ret == GST_FLOW_FLUSHING ||
      video_flow_ret <= GST_FLOW_NOT_NEGOTIATED)
    return video_flow_ret;
  if (audio_flow_ret == GST_FLOW_FLUSHING ||
      audio_flow_ret <= GST_FLOW_NOT_NEGOTIATED)
    return audio_flow_ret;
  return GST_FLOW_OK;
}

static gboolean gst_aja_src_demux_sink_event(GstPad *pad, GstObject *parent,
                                             GstEvent *event) {
  GstAjaSrcDemux *self = GST_AJA_SRC_DEMUX(parent);

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
      GstCaps *caps;
      GstStructure *s;
      GstAudioInfo audio_info;
      gint audio_channels = 0;

      gst_event_parse_caps(event, &caps);
      s = gst_caps_get_structure(caps, 0);

      gst_structure_get_int(s, "audio-channels", &audio_channels);

      GstCaps *audio_caps, *video_caps;

      gst_audio_info_init(&audio_info);
      gst_audio_info_set_format(&audio_info, GST_AUDIO_FORMAT_S32LE, 48000,
                                audio_channels ? audio_channels : 1, NULL);
      audio_caps = gst_audio_info_to_caps(&audio_info);
      gst_pad_set_caps(self->audio_src, audio_caps);
      gst_caps_unref(audio_caps);

      video_caps = gst_caps_ref(caps);
      gst_event_unref(event);
      video_caps = gst_caps_make_writable(video_caps);
      s = gst_caps_get_structure(video_caps, 0);
      gst_structure_remove_field(s, "audio-channels");
      gst_pad_set_caps(self->video_src, video_caps);
      gst_caps_unref(video_caps);

      return TRUE;
    }
    default:
      return gst_pad_event_default(pad, parent, event);
  }
}
