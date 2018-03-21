/* 
 * Copyright 2006, 2007, 2008, 2009, 2010 Fluendo S.A. 
 *  Authors: Jan Schmidt <jan@fluendo.com>
 *           Kapil Agrawal <kapil@fluendo.com>
 *           Julien Moutte <julien@fluendo.com>
 *
 * Copyright (C) 2011 Jan Schmidt <thaytan@noraisin.net>
 *
 * This library is licensed under 4 different licenses and you
 * can choose to use it under the terms of any one of them. The
 * four licenses are the MPL 1.1, the LGPL, the GPL and the MIT
 * license.
 *
 * MPL:
 * 
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * LGPL:
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
 *
 * GPL:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * MIT:
 *
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>

#include <gst/tag/tag.h>
#include <gst/video/video.h>
#include <gst/mpegts/mpegts.h>
#include <gst/pbutils/pbutils.h>

#include "mpegtsmux.h"

#include "mpegtsmux_aac.h"
#include "mpegtsmux_ttxt.h"
#include "mpegtsmux_opus.h"
#include "mpegtsmux_jpeg2000.h"
#include <gst/videoparsers/gstjpeg2000parse.h>
#include <gst/video/video-color.h>

GST_DEBUG_CATEGORY (mpegtsmux_debug);
#define GST_CAT_DEFAULT mpegtsmux_debug

#define COLLECT_DATA_PAD(collect_data) (((GstCollectData *)(collect_data))->pad)

enum
{
  PROP_0,
  PROP_PROG_MAP,
  PROP_M2TS_MODE,
  PROP_PAT_INTERVAL,
  PROP_PMT_INTERVAL,
  PROP_ALIGNMENT,
  PROP_SI_INTERVAL
};

#define MPEGTSMUX_DEFAULT_ALIGNMENT    -1
#define MPEGTSMUX_DEFAULT_M2TS         FALSE

static GstStaticPadTemplate mpegtsmux_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/mpeg, "
        "parsed = (boolean) TRUE, "
        "mpegversion = (int) { 1, 2, 4 }, "
        "systemstream = (boolean) false; "
        "video/x-dirac;"
        "image/x-jpc;"
        "video/x-h264,stream-format=(string)byte-stream,"
        "alignment=(string){au, nal}; "
        "video/x-h265,stream-format=(string)byte-stream,"
        "alignment=(string){au, nal}; "
        "audio/mpeg, "
        "parsed = (boolean) TRUE, "
        "mpegversion = (int) { 1, 2 };"
        "audio/mpeg, "
        "framed = (boolean) TRUE, "
        "mpegversion = (int) 4, stream-format = (string) adts;"
        "audio/mpeg, "
        "mpegversion = (int) 4, stream-format = (string) raw;"
        "audio/x-lpcm, "
        "width = (int) { 16, 20, 24 }, "
        "rate = (int) { 48000, 96000 }, "
        "channels = (int) [ 1, 8 ], "
        "dynamic_range = (int) [ 0, 255 ], "
        "emphasis = (boolean) { FALSE, TRUE }, "
        "mute = (boolean) { FALSE, TRUE }; "
        "audio/x-ac3, framed = (boolean) TRUE;"
        "audio/x-dts, framed = (boolean) TRUE;"
        "audio/x-opus, "
        "channels = (int) [1, 8], "
        "channel-mapping-family = (int) {0, 1};"
        "subpicture/x-dvb; application/x-teletext; meta/x-klv, parsed=true;"
        "image/x-jpc, profile = (int)[0, 49151];"));

static GstStaticPadTemplate mpegtsmux_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts, "
        "systemstream = (boolean) true, " "packetsize = (int) { 188, 192} ")
    );

static void gst_mpegtsmux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpegtsmux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void mpegtsmux_reset (MpegTsMux * mux, gboolean alloc);
static void mpegtsmux_dispose (GObject * object);
static void alloc_packet_cb (GstBuffer ** _buf, void *user_data);
static gboolean new_packet_cb (GstBuffer * buf, void *user_data,
    gint64 new_pcr);
static void release_buffer_cb (guint8 * data, void *user_data);
static GstFlowReturn mpegtsmux_collect_packet (MpegTsMux * mux,
    GstBuffer * buf);
static GstFlowReturn mpegtsmux_push_packets (MpegTsMux * mux, gboolean force);
static gboolean new_packet_m2ts (MpegTsMux * mux, GstBuffer * buf,
    gint64 new_pcr);

static void mpegtsmux_prepare_srcpad (MpegTsMux * mux);
GstFlowReturn mpegtsmux_clip_inc_running_time (GstCollectPads * pads,
    GstCollectData * cdata, GstBuffer * buf, GstBuffer ** outbuf,
    gpointer user_data);
static GstFlowReturn mpegtsmux_collected_buffer (GstCollectPads * pads,
    GstCollectData * data, GstBuffer * buf, MpegTsMux * mux);

static gboolean mpegtsmux_sink_event (GstCollectPads * pads,
    GstCollectData * data, GstEvent * event, gpointer user_data);
static GstPad *mpegtsmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void mpegtsmux_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn mpegtsmux_change_state (GstElement * element,
    GstStateChange transition);
static gboolean mpegtsmux_send_event (GstElement * element, GstEvent * event);
static void mpegtsmux_set_header_on_caps (MpegTsMux * mux);
static gboolean mpegtsmux_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

#if 0
static void mpegtsmux_set_index (GstElement * element, GstIndex * index);
static GstIndex *mpegtsmux_get_index (GstElement * element);

static GstFormat pts_format;
static GstFormat spn_format;
#endif

typedef struct
{
  GstMapInfo map_info;
  GstBuffer *buffer;
} StreamData;

G_DEFINE_TYPE (MpegTsMux, mpegtsmux, GST_TYPE_ELEMENT)

/* Takes over the ref on the buffer */
     static StreamData *stream_data_new (GstBuffer * buffer)
{
  StreamData *res = g_new (StreamData, 1);
  res->buffer = buffer;
  gst_buffer_map (buffer, &(res->map_info), GST_MAP_READ);

  return res;
}

static void
stream_data_free (StreamData * data)
{
  if (data) {
    gst_buffer_unmap (data->buffer, &data->map_info);
    gst_buffer_unref (data->buffer);
    g_free (data);
  }
}

#define parent_class mpegtsmux_parent_class

static void
mpegtsmux_class_init (MpegTsMuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gst_element_class_add_static_pad_template (gstelement_class,
      &mpegtsmux_sink_factory);
  gst_element_class_add_static_pad_template (gstelement_class,
      &mpegtsmux_src_factory);

  gst_element_class_set_static_metadata (gstelement_class,
      "MPEG Transport Stream Muxer", "Codec/Muxer",
      "Multiplexes media streams into an MPEG Transport Stream",
      "Fluendo <contact@fluendo.com>");

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_mpegtsmux_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_mpegtsmux_get_property);
  gobject_class->dispose = mpegtsmux_dispose;

  gstelement_class->request_new_pad = mpegtsmux_request_new_pad;
  gstelement_class->release_pad = mpegtsmux_release_pad;
  gstelement_class->change_state = mpegtsmux_change_state;
  gstelement_class->send_event = mpegtsmux_send_event;

#if 0
  gstelement_class->set_index = GST_DEBUG_FUNCPTR (mpegtsmux_set_index);
  gstelement_class->get_index = GST_DEBUG_FUNCPTR (mpegtsmux_get_index);
