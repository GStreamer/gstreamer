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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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

#include "mpegtsmux.h"

#include "mpegtsmux_h264.h"
#include "mpegtsmux_aac.h"

GST_DEBUG_CATEGORY (mpegtsmux_debug);
#define GST_CAT_DEFAULT mpegtsmux_debug

enum
{
  ARG_0,
  ARG_PROG_MAP,
  ARG_M2TS_MODE,
  ARG_PAT_INTERVAL,
  ARG_PMT_INTERVAL
};

static GstStaticPadTemplate mpegtsmux_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) { 1, 2, 4 }, "
        "systemstream = (boolean) false; "
        "video/x-dirac;"
        "video/x-h264,stream-format=(string)byte-stream;"
        "audio/mpeg, "
        "mpegversion = (int) { 1, 2, 4 };"
        "audio/x-lpcm, "
        "width = (int) { 16, 20, 24 }, "
        "rate = (int) { 48000, 96000 }, "
        "channels = (int) [ 1, 8 ], "
        "dynamic_range = (int) [ 0, 255 ], "
        "emphasis = (boolean) { FALSE, TRUE }, "
        "mute = (boolean) { FALSE, TRUE }; " "audio/x-ac3;" "audio/x-dts"));

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

static void mpegtsmux_dispose (GObject * object);
static gboolean new_packet_cb (guint8 * data, guint len, void *user_data,
    gint64 new_pcr);
static void release_buffer_cb (guint8 * data, void *user_data);

static void mpegtsdemux_prepare_srcpad (MpegTsMux * mux);
static GstFlowReturn mpegtsmux_collected (GstCollectPads * pads,
    MpegTsMux * mux);
static GstPad *mpegtsmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void mpegtsmux_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn mpegtsmux_change_state (GstElement * element,
    GstStateChange transition);
static void mpegtsdemux_set_header_on_caps (MpegTsMux * mux);

GST_BOILERPLATE (MpegTsMux, mpegtsmux, GstElement, GST_TYPE_ELEMENT);

static void
mpegtsmux_base_init (gpointer g_class)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mpegtsmux_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mpegtsmux_src_factory));

  gst_element_class_set_details_simple (element_class,
      "MPEG Transport Stream Muxer", "Codec/Muxer",
      "Multiplexes media streams into an MPEG Transport Stream",
      "Fluendo <contact@fluendo.com>");
}

