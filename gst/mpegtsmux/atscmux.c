/* ATSC Transport Stream muxer
 * Copyright (C) 2019 Mathieu Duponchelle <mathieu@centricular.com>
 *
 * atscmux.c:
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

#include "atscmux.h"

GST_DEBUG_CATEGORY (atscmux_debug);
#define GST_CAT_DEFAULT atscmux_debug

G_DEFINE_TYPE (ATSCMux, atscmux, GST_TYPE_MPEG_TSMUX)
#define parent_class atscmux_parent_class
#define ATSCMUX_ST_PS_AUDIO_EAC3 0x87
     static GstStaticPadTemplate atscmux_sink_factory =
         GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/mpeg, "
        "parsed = (boolean) TRUE, "
        "mpegversion = (int) 2, "
        "systemstream = (boolean) false; "
        "video/x-h264,stream-format=(string)byte-stream,"
        "alignment=(string){au, nal}; "
        "audio/x-ac3, framed = (boolean) TRUE;"
        "audio/x-eac3, framed = (boolean) TRUE;"));

     static void
         atscmux_stream_get_es_descrs (TsMuxStream * stream,
    GstMpegtsPMTStream * pmt_stream, MpegTsMux * mpegtsmux)
{
  GstMpegtsDescriptor *descriptor;

  tsmux_stream_default_get_es_descrs (stream, pmt_stream);

  if (stream->stream_type == ATSCMUX_ST_PS_AUDIO_EAC3) {
    guint8 add_info[4];
    guint8 *pos;

    pos = add_info;

    /* audio_stream_descriptor () | ATSC A/52-2018 Annex G
     *
     * descriptor_tag     8 uimsbf
     * descriptor_length  8 uimsbf
     * reserved           1 '1'
     * bsid_flag          1 bslbf
     * mainid_flag        1 bslbf
     * asvc_flag          1 bslbf
     * mixinfoexists      1 bslbf
     * substream1_flag    1 bslbf
     * substream2_flag    1 bslbf
     * substream3_flag    1 bslbf
     * reserved           1 '1'
     * full_service_flag  1 bslbf
     * audio_service_type 3 uimsbf
     * number_of_channels 3 uimsbf
     * [...]
     */

    *pos++ = 0xCC;
    *pos++ = 2;

    /* 1 bit reserved, all other flags unset */
    *pos++ = 0x80;

    /* 1 bit reserved,
     * 1 bit set for full_service_flag,
     * 3 bits hardcoded audio_service_type "Complete Main",
     * 3 bits number_of_channels
     */
    switch (stream->audio_channels) {
      case 1:
        *pos++ = 0xC0;          /* Mono */
        break;
      case 2:
        *pos++ = 0xC0 | 0x2;    /* 2-channel (stereo) */
      case 3:
      case 4:
      case 5:
        *pos++ = 0xC0 | 0x4;    /* Multichannel audio (> 2 channels; <= 3/2 + LFE channels) */
        break;
      case 6:
      default:
        *pos++ = 0xC0 | 0x5;    /* Multichannel audio(> 3/2 + LFE channels) */
    }

    descriptor = gst_mpegts_descriptor_from_registration ("EAC3", add_info, 4);
    g_ptr_array_add (pmt_stream->descriptors, descriptor);

    descriptor =
        gst_mpegts_descriptor_from_custom (GST_MTS_DESC_ATSC_EAC3, add_info, 4);
    g_ptr_array_add (pmt_stream->descriptors, descriptor);
  }
}

static TsMuxStream *
atscmux_create_new_stream (guint16 new_pid,
    TsMuxStreamType stream_type, MpegTsMux * mpegtsmux)
{
  TsMuxStream *ret = tsmux_stream_new (new_pid, stream_type);

  if (stream_type == ATSCMUX_ST_PS_AUDIO_EAC3) {
    ret->id = 0xBD;
    ret->pi.flags |= TSMUX_PACKET_FLAG_PES_FULL_HEADER;
    ret->is_audio = TRUE;
  }

  tsmux_stream_set_get_es_descriptors_func (ret,
      (TsMuxStreamGetESDescriptorsFunc) atscmux_stream_get_es_descrs,
      mpegtsmux);

  return ret;
}

static TsMux *
atscmux_create_ts_mux (MpegTsMux * mpegtsmux)
{
  TsMux *ret = ((MpegTsMuxClass *) parent_class)->create_ts_mux (mpegtsmux);

  tsmux_set_new_stream_func (ret,
      (TsMuxNewStreamFunc) atscmux_create_new_stream, mpegtsmux);

  return ret;
}

static guint
atscmux_handle_media_type (MpegTsMux * mux, const gchar * media_type,
    MpegTsPadData * ts_data)
{
  guint ret = TSMUX_ST_RESERVED;

  if (!g_strcmp0 (media_type, "audio/x-eac3")) {
    ret = ATSCMUX_ST_PS_AUDIO_EAC3;
  }

  return ret;
}

static void
atscmux_class_init (ATSCMuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  MpegTsMuxClass *mpegtsmux_class = (MpegTsMuxClass *) klass;

  GST_DEBUG_CATEGORY_INIT (atscmux_debug, "atscmux", 0, "ATSC muxer");

  gst_element_class_set_static_metadata (gstelement_class,
      "ATSC Transport Stream Muxer", "Codec/Muxer",
      "Multiplexes media streams into an ATSC-compliant Transport Stream",
      "Mathieu Duponchelle <mathieu@centricular.com>");

  mpegtsmux_class->create_ts_mux = atscmux_create_ts_mux;
  mpegtsmux_class->handle_media_type = atscmux_handle_media_type;

  gst_element_class_add_static_pad_template (gstelement_class,
      &atscmux_sink_factory);
}

static void
atscmux_init (ATSCMux * mux)
{
}