#endif

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PROG_MAP,
      g_param_spec_boxed ("prog-map", "Program map",
          "A GstStructure specifies the mapping from elementary streams to programs",
          GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_M2TS_MODE,
      g_param_spec_boolean ("m2ts-mode", "M2TS(192 bytes) Mode",
          "Set to TRUE to output Blu-Ray disc format with 192 byte packets. "
          "FALSE for standard TS format with 188 byte packets.",
          MPEGTSMUX_DEFAULT_M2TS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PAT_INTERVAL,
      g_param_spec_uint ("pat-interval", "PAT interval",
          "Set the interval (in ticks of the 90kHz clock) for writing out the PAT table",
          1, G_MAXUINT, TSMUX_DEFAULT_PAT_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PMT_INTERVAL,
      g_param_spec_uint ("pmt-interval", "PMT interval",
          "Set the interval (in ticks of the 90kHz clock) for writing out the PMT table",
          1, G_MAXUINT, TSMUX_DEFAULT_PMT_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ALIGNMENT,
      g_param_spec_int ("alignment", "packet alignment",
          "Number of packets per buffer (padded with dummy packets on EOS) "
          "(-1 = auto, 0 = all available packets, 7 for UDP streaming)",
          -1, G_MAXINT, MPEGTSMUX_DEFAULT_ALIGNMENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SI_INTERVAL,
      g_param_spec_uint ("si-interval", "SI interval",
          "Set the interval (in ticks of the 90kHz clock) for writing out the Service"
          "Information tables", 1, G_MAXUINT, TSMUX_DEFAULT_SI_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
mpegtsmux_init (MpegTsMux * mux)
{
  mux->srcpad =
      gst_pad_new_from_static_template (&mpegtsmux_src_factory, "src");
  gst_pad_use_fixed_caps (mux->srcpad);
  gst_pad_set_event_function (mux->srcpad,
      GST_DEBUG_FUNCPTR (mpegtsmux_src_event));
  gst_element_add_pad (GST_ELEMENT (mux), mux->srcpad);

  mux->collect = gst_collect_pads_new ();
  gst_collect_pads_set_buffer_function (mux->collect,
      (GstCollectPadsBufferFunction)
      GST_DEBUG_FUNCPTR (mpegtsmux_collected_buffer), mux);

  gst_collect_pads_set_event_function (mux->collect,
      (GstCollectPadsEventFunction) GST_DEBUG_FUNCPTR (mpegtsmux_sink_event),
      mux);
  gst_collect_pads_set_clip_function (mux->collect, (GstCollectPadsClipFunction)
      GST_DEBUG_FUNCPTR (mpegtsmux_clip_inc_running_time), mux);

  mux->adapter = gst_adapter_new ();
  mux->out_adapter = gst_adapter_new ();

  /* properties */
  mux->m2ts_mode = MPEGTSMUX_DEFAULT_M2TS;
  mux->pat_interval = TSMUX_DEFAULT_PAT_INTERVAL;
  mux->pmt_interval = TSMUX_DEFAULT_PMT_INTERVAL;
  mux->si_interval = TSMUX_DEFAULT_SI_INTERVAL;
  mux->prog_map = NULL;
  mux->alignment = MPEGTSMUX_DEFAULT_ALIGNMENT;

  /* initial state */
  mpegtsmux_reset (mux, TRUE);
}

static void
mpegtsmux_pad_reset (MpegTsPadData * pad_data)
{
  pad_data->dts = GST_CLOCK_STIME_NONE;
  pad_data->prog_id = -1;
#if 0
  pad_data->prog_id = -1;
  pad_data->element_index_writer_id = -1;
#endif

  if (pad_data->free_func)
    pad_data->free_func (pad_data->prepare_data);
  pad_data->prepare_data = NULL;
  pad_data->prepare_func = NULL;
  pad_data->free_func = NULL;

  if (pad_data->codec_data)
    gst_buffer_replace (&pad_data->codec_data, NULL);

  /* reference owned elsewhere */
  pad_data->stream = NULL;
  pad_data->prog = NULL;

  if (pad_data->language) {
    g_free (pad_data->language);
    pad_data->language = NULL;
  }

}

static void
mpegtsmux_reset (MpegTsMux * mux, gboolean alloc)
{
  GstBuffer *buf;
  GSList *walk;

  mux->first = TRUE;
  mux->last_flow_ret = GST_FLOW_OK;
  mux->previous_pcr = -1;
  mux->previous_offset = 0;
  mux->pcr_rate_num = mux->pcr_rate_den = 1;
  mux->last_ts = 0;
  mux->is_delta = TRUE;
  mux->is_header = FALSE;

  mux->streamheader_sent = FALSE;
  mux->pending_key_unit_ts = GST_CLOCK_TIME_NONE;
  gst_event_replace (&mux->force_key_unit_event, NULL);
#if 0
  mux->spn_count = 0;

  if (mux->element_index) {
    gst_object_unref (mux->element_index);
    mux->element_index = NULL;
  }
#endif
  if (mux->adapter)
    gst_adapter_clear (mux->adapter);
  if (mux->out_adapter)
    gst_adapter_clear (mux->out_adapter);

  if (mux->tsmux) {
    tsmux_free (mux->tsmux);
    mux->tsmux = NULL;
  }

  if (mux->programs) {
    g_hash_table_destroy (mux->programs);
  }
  mux->programs = g_hash_table_new (g_direct_hash, g_direct_equal);

  while ((buf = g_queue_pop_head (&mux->streamheader)))
    gst_buffer_unref (buf);

  gst_event_replace (&mux->force_key_unit_event, NULL);
  gst_buffer_replace (&mux->out_buffer, NULL);

  if (mux->collect) {
    GST_COLLECT_PADS_STREAM_LOCK (mux->collect);
    for (walk = mux->collect->data; walk != NULL; walk = g_slist_next (walk))
      mpegtsmux_pad_reset ((MpegTsPadData *) walk->data);
    GST_COLLECT_PADS_STREAM_UNLOCK (mux->collect);
  }

  if (alloc) {
    mux->tsmux = tsmux_new ();
    tsmux_set_write_func (mux->tsmux, new_packet_cb, mux);
    tsmux_set_alloc_func (mux->tsmux, alloc_packet_cb, mux);
  }
}

static void
mpegtsmux_dispose (GObject * object)
{
  MpegTsMux *mux = GST_MPEG_TSMUX (object);

  mpegtsmux_reset (mux, FALSE);

  if (mux->adapter) {
    g_object_unref (mux->adapter);
    mux->adapter = NULL;
  }
  if (mux->out_adapter) {
    g_object_unref (mux->out_adapter);
    mux->out_adapter = NULL;
  }
  if (mux->collect) {
    gst_object_unref (mux->collect);
    mux->collect = NULL;
  }
  if (mux->prog_map) {
    gst_structure_free (mux->prog_map);
    mux->prog_map = NULL;
  }
  if (mux->programs) {
    g_hash_table_destroy (mux->programs);
    mux->programs = NULL;
  }
  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_mpegtsmux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  MpegTsMux *mux = GST_MPEG_TSMUX (object);
  GSList *walk;

  switch (prop_id) {
    case PROP_M2TS_MODE:
      /*set incase if the output stream need to be of 192 bytes */
      mux->m2ts_mode = g_value_get_boolean (value);
      break;
    case PROP_PROG_MAP:
    {
      const GstStructure *s = gst_value_get_structure (value);
      if (mux->prog_map) {
        gst_structure_free (mux->prog_map);
      }
      if (s)
        mux->prog_map = gst_structure_copy (s);
      else
        mux->prog_map = NULL;
      break;
    }
    case PROP_PAT_INTERVAL:
      mux->pat_interval = g_value_get_uint (value);
      if (mux->tsmux)
        tsmux_set_pat_interval (mux->tsmux, mux->pat_interval);
      break;
    case PROP_PMT_INTERVAL:
      walk = mux->collect->data;
      mux->pmt_interval = g_value_get_uint (value);

      while (walk) {
        MpegTsPadData *ts_data = (MpegTsPadData *) walk->data;

        tsmux_set_pmt_interval (ts_data->prog, mux->pmt_interval);
        walk = g_slist_next (walk);
      }
      break;
    case PROP_ALIGNMENT:
      mux->alignment = g_value_get_int (value);
      break;
    case PROP_SI_INTERVAL:
      mux->si_interval = g_value_get_uint (value);
      tsmux_set_si_interval (mux->tsmux, mux->si_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpegtsmux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  MpegTsMux *mux = GST_MPEG_TSMUX (object);

  switch (prop_id) {
    case PROP_M2TS_MODE:
      g_value_set_boolean (value, mux->m2ts_mode);
      break;
    case PROP_PROG_MAP:
      gst_value_set_structure (value, mux->prog_map);
      break;
    case PROP_PAT_INTERVAL:
      g_value_set_uint (value, mux->pat_interval);
      break;
    case PROP_PMT_INTERVAL:
      g_value_set_uint (value, mux->pmt_interval);
      break;
    case PROP_ALIGNMENT:
      g_value_set_int (value, mux->alignment);
      break;
    case PROP_SI_INTERVAL:
      g_value_set_uint (value, mux->si_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#if 0
static void
mpegtsmux_set_index (GstElement * element, GstIndex * index)
{
  MpegTsMux *mux = GST_MPEG_TSMUX (element);

  GST_OBJECT_LOCK (mux);
  if (mux->element_index)
    gst_object_unref (mux->element_index);
  mux->element_index = index ? gst_object_ref (index) : NULL;
  GST_OBJECT_UNLOCK (mux);

  GST_DEBUG_OBJECT (mux, "Set index %" GST_PTR_FORMAT, mux->element_index);
}

static GstIndex *
mpegtsmux_get_index (GstElement * element)
{
  GstIndex *result = NULL;
  MpegTsMux *mux = GST_MPEG_TSMUX (element);

  GST_OBJECT_LOCK (mux);
  if (mux->element_index)
    result = gst_object_ref (mux->element_index);
  GST_OBJECT_UNLOCK (mux);

  GST_DEBUG_OBJECT (mux, "Returning index %" GST_PTR_FORMAT, result);

  return result;
}
#endif

static void
release_buffer_cb (guint8 * data, void *user_data)
{
  stream_data_free (user_data);
}

static GstFlowReturn
mpegtsmux_create_stream (MpegTsMux * mux, MpegTsPadData * ts_data)
{
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstCaps *caps;
  GstStructure *s;
  GstPad *pad;
  TsMuxStreamType st = TSMUX_ST_RESERVED;
  const gchar *mt;
  const GValue *value = NULL;
  GstBuffer *codec_data = NULL;
  guint8 opus_channel_config_code = 0;
  guint16 profile = 0;
  guint8 main_level = 0;
  guint32 max_rate = 0;
  guint8 color_spec = 0;
  j2k_private_data *private_data = NULL;

  pad = ts_data->collect.pad;
  caps = gst_pad_get_current_caps (pad);
  if (caps == NULL)
    goto not_negotiated;

  GST_DEBUG_OBJECT (pad, "Creating stream with PID 0x%04x for caps %"
      GST_PTR_FORMAT, ts_data->pid, caps);

  s = gst_caps_get_structure (caps, 0);

  mt = gst_structure_get_name (s);
  value = gst_structure_get_value (s, "codec_data");
  if (value != NULL)
    codec_data = gst_value_get_buffer (value);

  if (strcmp (mt, "video/x-dirac") == 0) {
    st = TSMUX_ST_VIDEO_DIRAC;
  } else if (strcmp (mt, "audio/x-ac3") == 0) {
    st = TSMUX_ST_PS_AUDIO_AC3;
  } else if (strcmp (mt, "audio/x-dts") == 0) {
    st = TSMUX_ST_PS_AUDIO_DTS;
  } else if (strcmp (mt, "audio/x-lpcm") == 0) {
    st = TSMUX_ST_PS_AUDIO_LPCM;
  } else if (strcmp (mt, "video/x-h264") == 0) {
    st = TSMUX_ST_VIDEO_H264;
  } else if (strcmp (mt, "video/x-h265") == 0) {
    st = TSMUX_ST_VIDEO_HEVC;
  } else if (strcmp (mt, "audio/mpeg") == 0) {
    gint mpegversion;

    if (!gst_structure_get_int (s, "mpegversion", &mpegversion)) {
      GST_ERROR_OBJECT (pad, "caps missing mpegversion");
      goto not_negotiated;
    }

    switch (mpegversion) {
      case 1:
        st = TSMUX_ST_AUDIO_MPEG1;
        break;
      case 2:
        st = TSMUX_ST_AUDIO_MPEG2;
        break;
      case 4:
      {
        st = TSMUX_ST_AUDIO_AAC;
        if (codec_data) {       /* TODO - Check stream format - codec data should only come with RAW stream */
          GST_DEBUG_OBJECT (pad,
              "we have additional codec data (%" G_GSIZE_FORMAT " bytes)",
              gst_buffer_get_size (codec_data));
          ts_data->codec_data = gst_buffer_ref (codec_data);
          ts_data->prepare_func = mpegtsmux_prepare_aac;
        } else {
          ts_data->codec_data = NULL;
        }
        break;
      }
      default:
        GST_WARNING_OBJECT (pad, "unsupported mpegversion %d", mpegversion);
        goto not_negotiated;
    }
  } else if (strcmp (mt, "video/mpeg") == 0) {
    gint mpegversion;

    if (!gst_structure_get_int (s, "mpegversion", &mpegversion)) {
      GST_ERROR_OBJECT (pad, "caps missing mpegversion");
      goto not_negotiated;
    }

    switch (mpegversion) {
      case 1:
        st = TSMUX_ST_VIDEO_MPEG1;
        break;
      case 2:
        st = TSMUX_ST_VIDEO_MPEG2;
        break;
      case 4:
        st = TSMUX_ST_VIDEO_MPEG4;
        break;
      default:
        GST_WARNING_OBJECT (pad, "unsupported mpegversion %d", mpegversion);
        goto not_negotiated;
    }
  } else if (strcmp (mt, "subpicture/x-dvb") == 0) {
    st = TSMUX_ST_PS_DVB_SUBPICTURE;
  } else if (strcmp (mt, "application/x-teletext") == 0) {
    st = TSMUX_ST_PS_TELETEXT;
    /* needs a particularly sized layout */
    ts_data->prepare_func = mpegtsmux_prepare_teletext;
  } else if (strcmp (mt, "audio/x-opus") == 0) {
    guint8 channels, mapping_family, stream_count, coupled_count;
    guint8 channel_mapping[256];

    if (!gst_codec_utils_opus_parse_caps (caps, NULL, &channels,
            &mapping_family, &stream_count, &coupled_count, channel_mapping)) {
      GST_ERROR_OBJECT (pad, "Incomplete Opus caps");
      goto not_negotiated;
    }

    if (channels <= 2 && mapping_family == 0) {
      opus_channel_config_code = channels;
    } else if (channels == 2 && mapping_family == 255 && stream_count == 1
        && coupled_count == 1) {
      /* Dual mono */
      opus_channel_config_code = 0;
    } else if (channels >= 2 && channels <= 8 && mapping_family == 1) {
      static const guint8 coupled_stream_counts[9] = {
        1, 0, 1, 1, 2, 2, 2, 3, 3
      };
      static const guint8 channel_map_a[8][8] = {
        {0},
        {0, 1},
        {0, 2, 1},
        {0, 1, 2, 3},
        {0, 4, 1, 2, 3},
        {0, 4, 1, 2, 3, 5},
        {0, 4, 1, 2, 3, 5, 6},
        {0, 6, 1, 2, 3, 4, 5, 7},
      };
      static const guint8 channel_map_b[8][8] = {
        {0},
        {0, 1},
        {0, 1, 2},
        {0, 1, 2, 3},
        {0, 1, 2, 3, 4},
        {0, 1, 2, 3, 4, 5},
        {0, 1, 2, 3, 4, 5, 6},
        {0, 1, 2, 3, 4, 5, 6, 7},
      };

      /* Vorbis mapping */
      if (stream_count == channels - coupled_stream_counts[channels] &&
          coupled_count == coupled_stream_counts[channels] &&
          memcmp (channel_mapping, channel_map_a[channels - 1],
              channels) == 0) {
        opus_channel_config_code = channels;
      } else if (stream_count == channels - coupled_stream_counts[channels] &&
          coupled_count == coupled_stream_counts[channels] &&
          memcmp (channel_mapping, channel_map_b[channels - 1],
              channels) == 0) {
        opus_channel_config_code = channels | 0x80;
      } else {
        GST_FIXME_OBJECT (pad, "Opus channel mapping not handled");
        goto not_negotiated;
      }
    }

    st = TSMUX_ST_PS_OPUS;
    ts_data->prepare_func = mpegtsmux_prepare_opus;
  } else if (strcmp (mt, "meta/x-klv") == 0) {
    st = TSMUX_ST_PS_KLV;
  } else if (strcmp (mt, "image/x-jpc") == 0) {
    /*
     * See this document for more details on standard:
     *
     * https://www.itu.int/rec/T-REC-H.222.0-201206-S/en
     *  Annex S describes J2K details
     *  Page 104 of this document describes J2k video descriptor
     */

    const GValue *vProfile = gst_structure_get_value (s, "profile");
    const GValue *vMainlevel = gst_structure_get_value (s, "main-level");
    const GValue *vFramerate = gst_structure_get_value (s, "framerate");
    const GValue *vColorimetry = gst_structure_get_value (s, "colorimetry");
    private_data = g_new0 (j2k_private_data, 1);
    profile = g_value_get_uint (vProfile);
    if (profile != GST_JPEG2000_PARSE_PROFILE_BC_SINGLE) {
      /* for now, we will relax the condition that the profile must equal GST_JPEG2000_PARSE_PROFILE_BC_SINGLE */
      /*GST_ERROR_OBJECT (pad, "Invalid JPEG 2000 profile %d", profile);
         goto not_negotiated; */
    }
    /* for now, we will relax the condition that the main level must be present */
    if (vMainlevel) {
      main_level = g_value_get_uint (vMainlevel);
      if (main_level > 11) {
        GST_ERROR_OBJECT (pad, "Invalid main level %d", main_level);
        goto not_negotiated;
      }
      if (main_level >= 6) {
        max_rate = 2 ^ (main_level - 6) * 1600 * 1000000;
      } else {
        switch (main_level) {
          case 0:
          case 1:
          case 2:
          case 3:
            max_rate = 200 * 1000000;
            break;
          case 4:
            max_rate = 400 * 1000000;
            break;
          case 5:
            max_rate = 800 * 1000000;
            break;
          default:
            break;
        }
      }
    } else {
      /*GST_ERROR_OBJECT (pad, "Missing main level");
         goto not_negotiated; */
    }
    /* We always mux video in J2K-over-MPEG-TS non-interlaced mode */
    private_data->interlace = FALSE;
    private_data->den = 0;
    private_data->num = 0;
    private_data->max_bitrate = max_rate;
    private_data->color_spec = 1;
    /* these two fields are not used, since we always mux as non-interlaced */
    private_data->Fic = 1;
    private_data->Fio = 0;

    /* Get Framerate */
    if (vFramerate != NULL) {
      /* Data for ELSM header */
      private_data->num = gst_value_get_fraction_numerator (vFramerate);
      private_data->den = gst_value_get_fraction_denominator (vFramerate);
    }
    /* Get Colorimetry */
    if (vColorimetry) {
      const char *colorimetry = g_value_get_string (vColorimetry);
      color_spec = GST_MPEGTS_JPEG2000_COLORSPEC_SRGB;  /* RGB as default */
      if (g_str_equal (colorimetry, GST_VIDEO_COLORIMETRY_BT601)) {
        color_spec = GST_MPEGTS_JPEG2000_COLORSPEC_REC601;
      } else {
        if (g_str_equal (colorimetry, GST_VIDEO_COLORIMETRY_BT709)
            || g_str_equal (colorimetry, GST_VIDEO_COLORIMETRY_SMPTE240M)) {
          color_spec = GST_MPEGTS_JPEG2000_COLORSPEC_REC709;
        }
      }
      private_data->color_spec = color_spec;
    } else {
      GST_ERROR_OBJECT (pad, "Colorimetry not present in caps");
      goto not_negotiated;
    }
    st = TSMUX_ST_VIDEO_JP2K;
    ts_data->prepare_func = mpegtsmux_prepare_jpeg2000;
    ts_data->prepare_data = private_data;
    ts_data->free_func = mpegtsmux_free_jpeg2000;
  }

  if (st != TSMUX_ST_RESERVED) {
    ts_data->stream = tsmux_create_stream (mux->tsmux, st, ts_data->pid,
        ts_data->language);
  } else {
    GST_DEBUG_OBJECT (pad, "Failed to determine stream type");
  }

  if (ts_data->stream != NULL) {
    const char *interlace_mode = gst_structure_get_string (s, "interlace-mode");
    gst_structure_get_int (s, "rate", &ts_data->stream->audio_sampling);
    gst_structure_get_int (s, "channels", &ts_data->stream->audio_channels);
    gst_structure_get_int (s, "bitrate", &ts_data->stream->audio_bitrate);

    /* frame rate */
    gst_structure_get_fraction (s, "framerate", &ts_data->stream->num,
        &ts_data->stream->den);

    /* Interlace mode */
    ts_data->stream->interlace_mode = FALSE;
    if (interlace_mode) {
      ts_data->stream->interlace_mode =
          g_str_equal (interlace_mode, "interleaved");
    }
    /* Width and Height */
    gst_structure_get_int (s, "width", &ts_data->stream->horizontal_size);
    gst_structure_get_int (s, "height", &ts_data->stream->vertical_size);

    ts_data->stream->color_spec = color_spec;
    ts_data->stream->max_bitrate = max_rate;
    ts_data->stream->profile_and_level = profile | main_level;

    ts_data->stream->opus_channel_config_code = opus_channel_config_code;

    tsmux_stream_set_buffer_release_func (ts_data->stream, release_buffer_cb);
    tsmux_program_add_stream (ts_data->prog, ts_data->stream);

    ret = GST_FLOW_OK;
  }
#if 0
  GST_OBJECT_LOCK (mux);
  if (mux->element_index) {
    gboolean parsed = FALSE;

    if (ts_data->stream->is_video_stream) {
      if (gst_structure_get_boolean (s, "parsed", &parsed) && parsed) {
        if (ts_data->element_index_writer_id == -1) {
          gst_index_get_writer_id (mux->element_index, GST_OBJECT (mux),
              &ts_data->element_index_writer_id);
          GST_DEBUG_OBJECT (mux, "created GstIndex writer_id = %d for stream",
              ts_data->element_index_writer_id);
          gst_index_add_format (mux->element_index,
              ts_data->element_index_writer_id, pts_format);
          gst_index_add_format (mux->element_index,
              ts_data->element_index_writer_id, spn_format);
        }
      } else {
        GST_WARNING_OBJECT (pad, "no indexing for (unparsed) stream !");
      }
    }
  }
  GST_OBJECT_UNLOCK (mux);
#endif
  gst_caps_unref (caps);
  return ret;
  /* ERRORS */
not_negotiated:
  {
    g_free (private_data);
    GST_DEBUG_OBJECT (pad, "Sink pad caps were not set before pushing");
    if (caps)
      gst_caps_unref (caps);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstFlowReturn
mpegtsmux_create_streams (MpegTsMux * mux)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GSList *walk = mux->collect->data;

  /* Create the streams */
  while (walk) {
    GstCollectData *c_data = (GstCollectData *) walk->data;
    MpegTsPadData *ts_data = (MpegTsPadData *) walk->data;
    gchar *name = NULL;

    walk = g_slist_next (walk);

    if (ts_data->prog_id == -1) {
      name = GST_PAD_NAME (c_data->pad);
      if (mux->prog_map != NULL && gst_structure_has_field (mux->prog_map,
              name)) {
        gint idx;
        gboolean ret = gst_structure_get_int (mux->prog_map, name, &idx);
        if (!ret) {
          GST_ELEMENT_ERROR (mux, STREAM, MUX,
              ("Reading program map failed. Assuming default"), (NULL));
          idx = DEFAULT_PROG_ID;
        }
        if (idx < 0) {
          GST_DEBUG_OBJECT (mux, "Program number %d associate with pad %s less "
              "than zero; DEFAULT_PROGRAM = %d is used instead",
              idx, name, DEFAULT_PROG_ID);
          idx = DEFAULT_PROG_ID;
        }
        ts_data->prog_id = idx;
      } else {
        ts_data->prog_id = DEFAULT_PROG_ID;
      }
    }

    ts_data->prog =
        g_hash_table_lookup (mux->programs, GINT_TO_POINTER (ts_data->prog_id));
    if (ts_data->prog == NULL) {
      ts_data->prog = tsmux_program_new (mux->tsmux, ts_data->prog_id);
      if (ts_data->prog == NULL)
        goto no_program;
      tsmux_set_pmt_interval (ts_data->prog, mux->pmt_interval);
      g_hash_table_insert (mux->programs,
          GINT_TO_POINTER (ts_data->prog_id), ts_data->prog);

      /* Take the first stream of the program for the PCR */
      GST_DEBUG_OBJECT (COLLECT_DATA_PAD (ts_data),
          "Use stream (pid=%d) from pad as PCR for program (prog_id = %d)",
          ts_data->pid, ts_data->prog_id);

      tsmux_program_set_pcr_stream (ts_data->prog, ts_data->stream);
    }

    if (ts_data->stream == NULL) {
      ret = mpegtsmux_create_stream (mux, ts_data);
      if (ret != GST_FLOW_OK)
        goto no_stream;
    }
  }

  return GST_FLOW_OK;

  /* ERRORS */
no_program:
  {
    GST_ELEMENT_ERROR (mux, STREAM, MUX,
        ("Could not create new program"), (NULL));
    return GST_FLOW_ERROR;
  }
no_stream:
  {
    GST_ELEMENT_ERROR (mux, STREAM, MUX,
        ("Could not create handler for stream"), (NULL));
    return ret;
  }
}

static gboolean
mpegtsmux_sink_event (GstCollectPads * pads, GstCollectData * data,
    GstEvent * event, gpointer user_data)
{
  MpegTsMux *mux = GST_MPEG_TSMUX (user_data);
  gboolean res = FALSE;
  gboolean forward = TRUE;
  MpegTsPadData *pad_data = (MpegTsPadData *) data;

#ifndef GST_DISABLE_GST_DEBUG
  GstPad *pad;

  pad = data->pad;
#endif

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      GstClockTime timestamp, stream_time, running_time;
      gboolean all_headers;
      guint count;

      if (!gst_video_event_is_force_key_unit (event))
        goto out;

      res = TRUE;
      forward = FALSE;

      gst_video_event_parse_downstream_force_key_unit (event,
          &timestamp, &stream_time, &running_time, &all_headers, &count);
      GST_INFO_OBJECT (pad, "have downstream force-key-unit event, "
          "seqnum %d, running-time %" GST_TIME_FORMAT " count %d",
          gst_event_get_seqnum (event), GST_TIME_ARGS (running_time), count);

      if (mux->force_key_unit_event != NULL) {
        GST_INFO_OBJECT (mux, "skipping downstream force key unit event "
            "as an upstream force key unit is already queued");
        goto out;
      }

      if (!all_headers)
        goto out;

      mux->pending_key_unit_ts = running_time;
      gst_event_replace (&mux->force_key_unit_event, event);
      break;
    }
    case GST_EVENT_TAG:{
      GstTagList *list;
      gchar *lang = NULL;

      GST_DEBUG_OBJECT (mux, "received tag event");
      gst_event_parse_tag (event, &list);

      /* Matroska wants ISO 639-2B code, taglist most likely contains 639-1 */
      if (gst_tag_list_get_string (list, GST_TAG_LANGUAGE_CODE, &lang)) {
        const gchar *lang_code;

        lang_code = gst_tag_get_language_code_iso_639_2B (lang);
        if (lang_code) {
          GST_DEBUG_OBJECT (pad, "Setting language to '%s'", lang_code);

          g_free (pad_data->language);
          pad_data->language = g_strdup (lang_code);
        } else {
          GST_WARNING_OBJECT (pad, "Did not get language code for '%s'", lang);
        }
        g_free (lang);
      }

      /* handled this, don't want collectpads to forward it downstream */
      res = TRUE;
      forward = gst_tag_list_get_scope (list) == GST_TAG_SCOPE_GLOBAL;
      break;
    }
    case GST_EVENT_STREAM_START:{
      GstStreamFlags flags;

      gst_event_parse_stream_flags (event, &flags);

      /* Don't wait for data on sparse inputs like metadata streams */
      if ((flags & GST_STREAM_FLAG_SPARSE)) {
        GST_COLLECT_PADS_STATE_UNSET (data, GST_COLLECT_PADS_STATE_LOCKED);
        gst_collect_pads_set_waiting (pads, data, FALSE);
        GST_COLLECT_PADS_STATE_SET (data, GST_COLLECT_PADS_STATE_LOCKED);
      }
      break;
    }
    default:
      break;
  }

out:
  if (!forward)
    gst_event_unref (event);
  else
    res = gst_collect_pads_event_default (pads, data, event, FALSE);

  return res;
}

static gboolean
mpegtsmux_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  MpegTsMux *mux = GST_MPEG_TSMUX (parent);
  gboolean res = TRUE, forward = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      GstIterator *iter;
      GstIteratorResult iter_ret;
      GstPad *sinkpad;
      GValue sinkpad_value = G_VALUE_INIT;
      GstClockTime running_time;
      gboolean all_headers, done, res = FALSE;
      guint count;

      if (!gst_video_event_is_force_key_unit (event))
        break;

      forward = FALSE;

      gst_video_event_parse_upstream_force_key_unit (event,
          &running_time, &all_headers, &count);

      GST_INFO_OBJECT (mux, "received upstream force-key-unit event, "
          "seqnum %d running_time %" GST_TIME_FORMAT " all_headers %d count %d",
          gst_event_get_seqnum (event), GST_TIME_ARGS (running_time),
          all_headers, count);

      if (!all_headers)
        break;

      mux->pending_key_unit_ts = running_time;
      gst_event_replace (&mux->force_key_unit_event, event);

      iter = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (mux));
      done = FALSE;
      while (!done) {
        gboolean tmp;

        iter_ret = gst_iterator_next (iter, &sinkpad_value);
        sinkpad = g_value_get_object (&sinkpad_value);

        switch (iter_ret) {
          case GST_ITERATOR_DONE:
            done = TRUE;
            break;
          case GST_ITERATOR_OK:
            GST_INFO_OBJECT (pad, "forwarding");
            tmp = gst_pad_push_event (sinkpad, gst_event_ref (event));
            GST_INFO_OBJECT (mux, "result %d", tmp);
            /* succeed if at least one pad succeeds */
            res |= tmp;
            break;
          case GST_ITERATOR_ERROR:
            done = TRUE;
            break;
          case GST_ITERATOR_RESYNC:
            break;
        }
        g_value_reset (&sinkpad_value);
      }
      g_value_unset (&sinkpad_value);
      gst_iterator_free (iter);
      break;
    }
    default:
      break;
  }

  if (forward)
    res = gst_pad_event_default (pad, parent, event);
  else
    gst_event_unref (event);

  return res;
}

static GstEvent *
check_pending_key_unit_event (GstEvent * pending_event, GstSegment * segment,
    GstClockTime timestamp, guint flags, GstClockTime pending_key_unit_ts)
{
  GstClockTime running_time, stream_time;
  gboolean all_headers;
  guint count;
  GstEvent *event = NULL;

  g_assert (segment != NULL);

  if (pending_event == NULL)
    goto out;

  if (GST_CLOCK_TIME_IS_VALID (pending_key_unit_ts) &&
      timestamp == GST_CLOCK_TIME_NONE)
    goto out;

  running_time = timestamp;

  GST_INFO ("now %" GST_TIME_FORMAT " wanted %" GST_TIME_FORMAT,
      GST_TIME_ARGS (running_time), GST_TIME_ARGS (pending_key_unit_ts));
  if (GST_CLOCK_TIME_IS_VALID (pending_key_unit_ts) &&
      running_time < pending_key_unit_ts)
    goto out;

  if (flags & GST_BUFFER_FLAG_DELTA_UNIT) {
    GST_INFO ("pending force key unit, waiting for keyframe");
    goto out;
  }

  stream_time = gst_segment_to_stream_time (segment,
      GST_FORMAT_TIME, timestamp);

  if (GST_EVENT_TYPE (pending_event) == GST_EVENT_CUSTOM_DOWNSTREAM) {
    gst_video_event_parse_downstream_force_key_unit (pending_event,
        NULL, NULL, NULL, &all_headers, &count);
  } else {
    gst_video_event_parse_upstream_force_key_unit (pending_event, NULL,
        &all_headers, &count);
  }

  event =
      gst_video_event_new_downstream_force_key_unit (timestamp, stream_time,
      running_time, all_headers, count);
  gst_event_set_seqnum (event, gst_event_get_seqnum (pending_event));

out:
  return event;
}

GstFlowReturn
mpegtsmux_clip_inc_running_time (GstCollectPads * pads,
    GstCollectData * cdata, GstBuffer * buf, GstBuffer ** outbuf,
    gpointer user_data)
{
  MpegTsPadData *pad_data = (MpegTsPadData *) cdata;
  GstClockTime time;

  *outbuf = buf;

  /* PTS */
  time = GST_BUFFER_PTS (buf);

  /* invalid left alone and passed */
  if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (time))) {
    time = gst_segment_to_running_time (&cdata->segment, GST_FORMAT_TIME, time);
    if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (time))) {
      GST_DEBUG_OBJECT (cdata->pad, "clipping buffer on pad outside segment");
      gst_buffer_unref (buf);
      *outbuf = NULL;
      goto beach;
    } else {
      GST_LOG_OBJECT (cdata->pad, "buffer pts %" GST_TIME_FORMAT " ->  %"
          GST_TIME_FORMAT " running time",
          GST_TIME_ARGS (GST_BUFFER_PTS (buf)), GST_TIME_ARGS (time));
      buf = *outbuf = gst_buffer_make_writable (buf);
      GST_BUFFER_PTS (*outbuf) = time;
    }
  }

  /* DTS */
  time = GST_BUFFER_DTS (buf);

  /* invalid left alone and passed */
  if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (time))) {
    gint sign;
    gint64 dts;

    sign = gst_segment_to_running_time_full (&cdata->segment, GST_FORMAT_TIME,
        time, &time);

    if (sign > 0)
      dts = (gint64) time;
    else
      dts = -((gint64) time);

    GST_LOG_OBJECT (cdata->pad, "buffer dts %" GST_TIME_FORMAT " -> %"
        GST_STIME_FORMAT " running time", GST_TIME_ARGS (GST_BUFFER_DTS (buf)),
        GST_STIME_ARGS (dts));

    if (GST_CLOCK_STIME_IS_VALID (pad_data->dts) && dts < pad_data->dts) {
      /* Ignore DTS going backward */
      GST_WARNING_OBJECT (cdata->pad, "ignoring DTS going backward");
      dts = pad_data->dts;
    }

    *outbuf = gst_buffer_make_writable (buf);
    if (sign > 0)
      GST_BUFFER_DTS (*outbuf) = time;
    else
      GST_BUFFER_DTS (*outbuf) = GST_CLOCK_TIME_NONE;

    pad_data->dts = dts;
  } else {
    pad_data->dts = GST_CLOCK_STIME_NONE;
  }

beach:
  return GST_FLOW_OK;
}

