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

#include <stdio.h>
#include <string.h>

#include <gst/tag/tag.h>
#include <gst/video/video.h>
#include <gst/mpegts/mpegts.h>
#include <gst/pbutils/pbutils.h>
#include <gst/videoparsers/gstjpeg2000parse.h>
#include <gst/video/video-color.h>

#include "gstbasetsmux.h"
#include "gstbasetsmuxaac.h"
#include "gstbasetsmuxttxt.h"
#include "gstbasetsmuxopus.h"
#include "gstbasetsmuxjpeg2000.h"

GST_DEBUG_CATEGORY (gst_base_ts_mux_debug);
#define GST_CAT_DEFAULT gst_base_ts_mux_debug

/* GstBaseTsMuxPad */

G_DEFINE_TYPE (GstBaseTsMuxPad, gst_base_ts_mux_pad, GST_TYPE_AGGREGATOR_PAD);

/* Internals */

static void
gst_base_ts_mux_pad_reset (GstBaseTsMuxPad * pad)
{
  pad->dts = GST_CLOCK_STIME_NONE;
  pad->prog_id = -1;

  if (pad->free_func)
    pad->free_func (pad->prepare_data);
  pad->prepare_data = NULL;
  pad->prepare_func = NULL;
  pad->free_func = NULL;

  if (pad->codec_data)
    gst_buffer_replace (&pad->codec_data, NULL);

  /* reference owned elsewhere */
  pad->stream = NULL;
  pad->prog = NULL;

  if (pad->language) {
    g_free (pad->language);
    pad->language = NULL;
  }
}

/* GstAggregatorPad implementation */

static GstFlowReturn
gst_base_ts_mux_pad_flush (GstAggregatorPad * agg_pad, GstAggregator * agg)
{
  GList *cur;
  GstBaseTsMux *mux = GST_BASE_TS_MUX (agg);

  /* Send initial segments again after a flush-stop, and also resend the
   * header sections */
  mux->first = TRUE;

  /* output PAT, SI tables */
  tsmux_resend_pat (mux->tsmux);
  tsmux_resend_si (mux->tsmux);

  /* output PMT for each program */
  for (cur = mux->tsmux->programs; cur; cur = cur->next) {
    TsMuxProgram *program = (TsMuxProgram *) cur->data;

    tsmux_resend_pmt (program);
  }

  return GST_FLOW_OK;
}

/* GObject implementation */

static void
gst_base_ts_mux_pad_dispose (GObject * obj)
{
  GstBaseTsMuxPad *ts_pad = GST_BASE_TS_MUX_PAD (obj);

  gst_base_ts_mux_pad_reset (ts_pad);

  G_OBJECT_CLASS (gst_base_ts_mux_pad_parent_class)->dispose (obj);
}

static void
gst_base_ts_mux_pad_class_init (GstBaseTsMuxPadClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAggregatorPadClass *gstaggpad_class = GST_AGGREGATOR_PAD_CLASS (klass);

  gobject_class->dispose = gst_base_ts_mux_pad_dispose;
  gstaggpad_class->flush = gst_base_ts_mux_pad_flush;

  gst_type_mark_as_plugin_api (GST_TYPE_BASE_TS_MUX, 0);
}

static void
gst_base_ts_mux_pad_init (GstBaseTsMuxPad * vaggpad)
{
}

/* GstBaseTsMux */

enum
{
  PROP_0,
  PROP_PROG_MAP,
  PROP_PAT_INTERVAL,
  PROP_PMT_INTERVAL,
  PROP_ALIGNMENT,
  PROP_SI_INTERVAL,
  PROP_BITRATE,
  PROP_PCR_INTERVAL,
  PROP_SCTE_35_PID,
  PROP_SCTE_35_NULL_INTERVAL
};

#define DEFAULT_SCTE_35_PID 0

#define BASETSMUX_DEFAULT_ALIGNMENT    -1

#define CLOCK_BASE 9LL
#define CLOCK_FREQ (CLOCK_BASE * 10000) /* 90 kHz PTS clock */
#define CLOCK_FREQ_SCR (CLOCK_FREQ * 300)       /* 27 MHz SCR clock */

#define GSTTIME_TO_MPEGTIME(time) \
    (((time) > 0 ? (gint64) 1 : (gint64) -1) * \
    (gint64) gst_util_uint64_scale (ABS(time), CLOCK_BASE, GST_MSECOND/10))
/* 27 MHz SCR conversions: */
#define MPEG_SYS_TIME_TO_GSTTIME(time) (gst_util_uint64_scale ((time), \
                        GST_USECOND, CLOCK_FREQ_SCR / 1000000))
#define GSTTIME_TO_MPEG_SYS_TIME(time) (gst_util_uint64_scale ((time), \
                        CLOCK_FREQ_SCR / 1000000, GST_USECOND))

#define DEFAULT_PROG_ID	0

static GstStaticPadTemplate gst_base_ts_mux_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts, "
        "systemstream = (boolean) true, " "packetsize = (int) { 188, 192} ")
    );

typedef struct
{
  GstMapInfo map_info;
  GstBuffer *buffer;
} StreamData;

G_DEFINE_TYPE (GstBaseTsMux, gst_base_ts_mux, GST_TYPE_AGGREGATOR);

/* Internals */

/* Takes over the ref on the buffer */
static StreamData *
stream_data_new (GstBuffer * buffer)
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

#define parent_class gst_base_ts_mux_parent_class