static void
mpegtsmux_class_init (MpegTsMuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_mpegtsmux_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_mpegtsmux_get_property);
  gobject_class->dispose = mpegtsmux_dispose;

  gstelement_class->request_new_pad = mpegtsmux_request_new_pad;
  gstelement_class->release_pad = mpegtsmux_release_pad;
  gstelement_class->change_state = mpegtsmux_change_state;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PROG_MAP,
      g_param_spec_boxed ("prog-map", "Program map",
          "A GstStructure specifies the mapping from elementary streams to programs",
          GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_M2TS_MODE,
      g_param_spec_boolean ("m2ts-mode", "M2TS(192 bytes) Mode",
          "Set to TRUE to output Blu-Ray disc format with 192 byte packets. "
          "FALSE for standard TS format with 188 byte packets.", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PAT_INTERVAL,
      g_param_spec_uint ("pat-interval", "PAT interval",
          "Set the interval (in ticks of the 90kHz clock) for writing out the PAT table",
          1, G_MAXUINT, TSMUX_DEFAULT_PAT_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PMT_INTERVAL,
      g_param_spec_uint ("pmt-interval", "PMT interval",
          "Set the interval (in ticks of the 90kHz clock) for writing out the PMT table",
          1, G_MAXUINT, TSMUX_DEFAULT_PMT_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
mpegtsmux_init (MpegTsMux * mux, MpegTsMuxClass * g_class)
{
  mux->srcpad =
      gst_pad_new_from_static_template (&mpegtsmux_src_factory, "src");
  gst_pad_use_fixed_caps (mux->srcpad);
  gst_element_add_pad (GST_ELEMENT (mux), mux->srcpad);

  mux->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (mux->collect,
      (GstCollectPadsFunction) GST_DEBUG_FUNCPTR (mpegtsmux_collected), mux);

  mux->tsmux = tsmux_new ();
  tsmux_set_write_func (mux->tsmux, new_packet_cb, mux);

  mux->programs = g_new0 (TsMuxProgram *, MAX_PROG_NUMBER);
  mux->first = TRUE;
  mux->last_flow_ret = GST_FLOW_OK;
  mux->adapter = gst_adapter_new ();
  mux->m2ts_mode = FALSE;
  mux->pat_interval = TSMUX_DEFAULT_PAT_INTERVAL;
  mux->pmt_interval = TSMUX_DEFAULT_PMT_INTERVAL;
  mux->first_pcr = TRUE;
  mux->last_ts = 0;
  mux->is_delta = TRUE;

  mux->prog_map = NULL;
  mux->streamheader = NULL;
  mux->streamheader_sent = FALSE;
}

static void
mpegtsmux_dispose (GObject * object)
{
  MpegTsMux *mux = GST_MPEG_TSMUX (object);

  if (mux->adapter) {
    gst_adapter_clear (mux->adapter);
    g_object_unref (mux->adapter);
    mux->adapter = NULL;
  }
  if (mux->collect) {
    gst_object_unref (mux->collect);
    mux->collect = NULL;
  }
  if (mux->tsmux) {
    tsmux_free (mux->tsmux);
    mux->tsmux = NULL;
  }
  if (mux->prog_map) {
    gst_structure_free (mux->prog_map);
    mux->prog_map = NULL;
  }
  if (mux->programs) {
    g_free (mux->programs);
    mux->programs = NULL;
  }
  if (mux->streamheader) {
    GstBuffer *buf;
    GList *sh;

    sh = mux->streamheader;
    while (sh) {
      buf = sh->data;
      gst_buffer_unref (buf);
      sh = g_list_next (sh);
    }
    g_list_free (mux->streamheader);
    mux->streamheader = NULL;
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
    case ARG_M2TS_MODE:
      /*set incase if the output stream need to be of 192 bytes */
      mux->m2ts_mode = g_value_get_boolean (value);
      break;
    case ARG_PROG_MAP:
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
    case ARG_PAT_INTERVAL:
      mux->pat_interval = g_value_get_uint (value);
      if (mux->tsmux)
        tsmux_set_pat_interval (mux->tsmux, mux->pat_interval);
      break;
    case ARG_PMT_INTERVAL:
      walk = mux->collect->data;
      mux->pmt_interval = g_value_get_uint (value);

      while (walk) {
        MpegTsPadData *ts_data = (MpegTsPadData *) walk->data;

        tsmux_set_pmt_interval (ts_data->prog, mux->pmt_interval);
        walk = g_slist_next (walk);
      }
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
    case ARG_M2TS_MODE:
      g_value_set_boolean (value, mux->m2ts_mode);
      break;
    case ARG_PROG_MAP:
      gst_value_set_structure (value, mux->prog_map);
      break;
    case ARG_PAT_INTERVAL:
      g_value_set_uint (value, mux->pat_interval);
      break;
    case ARG_PMT_INTERVAL:
      g_value_set_uint (value, mux->pmt_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
release_buffer_cb (guint8 * data, void *user_data)
{
  GstBuffer *buf = (GstBuffer *) user_data;
  gst_buffer_unref (buf);
}

static GstFlowReturn
mpegtsmux_create_stream (MpegTsMux * mux, MpegTsPadData * ts_data, GstPad * pad)
{
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstCaps *caps = gst_pad_get_negotiated_caps (pad);
  GstStructure *s;

  if (caps == NULL) {
    GST_DEBUG_OBJECT (pad, "Sink pad caps were not set before pushing");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  s = gst_caps_get_structure (caps, 0);
  g_return_val_if_fail (s != NULL, FALSE);

  if (gst_structure_has_name (s, "video/x-dirac")) {
    GST_DEBUG_OBJECT (pad, "Creating Dirac stream with PID 0x%04x",
        ts_data->pid);
    ts_data->stream = tsmux_create_stream (mux->tsmux, TSMUX_ST_VIDEO_DIRAC,
        ts_data->pid);
  } else if (gst_structure_has_name (s, "audio/x-ac3")) {
    GST_DEBUG_OBJECT (pad, "Creating AC3 stream with PID 0x%04x", ts_data->pid);
    ts_data->stream = tsmux_create_stream (mux->tsmux, TSMUX_ST_PS_AUDIO_AC3,
        ts_data->pid);
  } else if (gst_structure_has_name (s, "audio/x-dts")) {
    GST_DEBUG_OBJECT (pad, "Creating DTS stream with PID 0x%04x", ts_data->pid);
    ts_data->stream = tsmux_create_stream (mux->tsmux, TSMUX_ST_PS_AUDIO_DTS,
        ts_data->pid);
  } else if (gst_structure_has_name (s, "audio/x-lpcm")) {
    GST_DEBUG_OBJECT (pad, "Creating LPCM stream with PID 0x%04x",
        ts_data->pid);
    ts_data->stream = tsmux_create_stream (mux->tsmux, TSMUX_ST_PS_AUDIO_LPCM,
        ts_data->pid);
  } else if (gst_structure_has_name (s, "video/x-h264")) {
    const GValue *value;
    GST_DEBUG_OBJECT (pad, "Creating H264 stream with PID 0x%04x",
        ts_data->pid);
    /* Codec data contains SPS/PPS which need to go in stream for valid ES */
    value = gst_structure_get_value (s, "codec_data");
    if (value) {
      ts_data->codec_data = gst_buffer_ref (gst_value_get_buffer (value));
      GST_DEBUG_OBJECT (pad, "we have additional codec data (%d bytes)",
          GST_BUFFER_SIZE (ts_data->codec_data));
      ts_data->prepare_func = mpegtsmux_prepare_h264;
      ts_data->free_func = mpegtsmux_free_h264;
    } else {
      ts_data->codec_data = NULL;
    }
    ts_data->stream = tsmux_create_stream (mux->tsmux, TSMUX_ST_VIDEO_H264,
        ts_data->pid);
  } else if (gst_structure_has_name (s, "audio/mpeg")) {
    gint mpegversion;
    if (!gst_structure_get_int (s, "mpegversion", &mpegversion)) {
      GST_ELEMENT_ERROR (pad, STREAM, FORMAT,
          ("Invalid data format presented"),
          ("Caps with type audio/mpeg did not have mpegversion"));
      goto beach;
    }

    switch (mpegversion) {
      case 1:
        GST_DEBUG_OBJECT (pad, "Creating MPEG Audio, version 1 stream with "
            "PID 0x%04x", ts_data->pid);
        ts_data->stream = tsmux_create_stream (mux->tsmux, TSMUX_ST_AUDIO_MPEG1,
            ts_data->pid);
        break;
      case 2:
        GST_DEBUG_OBJECT (pad, "Creating MPEG Audio, version 2 stream with "
            "PID 0x%04x", ts_data->pid);
        ts_data->stream = tsmux_create_stream (mux->tsmux, TSMUX_ST_AUDIO_MPEG2,
            ts_data->pid);
        break;
      case 4:
      {
        const GValue *value;
        /* Codec data contains SPS/PPS which need to go in stream for valid ES */
        value = gst_structure_get_value (s, "codec_data");
        if (value) {
          ts_data->codec_data = gst_buffer_ref (gst_value_get_buffer (value));
          GST_DEBUG_OBJECT (pad, "we have additional codec data (%d bytes)",
              GST_BUFFER_SIZE (ts_data->codec_data));
          ts_data->prepare_func = mpegtsmux_prepare_aac;
        } else {
          ts_data->codec_data = NULL;
        }
        GST_DEBUG_OBJECT (pad, "Creating MPEG Audio, version 4 stream with "
            "PID 0x%04x", ts_data->pid);
        ts_data->stream = tsmux_create_stream (mux->tsmux, TSMUX_ST_AUDIO_AAC,
            ts_data->pid);
        break;
      }
      default:
        GST_WARNING_OBJECT (pad, "unsupported mpegversion %d", mpegversion);
        goto beach;
    }
  } else if (gst_structure_has_name (s, "video/mpeg")) {
    gint mpegversion;
    if (!gst_structure_get_int (s, "mpegversion", &mpegversion)) {
      GST_ELEMENT_ERROR (mux, STREAM, FORMAT,
          ("Invalid data format presented"),
          ("Caps with type video/mpeg did not have mpegversion"));
      goto beach;
    }

    if (mpegversion == 1) {
      GST_DEBUG_OBJECT (pad,
          "Creating MPEG Video, version 1 stream with PID 0x%04x",
          ts_data->pid);
      ts_data->stream = tsmux_create_stream (mux->tsmux, TSMUX_ST_VIDEO_MPEG1,
          ts_data->pid);
    } else if (mpegversion == 2) {
      GST_DEBUG_OBJECT (pad,
          "Creating MPEG Video, version 2 stream with PID 0x%04x",
          ts_data->pid);
      ts_data->stream = tsmux_create_stream (mux->tsmux, TSMUX_ST_VIDEO_MPEG2,
          ts_data->pid);
    } else {
      GST_DEBUG_OBJECT (pad,
          "Creating MPEG Video, version 4 stream with PID 0x%04x",
          ts_data->pid);
      ts_data->stream = tsmux_create_stream (mux->tsmux, TSMUX_ST_VIDEO_MPEG4,
          ts_data->pid);
    }
  }

  if (ts_data->stream != NULL) {
    gst_structure_get_int (s, "rate", &ts_data->stream->audio_sampling);
    gst_structure_get_int (s, "channels", &ts_data->stream->audio_channels);
    gst_structure_get_int (s, "bitrate", &ts_data->stream->audio_bitrate);

    tsmux_stream_set_buffer_release_func (ts_data->stream, release_buffer_cb);
    tsmux_program_add_stream (ts_data->prog, ts_data->stream);

    ret = GST_FLOW_OK;
  }

beach:
  gst_caps_unref (caps);
  return ret;
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
        if (idx < 0 || idx >= MAX_PROG_NUMBER) {
          GST_DEBUG_OBJECT (mux, "Program number %d associate with pad %s out "
              "of range (max = %d); DEFAULT_PROGRAM = %d is used instead",
              idx, name, MAX_PROG_NUMBER, DEFAULT_PROG_ID);
          idx = DEFAULT_PROG_ID;
        }
        ts_data->prog_id = idx;
      } else {
        ts_data->prog_id = DEFAULT_PROG_ID;
      }
    }

    ts_data->prog = mux->programs[ts_data->prog_id];
    if (ts_data->prog == NULL) {
      ts_data->prog = tsmux_program_new (mux->tsmux);
      if (ts_data->prog == NULL)
        goto no_program;
      tsmux_set_pmt_interval (ts_data->prog, mux->pmt_interval);
      mux->programs[ts_data->prog_id] = ts_data->prog;
    }

    if (ts_data->stream == NULL) {
      ret = mpegtsmux_create_stream (mux, ts_data, c_data->pad);
      if (ret != GST_FLOW_OK)
        goto no_stream;
    }
  }

  return GST_FLOW_OK;
no_program:
  GST_ELEMENT_ERROR (mux, STREAM, MUX,
      ("Could not create new program"), (NULL));
  return GST_FLOW_ERROR;
no_stream:
  GST_ELEMENT_ERROR (mux, STREAM, MUX,
      ("Could not create handler for stream"), (NULL));
  return ret;
}

static MpegTsPadData *
mpegtsmux_choose_best_stream (MpegTsMux * mux)
{
  MpegTsPadData *best = NULL;
  GstCollectData *c_best = NULL;
  GSList *walk;

  for (walk = mux->collect->data; walk != NULL; walk = g_slist_next (walk)) {
    GstCollectData *c_data = (GstCollectData *) walk->data;
    MpegTsPadData *ts_data = (MpegTsPadData *) walk->data;

    if (ts_data->eos == FALSE) {
      if (ts_data->queued_buf == NULL) {
        GstBuffer *buf;

        ts_data->queued_buf = buf =
            gst_collect_pads_peek (mux->collect, c_data);

        if (buf != NULL) {
          if (ts_data->prepare_func) {
            buf = ts_data->prepare_func (buf, ts_data, mux);
            if (buf) {          /* Take the prepared buffer instead */
              gst_buffer_unref (ts_data->queued_buf);
              ts_data->queued_buf = buf;
            } else {            /* If data preparation returned NULL, use unprepared one */
              buf = ts_data->queued_buf;
            }
          }
          if (GST_BUFFER_TIMESTAMP (buf) != GST_CLOCK_TIME_NONE) {
            /* Ignore timestamps that go backward for now. FIXME: Handle all
             * incoming PTS */
            if (ts_data->last_ts == GST_CLOCK_TIME_NONE ||
                ts_data->last_ts < GST_BUFFER_TIMESTAMP (buf)) {
              ts_data->cur_ts = ts_data->last_ts =
                  gst_segment_to_running_time (&c_data->segment,
                  GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (buf));
            } else {
              GST_DEBUG_OBJECT (mux, "Ignoring PTS that has gone backward");
            }
          } else
            ts_data->cur_ts = GST_CLOCK_TIME_NONE;

          GST_DEBUG_OBJECT (mux, "Pulled buffer with ts %" GST_TIME_FORMAT
              " (uncorrected ts %" GST_TIME_FORMAT " %" G_GUINT64_FORMAT
              ") for PID 0x%04x",
              GST_TIME_ARGS (ts_data->cur_ts),
              GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
              GST_BUFFER_TIMESTAMP (buf), ts_data->pid);

          /* Choose a stream we've never seen a timestamp for to ensure
           * we push enough buffers from it to reach a timestamp */
          if (ts_data->last_ts == GST_CLOCK_TIME_NONE) {
            best = ts_data;
            c_best = c_data;
          }
        } else {
          ts_data->eos = TRUE;
          continue;
        }
      }

      /* If we don't yet have a best pad, take this one, otherwise take
       * whichever has the oldest timestamp */
      if (best != NULL) {
        if (ts_data->last_ts != GST_CLOCK_TIME_NONE &&
            best->last_ts != GST_CLOCK_TIME_NONE &&
            ts_data->last_ts < best->last_ts) {
          best = ts_data;
          c_best = c_data;
        }
      } else {
        best = ts_data;
        c_best = c_data;
      }
    }
  }
  if (c_best) {
    gst_buffer_unref (gst_collect_pads_pop (mux->collect, c_best));
  }

  return best;
}

#define COLLECT_DATA_PAD(collect_data) (((GstCollectData *)(collect_data))->pad)

static GstFlowReturn
mpegtsmux_collected (GstCollectPads * pads, MpegTsMux * mux)
{
  GstFlowReturn ret = GST_FLOW_OK;
  MpegTsPadData *best = NULL;

  GST_DEBUG_OBJECT (mux, "Pads collected");

  if (G_UNLIKELY (mux->first)) {
    ret = mpegtsmux_create_streams (mux);
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      return ret;

    mpegtsdemux_prepare_srcpad (mux);

    mux->first = FALSE;
  }

  best = mpegtsmux_choose_best_stream (mux);

  if (best != NULL) {
    TsMuxProgram *prog = best->prog;
    GstBuffer *buf = best->queued_buf;
    gint64 pts = -1;
    gboolean delta = TRUE;

    if (prog == NULL) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX,
          ("Stream on pad %" GST_PTR_FORMAT
              " is not associated with any program", best), (NULL));
      return GST_FLOW_ERROR;
    }

    if (G_UNLIKELY (prog->pcr_stream == NULL)) {
      if (best) {
        /* Take the first data stream for the PCR */
        GST_DEBUG_OBJECT (COLLECT_DATA_PAD (best),
            "Use stream (pid=%d) from pad as PCR for program (prog_id = %d)",
            MPEG_TS_PAD_DATA (best)->pid, MPEG_TS_PAD_DATA (best)->prog_id);

        /* Set the chosen PCR stream */
        tsmux_program_set_pcr_stream (prog, best->stream);
      }
    }

    g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);
    if (best->stream->is_video_stream)
      delta = GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
    GST_DEBUG_OBJECT (mux, "delta: %d", delta);

    GST_DEBUG_OBJECT (COLLECT_DATA_PAD (best),
        "Chose stream for output (PID: 0x%04x)", best->pid);

    if (GST_CLOCK_TIME_IS_VALID (best->cur_ts)) {
      pts = GSTTIME_TO_MPEGTIME (best->cur_ts);
      GST_DEBUG_OBJECT (mux, "Buffer has TS %" GST_TIME_FORMAT " pts %"
          G_GINT64_FORMAT, GST_TIME_ARGS (best->cur_ts), pts);
    }

    tsmux_stream_add_data (best->stream, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf), buf, pts, -1, !delta);
    best->queued_buf = NULL;

    mux->is_delta = delta;
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
    if (prog->pcr_stream == best->stream) {
      mux->last_ts = best->last_ts;
    }
  } else {
    /* FIXME: Drain all remaining streams */
    /* At EOS */
    gst_pad_push_event (mux->srcpad, gst_event_new_eos ());
  }

  return ret;
write_fail:
  return mux->last_flow_ret;
}

static GstPad *
mpegtsmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
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

  pad_data = (MpegTsPadData *) gst_collect_pads_add_pad (mux->collect, pad,
      sizeof (MpegTsPadData));
  if (pad_data == NULL)
    goto pad_failure;

  pad_data->pid = pid;
  pad_data->last_ts = GST_CLOCK_TIME_NONE;
  pad_data->codec_data = NULL;
  pad_data->prepare_data = NULL;
  pad_data->prepare_func = NULL;
  pad_data->free_func = NULL;
  pad_data->prog_id = -1;
  pad_data->prog = NULL;

  if (G_UNLIKELY (!gst_element_add_pad (element, pad)))
    goto could_not_add;

  return pad;

stream_exists:
  GST_ELEMENT_ERROR (element, STREAM, MUX, ("Duplicate PID requested"), (NULL));
  return NULL;

could_not_add:
  GST_ELEMENT_ERROR (element, STREAM, FAILED,
      ("Internal data stream error."), ("Could not add pad to element"));
  gst_collect_pads_remove_pad (mux->collect, pad);
  gst_object_unref (pad);
  return NULL;
pad_failure:
  GST_ELEMENT_ERROR (element, STREAM, FAILED,
      ("Internal data stream error."), ("Could not add pad to collectpads"));
  gst_object_unref (pad);
  return NULL;
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
  g_return_if_fail (len >= 2);

  if (!mux->streamheader_sent) {
    guint pid = ((data[1] & 0x1f) << 8) | data[2];
    /* if it's a PAT or a PMT */
    if (pid == 0x00 || (pid >= TSMUX_START_PMT_PID && pid < TSMUX_START_ES_PID)) {
      mux->streamheader =
          g_list_append (mux->streamheader, gst_buffer_copy (buf));
    } else if (mux->streamheader) {
      mpegtsdemux_set_header_on_caps (mux);
      mux->streamheader_sent = TRUE;
    }
  }

  /* Set the caps on the buffer only after possibly setting the stream headers
   * into the pad caps above */
  gst_buffer_set_caps (buf, GST_PAD_CAPS (mux->srcpad));

  if (mux->is_delta) {
    GST_LOG_OBJECT (mux, "marking as delta unit");
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_DEBUG_OBJECT (mux, "marking as non-delta unit");
    mux->is_delta = TRUE;
  }
}