static GstFlowReturn
mpegtsmux_collected_buffer (GstCollectPads * pads, GstCollectData * data,
    GstBuffer * buf, MpegTsMux * mux)
{
  GstFlowReturn ret = GST_FLOW_OK;
  MpegTsPadData *best = (MpegTsPadData *) data;
  TsMuxProgram *prog;
  gint64 pts = GST_CLOCK_STIME_NONE;
  gint64 dts = GST_CLOCK_STIME_NONE;
  gboolean delta = TRUE, header = FALSE;
  StreamData *stream_data;

  GST_DEBUG_OBJECT (mux, "Pads collected");

  if (G_UNLIKELY (mux->first)) {
    ret = mpegtsmux_create_streams (mux);
    if (G_UNLIKELY (ret != GST_FLOW_OK)) {
      if (buf)
        gst_buffer_unref (buf);
      return ret;
    }

    mpegtsmux_prepare_srcpad (mux);

    mux->first = FALSE;
  }

  if (G_UNLIKELY (best == NULL)) {
    /* EOS */
    GST_INFO_OBJECT (mux, "EOS");
    /* drain some possibly cached data */
    new_packet_m2ts (mux, NULL, -1);
    mpegtsmux_push_packets (mux, TRUE);
    gst_pad_push_event (mux->srcpad, gst_event_new_eos ());

    if (buf)
      gst_buffer_unref (buf);

    return GST_FLOW_OK;
  }

  prog = best->prog;
  if (prog == NULL)
    goto no_program;

  g_assert (buf != NULL);

  if (best->prepare_func) {
    GstBuffer *tmp;

    tmp = best->prepare_func (buf, best, mux);
    g_assert (tmp);
    gst_buffer_unref (buf);
    buf = tmp;
  }

  if (mux->force_key_unit_event != NULL && best->stream->is_video_stream) {
    GstEvent *event;

    event = check_pending_key_unit_event (mux->force_key_unit_event,
        &best->collect.segment, GST_BUFFER_PTS (buf),
        GST_BUFFER_FLAGS (buf), mux->pending_key_unit_ts);
    if (event) {
      GstClockTime running_time;
      guint count;
      GList *cur;

      mux->pending_key_unit_ts = GST_CLOCK_TIME_NONE;
      gst_event_replace (&mux->force_key_unit_event, NULL);

      gst_video_event_parse_downstream_force_key_unit (event,
          NULL, NULL, &running_time, NULL, &count);

      GST_INFO_OBJECT (mux, "pushing downstream force-key-unit event %d "
          "%" GST_TIME_FORMAT " count %d", gst_event_get_seqnum (event),
          GST_TIME_ARGS (running_time), count);
      gst_pad_push_event (mux->srcpad, event);

      /* output PAT, SI tables */
      tsmux_resend_pat (mux->tsmux);
      tsmux_resend_si (mux->tsmux);

      /* output PMT for each program */
      for (cur = mux->tsmux->programs; cur; cur = cur->next) {
        TsMuxProgram *program = (TsMuxProgram *) cur->data;

        tsmux_resend_pmt (program);
      }
    }
  }

  if (G_UNLIKELY (prog->pcr_stream == NULL)) {
    /* Take the first data stream for the PCR */
    GST_DEBUG_OBJECT (COLLECT_DATA_PAD (best),
        "Use stream (pid=%d) from pad as PCR for program (prog_id = %d)",
        best->pid, best->prog_id);

    /* Set the chosen PCR stream */
    tsmux_program_set_pcr_stream (prog, best->stream);
  }

  GST_DEBUG_OBJECT (COLLECT_DATA_PAD (best),
      "Chose stream for output (PID: 0x%04x)", best->pid);

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (buf))) {
    pts = GSTTIME_TO_MPEGTIME (GST_BUFFER_PTS (buf));
    GST_DEBUG_OBJECT (mux, "Buffer has PTS  %" GST_TIME_FORMAT " pts %"
        G_GINT64_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (buf)), pts);
  }

  if (GST_CLOCK_STIME_IS_VALID (best->dts)) {
    dts = GSTTIME_TO_MPEGTIME (best->dts);
    GST_DEBUG_OBJECT (mux, "Buffer has DTS %" GST_STIME_FORMAT " dts %"
        G_GINT64_FORMAT, GST_STIME_ARGS (best->dts), dts);
  }

  /* should not have a DTS without PTS */
  if (!GST_CLOCK_STIME_IS_VALID (pts) && GST_CLOCK_STIME_IS_VALID (dts)) {
    GST_DEBUG_OBJECT (mux, "using DTS for unknown PTS");
    pts = dts;
  }

  if (best->stream->is_video_stream) {
    delta = GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
    header = GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_HEADER);