static void
gst_base_ts_mux_set_header_on_caps (GstBaseTsMux * mux)
{
  GstBuffer *buf;
  GstStructure *structure;
  GValue array = { 0 };
  GValue value = { 0 };
  GstCaps *caps;

  caps =
      gst_caps_make_writable (gst_pad_get_current_caps (GST_AGGREGATOR_SRC_PAD
          (mux)));
  structure = gst_caps_get_structure (caps, 0);

  g_value_init (&array, GST_TYPE_ARRAY);

  GST_LOG_OBJECT (mux, "setting %u packets into streamheader",
      g_queue_get_length (&mux->streamheader));

  while ((buf = GST_BUFFER (g_queue_pop_head (&mux->streamheader)))) {
    g_value_init (&value, GST_TYPE_BUFFER);
    gst_value_take_buffer (&value, buf);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);
  }

  gst_structure_set_value (structure, "streamheader", &array);
  gst_aggregator_set_src_caps (GST_AGGREGATOR (mux), caps);
  g_value_unset (&array);
  gst_caps_unref (caps);
}

static gboolean
steal_si_section (GstMpegtsSectionType * type, TsMuxSection * section,
    TsMux * mux)
{
  g_hash_table_insert (mux->si_sections, type, section);

  return TRUE;
}

static void
gst_base_ts_mux_reset (GstBaseTsMux * mux, gboolean alloc)
{
  GstBuffer *buf;
  GstBaseTsMuxClass *klass = GST_BASE_TS_MUX_GET_CLASS (mux);
  GHashTable *si_sections = NULL;
  GList *l;

  mux->first = TRUE;
  mux->last_flow_ret = GST_FLOW_OK;
  mux->last_ts = 0;
  mux->is_delta = TRUE;
  mux->is_header = FALSE;

  mux->streamheader_sent = FALSE;
  mux->pending_key_unit_ts = GST_CLOCK_TIME_NONE;
  gst_event_replace (&mux->force_key_unit_event, NULL);

  if (mux->out_adapter)
    gst_adapter_clear (mux->out_adapter);

  if (mux->tsmux) {
    if (mux->tsmux->si_sections)
      si_sections = g_hash_table_ref (mux->tsmux->si_sections);

    tsmux_free (mux->tsmux);
    mux->tsmux = NULL;
  }

  if (mux->programs) {
    g_hash_table_destroy (mux->programs);
  }
  mux->programs = g_hash_table_new (g_direct_hash, g_direct_equal);

  while ((buf = GST_BUFFER (g_queue_pop_head (&mux->streamheader))))
    gst_buffer_unref (buf);

  gst_event_replace (&mux->force_key_unit_event, NULL);
  gst_buffer_replace (&mux->out_buffer, NULL);

  GST_OBJECT_LOCK (mux);

  for (l = GST_ELEMENT (mux)->sinkpads; l; l = l->next) {
    gst_base_ts_mux_pad_reset (GST_BASE_TS_MUX_PAD (l->data));
  }

  GST_OBJECT_UNLOCK (mux);

  if (alloc) {
    g_assert (klass->create_ts_mux);

    mux->tsmux = klass->create_ts_mux (mux);

    /* Preserve user-specified sections across resets */
    if (si_sections)
      g_hash_table_foreach_steal (si_sections, (GHRFunc) steal_si_section,
          mux->tsmux);
  }

  if (si_sections)
    g_hash_table_unref (si_sections);

  if (klass->reset)
    klass->reset (mux);
}

static void
release_buffer_cb (guint8 * data, void *user_data)
{
  stream_data_free ((StreamData *) user_data);
}