static gboolean
new_packet_m2ts (MpegTsMux * mux, guint8 * data, guint len, gint64 new_pcr)
{
  GstBuffer *buf, *out_buf;
  GstFlowReturn ret;
  int chunk_bytes;

  GST_LOG_OBJECT (mux, "Have buffer with new_pcr=%" G_GINT64_FORMAT " size %d",
      new_pcr, len);

  buf = gst_buffer_new_and_alloc (M2TS_PACKET_LENGTH);
  if (G_UNLIKELY (buf == NULL)) {
    GST_ELEMENT_ERROR (mux, STREAM, MUX,
        ("Failed allocating output buffer"), (NULL));
    mux->last_flow_ret = GST_FLOW_ERROR;
    return FALSE;
  }

  new_packet_common_init (mux, buf, data, len);

  /* copies the TS data of 188 bytes to the m2ts buffer at an offset
     of 4 bytes to leave space for writing the timestamp later */
  memcpy (GST_BUFFER_DATA (buf) + 4, data, len);

  if (new_pcr < 0) {
    /* If theres no pcr in current ts packet then just add the packet
       to the adapter for later output when we see a PCR */
    GST_LOG_OBJECT (mux, "Accumulating non-PCR packet");
    gst_adapter_push (mux->adapter, buf);
    return TRUE;
  }

  chunk_bytes = gst_adapter_available (mux->adapter);

  /* We have a new PCR, output anything in the adapter */
  if (mux->first_pcr) {
    /* We can't generate sensible timestamps for anything that might
     * be in the adapter preceding the first PCR and will hit a divide
     * by zero, so empty the adapter. This is probably a null op. */
    gst_adapter_clear (mux->adapter);
    /* Warn if we threw anything away */
    if (chunk_bytes) {
      GST_ELEMENT_WARNING (mux, STREAM, MUX,
          ("Discarding %d bytes from stream preceding first PCR",
              chunk_bytes / M2TS_PACKET_LENGTH * NORMAL_TS_PACKET_LENGTH),
          (NULL));
      chunk_bytes = 0;
    }
    mux->first_pcr = FALSE;
  }

  if (chunk_bytes) {
    /* Start the PCR offset counting at 192 bytes: At the end of the packet
     * that had the last PCR */
    guint64 pcr_bytes = M2TS_PACKET_LENGTH, ts_rate;

    /* Include the pending packet size to get the ts_rate right */
    chunk_bytes += M2TS_PACKET_LENGTH;

    /* calculate rate based on latest and previous pcr values */
    ts_rate = gst_util_uint64_scale (chunk_bytes, CLOCK_FREQ_SCR,
        (new_pcr - mux->previous_pcr));
    GST_LOG_OBJECT (mux, "Processing pending packets with ts_rate %"
        G_GUINT64_FORMAT, ts_rate);

    while (1) {
      guint64 cur_pcr;

      /* Loop, pulling packets of the adapter, updating their 4 byte
       * timestamp header and pushing */

      /* The header is the bottom 30 bits of the PCR, apparently not
       * encoded into base + ext as in the packets themselves, so
       * we can just interpolate, mask and insert */
      cur_pcr = (mux->previous_pcr +
          gst_util_uint64_scale (pcr_bytes, CLOCK_FREQ_SCR, ts_rate));

      out_buf = gst_adapter_take_buffer (mux->adapter, M2TS_PACKET_LENGTH);
      if (G_UNLIKELY (!out_buf))
        break;
      gst_buffer_set_caps (out_buf, GST_PAD_CAPS (mux->srcpad));
      GST_BUFFER_TIMESTAMP (out_buf) = MPEG_SYS_TIME_TO_GSTTIME (cur_pcr);

      /* Write the 4 byte timestamp value, bottom 30 bits only = PCR */
      GST_WRITE_UINT32_BE (GST_BUFFER_DATA (out_buf), cur_pcr & 0x3FFFFFFF);

      GST_LOG_OBJECT (mux, "Outputting a packet of length %d PCR %"
          G_GUINT64_FORMAT, M2TS_PACKET_LENGTH, cur_pcr);
      ret = gst_pad_push (mux->srcpad, out_buf);
      if (G_UNLIKELY (ret != GST_FLOW_OK)) {
        mux->last_flow_ret = ret;
        return FALSE;
      }
      pcr_bytes += M2TS_PACKET_LENGTH;
    }
  }

  /* Finally, output the passed in packet */
  /* Only write the bottom 30 bits of the PCR */
  GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf), new_pcr & 0x3FFFFFFF);
  GST_BUFFER_TIMESTAMP (buf) = MPEG_SYS_TIME_TO_GSTTIME (new_pcr);

  GST_LOG_OBJECT (mux, "Outputting a packet of length %d PCR %"
      G_GUINT64_FORMAT, M2TS_PACKET_LENGTH, new_pcr);
  ret = gst_pad_push (mux->srcpad, buf);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    mux->last_flow_ret = ret;
    return FALSE;
  }

  mux->previous_pcr = new_pcr;

  return TRUE;
}