#if 0
    GST_OBJECT_LOCK (mux);
    if (mux->element_index && !delta && best->element_index_writer_id != -1) {
      gst_index_add_association (mux->element_index,
          best->element_index_writer_id,
          GST_ASSOCIATION_FLAG_KEY_UNIT, spn_format, mux->spn_count,
          pts_format, pts, NULL);
    }
    GST_OBJECT_UNLOCK (mux);
#endif
  }

  if (best->stream->is_meta && gst_buffer_get_size (buf) > (G_MAXUINT16 - 3)) {
    GST_WARNING_OBJECT (mux, "KLV meta unit too big, splitting not supported");

    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }

  GST_DEBUG_OBJECT (mux, "delta: %d", delta);

  stream_data = stream_data_new (buf);
  tsmux_stream_add_data (best->stream, stream_data->map_info.data,
      stream_data->map_info.size, stream_data, pts, dts, !delta);

  /* outgoing ts follows ts of PCR program stream */
  if (prog->pcr_stream == best->stream) {
    /* prefer DTS if present for PCR as it should be monotone */
    mux->last_ts =
        GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DTS (buf)) ?
        GST_BUFFER_DTS (buf) : GST_BUFFER_PTS (buf);
  }

  mux->is_delta = delta;
  mux->is_header = header;
  while (tsmux_stream_bytes_in_buffer (best->stream) > 0) {
    if (!tsmux_write_stream_packet (mux->tsmux, best->stream)) {
      /* Failed writing data for some reason. Set appropriate error */
      GST_DEBUG_OBJECT (mux, "Failed to write data packet");
      GST_ELEMENT_ERROR (mux, STREAM, MUX,
          ("Failed writing output data to stream %04x", best->stream->id),
          (NULL));
      goto write_fail;
    }
  }
  /* flush packet cache */
  return mpegtsmux_push_packets (mux, FALSE);

  /* ERRORS */