static GstFlowReturn
gst_base_ts_mux_create_stream (GstBaseTsMux * mux, GstBaseTsMuxPad * ts_pad)
{
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstCaps *caps;
  GstStructure *s;
  GstPad *pad;
  guint st = TSMUX_ST_RESERVED;
  const gchar *mt;
  const GValue *value = NULL;
  GstBuffer *codec_data = NULL;
  guint8 opus_channel_config_code = 0;
  guint16 profile = GST_JPEG2000_PARSE_PROFILE_NONE;
  guint8 main_level = 0;
  guint32 max_rate = 0;
  guint8 color_spec = 0;
  j2k_private_data *private_data = NULL;
  const gchar *stream_format = NULL;

  pad = GST_PAD (ts_pad);
  caps = gst_pad_get_current_caps (pad);
  if (caps == NULL)
    goto not_negotiated;

  GST_DEBUG_OBJECT (pad, "Creating stream with PID 0x%04x for caps %"
      GST_PTR_FORMAT, ts_pad->pid, caps);

  s = gst_caps_get_structure (caps, 0);

  mt = gst_structure_get_name (s);
  value = gst_structure_get_value (s, "codec_data");
  if (value != NULL)
    codec_data = gst_value_get_buffer (value);

  stream_format = gst_structure_get_string (s, "stream-format");

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
      case 1:{
        int mpegaudioversion = 1;       /* Assume mpegaudioversion=1 for backwards compatibility */
        (void) gst_structure_get_int (s, "mpegaudioversion", &mpegaudioversion);

        if (mpegaudioversion == 1)
          st = TSMUX_ST_AUDIO_MPEG1;
        else
          st = TSMUX_ST_AUDIO_MPEG2;
        break;
      }
      case 2:{
        /* mpegversion=2 in GStreamer refers to MPEG-2 Part 7 audio,  */

        st = TSMUX_ST_AUDIO_AAC;

        /* Check the stream format. If raw, make dummy internal codec data from the caps */
        if (g_strcmp0 (stream_format, "raw") == 0) {
          ts_pad->codec_data =
              gst_base_ts_mux_aac_mpeg2_make_codec_data (mux, caps);
          ts_pad->prepare_func = gst_base_ts_mux_prepare_aac_mpeg2;
          if (ts_pad->codec_data == NULL) {
            GST_ERROR_OBJECT (mux, "Invalid or incomplete caps for MPEG-2 AAC");
            goto not_negotiated;
          }
        }
        break;
      }
      case 4:
      {
        st = TSMUX_ST_AUDIO_AAC;

        /* Check the stream format. We need codec_data with RAW streams and mpegversion=4 */
        if (g_strcmp0 (stream_format, "raw") == 0) {
          if (codec_data) {
            GST_DEBUG_OBJECT (pad,
                "we have additional codec data (%" G_GSIZE_FORMAT " bytes)",
                gst_buffer_get_size (codec_data));
            ts_pad->codec_data = gst_buffer_ref (codec_data);
            ts_pad->prepare_func = gst_base_ts_mux_prepare_aac_mpeg4;
          } else {
            ts_pad->codec_data = NULL;
            GST_ERROR_OBJECT (mux, "Need codec_data for raw MPEG-4 AAC");
            goto not_negotiated;
          }
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
    ts_pad->prepare_func = gst_base_ts_mux_prepare_teletext;
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
    ts_pad->prepare_func = gst_base_ts_mux_prepare_opus;
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
    /* for now, we relax the condition that profile must exist and equal
     * GST_JPEG2000_PARSE_PROFILE_BC_SINGLE */
    if (vProfile) {
      profile = g_value_get_int (vProfile);
      if (profile != GST_JPEG2000_PARSE_PROFILE_BC_SINGLE) {
        GST_LOG_OBJECT (pad, "Invalid JPEG 2000 profile %d", profile);
        /*goto not_negotiated; */
      }
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
    ts_pad->prepare_func = gst_base_ts_mux_prepare_jpeg2000;
    ts_pad->prepare_data = private_data;
    ts_pad->free_func = gst_base_ts_mux_free_jpeg2000;
  } else {
    GstBaseTsMuxClass *klass = GST_BASE_TS_MUX_GET_CLASS (mux);

    if (klass->handle_media_type) {
      st = klass->handle_media_type (mux, mt, ts_pad);
    }
  }


  if (st != TSMUX_ST_RESERVED) {
    ts_pad->stream = tsmux_create_stream (mux->tsmux, st, ts_pad->pid,
        ts_pad->language);
  } else {
    GST_DEBUG_OBJECT (pad, "Failed to determine stream type");
  }

  if (ts_pad->stream != NULL) {
    const char *interlace_mode = gst_structure_get_string (s, "interlace-mode");
    gst_structure_get_int (s, "rate", &ts_pad->stream->audio_sampling);
    gst_structure_get_int (s, "channels", &ts_pad->stream->audio_channels);
    gst_structure_get_int (s, "bitrate", &ts_pad->stream->audio_bitrate);

    /* frame rate */
    gst_structure_get_fraction (s, "framerate", &ts_pad->stream->num,
        &ts_pad->stream->den);

    /* Interlace mode */
    ts_pad->stream->interlace_mode = FALSE;
    if (interlace_mode) {
      ts_pad->stream->interlace_mode =
          g_str_equal (interlace_mode, "interleaved");
    }
    /* Width and Height */
    gst_structure_get_int (s, "width", &ts_pad->stream->horizontal_size);
    gst_structure_get_int (s, "height", &ts_pad->stream->vertical_size);

    ts_pad->stream->color_spec = color_spec;
    ts_pad->stream->max_bitrate = max_rate;
    ts_pad->stream->profile_and_level = profile | main_level;

    ts_pad->stream->opus_channel_config_code = opus_channel_config_code;

    tsmux_stream_set_buffer_release_func (ts_pad->stream, release_buffer_cb);
    tsmux_program_add_stream (ts_pad->prog, ts_pad->stream);

    ret = GST_FLOW_OK;
  }
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
gst_base_ts_mux_create_pad_stream (GstBaseTsMux * mux, GstPad * pad)
{
  GstBaseTsMuxPad *ts_pad = GST_BASE_TS_MUX_PAD (pad);
  gchar *name = NULL;
  gchar *pcr_name;
  GstFlowReturn ret = GST_FLOW_OK;

  if (ts_pad->prog_id == -1) {
    name = GST_PAD_NAME (pad);
    if (mux->prog_map != NULL && gst_structure_has_field (mux->prog_map, name)) {
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
      ts_pad->prog_id = idx;
    } else {
      ts_pad->prog_id = DEFAULT_PROG_ID;
    }
  }

  ts_pad->prog =
      (TsMuxProgram *) g_hash_table_lookup (mux->programs,
      GINT_TO_POINTER (ts_pad->prog_id));
  if (ts_pad->prog == NULL) {
    ts_pad->prog = tsmux_program_new (mux->tsmux, ts_pad->prog_id);
    if (ts_pad->prog == NULL)
      goto no_program;
    tsmux_set_pmt_interval (ts_pad->prog, mux->pmt_interval);
    tsmux_program_set_scte35_pid (ts_pad->prog, mux->scte35_pid);
    tsmux_program_set_scte35_interval (ts_pad->prog, mux->scte35_null_interval);
    g_hash_table_insert (mux->programs, GINT_TO_POINTER (ts_pad->prog_id),
        ts_pad->prog);
  }

  if (ts_pad->stream == NULL) {
    ret = gst_base_ts_mux_create_stream (mux, ts_pad);
    if (ret != GST_FLOW_OK)
      goto no_stream;
  }

  if (ts_pad->prog->pcr_stream == NULL) {
    /* Take the first stream of the program for the PCR */
    GST_DEBUG_OBJECT (ts_pad,
        "Use stream (pid=%d) from pad as PCR for program (prog_id = %d)",
        ts_pad->pid, ts_pad->prog_id);

    tsmux_program_set_pcr_stream (ts_pad->prog, ts_pad->stream);
  }

  /* Check for user-specified PCR PID */
  pcr_name = g_strdup_printf ("PCR_%d", ts_pad->prog->pgm_number);
  if (mux->prog_map && gst_structure_has_field (mux->prog_map, pcr_name)) {
    const gchar *sink_name = gst_structure_get_string (mux->prog_map, pcr_name);

    if (!g_strcmp0 (name, sink_name)) {
      GST_DEBUG_OBJECT (mux, "User specified stream (pid=%d) as PCR for "
          "program (prog_id = %d)", ts_pad->pid, ts_pad->prog->pgm_number);
      tsmux_program_set_pcr_stream (ts_pad->prog, ts_pad->stream);
    }
  }
  g_free (pcr_name);

  return ret;

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
gst_base_ts_mux_create_pad_stream_func (GstElement * element, GstPad * pad,
    gpointer user_data)
{
  GstFlowReturn *ret = user_data;

  *ret = gst_base_ts_mux_create_pad_stream (GST_BASE_TS_MUX (element), pad);

  return *ret == GST_FLOW_OK;
}

static GstFlowReturn
gst_base_ts_mux_create_streams (GstBaseTsMux * mux)
{
  GstFlowReturn ret = GST_FLOW_OK;

  gst_element_foreach_sink_pad (GST_ELEMENT_CAST (mux),
      gst_base_ts_mux_create_pad_stream_func, &ret);

  return ret;
}

static void
new_packet_common_init (GstBaseTsMux * mux, GstBuffer * buf, guint8 * data,
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
      gst_base_ts_mux_set_header_on_caps (mux);
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
gst_base_ts_mux_push_packets (GstBaseTsMux * mux, gboolean force)
{
  GstBufferList *buffer_list;
  gint align = mux->alignment;
  gint av, packet_size;

  packet_size = mux->packet_size;

  if (align < 0)
    align = mux->automatic_alignment;

  av = gst_adapter_available (mux->out_adapter);
  GST_LOG_OBJECT (mux, "align %d, av %d", align, av);

  if (av == 0)
    return GST_FLOW_OK;

  /* no alignment, just push all available data */
  if (align == 0) {
    buffer_list = gst_adapter_take_buffer_list (mux->out_adapter, av);
    return gst_aggregator_finish_buffer_list (GST_AGGREGATOR (mux),
        buffer_list);
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

      if (packet_size > GST_BASE_TS_MUX_NORMAL_PACKET_LENGTH) {
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
      memset (data + offset + 4, 0, GST_BASE_TS_MUX_NORMAL_PACKET_LENGTH - 4);
      data += packet_size;
    }

    gst_buffer_unmap (buf, &map);
    gst_buffer_list_add (buffer_list, buf);
  }

  return gst_aggregator_finish_buffer_list (GST_AGGREGATOR (mux), buffer_list);
}

static GstFlowReturn
gst_base_ts_mux_collect_packet (GstBaseTsMux * mux, GstBuffer * buf)
{
  GST_LOG_OBJECT (mux, "collecting packet size %" G_GSIZE_FORMAT,
      gst_buffer_get_size (buf));
  gst_adapter_push (mux->out_adapter, buf);

  return GST_FLOW_OK;
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

/* Called when the TsMux has prepared a packet for output. Return FALSE
 * on error */
static gboolean
new_packet_cb (GstBuffer * buf, void *user_data, gint64 new_pcr)
{
  GstBaseTsMux *mux = (GstBaseTsMux *) user_data;
  GstBaseTsMuxClass *klass = GST_BASE_TS_MUX_GET_CLASS (mux);
  GstMapInfo map;

  g_assert (klass->output_packet);

  gst_buffer_map (buf, &map, GST_MAP_READWRITE);

  if (!GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (buf)))
    GST_BUFFER_PTS (buf) = mux->last_ts;

  /* do common init (flags and streamheaders) */
  new_packet_common_init (mux, buf, map.data, map.size);

  gst_buffer_unmap (buf, &map);

  return klass->output_packet (mux, buf, new_pcr);
}

/* called when TsMux needs new packet to write into */
static void
alloc_packet_cb (GstBuffer ** buf, void *user_data)
{
  GstBaseTsMux *mux = (GstBaseTsMux *) user_data;
  GstBaseTsMuxClass *klass = GST_BASE_TS_MUX_GET_CLASS (mux);

  g_assert (klass->allocate_packet);

  klass->allocate_packet (mux, buf);
}

static GstFlowReturn
gst_base_ts_mux_aggregate_buffer (GstBaseTsMux * mux,
    GstAggregatorPad * agg_pad, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBaseTsMuxPad *best = GST_BASE_TS_MUX_PAD (agg_pad);
  TsMuxProgram *prog;
  gint64 pts = GST_CLOCK_STIME_NONE;
  gint64 dts = GST_CLOCK_STIME_NONE;
  gboolean delta = TRUE, header = FALSE;
  StreamData *stream_data;
  GstMpegtsSection *scte_section = NULL;

  GST_DEBUG_OBJECT (mux, "Pads collected");

  if (buf && gst_buffer_get_size (buf) == 0
      && GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_GAP)) {
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }

  if (G_UNLIKELY (mux->first)) {
    ret = gst_base_ts_mux_create_streams (mux);
    if (G_UNLIKELY (ret != GST_FLOW_OK)) {
      if (buf)
        gst_buffer_unref (buf);
      return ret;
    }

    mux->first = FALSE;
  }

  prog = best->prog;
  if (prog == NULL) {
    GList *cur;

    gst_base_ts_mux_create_pad_stream (mux, GST_PAD (best));
    tsmux_resend_pat (mux->tsmux);
    tsmux_resend_si (mux->tsmux);
    prog = best->prog;
    g_assert_nonnull (prog);

    /* output PMT for each program */
    for (cur = mux->tsmux->programs; cur; cur = cur->next) {
      TsMuxProgram *program = (TsMuxProgram *) cur->data;

      tsmux_resend_pmt (program);
    }
  }

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
        &agg_pad->segment, GST_BUFFER_PTS (buf),
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
      gst_pad_push_event (GST_AGGREGATOR_SRC_PAD (mux), event);

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
    GST_DEBUG_OBJECT (best,
        "Use stream (pid=%d) from pad as PCR for program (prog_id = %d)",
        best->pid, best->prog_id);

    /* Set the chosen PCR stream */
    tsmux_program_set_pcr_stream (prog, best->stream);
  }

  GST_DEBUG_OBJECT (best, "Chose stream for output (PID: 0x%04x)", best->pid);

  GST_OBJECT_LOCK (mux);
  scte_section = mux->pending_scte35_section;
  mux->pending_scte35_section = NULL;
  GST_OBJECT_UNLOCK (mux);
  if (G_UNLIKELY (scte_section)) {
    GST_DEBUG_OBJECT (mux, "Sending pending SCTE section");
    if (!tsmux_send_section (mux->tsmux, scte_section))
      GST_ERROR_OBJECT (mux, "Error sending SCTE section !");
  }

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
  return gst_base_ts_mux_push_packets (mux, FALSE);

  /* ERRORS */
write_fail:
  {
    return mux->last_flow_ret;
  }
}

/* GstElement implementation */

static GstPad *
gst_base_ts_mux_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstBaseTsMux *mux = GST_BASE_TS_MUX (element);
  gint pid = -1;
  GstPad *pad = NULL;

  if (name != NULL && sscanf (name, "sink_%d", &pid) == 1) {
    if (tsmux_find_stream (mux->tsmux, pid))
      goto stream_exists;
    /* Make sure we don't use reserved PID.
     * FIXME : This should be extended to other variants (ex: ATSC) reserved PID */
    if (pid < TSMUX_START_ES_PID)
      goto invalid_stream_pid;
  } else {
    pid = tsmux_get_new_pid (mux->tsmux);
  }

  pad = (GstPad *)
      GST_ELEMENT_CLASS (parent_class)->request_new_pad (element,
      templ, name, caps);

  gst_base_ts_mux_pad_reset (GST_BASE_TS_MUX_PAD (pad));
  GST_BASE_TS_MUX_PAD (pad)->pid = pid;

  return pad;

  /* ERRORS */
stream_exists:
  {
    GST_ELEMENT_ERROR (element, STREAM, MUX, ("Duplicate PID requested"),
        (NULL));
    return NULL;
  }

invalid_stream_pid:
  {
    GST_ELEMENT_ERROR (element, STREAM, MUX,
        ("Invalid Elementary stream PID (< 0x40)"), (NULL));
    return NULL;
  }
}

static void
gst_base_ts_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstBaseTsMux *mux = GST_BASE_TS_MUX (element);

  if (mux->tsmux) {
    GList *cur;
    GstBaseTsMuxPad *ts_pad = GST_BASE_TS_MUX_PAD (pad);
    gint pid = ts_pad->pid;

    if (ts_pad->prog) {
      if (ts_pad->prog->pcr_stream == ts_pad->stream) {
        tsmux_program_set_pcr_stream (ts_pad->prog, NULL);
      }
      if (tsmux_remove_stream (mux->tsmux, pid, ts_pad->prog)) {
        g_hash_table_remove (mux->programs, GINT_TO_POINTER (ts_pad->prog_id));
      }
    }

    tsmux_resend_pat (mux->tsmux);
    tsmux_resend_si (mux->tsmux);

    /* output PMT for each program */
    for (cur = mux->tsmux->programs; cur; cur = cur->next) {
      TsMuxProgram *program = (TsMuxProgram *) cur->data;

      tsmux_resend_pmt (program);
    }
  }

  GST_ELEMENT_CLASS (parent_class)->release_pad (element, pad);
}

static gboolean
gst_base_ts_mux_send_event (GstElement * element, GstEvent * event)
{
  GstMpegtsSection *section;
  GstBaseTsMux *mux = GST_BASE_TS_MUX (element);

  section = gst_event_parse_mpegts_section (event);

  if (section) {
    GST_DEBUG ("Received event with mpegts section");

    if (section->section_type == GST_MPEGTS_SECTION_SCTE_SIT) {
      /* Will be sent from the streaming threads */
      GST_DEBUG_OBJECT (mux, "Storing SCTE event");
      GST_OBJECT_LOCK (element);
      if (mux->pending_scte35_section)
        gst_mpegts_section_unref (mux->pending_scte35_section);
      mux->pending_scte35_section = section;
      GST_OBJECT_UNLOCK (element);
    } else {
      /* TODO: Check that the section type is supported */
      tsmux_add_mpegts_si_section (mux->tsmux, section);
    }

    gst_event_unref (event);

    return TRUE;
  }

  return GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
}

/* GstAggregator implementation */

static gboolean
gst_base_ts_mux_sink_event (GstAggregator * agg, GstAggregatorPad * agg_pad,
    GstEvent * event)
{
  GstAggregatorClass *agg_class = GST_AGGREGATOR_CLASS (parent_class);
  GstBaseTsMux *mux = GST_BASE_TS_MUX (agg);
  GstBaseTsMuxPad *ts_pad = GST_BASE_TS_MUX_PAD (agg_pad);
  gboolean res = FALSE;
  gboolean forward = TRUE;

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
      GST_INFO_OBJECT (ts_pad, "have downstream force-key-unit event, "
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
          GST_DEBUG_OBJECT (ts_pad, "Setting language to '%s'", lang_code);

          g_free (ts_pad->language);
          ts_pad->language = g_strdup (lang_code);
        } else {
          GST_WARNING_OBJECT (ts_pad, "Did not get language code for '%s'",
              lang);
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
      /*
         if ((flags & GST_STREAM_FLAG_SPARSE)) {
         GST_COLLECT_PADS_STATE_UNSET (data, GST_COLLECT_PADS_STATE_LOCKED);
         gst_collect_pads_set_waiting (pads, data, FALSE);
         GST_COLLECT_PADS_STATE_SET (data, GST_COLLECT_PADS_STATE_LOCKED);
         }
       */
      break;
    }
    default:
      break;
  }

out:
  if (!forward)
    gst_event_unref (event);
  else
    res = agg_class->sink_event (agg, agg_pad, event);

  return res;
}