static gboolean
new_packet_normal_ts (MpegTsMux * mux, guint8 * data, guint len, gint64 new_pcr)
{
  GstBuffer *buf;
  GstFlowReturn ret;

  /* Output a normal TS packet */
  GST_LOG_OBJECT (mux, "Outputting a packet of length %d", len);
  buf = gst_buffer_new_and_alloc (len);
  if (G_UNLIKELY (buf == NULL)) {
    mux->last_flow_ret = GST_FLOW_ERROR;
    return FALSE;
  }

  new_packet_common_init (mux, buf, data, len);

  memcpy (GST_BUFFER_DATA (buf), data, len);
  GST_BUFFER_TIMESTAMP (buf) = mux->last_ts;

  ret = gst_pad_push (mux->srcpad, buf);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    mux->last_flow_ret = ret;
    return FALSE;
  }

  return TRUE;
}

static gboolean
new_packet_cb (guint8 * data, guint len, void *user_data, gint64 new_pcr)
{
  /* Called when the TsMux has prepared a packet for output. Return FALSE
   * on error */
  MpegTsMux *mux = (MpegTsMux *) user_data;

  if (mux->m2ts_mode == TRUE) {
    return new_packet_m2ts (mux, data, len, new_pcr);
  }

  return new_packet_normal_ts (mux, data, len, new_pcr);
}