write_fail:
  {
    return mux->last_flow_ret;
  }
no_program:
  {
    if (buf)
      gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (mux, STREAM, MUX,
        ("Stream on pad %" GST_PTR_FORMAT
            " is not associated with any program", COLLECT_DATA_PAD (best)),
        (NULL));
    return GST_FLOW_ERROR;
  }
}

static GstPad *
mpegtsmux_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  MpegTsMux *mux = GST_MPEG_TSMUX (element);
  gint pid = -1;
  gchar *pad_name = NULL;
  GstPad *pad = NULL;
  MpegTsPadData *pad_data = NULL;

  if (name != NULL && sscanf (name, "sink_%d", &pid) == 1) {
    if (tsmux_find_stream (mux->tsmux, pid))
      goto stream_exists;
  } else {
    pid = tsmux_get_new_pid (mux->tsmux);
  }

  pad_name = g_strdup_printf ("sink_%d", pid);
  pad = gst_pad_new_from_template (templ, pad_name);
  g_free (pad_name);

  pad_data = (MpegTsPadData *)
      gst_collect_pads_add_pad (mux->collect, pad, sizeof (MpegTsPadData),
      (GstCollectDataDestroyNotify) (mpegtsmux_pad_reset), TRUE);
  if (pad_data == NULL)
    goto pad_failure;

  mpegtsmux_pad_reset (pad_data);
  pad_data->pid = pid;

  if (G_UNLIKELY (!gst_element_add_pad (element, pad)))
    goto could_not_add;

  return pad;

  /* ERRORS */