static gboolean
gst_base_ts_mux_src_event (GstAggregator * agg, GstEvent * event)
{
  GstAggregatorClass *agg_class = GST_AGGREGATOR_CLASS (parent_class);
  GstBaseTsMux *mux = GST_BASE_TS_MUX (agg);
  gboolean res = TRUE, forward = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      GstIterator *iter;
      GValue sinkpad_value = G_VALUE_INIT;
      GstClockTime running_time;
      gboolean all_headers, done = FALSE, res = FALSE;
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

      while (!done) {
        switch (gst_iterator_next (iter, &sinkpad_value)) {
          case GST_ITERATOR_OK:{
            GstPad *sinkpad = g_value_get_object (&sinkpad_value);
            gboolean tmp;

            GST_INFO_OBJECT (GST_AGGREGATOR_SRC_PAD (agg), "forwarding");
            tmp = gst_pad_push_event (sinkpad, gst_event_ref (event));
            GST_INFO_OBJECT (mux, "result %d", tmp);
            /* succeed if at least one pad succeeds */
            res |= tmp;
            break;
          }
          case GST_ITERATOR_DONE:
            done = TRUE;
            break;
          case GST_ITERATOR_RESYNC:
            gst_iterator_resync (iter);
            break;
          case GST_ITERATOR_ERROR:
            g_assert_not_reached ();
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
    res = agg_class->src_event (agg, event);
  else
    gst_event_unref (event);

  return res;
}

static GstBuffer *
gst_base_ts_mux_clip (GstAggregator * agg,
    GstAggregatorPad * agg_pad, GstBuffer * buf)
{
  GstBaseTsMuxPad *pad = GST_BASE_TS_MUX_PAD (agg_pad);
  GstClockTime time;
  GstBuffer *ret;

  ret = buf;

  /* PTS */
  time = GST_BUFFER_PTS (buf);

  /* invalid left alone and passed */
  if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (time))) {
    time =
        gst_segment_to_running_time (&agg_pad->segment, GST_FORMAT_TIME, time);
    if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (time))) {
      GST_DEBUG_OBJECT (pad, "clipping buffer on pad outside segment");
      gst_buffer_unref (buf);
      ret = NULL;
      goto beach;
    } else {
      GST_LOG_OBJECT (pad, "buffer pts %" GST_TIME_FORMAT " ->  %"
          GST_TIME_FORMAT " running time",
          GST_TIME_ARGS (GST_BUFFER_PTS (buf)), GST_TIME_ARGS (time));
      buf = ret = gst_buffer_make_writable (buf);
      GST_BUFFER_PTS (ret) = time;
    }
  }

  /* DTS */
  time = GST_BUFFER_DTS (buf);

  /* invalid left alone and passed */
  if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (time))) {
    gint sign;
    gint64 dts;

    sign = gst_segment_to_running_time_full (&agg_pad->segment, GST_FORMAT_TIME,
        time, &time);

    if (sign > 0)
      dts = (gint64) time;
    else
      dts = -((gint64) time);

    GST_LOG_OBJECT (pad, "buffer dts %" GST_TIME_FORMAT " -> %"
        GST_STIME_FORMAT " running time", GST_TIME_ARGS (GST_BUFFER_DTS (buf)),
        GST_STIME_ARGS (dts));

    if (GST_CLOCK_STIME_IS_VALID (pad->dts) && dts < pad->dts) {
      /* Ignore DTS going backward */
      GST_WARNING_OBJECT (pad, "ignoring DTS going backward");
      dts = pad->dts;
    }

    ret = gst_buffer_make_writable (buf);
    if (sign > 0)
      GST_BUFFER_DTS (ret) = time;
    else
      GST_BUFFER_DTS (ret) = GST_CLOCK_TIME_NONE;

    pad->dts = dts;
  } else {
    pad->dts = GST_CLOCK_STIME_NONE;
  }