static void
mpegtsdemux_set_header_on_caps (MpegTsMux * mux)
{
  GstBuffer *buf;
  GstStructure *structure;
  GValue array = { 0 };
  GValue value = { 0 };
  GstCaps *caps;
  GList *sh;

  caps = gst_caps_copy (GST_PAD_CAPS (mux->srcpad));
  structure = gst_caps_get_structure (caps, 0);

  g_value_init (&array, GST_TYPE_ARRAY);

  sh = mux->streamheader;
  while (sh) {
    buf = sh->data;
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);
    g_value_init (&value, GST_TYPE_BUFFER);
    gst_value_take_buffer (&value, buf);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);
    sh = g_list_next (sh);
  }

  g_list_free (mux->streamheader);
  mux->streamheader = NULL;

  gst_structure_set_value (structure, "streamheader", &array);
  gst_pad_set_caps (mux->srcpad, caps);
  g_value_unset (&array);
  gst_caps_unref (caps);
}

static void
mpegtsdemux_prepare_srcpad (MpegTsMux * mux)
{
  GstEvent *new_seg =
      gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES, 0, -1, 0);
  GstCaps *caps = gst_caps_new_simple ("video/mpegts",
      "systemstream", G_TYPE_BOOLEAN, TRUE,
      "packetsize", G_TYPE_INT,
      (mux->m2ts_mode ? M2TS_PACKET_LENGTH : NORMAL_TS_PACKET_LENGTH),
      NULL);

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
      if (mux->adapter)
        gst_adapter_clear (mux->adapter);
      break;
    default:
      break;
  }

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
  if (!gst_element_register (plugin, "mpegtsmux", GST_RANK_PRIMARY,
          mpegtsmux_get_type ()))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (mpegtsmux_debug, "mpegtsmux", 0,
      "MPEG Transport Stream muxer");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    "mpegtsmux", "MPEG-TS muxer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