stream_exists:
  {
    GST_ELEMENT_ERROR (element, STREAM, MUX, ("Duplicate PID requested"),
        (NULL));
    return NULL;
  }
could_not_add:
  {
    GST_ELEMENT_ERROR (element, STREAM, FAILED,
        ("Internal data stream error."), ("Could not add pad to element"));
    gst_collect_pads_remove_pad (mux->collect, pad);
    gst_object_unref (pad);
    return NULL;
  }
pad_failure:
  {
    GST_ELEMENT_ERROR (element, STREAM, FAILED,
        ("Internal data stream error."), ("Could not add pad to collectpads"));
    gst_object_unref (pad);
    return NULL;
  }
}

static void
mpegtsmux_release_pad (GstElement * element, GstPad * pad)
{
  MpegTsMux *mux = GST_MPEG_TSMUX (element);

  GST_DEBUG_OBJECT (mux, "Pad %" GST_PTR_FORMAT " being released", pad);

  if (mux->collect) {
    gst_collect_pads_remove_pad (mux->collect, pad);
  }

  /* chain up */
  gst_element_remove_pad (element, pad);
}

static void
new_packet_common_init (MpegTsMux * mux, GstBuffer * buf, guint8 * data,
    guint len)
{
  /* Packets should be at least 188 bytes, but check anyway */
  g_assert (len >= 2 || !data);

  if (!mux->streamheader_sent && data) {
    guint pid = ((data[1] & 0x1f) << 8) | data[2];
    /* if it's a PAT or a PMT */
    if (pid == 0x00 || (pid >= TSMUX_START_PMT_PID && pid < TSMUX_START_ES_PID)) {
      GstBuffer *hbuf;

      if (!buf) {
        hbuf = gst_buffer_new_and_alloc (len);
        gst_buffer_fill (hbuf, 0, data, len);
      } else {
        hbuf = gst_buffer_copy (buf);
      }
      GST_LOG_OBJECT (mux,
          "Collecting packet with pid 0x%04x into streamheaders", pid);

      g_queue_push_tail (&mux->streamheader, hbuf);
    } else if (!g_queue_is_empty (&mux->streamheader)) {
      mpegtsmux_set_header_on_caps (mux);
      mux->streamheader_sent = TRUE;
    }
  }

  if (buf) {
    if (mux->is_header) {
      GST_LOG_OBJECT (mux, "marking as header buffer");
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_HEADER);
    }
    if (mux->is_delta) {
      GST_LOG_OBJECT (mux, "marking as delta unit");
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
    } else {
      GST_DEBUG_OBJECT (mux, "marking as non-delta unit");
      mux->is_delta = TRUE;
    }
  }
}