beach:
  return ret;
}

static GstFlowReturn
gst_base_ts_mux_update_src_caps (GstAggregator * agg, GstCaps * caps,
    GstCaps ** ret)
{
  GstBaseTsMux *mux = GST_BASE_TS_MUX (agg);
  GstStructure *s;

  *ret = gst_caps_copy (caps);
  s = gst_caps_get_structure (*ret, 0);
  gst_structure_set (s, "packetsize", G_TYPE_INT, mux->packet_size, NULL);

  return GST_FLOW_OK;
}

static GstBaseTsMuxPad *
gst_base_ts_mux_find_best_pad (GstAggregator * aggregator)
{
  GstBaseTsMuxPad *best = NULL;
  GstClockTime best_ts = GST_CLOCK_TIME_NONE;
  GList *l;

  GST_OBJECT_LOCK (aggregator);

  for (l = GST_ELEMENT_CAST (aggregator)->sinkpads; l; l = l->next) {
    GstBaseTsMuxPad *tpad = GST_BASE_TS_MUX_PAD (l->data);
    GstAggregatorPad *apad = GST_AGGREGATOR_PAD_CAST (tpad);
    GstBuffer *buffer;

    buffer = gst_aggregator_pad_peek_buffer (apad);
    if (!buffer)
      continue;
    if (best_ts == GST_CLOCK_TIME_NONE) {
      best = tpad;
      best_ts = GST_BUFFER_DTS_OR_PTS (buffer);
    } else if (GST_BUFFER_DTS_OR_PTS (buffer) != GST_CLOCK_TIME_NONE) {
      GstClockTime t = GST_BUFFER_DTS_OR_PTS (buffer);
      if (t < best_ts) {
        best = tpad;
        best_ts = t;
      }
    }
    gst_buffer_unref (buffer);
  }

  if (best)
    gst_object_ref (best);

  GST_OBJECT_UNLOCK (aggregator);

  GST_DEBUG_OBJECT (aggregator,
      "Best pad found with %" GST_TIME_FORMAT ": %" GST_PTR_FORMAT,
      GST_TIME_ARGS (best_ts), best);

  return best;
}