static GstFlowReturn
mpegtsmux_push_packets (MpegTsMux * mux, gboolean force)
{
  GstBufferList *buffer_list;
  gint align = mux->alignment;
  gint av, packet_size;

  if (mux->m2ts_mode) {
    packet_size = M2TS_PACKET_LENGTH;
    if (align < 0)
      align = 32;
  } else {
    packet_size = NORMAL_TS_PACKET_LENGTH;
    if (align < 0)
      align = 0;
  }

  av = gst_adapter_available (mux->out_adapter);
  GST_LOG_OBJECT (mux, "align %d, av %d", align, av);

  if (av == 0)
    return GST_FLOW_OK;

  /* no alignment, just push all available data */
  if (align == 0) {
    buffer_list = gst_adapter_take_buffer_list (mux->out_adapter, av);
    return gst_pad_push_list (mux->srcpad, buffer_list);
  }

  align *= packet_size;

  if (!force && align > av)
    return GST_FLOW_OK;

  buffer_list = gst_buffer_list_new_sized ((av / align) + 1);

  GST_LOG_OBJECT (mux, "aligning to %d bytes", align);
  while (align <= av) {
    GstBuffer *buf;
    GstClockTime pts;

    pts = gst_adapter_prev_pts (mux->out_adapter, NULL);
    buf = gst_adapter_take_buffer (mux->out_adapter, align);

    GST_BUFFER_PTS (buf) = pts;

    gst_buffer_list_add (buffer_list, buf);
    av -= align;
  }

  if (av > 0 && force) {
    GstBuffer *buf;
    GstClockTime pts;
    guint8 *data;
    guint32 header;
    gint dummy;
    GstMapInfo map;

    GST_LOG_OBJECT (mux, "handling %d leftover bytes", av);

    pts = gst_adapter_prev_pts (mux->out_adapter, NULL);
    buf = gst_buffer_new_and_alloc (align);

    GST_BUFFER_PTS (buf) = pts;

    gst_buffer_map (buf, &map, GST_MAP_READ);
    data = map.data;

    gst_adapter_copy (mux->out_adapter, data, 0, av);
    gst_adapter_clear (mux->out_adapter);

    data += av;
    header = GST_READ_UINT32_BE (data - packet_size);

    dummy = (map.size - av) / packet_size;
    GST_LOG_OBJECT (mux, "adding %d null packets", dummy);

    for (; dummy > 0; dummy--) {
      gint offset;

      if (packet_size > NORMAL_TS_PACKET_LENGTH) {
        GST_WRITE_UINT32_BE (data, header);
        /* simply increase header a bit and never mind too much */
        header++;
        offset = 4;
      } else {
        offset = 0;
      }
      GST_WRITE_UINT8 (data + offset, TSMUX_SYNC_BYTE);
      /* null packet PID */
      GST_WRITE_UINT16_BE (data + offset + 1, 0x1FFF);
      /* no adaptation field exists | continuity counter undefined */
      GST_WRITE_UINT8 (data + offset + 3, 0x10);
      /* payload */
      memset (data + offset + 4, 0, NORMAL_TS_PACKET_LENGTH - 4);
      data += packet_size;
    }

    gst_buffer_unmap (buf, &map);
    gst_buffer_list_add (buffer_list, buf);
  }

  return gst_pad_push_list (mux->srcpad, buffer_list);
}

static GstFlowReturn
mpegtsmux_collect_packet (MpegTsMux * mux, GstBuffer * buf)
{
  GST_LOG_OBJECT (mux, "collecting packet size %" G_GSIZE_FORMAT,
      gst_buffer_get_size (buf));
  gst_adapter_push (mux->out_adapter, buf);

  return GST_FLOW_OK;
}

static gboolean
new_packet_m2ts (MpegTsMux * mux, GstBuffer * buf, gint64 new_pcr)
{
  GstBuffer *out_buf;
  int chunk_bytes;
  GstMapInfo map;

  GST_LOG_OBJECT (mux, "Have buffer %p with new_pcr=%" G_GINT64_FORMAT,
      buf, new_pcr);

  chunk_bytes = gst_adapter_available (mux->adapter);

  if (G_LIKELY (buf)) {
    if (new_pcr < 0) {
      /* If there is no pcr in current ts packet then just add the packet
         to the adapter for later output when we see a PCR */
      GST_LOG_OBJECT (mux, "Accumulating non-PCR packet");
      gst_adapter_push (mux->adapter, buf);
      goto exit;
    }

    /* no first interpolation point yet, then this is the one,
     * otherwise it is the second interpolation point */
    if (mux->previous_pcr < 0 && chunk_bytes) {
      mux->previous_pcr = new_pcr;
      mux->previous_offset = chunk_bytes;
      GST_LOG_OBJECT (mux, "Accumulating non-PCR packet");
      gst_adapter_push (mux->adapter, buf);
      goto exit;
    }
  } else {
    g_assert (new_pcr == -1);
  }

  /* interpolate if needed, and 2 points available */
  if (chunk_bytes && (new_pcr != mux->previous_pcr)) {
    gint64 offset = 0;

    GST_LOG_OBJECT (mux, "Processing pending packets; "
        "previous pcr %" G_GINT64_FORMAT ", previous offset %d, "
        "current pcr %" G_GINT64_FORMAT ", current offset %d",
        mux->previous_pcr, (gint) mux->previous_offset,
        new_pcr, (gint) chunk_bytes);

    g_assert (chunk_bytes > mux->previous_offset);
    /* if draining, use previous rate */
    if (G_LIKELY (new_pcr > 0)) {
      mux->pcr_rate_num = new_pcr - mux->previous_pcr;
      mux->pcr_rate_den = chunk_bytes - mux->previous_offset;
    }

    while (offset < chunk_bytes) {
      guint64 cur_pcr, ts;

      /* Loop, pulling packets of the adapter, updating their 4 byte
       * timestamp header and pushing */

      /* interpolate PCR */
      if (G_LIKELY (offset >= mux->previous_offset))
        cur_pcr = mux->previous_pcr +
            gst_util_uint64_scale (offset - mux->previous_offset,
            mux->pcr_rate_num, mux->pcr_rate_den);
      else
        cur_pcr = mux->previous_pcr -
            gst_util_uint64_scale (mux->previous_offset - offset,
            mux->pcr_rate_num, mux->pcr_rate_den);

      /* FIXME: what about DTS here? */
      ts = gst_adapter_prev_pts (mux->adapter, NULL);
      out_buf = gst_adapter_take_buffer (mux->adapter, M2TS_PACKET_LENGTH);
      g_assert (out_buf);
      offset += M2TS_PACKET_LENGTH;

      GST_BUFFER_PTS (out_buf) = ts;

      gst_buffer_map (out_buf, &map, GST_MAP_WRITE);

      /* The header is the bottom 30 bits of the PCR, apparently not
       * encoded into base + ext as in the packets themselves */
      GST_WRITE_UINT32_BE (map.data, cur_pcr & 0x3FFFFFFF);
      gst_buffer_unmap (out_buf, &map);

      GST_LOG_OBJECT (mux, "Outputting a packet of length %d PCR %"
          G_GUINT64_FORMAT, M2TS_PACKET_LENGTH, cur_pcr);
      mpegtsmux_collect_packet (mux, out_buf);
    }
  }

  if (G_UNLIKELY (!buf))
    goto exit;

  gst_buffer_map (buf, &map, GST_MAP_WRITE);

  /* Finally, output the passed in packet */
  /* Only write the bottom 30 bits of the PCR */
  GST_WRITE_UINT32_BE (map.data, new_pcr & 0x3FFFFFFF);

  gst_buffer_unmap (buf, &map);

  GST_LOG_OBJECT (mux, "Outputting a packet of length %d PCR %"
      G_GUINT64_FORMAT, M2TS_PACKET_LENGTH, new_pcr);
  mpegtsmux_collect_packet (mux, buf);

  if (new_pcr != mux->previous_pcr) {
    mux->previous_pcr = new_pcr;
    mux->previous_offset = -M2TS_PACKET_LENGTH;
  }

exit:
  return TRUE;
}

/* Called when the TsMux has prepared a packet for output. Return FALSE
 * on error */
static gboolean
new_packet_cb (GstBuffer * buf, void *user_data, gint64 new_pcr)
{
  MpegTsMux *mux = (MpegTsMux *) user_data;
  gint offset = 0;
  GstMapInfo map;

#if 0
  GST_LOG_OBJECT (mux, "handling packet %d", mux->spn_count);
  mux->spn_count++;
#endif

  if (mux->m2ts_mode) {
    offset = 4;
    gst_buffer_set_size (buf, NORMAL_TS_PACKET_LENGTH + offset);
  }

  gst_buffer_map (buf, &map, GST_MAP_READWRITE);

  if (offset) {
    /* there should be a better way to do this */
    memmove (map.data + offset, map.data, map.size - offset);
  }

  GST_BUFFER_PTS (buf) = mux->last_ts;
  /* do common init (flags and streamheaders) */
  new_packet_common_init (mux, buf, map.data + offset, map.size);

  gst_buffer_unmap (buf, &map);

  /* all is meant for downstream, including any prefix */
  if (offset)
    return new_packet_m2ts (mux, buf, new_pcr);
  else
    mpegtsmux_collect_packet (mux, buf);

  return TRUE;
}

/* called when TsMux needs new packet to write into */
static void
alloc_packet_cb (GstBuffer ** _buf, void *user_data)
{
  MpegTsMux *mux = (MpegTsMux *) user_data;
  GstBuffer *buf;
  gint offset = 0;

  if (mux->m2ts_mode == TRUE)
    offset = 4;

  buf = gst_buffer_new_and_alloc (NORMAL_TS_PACKET_LENGTH + offset);
  gst_buffer_set_size (buf, NORMAL_TS_PACKET_LENGTH);

  *_buf = buf;
}

static void
mpegtsmux_set_header_on_caps (MpegTsMux * mux)
{
  GstBuffer *buf;
  GstStructure *structure;
  GValue array = { 0 };
  GValue value = { 0 };
  GstCaps *caps;

  caps = gst_caps_make_writable (gst_pad_get_current_caps (mux->srcpad));
  structure = gst_caps_get_structure (caps, 0);

  g_value_init (&array, GST_TYPE_ARRAY);

  GST_LOG_OBJECT (mux, "setting %u packets into streamheader",
      g_queue_get_length (&mux->streamheader));

  while ((buf = g_queue_pop_head (&mux->streamheader))) {
    g_value_init (&value, GST_TYPE_BUFFER);
    gst_value_take_buffer (&value, buf);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);
  }

  gst_structure_set_value (structure, "streamheader", &array);
  gst_pad_set_caps (mux->srcpad, caps);
  g_value_unset (&array);
  gst_caps_unref (caps);
}

static void
mpegtsmux_prepare_srcpad (MpegTsMux * mux)
{
  GstSegment seg;
  /* we are not going to seek */
  GstEvent *new_seg;
  gchar s_id[32];
  GstCaps *caps = gst_caps_new_simple ("video/mpegts",
      "systemstream", G_TYPE_BOOLEAN, TRUE,
      "packetsize", G_TYPE_INT,
      (mux->m2ts_mode ? M2TS_PACKET_LENGTH : NORMAL_TS_PACKET_LENGTH),
      NULL);

  /* stream-start (FIXME: create id based on input ids) */
  g_snprintf (s_id, sizeof (s_id), "mpegtsmux-%08x", g_random_int ());
  gst_pad_push_event (mux->srcpad, gst_event_new_stream_start (s_id));

  gst_segment_init (&seg, GST_FORMAT_TIME);
  new_seg = gst_event_new_segment (&seg);

  /* Set caps on src pad from our template and push new segment */
  gst_pad_set_caps (mux->srcpad, caps);
  gst_caps_unref (caps);

  if (!gst_pad_push_event (mux->srcpad, new_seg)) {
    GST_WARNING_OBJECT (mux, "New segment event was not handled downstream");
  }
}

static GstStateChangeReturn
mpegtsmux_change_state (GstElement * element, GstStateChange transition)
{
  MpegTsMux *mux = GST_MPEG_TSMUX (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_collect_pads_start (mux->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_collect_pads_stop (mux->collect);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      mpegtsmux_reset (mux, TRUE);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
mpegtsmux_send_event (GstElement * element, GstEvent * event)
{
  GstMpegtsSection *section;
  MpegTsMux *mux = GST_MPEG_TSMUX (element);

  section = gst_event_parse_mpegts_section (event);
  gst_event_unref (event);

  if (section) {
    GST_DEBUG ("Received event with mpegts section");

    /* TODO: Check that the section type is supported */
    tsmux_add_mpegts_si_section (mux->tsmux, section);

    return TRUE;
  }

  return FALSE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_mpegts_initialize ();
  if (!gst_element_register (plugin, "mpegtsmux", GST_RANK_PRIMARY,
          mpegtsmux_get_type ()))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (mpegtsmux_debug, "mpegtsmux", 0,
      "MPEG Transport Stream muxer");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    mpegtsmux, "MPEG-TS muxer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