static gboolean
gst_base_ts_mux_are_all_pads_eos (GstBaseTsMux * mux)
{
  GList *l;
  gboolean ret = TRUE;

  GST_OBJECT_LOCK (mux);

  for (l = GST_ELEMENT_CAST (mux)->sinkpads; l; l = l->next) {
    GstBaseTsMuxPad *pad = GST_BASE_TS_MUX_PAD (l->data);

    if (!gst_aggregator_pad_is_eos (GST_AGGREGATOR_PAD (pad))) {
      ret = FALSE;
      break;
    }
  }

  GST_OBJECT_UNLOCK (mux);

  return ret;
}


static GstFlowReturn
gst_base_ts_mux_aggregate (GstAggregator * agg, gboolean timeout)
{
  GstBaseTsMux *mux = GST_BASE_TS_MUX (agg);
  GstFlowReturn ret = GST_FLOW_OK;
  GstBaseTsMuxPad *best = gst_base_ts_mux_find_best_pad (agg);

  if (best) {
    GstBuffer *buffer;

    buffer = gst_aggregator_pad_pop_buffer (GST_AGGREGATOR_PAD (best));

    ret =
        gst_base_ts_mux_aggregate_buffer (GST_BASE_TS_MUX (agg),
        GST_AGGREGATOR_PAD (best), buffer);

    gst_object_unref (best);

    if (ret != GST_FLOW_OK)
      goto done;
  }

  if (gst_base_ts_mux_are_all_pads_eos (mux)) {
    GstBaseTsMuxClass *klass = GST_BASE_TS_MUX_GET_CLASS (mux);
    /* drain some possibly cached data */
    if (klass->drain)
      klass->drain (mux);
    gst_base_ts_mux_push_packets (mux, TRUE);

    ret = GST_FLOW_EOS;
  }

done:
  return ret;
}

static gboolean
gst_base_ts_mux_start (GstAggregator * agg)
{
  gst_base_ts_mux_reset (GST_BASE_TS_MUX (agg), TRUE);

  return TRUE;
}

static gboolean
gst_base_ts_mux_stop (GstAggregator * agg)
{
  gst_base_ts_mux_reset (GST_BASE_TS_MUX (agg), TRUE);

  return TRUE;
}

/* GObject implementation */

static void
gst_base_ts_mux_dispose (GObject * object)
{
  GstBaseTsMux *mux = GST_BASE_TS_MUX (object);

  gst_base_ts_mux_reset (mux, FALSE);

  if (mux->out_adapter) {
    g_object_unref (mux->out_adapter);
    mux->out_adapter = NULL;
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
gst_base_ts_mux_constructed (GObject * object)
{
  GstBaseTsMux *mux = GST_BASE_TS_MUX (object);

  /* initial state */
  gst_base_ts_mux_reset (mux, TRUE);
}

static void
gst_base_ts_mux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseTsMux *mux = GST_BASE_TS_MUX (object);
  GList *l;

  switch (prop_id) {
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
      mux->pmt_interval = g_value_get_uint (value);
      GST_OBJECT_LOCK (mux);
      for (l = GST_ELEMENT_CAST (mux)->sinkpads; l; l = l->next) {
        GstBaseTsMuxPad *ts_pad = GST_BASE_TS_MUX_PAD (l->data);

        tsmux_set_pmt_interval (ts_pad->prog, mux->pmt_interval);
      }
      GST_OBJECT_UNLOCK (mux);
      break;
    case PROP_ALIGNMENT:
      mux->alignment = g_value_get_int (value);
      break;
    case PROP_SI_INTERVAL:
      mux->si_interval = g_value_get_uint (value);
      tsmux_set_si_interval (mux->tsmux, mux->si_interval);
      break;
    case PROP_BITRATE:
      mux->bitrate = g_value_get_uint64 (value);
      if (mux->tsmux)
        tsmux_set_bitrate (mux->tsmux, mux->bitrate);
      break;
    case PROP_PCR_INTERVAL:
      mux->pcr_interval = g_value_get_uint (value);
      if (mux->tsmux)
        tsmux_set_pcr_interval (mux->tsmux, mux->pcr_interval);
      break;
    case PROP_SCTE_35_PID:
      mux->scte35_pid = g_value_get_uint (value);
      break;
    case PROP_SCTE_35_NULL_INTERVAL:
      mux->scte35_null_interval = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_ts_mux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseTsMux *mux = GST_BASE_TS_MUX (object);

  switch (prop_id) {
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
    case PROP_BITRATE:
      g_value_set_uint64 (value, mux->bitrate);
      break;
    case PROP_PCR_INTERVAL:
      g_value_set_uint (value, mux->pcr_interval);
      break;
    case PROP_SCTE_35_PID:
      g_value_set_uint (value, mux->scte35_pid);
      break;
    case PROP_SCTE_35_NULL_INTERVAL:
      g_value_set_uint (value, mux->scte35_null_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Default vmethods implementation */

static TsMux *
gst_base_ts_mux_default_create_ts_mux (GstBaseTsMux * mux)
{
  TsMux *tsmux = tsmux_new ();
  tsmux_set_write_func (tsmux, new_packet_cb, mux);
  tsmux_set_alloc_func (tsmux, alloc_packet_cb, mux);
  tsmux_set_bitrate (tsmux, mux->bitrate);

  return tsmux;
}

static void
gst_base_ts_mux_default_allocate_packet (GstBaseTsMux * mux,
    GstBuffer ** buffer)
{
  GstBuffer *buf;

  buf = gst_buffer_new_and_alloc (mux->packet_size);

  *buffer = buf;
}

static gboolean
gst_base_ts_mux_default_output_packet (GstBaseTsMux * mux, GstBuffer * buffer,
    gint64 new_pcr)
{
  gst_base_ts_mux_collect_packet (mux, buffer);

  return TRUE;
}

/* Subclass API */

void
gst_base_ts_mux_set_packet_size (GstBaseTsMux * mux, gsize size)
{
  mux->packet_size = size;
}

void
gst_base_ts_mux_set_automatic_alignment (GstBaseTsMux * mux, gsize alignment)
{
  mux->automatic_alignment = alignment;
}

static void
gst_base_ts_mux_class_init (GstBaseTsMuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstAggregatorClass *gstagg_class = GST_AGGREGATOR_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_base_ts_mux_debug, "basetsmux", 0,
      "MPEG Transport Stream muxer");

  gst_element_class_set_static_metadata (gstelement_class,
      "MPEG Transport Stream Muxer", "Codec/Muxer",
      "Multiplexes media streams into an MPEG Transport Stream",
      "Fluendo <contact@fluendo.com>");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_base_ts_mux_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_base_ts_mux_get_property);
  gobject_class->dispose = gst_base_ts_mux_dispose;
  gobject_class->constructed = gst_base_ts_mux_constructed;

  gstelement_class->request_new_pad = gst_base_ts_mux_request_new_pad;
  gstelement_class->release_pad = gst_base_ts_mux_release_pad;
  gstelement_class->send_event = gst_base_ts_mux_send_event;

  gstagg_class->update_src_caps = gst_base_ts_mux_update_src_caps;
  gstagg_class->aggregate = gst_base_ts_mux_aggregate;
  gstagg_class->clip = gst_base_ts_mux_clip;
  gstagg_class->sink_event = gst_base_ts_mux_sink_event;
  gstagg_class->src_event = gst_base_ts_mux_src_event;
  gstagg_class->start = gst_base_ts_mux_start;
  gstagg_class->stop = gst_base_ts_mux_stop;

  klass->create_ts_mux = gst_base_ts_mux_default_create_ts_mux;
  klass->allocate_packet = gst_base_ts_mux_default_allocate_packet;
  klass->output_packet = gst_base_ts_mux_default_output_packet;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PROG_MAP,
      g_param_spec_boxed ("prog-map", "Program map",
          "A GstStructure specifies the mapping from elementary streams to programs",
          GST_TYPE_STRUCTURE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PAT_INTERVAL,
      g_param_spec_uint ("pat-interval", "PAT interval",
          "Set the interval (in ticks of the 90kHz clock) for writing out the PAT table",
          1, G_MAXUINT, TSMUX_DEFAULT_PAT_INTERVAL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PMT_INTERVAL,
      g_param_spec_uint ("pmt-interval", "PMT interval",
          "Set the interval (in ticks of the 90kHz clock) for writing out the PMT table",
          1, G_MAXUINT, TSMUX_DEFAULT_PMT_INTERVAL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ALIGNMENT,
      g_param_spec_int ("alignment", "packet alignment",
          "Number of packets per buffer (padded with dummy packets on EOS) "
          "(-1 = auto, 0 = all available packets, 7 for UDP streaming)",
          -1, G_MAXINT, BASETSMUX_DEFAULT_ALIGNMENT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SI_INTERVAL,
      g_param_spec_uint ("si-interval", "SI interval",
          "Set the interval (in ticks of the 90kHz clock) for writing out the Service"
          "Information tables", 1, G_MAXUINT, TSMUX_DEFAULT_SI_INTERVAL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BITRATE,
      g_param_spec_uint64 ("bitrate", "Bitrate (in bits per second)",
          "Set the target bitrate, will insert null packets as padding "
          " to achieve multiplex-wide constant bitrate (0 means no padding)",
          0, G_MAXUINT64, TSMUX_DEFAULT_BITRATE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PCR_INTERVAL,
      g_param_spec_uint ("pcr-interval", "PCR interval",
          "Set the interval (in ticks of the 90kHz clock) for writing PCR",
          1, G_MAXUINT, TSMUX_DEFAULT_PCR_INTERVAL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SCTE_35_PID,
      g_param_spec_uint ("scte-35-pid", "SCTE-35 PID",
          "PID to use for inserting SCTE-35 packets (0: unused)",
          0, G_MAXUINT, DEFAULT_SCTE_35_PID,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_SCTE_35_NULL_INTERVAL, g_param_spec_uint ("scte-35-null-interval",
          "SCTE-35 NULL packet interval",
          "Set the interval (in ticks of the 90kHz clock) for writing SCTE-35 NULL (heartbeat) packets."
          " (only valid if scte-35-pid is different from 0)", 1, G_MAXUINT,
          TSMUX_DEFAULT_SCTE_35_NULL_INTERVAL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &gst_base_ts_mux_src_factory, GST_TYPE_AGGREGATOR_PAD);

  gst_type_mark_as_plugin_api (GST_TYPE_BASE_TS_MUX_PAD, 0);
}

static void
gst_base_ts_mux_init (GstBaseTsMux * mux)
{
  mux->out_adapter = gst_adapter_new ();

  /* properties */
  mux->pat_interval = TSMUX_DEFAULT_PAT_INTERVAL;
  mux->pmt_interval = TSMUX_DEFAULT_PMT_INTERVAL;
  mux->si_interval = TSMUX_DEFAULT_SI_INTERVAL;
  mux->pcr_interval = TSMUX_DEFAULT_PCR_INTERVAL;
  mux->prog_map = NULL;
  mux->alignment = BASETSMUX_DEFAULT_ALIGNMENT;
  mux->bitrate = TSMUX_DEFAULT_BITRATE;
  mux->scte35_pid = DEFAULT_SCTE_35_PID;
  mux->scte35_null_interval = TSMUX_DEFAULT_SCTE_35_NULL_INTERVAL;

  mux->packet_size = GST_BASE_TS_MUX_NORMAL_PACKET_LENGTH;
  mux->automatic_alignment = 0;
}
