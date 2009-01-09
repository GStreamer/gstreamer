 /*
  * This library is licensed under 2 different licenses and you
  * can choose to use it under the terms of either one of them. The
  * two licenses are the MPL 1.1 and the LGPL.
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
  * The Original Code is Fluendo MPEG Demuxer plugin.
  *
  * The Initial Developer of the Original Code is Fluendo, S.L.
  * Portions created by Fluendo, S.L. are Copyright (C) 2005
  * Fluendo, S.L. All Rights Reserved.
  *
  * Contributor(s): Wim Taymans <wim@fluendo.com>
  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>

#ifdef USE_LIBOIL
#include <liboil/liboil.h>
#endif

#include "gstmpegdefs.h"
#include "gstmpegtsdemux.h"
#include "flutspatinfo.h"
#include "flutspmtinfo.h"

#ifndef GST_CHECK_VERSION
#define GST_CHECK_VERSION(major,minor,micro)  \
    (GST_VERSION_MAJOR > (major) || \
     (GST_VERSION_MAJOR == (major) && GST_VERSION_MINOR > (minor)) || \
     (GST_VERSION_MAJOR == (major) && GST_VERSION_MINOR == (minor) && \
      GST_VERSION_MICRO >= (micro)))
#endif

#ifndef GST_BUFFER_IS_DISCONT
#define GST_BUFFER_IS_DISCONT(buffer) \
    (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))
#endif

GST_DEBUG_CATEGORY_STATIC (gstflutsdemux_debug);
#define GST_CAT_DEFAULT (gstflutsdemux_debug)

/* elementfactory information */
#ifdef USE_LIBOIL
#define LONGNAME "The Fluendo MPEG Transport stream demuxer (liboil build)"
#else
#define LONGNAME "The Fluendo MPEG Transport stream demuxer"
#endif

#ifndef __always_inline
#if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
#define __always_inline inline __attribute__((always_inline))
#else
#define __always_inline inline
#endif
#endif

#ifndef DISABLE_INLINE
#define FORCE_INLINE __always_inline
#else
#define FORCE_INLINE
#endif

static GstElementDetails fluts_demux_details = {
  LONGNAME,
  "Codec/Demuxer",
  "Demultiplexes MPEG2 Transport Streams",
  "Wim Taymans <wim@fluendo.com>"
};

/* MPEG2Demux signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_PROP_ES_PIDS        ""
#define DEFAULT_PROP_CHECK_CRC      TRUE
#define DEFAULT_PROP_PROGRAM_NUMBER -1

/* latency in mseconds */
#define TS_LATENCY 700

enum
{
  PROP_0,
  PROP_ES_PIDS,
  PROP_CHECK_CRC,
  PROP_PROGRAM_NUMBER,
  PROP_PAT_INFO,
  PROP_PMT_INFO,
  PROP_M2TS
};

#define VIDEO_CAPS \
  GST_STATIC_CAPS (\
    "video/mpeg, " \
      "mpegversion = (int) { 1, 2, 4 }, " \
      "systemstream = (boolean) FALSE; " \
    "video/x-h264;" \
    "video/x-dirac" \
  )

#define AUDIO_CAPS \
  GST_STATIC_CAPS ( \
    "audio/mpeg, " \
      "mpegversion = (int) { 1, 4 };" \
    "audio/x-lpcm, " \
      "width = (int) { 16, 20, 24 }, " \
      "rate = (int) { 48000, 96000 }, " \
      "channels = (int) [ 1, 8 ], " \
      "dynamic_range = (int) [ 0, 255 ], " \
      "emphasis = (boolean) { FALSE, TRUE }, " \
      "mute = (boolean) { FALSE, TRUE }; " \
    "audio/x-ac3;" \
    "audio/x-dts" \
  )

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts")
    );

static GstStaticPadTemplate video_template =
GST_STATIC_PAD_TEMPLATE ("video_%04x",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    VIDEO_CAPS);

static GstStaticPadTemplate audio_template =
GST_STATIC_PAD_TEMPLATE ("audio_%04x",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    AUDIO_CAPS);

static GstStaticPadTemplate private_template =
GST_STATIC_PAD_TEMPLATE ("private_%04x",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static void gst_fluts_demux_base_init (GstFluTSDemuxClass * klass);
static void gst_fluts_demux_class_init (GstFluTSDemuxClass * klass);
static void gst_fluts_demux_init (GstFluTSDemux * demux);
static void gst_fluts_demux_finalize (GstFluTSDemux * demux);
static void gst_fluts_demux_reset (GstFluTSDemux * demux);

//static void gst_fluts_demux_remove_pads (GstFluTSDemux * demux);
static void gst_fluts_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_fluts_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_fluts_demux_is_PMT (GstFluTSDemux * demux, guint16 PID);

static gboolean gst_fluts_demux_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_fluts_demux_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_fluts_demux_sink_setcaps (GstPad * pad, GstCaps * caps);

static GstClock *gst_fluts_demux_provide_clock (GstElement * element);
static gboolean gst_fluts_demux_src_pad_query (GstPad * pad, GstQuery * query);

static GstStateChangeReturn gst_fluts_demux_change_state (GstElement * element,
    GstStateChange transition);

static FluTsPmtInfo *fluts_demux_build_pmt_info (GstFluTSDemux * demux,
    guint16 pmt_pid);

static GstElementClass *parent_class = NULL;

/*static guint gst_fluts_demux_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_fluts_demux_get_type (void)
{
  static GType fluts_demux_type = 0;

  if (G_UNLIKELY (!fluts_demux_type)) {
    static const GTypeInfo fluts_demux_info = {
      sizeof (GstFluTSDemuxClass),
      (GBaseInitFunc) gst_fluts_demux_base_init,
      NULL,
      (GClassInitFunc) gst_fluts_demux_class_init,
      NULL,
      NULL,
      sizeof (GstFluTSDemux),
      0,
      (GInstanceInitFunc) gst_fluts_demux_init,
    };

    fluts_demux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstMpegTSDemux",
        &fluts_demux_info, 0);

    GST_DEBUG_CATEGORY_INIT (gstflutsdemux_debug, "mpegtsdemux", 0,
        "MPEG program stream demultiplexer element");
  }

  return fluts_demux_type;
}

static void
gst_fluts_demux_base_init (GstFluTSDemuxClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  klass->sink_template = gst_static_pad_template_get (&sink_template);
  klass->video_template = gst_static_pad_template_get (&video_template);
  klass->audio_template = gst_static_pad_template_get (&audio_template);
  klass->private_template = gst_static_pad_template_get (&private_template);

  gst_element_class_add_pad_template (element_class, klass->video_template);
  gst_element_class_add_pad_template (element_class, klass->audio_template);
  gst_element_class_add_pad_template (element_class, klass->private_template);
  gst_element_class_add_pad_template (element_class, klass->sink_template);

  gst_element_class_set_details (element_class, &fluts_demux_details);
}

static void
gst_fluts_demux_class_init (GstFluTSDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = (GObjectFinalizeFunc) gst_fluts_demux_finalize;
  gobject_class->set_property = gst_fluts_demux_set_property;
  gobject_class->get_property = gst_fluts_demux_get_property;

  g_object_class_install_property (gobject_class, PROP_ES_PIDS,
      g_param_spec_string ("es-pids",
          "Colon separated list of PIDs containing Elementary Streams",
          "PIDs to treat as Elementary Streams in the absence of a PMT, "
          "eg 0x10:0x11:0x20", DEFAULT_PROP_ES_PIDS, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CHECK_CRC,
      g_param_spec_boolean ("check-crc", "Check CRC",
          "Enable CRC checking", DEFAULT_PROP_CHECK_CRC, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_PROGRAM_NUMBER,
      g_param_spec_int ("program-number", "Program Number",
          "Program number to demux for (-1 to ignore)", -1, G_MAXINT,
          DEFAULT_PROP_PROGRAM_NUMBER, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PAT_INFO,
      g_param_spec_value_array ("pat-info",
          "GValueArray containing GObjects with properties",
          "Array of GObjects containing information from the TS PAT "
          "about all programs listed in the current Program Association "
          "Table (PAT)",
          g_param_spec_object ("flu-pat-streaminfo", "FluPATStreamInfo",
              "Fluendo TS Demuxer PAT Stream info object",
              FLUTS_TYPE_PAT_INFO, G_PARAM_READABLE), G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_PMT_INFO,
      g_param_spec_object ("pmt-info",
          "Information about the current program",
          "GObject with properties containing information from the TS PMT "
          "about the currently selected program and its streams",
          FLUTS_TYPE_PMT_INFO, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_M2TS,
      g_param_spec_boolean ("m2ts_mode", "M2TS(192 bytes) Mode",
          "Defines if the input is normal TS ie .ts(188 bytes)"
          "or Blue-Ray Format ie .m2ts(192 bytes).", FALSE, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_fluts_demux_change_state;
  gstelement_class->provide_clock = gst_fluts_demux_provide_clock;
}

static void
gst_fluts_demux_init (GstFluTSDemux * demux)
{
  GstFluTSDemuxClass *klass = GST_FLUTS_DEMUX_GET_CLASS (demux);

  demux->streams = g_malloc0 (sizeof (GstFluTSStream *) * (FLUTS_MAX_PID + 1));
  demux->sinkpad = gst_pad_new_from_template (klass->sink_template, "sink");
  gst_pad_set_chain_function (demux->sinkpad, gst_fluts_demux_chain);
  gst_pad_set_event_function (demux->sinkpad, gst_fluts_demux_sink_event);
  gst_pad_set_setcaps_function (demux->sinkpad, gst_fluts_demux_sink_setcaps);
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  demux->elementary_pids = NULL;
  demux->nb_elementary_pids = 0;
  demux->check_crc = DEFAULT_PROP_CHECK_CRC;
  demux->program_number = DEFAULT_PROP_PROGRAM_NUMBER;
  demux->packetsize = FLUTS_NORMAL_TS_PACKETSIZE;
  demux->m2ts_mode = FALSE;

#ifdef USE_LIBOIL
  oil_init ();
#endif
}

static void
gst_fluts_demux_finalize (GstFluTSDemux * demux)
{
  gst_fluts_demux_reset (demux);
  g_free (demux->streams);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (demux));
}

static void
gst_fluts_demux_reset (GstFluTSDemux * demux)
{
  /* Clean up the streams and pads we allocated */
  gint i;

  for (i = 0; i < FLUTS_MAX_PID + 1; i++) {
    GstFluTSStream *stream = demux->streams[i];

    if (stream != NULL) {
      if (stream->pad)
        gst_element_remove_pad (GST_ELEMENT_CAST (demux), stream->pad);
      if (stream->ES_info)
        gst_mpeg_descriptor_free (stream->ES_info);

      if (stream->PMT.entries)
        g_array_free (stream->PMT.entries, TRUE);
      if (stream->PMT.program_info)
        gst_mpeg_descriptor_free (stream->PMT.program_info);

      if (stream->PAT.entries)
        g_array_free (stream->PAT.entries, TRUE);

      switch (stream->PID_type) {
        case PID_TYPE_ELEMENTARY:
          gst_pes_filter_uninit (&stream->filter);
          break;
        case PID_PROGRAM_ASSOCIATION_TABLE:
        case PID_CONDITIONAL_ACCESS_TABLE:
        case PID_TYPE_PROGRAM_MAP:
          gst_section_filter_uninit (&stream->section_filter);
          break;
      }

      g_free (stream);
      demux->streams[i] = NULL;
    }
  }

  if (demux->clock) {
    g_object_unref (demux->clock);
    demux->clock = NULL;
  }
}

#if 0
static void
gst_fluts_demux_remove_pads (GstFluTSDemux * demux)
{
  /* remove pads we added in preparation for adding new ones */
  /* FIXME: instead of walking all streams, we should retain a list only
   * of streams that have added pads */
  gint i;

  if (demux->need_no_more_pads) {
    gst_element_no_more_pads ((GstElement *) demux);
    demux->need_no_more_pads = FALSE;
  }

  for (i = 0; i < FLUTS_MAX_PID + 1; i++) {
    GstFluTSStream *stream = demux->streams[i];

    if (stream != NULL) {

      if (GST_IS_PAD (stream->pad)) {
        gst_pad_push_event (stream->pad, gst_event_new_eos ());
        gst_element_remove_pad (GST_ELEMENT_CAST (demux), stream->pad);
      }
      stream->pad = NULL;

      if (stream->PID_type == PID_TYPE_ELEMENTARY)
        gst_pes_filter_drain (&stream->filter);
    }
  }
}
#endif


static guint32 crc_tab[256] = {
  0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
  0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
  0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
  0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
  0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
  0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
  0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
  0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
  0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
  0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
  0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
  0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
  0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
  0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
  0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
  0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
  0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
  0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
  0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
  0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
  0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
  0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
  0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
  0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
  0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
  0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
  0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
  0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
  0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
  0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
  0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
  0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
  0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
  0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
  0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
  0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
  0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
  0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
  0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
  0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
  0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
  0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
  0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

/*This function fills the value of negotiated packetsize at sinkpad*/
static gboolean
gst_fluts_demux_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstFluTSDemux *demux = GST_FLUTS_DEMUX (gst_pad_get_parent (pad));
  gboolean ret = FALSE;
  GstStructure *structure = NULL;
  gint expected_packetsize =
      (demux->
      m2ts_mode ? FLUTS_M2TS_TS_PACKETSIZE : FLUTS_NORMAL_TS_PACKETSIZE);
  gint packetsize = expected_packetsize;

  structure = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (demux, "setcaps called with %" GST_PTR_FORMAT, caps);

  if (!gst_structure_get_int (structure, "packetsize", &packetsize)) {
    GST_DEBUG_OBJECT (demux, "packetsize parameter not found in sink caps");
  }

  if (packetsize < expected_packetsize) {
    GST_WARNING_OBJECT (demux, "packetsize = %" G_GINT32_FORMAT "is less then"
        "expected packetsize of %d bytes", packetsize, expected_packetsize);
    goto beach;
  }

  /* here we my have a correct value for packet size */
  demux->packetsize = packetsize;
  ret = TRUE;

beach:
  gst_object_unref (demux);
  return ret;
}

static FORCE_INLINE guint32
gst_fluts_demux_calc_crc32 (guint8 * data, guint datalen)
{
  gint i;
  guint32 crc = 0xffffffff;

  for (i = 0; i < datalen; i++) {
    crc = (crc << 8) ^ crc_tab[((crc >> 24) ^ *data++) & 0xff];
  }
  return crc;
}

static FORCE_INLINE gboolean
gst_fluts_is_dirac_stream (GstFluTSStream * stream)
{
  gboolean is_dirac = FALSE;

  if (stream->stream_type != ST_VIDEO_DIRAC)
    return FALSE;

  if (stream->ES_info != NULL) {
    guint8 *dirac_desc;

    /* Check for a Registration Descriptor to confirm this is dirac */
    dirac_desc = gst_mpeg_descriptor_find (stream->ES_info, DESC_REGISTRATION);
    if (dirac_desc != NULL && DESC_LENGTH (dirac_desc) >= 4) {
      if (DESC_REGISTRATION_format_identifier (dirac_desc) == 0x64726163) {     /* 'drac' in hex */
        is_dirac = TRUE;
      }
    } else {
      /* Check for old mapping as originally specified too */
      dirac_desc = gst_mpeg_descriptor_find (stream->ES_info,
          DESC_DIRAC_TC_PRIVATE);
      if (dirac_desc != NULL && DESC_LENGTH (dirac_desc) == 0)
        is_dirac = TRUE;
    }
  }

  return is_dirac;
}

static FORCE_INLINE gboolean
gst_fluts_stream_is_video (GstFluTSStream * stream)
{
  switch (stream->stream_type) {
    case ST_VIDEO_MPEG1:
    case ST_VIDEO_MPEG2:
    case ST_VIDEO_MPEG4:
    case ST_VIDEO_H264:
      return TRUE;
    case ST_VIDEO_DIRAC:
      return gst_fluts_is_dirac_stream (stream);
  }

  return FALSE;
}

static gboolean
gst_fluts_demux_fill_stream (GstFluTSStream * stream, guint8 id,
    guint8 stream_type)
{
  GstPadTemplate *template;
  gchar *name;
  GstFluTSDemuxClass *klass;
  GstFluTSDemux *demux;
  GstCaps *caps;

  if (stream->stream_type && stream->stream_type != stream_type)
    goto wrong_type;

  demux = stream->demux;
  klass = GST_FLUTS_DEMUX_GET_CLASS (demux);

  name = NULL;
  template = NULL;
  caps = NULL;

  switch (stream_type) {
    case ST_VIDEO_MPEG1:
    case ST_VIDEO_MPEG2:
      template = klass->video_template;
      name = g_strdup_printf ("video_%04x", stream->PID);
      caps = gst_caps_new_simple ("video/mpeg",
          "mpegversion", G_TYPE_INT, stream_type == ST_VIDEO_MPEG1 ? 1 : 2,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      break;
    case ST_AUDIO_MPEG1:
    case ST_AUDIO_MPEG2:
      template = klass->audio_template;
      name = g_strdup_printf ("audio_%04x", stream->PID);
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, NULL);
      break;
    case ST_PRIVATE_DATA:
      /* check if there is an AC3 descriptor associated with this stream
       * from the PMT */
      if (gst_mpeg_descriptor_find (stream->ES_info, DESC_DVB_AC3)) {
        template = klass->audio_template;
        name = g_strdup_printf ("audio_%04x", stream->PID);
        caps = gst_caps_new_simple ("audio/x-ac3", NULL);
      } else if (gst_mpeg_descriptor_find (stream->ES_info, DESC_DVB_TELETEXT)) {
        template = klass->private_template;
        name = g_strdup_printf ("private_%04x", stream->PID);
        caps = gst_caps_new_simple ("private/teletext", NULL);
      }
      break;
    case ST_PRIVATE_SECTIONS:
    case ST_MHEG:
    case ST_DSMCC:
      break;
    case ST_AUDIO_AAC:
      template = klass->audio_template;
      name = g_strdup_printf ("audio_%04x", stream->PID);
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 4, NULL);
      break;
    case ST_VIDEO_MPEG4:
      template = klass->video_template;
      name = g_strdup_printf ("video_%04x", stream->PID);
      caps = gst_caps_new_simple ("video/mpeg",
          "mpegversion", G_TYPE_INT, 4,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      break;
    case ST_VIDEO_H264:
      template = klass->video_template;
      name = g_strdup_printf ("video_%04x", stream->PID);
      caps = gst_caps_new_simple ("video/x-h264", NULL);
      break;
    case ST_VIDEO_DIRAC:
      if (gst_fluts_is_dirac_stream (stream)) {
        template = klass->video_template;
        name = g_strdup_printf ("video_%04x", stream->PID);
        caps = gst_caps_new_simple ("video/x-dirac", NULL);
      }
      break;
    case ST_PS_AUDIO_AC3:
      template = klass->audio_template;
      name = g_strdup_printf ("audio_%04x", stream->PID);
      caps = gst_caps_new_simple ("audio/x-ac3", NULL);
      break;
    case ST_PS_AUDIO_DTS:
      template = klass->audio_template;
      name = g_strdup_printf ("audio_%04x", stream->PID);
      caps = gst_caps_new_simple ("audio/x-dts", NULL);
      break;
    case ST_PS_AUDIO_LPCM:
      template = klass->audio_template;
      name = g_strdup_printf ("audio_%04x", stream->PID);
      caps = gst_caps_new_simple ("audio/x-lpcm", NULL);
      break;
    case ST_PS_DVD_SUBPICTURE:
      break;
    default:
      break;
  }
  if (name == NULL || template == NULL || caps == NULL)
    return FALSE;

  stream->stream_type = stream_type;
  stream->id = id;
  GST_DEBUG ("creating new pad %s", name);
  stream->pad = gst_pad_new_from_template (template, name);
  gst_pad_use_fixed_caps (stream->pad);
  gst_pad_set_caps (stream->pad, caps);
  gst_caps_unref (caps);
  gst_pad_set_query_function (stream->pad,
      GST_DEBUG_FUNCPTR (gst_fluts_demux_src_pad_query));
  g_free (name);

  return TRUE;

wrong_type:
  {
    return FALSE;
  }
}

static FORCE_INLINE gboolean
fluts_is_elem_pid (GstFluTSDemux * demux, guint16 PID)
{
  int i;

  /* check if it's in our partial ts pid list */
  for (i = 0; i < demux->nb_elementary_pids; i++) {
    if (demux->elementary_pids[i] == PID) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
gst_fluts_demux_send_new_segment (GstFluTSDemux * demux,
    GstFluTSStream * stream, gint64 pts)
{
  GstFluTSStream *PCR_stream;
  GstFluTSStream *PMT_stream;
  GstClockTime time;
  guint64 base_PCR;

  /* for the reference start time we need to consult the PCR_PID of the
   * current PMT */
  if (demux->current_PMT == 0)
    goto no_pmt;

  PMT_stream = demux->streams[demux->current_PMT];
  if (PMT_stream == NULL)
    goto no_pmt_stream;

  PCR_stream = demux->streams[PMT_stream->PMT.PCR_PID];
  if (PCR_stream == NULL)
    goto no_pcr_stream;

  if (PCR_stream->base_PCR == -1) {
    GST_DEBUG_OBJECT (demux, "no base PCR, using last PCR %" G_GUINT64_FORMAT,
        PCR_stream->last_PCR);
    PCR_stream->base_PCR = PCR_stream->last_PCR;
  } else {
    GST_DEBUG_OBJECT (demux, "using base PCR %" G_GUINT64_FORMAT,
        PCR_stream->base_PCR);
  }
  if (PCR_stream->last_PCR == -1) {
    GST_DEBUG_OBJECT (demux, "no last PCR, using PTS %" G_GUINT64_FORMAT, pts);
    PCR_stream->base_PCR = pts;
    PCR_stream->last_PCR = pts;
  }
  base_PCR = PCR_stream->base_PCR;

  time = MPEGTIME_TO_GSTTIME (base_PCR);

  GST_DEBUG_OBJECT (demux, "segment PTS to (%" G_GUINT64_FORMAT ") time: %"
      G_GUINT64_FORMAT, base_PCR, time);

  if (demux->clock && demux->clock_base == GST_CLOCK_TIME_NONE) {
    demux->clock_base = gst_clock_get_time (demux->clock);
    gst_clock_set_calibration (demux->clock,
        gst_clock_get_internal_time (demux->clock), demux->clock_base, 1, 1);
  }

  gst_pad_push_event (stream->pad,
      gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, time, -1, 0));

  return TRUE;

  /* ERRORS */
no_pmt:
  {
    /* check if it's in our partial ts pid list */
    if (fluts_is_elem_pid (demux, stream->PID)) {
      GST_DEBUG_OBJECT (demux,
          "Elementary PID, using pts %" G_GUINT64_FORMAT, pts);
      time = MPEGTIME_TO_GSTTIME (pts) + stream->base_time;
      GST_DEBUG_OBJECT (demux, "segment PTS to (%" G_GUINT64_FORMAT ") time: %"
          G_GUINT64_FORMAT, pts, time);

      gst_pad_push_event (stream->pad,
          gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, time, -1, 0));
      return TRUE;
    }

  }
no_pmt_stream:
  {
    GST_DEBUG_OBJECT (demux, "no PMT stream found");
    return FALSE;
  }
no_pcr_stream:
  {
    GST_DEBUG_OBJECT (demux, "no PCR stream found");
    return FALSE;
  }
}

#ifndef GST_FLOW_IS_SUCCESS
#define GST_FLOW_IS_SUCCESS(ret) ((ret) >= GST_FLOW_OK)
#endif

static GstFlowReturn
gst_fluts_demux_combine_flows (GstFluTSDemux * demux, GstFluTSStream * stream,
    GstFlowReturn ret)
{
  gint i;

  /* store the value */
  stream->last_ret = ret;

  /* if it's success we can return the value right away */
  if (GST_FLOW_IS_SUCCESS (ret))
    goto done;

  /* any other error that is not-linked can be returned right
   * away */
  if (ret != GST_FLOW_NOT_LINKED)
    goto done;

  /* only return NOT_LINKED if all other pads returned NOT_LINKED */
  for (i = 0; i < FLUTS_MAX_PID + 1; i++) {
    if (!(stream = demux->streams[i]))
      continue;

    /* some other return value (must be SUCCESS but we can return
     * other values as well) */
    ret = stream->last_ret;
    if (ret != GST_FLOW_NOT_LINKED)
      goto done;
  }
  /* if we get here, all other pads were unlinked and we return
   * NOT_LINKED then */
done:
  return ret;
}


static GstFlowReturn
gst_fluts_demux_data_cb (GstPESFilter * filter, gboolean first,
    GstBuffer * buffer, GstFluTSStream * stream)
{
  GstFluTSDemux *demux;
  GstFlowReturn ret;
  GstPad *srcpad;
  gint64 pts;
  GstClockTime time;

  demux = stream->demux;
  srcpad = stream->pad;

  GST_LOG_OBJECT (demux, "got data on PID 0x%04x", stream->PID);

  if (first && filter->pts != -1) {
    pts = filter->pts;
    time = MPEGTIME_TO_GSTTIME (pts) + stream->base_time;

    if ((stream->last_time > 0 && stream->last_time < time &&
            time - stream->last_time > GST_SECOND * 60 * 10)
        || (stream->last_time > time
            && stream->last_time - time > GST_SECOND * 60 * 10)) {
      /* check first to see if we're in middle of detecting a discont in PCR.
       * if we are we're not sure what timestamp the buffer should have, best
       * to drop. */
      if (stream->PMT_pid <= FLUTS_MAX_PID && demux->streams[stream->PMT_pid] &&
          demux->streams[demux->streams[stream->PMT_pid]->PMT.PCR_PID] &&
          demux->streams[demux->streams[stream->PMT_pid]->PMT.PCR_PID]->
          discont_PCR) {
        GST_WARNING_OBJECT (demux, "middle of discont, dropping");
        goto bad_timestamp;
      }
      /* check for wraparounds */
      else if (stream->last_time > 0 && time < stream->last_time &&
          stream->last_time - time > MPEGTIME_TO_GSTTIME (G_MAXUINT32)) {
        /* wrap around occurred */
        if (stream->base_time + MPEGTIME_TO_GSTTIME ((guint64) (1) << 33) +
            MPEGTIME_TO_GSTTIME (pts) >
            stream->last_time + GST_SECOND * 60 * 10) {
          GST_DEBUG_OBJECT (demux,
              "looks like we have a corrupt packet because its pts is a lot lower than"
              " the previous pts but not a wraparound");
          goto bad_timestamp;
        }
        /* wraparound has occured but before we have detected in the pcr,
         * so check we're actually getting pcr's...if we are, don't update
         * the base time..just set the time and last_time correctly
         */
        if (stream->PMT_pid <= FLUTS_MAX_PID && demux->streams[stream->PMT_pid]
            && demux->streams[demux->streams[stream->PMT_pid]->PMT.PCR_PID]
            && demux->streams[demux->streams[stream->PMT_pid]->PMT.PCR_PID]->
            last_PCR > 0) {
          GST_DEBUG_OBJECT (demux, "timestamps wrapped before noticed in PCR");
          time = MPEGTIME_TO_GSTTIME (pts) + stream->base_time +
              MPEGTIME_TO_GSTTIME ((guint64) (1) << 33);
          stream->last_time = time;
        } else {
          stream->base_time = stream->base_time +
              MPEGTIME_TO_GSTTIME ((guint64) (1) << 33);
          time = MPEGTIME_TO_GSTTIME (pts) + stream->base_time;
          GST_DEBUG_OBJECT (demux,
              "timestamps wrapped around, compensating with new base time: %"
              GST_TIME_FORMAT "last time: %" GST_TIME_FORMAT " time: %"
              GST_TIME_FORMAT, GST_TIME_ARGS (stream->base_time),
              GST_TIME_ARGS (stream->last_time), GST_TIME_ARGS (time));
          stream->last_time = time;
        }
      } else if (stream->last_time > 0 && time > stream->last_time &&
          time - stream->last_time > MPEGTIME_TO_GSTTIME (G_MAXUINT32) &&
          stream->base_time > 0) {
        /* had a previous wrap around */
        if (time - MPEGTIME_TO_GSTTIME ((guint64) (1) << 33) +
            GST_SECOND * 60 * 10 < stream->last_time) {
          GST_DEBUG_OBJECT (demux,
              "looks like we have a corrupt packet because its pts is a lot higher than"
              " the previous pts but not because of a wraparound or pcr discont");
          goto bad_timestamp;
        }
        if (ABS ((time - MPEGTIME_TO_GSTTIME ((guint64) (1) << 33)) -
                stream->last_time) < GST_SECOND) {
          GST_DEBUG_OBJECT (demux,
              "timestamps wrapped around earlier but we have an out of pts: %"
              G_GUINT64_FORMAT ", as %" GST_TIME_FORMAT " translated to: %"
              GST_TIME_FORMAT " and last_time of %" GST_TIME_FORMAT, pts,
              GST_TIME_ARGS (time),
              GST_TIME_ARGS (time - MPEGTIME_TO_GSTTIME ((guint64) (1) << 33)),
              GST_TIME_ARGS (stream->last_time));
          time = time - MPEGTIME_TO_GSTTIME ((guint64) (1) << 33);
        } else {
          GST_DEBUG_OBJECT (demux,
              "timestamp may have wrapped around recently but not sure and pts"
              " is very different, dropping it timestamp of this packet: %"
              GST_TIME_FORMAT " compared to last timestamp: %" GST_TIME_FORMAT,
              GST_TIME_ARGS (time -
                  MPEGTIME_TO_GSTTIME ((guint64) (1) << (33))),
              GST_TIME_ARGS (stream->last_time));
          goto bad_timestamp;
        }

      } else {
        /* we must have a corrupt packet */
        GST_WARNING_OBJECT (demux, "looks like we have a corrupt packet because"
            " its timestamp is buggered timestamp: %" GST_TIME_FORMAT
            " compared to" " last timestamp: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (time), GST_TIME_ARGS (stream->last_time));
        goto bad_timestamp;
      }
    } else {                    /* do not set last_time if a packet with pts from before wrap
                                   around arrived after the wrap around occured */
      stream->last_time = time;
    }
  } else {
    time = GST_CLOCK_TIME_NONE;
    pts = -1;
  }

  GST_LOG_OBJECT (demux, "setting PTS to (%" G_GUINT64_FORMAT ") time: %"
      GST_TIME_FORMAT " on buffer %p first buffer: %d base_time: %"
      GST_TIME_FORMAT, pts, GST_TIME_ARGS (time), buffer, first,
      GST_TIME_ARGS (stream->base_time));

  GST_BUFFER_TIMESTAMP (buffer) = time;

  /* check if we have a pad already */
  if (srcpad == NULL) {
    /* fill in the last bits of the stream */
    /* if no stream type, then assume it based on the PES start code, 
     * needed for partial ts streams without PMT */
    if (G_UNLIKELY (stream->flags & FLUTS_STREAM_FLAG_STREAM_TYPE_UNKNOWN)) {
      if ((filter->start_code & 0xFFFFFFF0) == PACKET_VIDEO_START_CODE) {
        /* it is mpeg2 video */
        stream->stream_type = ST_VIDEO_MPEG2;
        stream->flags &= ~FLUTS_STREAM_FLAG_STREAM_TYPE_UNKNOWN;
        stream->flags |= FLUTS_STREAM_FLAG_IS_VIDEO;
        GST_DEBUG_OBJECT (demux, "Found stream 0x%04x without PMT with video "
            "start_code. Treating as video", stream->PID);
      } else if ((filter->start_code & 0xFFFFFFE0) == PACKET_AUDIO_START_CODE) {
        /* it is mpeg audio */
        stream->stream_type = ST_AUDIO_MPEG2;
        stream->flags &= ~FLUTS_STREAM_FLAG_STREAM_TYPE_UNKNOWN;
        GST_DEBUG_OBJECT (demux, "Found stream 0x%04x without PMT with audio "
            "start_code. Treating as audio", stream->PID);
      } else {
        GST_LOG_OBJECT (demux, "Stream start code on pid 0x%04x is: 0x%x",
            stream->PID, filter->start_code);
      }
    }
    if (!gst_fluts_demux_fill_stream (stream, filter->id, stream->stream_type))
      goto unknown_type;

    GST_DEBUG_OBJECT (demux,
        "New stream 0x%04x of type %d with caps %" GST_PTR_FORMAT, stream->PID,
        stream->stream_type, GST_PAD_CAPS (stream->pad));

    srcpad = stream->pad;

    /* activate and add */
    gst_pad_set_active (srcpad, TRUE);
    gst_element_add_pad (GST_ELEMENT_CAST (demux), srcpad);
    demux->need_no_more_pads = TRUE;

    /* send new_segment */
    gst_fluts_demux_send_new_segment (demux, stream, pts);
  }

  GST_DEBUG_OBJECT (demux, "pushing buffer");
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (srcpad));
  ret = gst_pad_push (srcpad, buffer);
  ret = gst_fluts_demux_combine_flows (demux, stream, ret);

  return ret;

  /* ERROR */
unknown_type:
  {
    GST_DEBUG_OBJECT (demux, "got unknown stream id 0x%02x, type 0x%02x",
        filter->id, filter->type);
    gst_buffer_unref (buffer);
    return gst_fluts_demux_combine_flows (demux, stream, GST_FLOW_NOT_LINKED);
  }
bad_timestamp:
  {
    gst_buffer_unref (buffer);
    return gst_fluts_demux_combine_flows (demux, stream, GST_FLOW_OK);
  }

}

static void
gst_fluts_demux_resync_cb (GstPESFilter * filter, GstFluTSStream * stream)
{
  /* does nothing for now */
}

static gboolean
gst_fluts_demux_is_reserved_PID (GstFluTSDemux * demux, guint16 PID)
{
  return (PID >= PID_RESERVED_FIRST) && (PID < PID_RESERVED_LAST);
}

/* This function assumes that provided PID never will be greater than
 * FLUTS_MAX_PID (13 bits), this is currently guaranteed as everywhere in
 * the code recovered PID at maximum is 13 bits long. 
 */
static FORCE_INLINE GstFluTSStream *
gst_fluts_demux_get_stream_for_PID (GstFluTSDemux * demux, guint16 PID)
{
  GstFluTSStream *stream = NULL;

  stream = demux->streams[PID];

  if (G_UNLIKELY (stream == NULL)) {
    stream = g_new0 (GstFluTSStream, 1);

    stream->demux = demux;
    stream->PID = PID;
    stream->pad = NULL;
    stream->base_PCR = -1;
    stream->last_PCR = -1;
    stream->last_PCR_difference = -1;
    stream->PMT.version_number = -1;
    stream->PAT.version_number = -1;
    stream->PMT_pid = FLUTS_MAX_PID + 1;
    stream->flags |= FLUTS_STREAM_FLAG_STREAM_TYPE_UNKNOWN;
    stream->pes_buffer_in_sync = FALSE;
    switch (PID) {
        /* check for fixed mapping */
      case PID_PROGRAM_ASSOCIATION_TABLE:
        stream->PID_type = PID_TYPE_PROGRAM_ASSOCIATION;
        /* initialise section filter */
        gst_section_filter_init (&stream->section_filter);
        break;
      case PID_CONDITIONAL_ACCESS_TABLE:
        stream->PID_type = PID_TYPE_CONDITIONAL_ACCESS;
        /* initialise section filter */
        gst_section_filter_init (&stream->section_filter);
        break;
      case PID_NULL_PACKET:
        stream->PID_type = PID_TYPE_NULL_PACKET;
        break;
      default:
        /* mark reserved PIDs */
        if (gst_fluts_demux_is_reserved_PID (demux, PID)) {
          stream->PID_type = PID_TYPE_RESERVED;
        } else {
          /* check if PMT found in PAT */
          if (gst_fluts_demux_is_PMT (demux, PID)) {
            stream->PID_type = PID_TYPE_PROGRAM_MAP;
            /* initialise section filter */
            gst_section_filter_init (&stream->section_filter);
          } else
            stream->PID_type = PID_TYPE_UNKNOWN;
        }
        break;
    }
    GST_DEBUG_OBJECT (demux, "creating stream %p for PID 0x%04x, PID_type %d",
        stream, PID, stream->PID_type);

    demux->streams[PID] = stream;
  }

  return stream;
}

/*
 * CA_section() {
 *   table_id                  8 uimsbf   == 0x01
 *   section_syntax_indicator  1 bslbf    == 1
 *   '0'                       1 bslbf    == 0
 *   reserved                  2 bslbf
 *   section_length           12 uimsbf   == 00xxxxx...
 *   reserved                 18 bslbf
 *   version_number            5 uimsbf
 *   current_next_indicator    1 bslbf
 *   section_number            8 uimsbf
 *   last_section_number       8 uimsbf
 *   for (i=0; i<N;i++) {
 *     descriptor()
 *   }
 *   CRC_32                   32 rpchof
 * }
 */
static FORCE_INLINE gboolean
gst_fluts_stream_parse_cat (GstFluTSStream * stream,
    guint8 * data, guint datalen)
{
  GstFluTSDemux *demux;

  demux = stream->demux;

  GST_DEBUG_OBJECT (demux, "parsing CA section");
  return TRUE;
}

static void
gst_fluts_activate_pmt (GstFluTSDemux * demux, GstFluTSStream * stream)
{
  GST_DEBUG_OBJECT (demux, "activating PMT 0x%08x", stream->PID);

  /* gst_fluts_demux_remove_pads (demux); */

  demux->current_PMT = stream->PID;

  /* PMT has been updated, signal the change */
  if (demux->current_PMT == stream->PID)
    g_object_notify ((GObject *) (demux), "pmt-info");
}

/*
 * TS_program_map_section() {
 *   table_id                          8 uimsbf   == 0x02
 *   section_syntax_indicator          1 bslbf    == 1
 *   '0'                               1 bslbf    == 0
 *   reserved                          2 bslbf
 *   section_length                   12 uimsbf   == 00xxxxx...
 *   program_number                   16 uimsbf
 *   reserved                          2 bslbf
 *   version_number                    5 uimsbf
 *   current_next_indicator            1 bslbf
 *   section_number                    8 uimsbf
 *   last_section_number               8 uimsbf
 *   reserved                          3 bslbf
 *   PCR_PID                          13 uimsbf
 *   reserved                          4 bslbf
 *   program_info_length              12 uimsbf   == 00xxxxx...
 *   for (i=0; i<N; i++) {
 *     descriptor()
 *   }
 *   for (i=0;i<N1;i++) {
 *     stream_type                     8 uimsbf
 *     reserved                        3 bslbf
 *     elementary_PID                 13 uimsnf
 *     reserved                        4 bslbf
 *     ES_info_length                 12 uimsbf   == 00xxxxx...
 *     for (i=0; i<N2; i++) {
 *       descriptor()
 *     }
 *   }
 *   CRC_32                           32 rpchof
 * }
 */
static FORCE_INLINE gboolean
gst_fluts_stream_parse_pmt (GstFluTSStream * stream,
    guint8 * data, guint datalen)
{
  GstFluTSDemux *demux;
  gint entries;
  guint32 CRC;
  GstFluTSPMT *PMT;
  guint version_number;
  guint8 current_next_indicator;
  guint16 program_number;

  demux = stream->demux;

  if (*data++ != 0x02)
    goto wrong_id;
  if ((data[0] & 0xc0) != 0x80)
    goto wrong_sync;
  if ((data[0] & 0x0c) != 0x00)
    goto wrong_seclen;

  data += 2;

  if (demux->check_crc)
    if (gst_fluts_demux_calc_crc32 (data - 3, datalen) != 0)
      goto wrong_crc;

  GST_DEBUG_OBJECT (demux, "PMT section_length: %d", datalen - 3);

  PMT = &stream->PMT;

  /* check if version number changed */
  version_number = (data[2] & 0x3e) >> 1;
  GST_DEBUG_OBJECT (demux, "PMT version_number: %d", version_number);

  current_next_indicator = (data[2] & 0x01);
  GST_DEBUG_OBJECT (demux, "PMT current_next_indicator %d",
      current_next_indicator);
  if (current_next_indicator == 0)
    goto not_yet_applicable;
  program_number = GST_READ_UINT16_BE (data);

  if (demux->program_number != -1 && demux->program_number != program_number) {
    goto wrong_program_number;
  }
  if (demux->program_number == -1) {
    GST_INFO_OBJECT (demux, "No program number set, so using first parsed PMT"
        "'s program number: %d", program_number);
    demux->program_number = program_number;
  }

  if (version_number == PMT->version_number)
    goto same_version;

  PMT->version_number = version_number;
  PMT->current_next_indicator = current_next_indicator;

  stream->PMT.program_number = program_number;
  data += 3;
  GST_DEBUG_OBJECT (demux, "PMT program_number: %d", PMT->program_number);

  PMT->section_number = *data++;
  GST_DEBUG_OBJECT (demux, "PMT section_number: %d", PMT->section_number);

  PMT->last_section_number = *data++;
  GST_DEBUG_OBJECT (demux, "PMT last_section_number: %d",
      PMT->last_section_number);

  PMT->PCR_PID = GST_READ_UINT16_BE (data);
  PMT->PCR_PID &= 0x1fff;
  data += 2;
  GST_DEBUG_OBJECT (demux, "PMT PCR_PID: 0x%04x", PMT->PCR_PID);
  /* create or get stream, not much we can say about it except that when we get
   * a data stream and we need a PCR, we can use the stream to get/store the
   * base_PCR. */
  gst_fluts_demux_get_stream_for_PID (demux, PMT->PCR_PID);

  if ((data[0] & 0x0c) != 0x00)
    goto wrong_pilen;

  PMT->program_info_length = GST_READ_UINT16_BE (data);
  PMT->program_info_length &= 0x0fff;
  /* FIXME: validate value of program_info_length */
  data += 2;

  /* FIXME: validate value of program_info_length, before using */

  /* parse descriptor */
  if (G_UNLIKELY (PMT->program_info))
    gst_mpeg_descriptor_free (PMT->program_info);
  PMT->program_info =
      gst_mpeg_descriptor_parse (data, PMT->program_info_length);

  /* skip descriptor */
  data += PMT->program_info_length;
  GST_DEBUG_OBJECT (demux, "PMT program_info_length: %d",
      PMT->program_info_length);

  entries = datalen - 3 - PMT->program_info_length - 9 - 4;

  if (G_UNLIKELY (PMT->entries))
    g_array_free (PMT->entries, TRUE);
  PMT->entries = g_array_new (FALSE, TRUE, sizeof (GstFluTSPMTEntry));

  while (entries > 0) {
    GstFluTSPMTEntry entry;
    GstFluTSStream *ES_stream;
    guint8 stream_type;
    guint16 ES_info_length;

    stream_type = *data++;

    entry.PID = GST_READ_UINT16_BE (data);
    entry.PID &= 0x1fff;
    data += 2;

    if ((data[0] & 0x0c) != 0x00)
      goto wrong_esilen;

    ES_info_length = GST_READ_UINT16_BE (data);
    ES_info_length &= 0x0fff;
    data += 2;

    /* get/create elementary stream */
    ES_stream = gst_fluts_demux_get_stream_for_PID (demux, entry.PID);
    /* check if PID unknown */
    if (ES_stream->PID_type == PID_TYPE_UNKNOWN) {
      /* set as elementary */
      ES_stream->PID_type = PID_TYPE_ELEMENTARY;
      /* set stream type */
      ES_stream->stream_type = stream_type;
      ES_stream->flags &= ~FLUTS_STREAM_FLAG_STREAM_TYPE_UNKNOWN;

      /* init base and last time */
      ES_stream->base_time = 0;
      ES_stream->last_time = 0;

      /* parse descriptor */
      ES_stream->ES_info = gst_mpeg_descriptor_parse (data, ES_info_length);

      if (stream_type == ST_PRIVATE_SECTIONS) {
        /* not really an ES, so use section filter not pes filter */
        /* initialise section filter */
        GstCaps *caps;
        gst_section_filter_init (&ES_stream->section_filter);
        ES_stream->PID_type = PID_TYPE_PRIVATE_SECTION;
        ES_stream->pad = gst_pad_new_from_static_template (&private_template,
            g_strdup_printf ("private_%04x", entry.PID));
        gst_pad_set_active (ES_stream->pad, TRUE);
        caps = gst_caps_new_simple ("application/x-mpegts-private-section",
            NULL);
        gst_pad_use_fixed_caps (ES_stream->pad);
        gst_pad_set_caps (ES_stream->pad, caps);
        gst_caps_unref (caps);

        gst_element_add_pad (GST_ELEMENT_CAST (demux), ES_stream->pad);
      } else {
        /* Recognise video streams based on stream_type */
        if (gst_fluts_stream_is_video (ES_stream))
          ES_stream->flags |= FLUTS_STREAM_FLAG_IS_VIDEO;

        /* set adaptor */
        gst_pes_filter_init (&ES_stream->filter, NULL, NULL);
        gst_pes_filter_set_callbacks (&ES_stream->filter,
            (GstPESFilterData) gst_fluts_demux_data_cb,
            (GstPESFilterResync) gst_fluts_demux_resync_cb, ES_stream);
        if (ES_stream->flags & FLUTS_STREAM_FLAG_IS_VIDEO)
          ES_stream->filter.allow_unbounded = TRUE;
        ES_stream->PMT_pid = stream->PID;
      }
    }
    /* skip descriptor */
    data += ES_info_length;
    GST_DEBUG_OBJECT (demux,
        "  PMT stream_type: %02x, PID: 0x%04x (ES_info_len %d)", stream_type,
        entry.PID, ES_info_length);

    g_array_append_val (PMT->entries, entry);

    entries -= 5 + ES_info_length;
  }
  CRC = GST_READ_UINT32_BE (data);
  GST_DEBUG_OBJECT (demux, "PMT CRC: 0x%08x", CRC);

  if (demux->program_number == -1) {
    /* No program specified, take the first PMT */
    if (demux->current_PMT == 0 || demux->current_PMT == stream->PID)
      gst_fluts_activate_pmt (demux, stream);
  } else {
    /* Program specified, activate this if it matches */
    if (demux->program_number == PMT->program_number)
      gst_fluts_activate_pmt (demux, stream);
  }

  return TRUE;

  /* ERRORS */
wrong_crc:
  {
    GST_DEBUG_OBJECT (demux, "wrong crc");
    return FALSE;
  }
same_version:
  {
    GST_DEBUG_OBJECT (demux, "same version as existing PMT");
    return TRUE;
  }
wrong_program_number:
  {
    GST_DEBUG_OBJECT (demux, "PMT is for program number we don't care about");
    return TRUE;
  }

not_yet_applicable:
  {
    GST_DEBUG_OBJECT (demux, "Ignoring PMT with current_next_indicator = 0");
    return TRUE;
  }
wrong_id:
  {
    GST_DEBUG_OBJECT (demux, "expected table_id == 0, got 0x%02x", data[0]);
    return FALSE;
  }
wrong_sync:
  {
    GST_DEBUG_OBJECT (demux, "expected sync 10, got %02x", data[0]);
    return FALSE;
  }
wrong_seclen:
  {
    GST_DEBUG_OBJECT (demux,
        "first two bits of section length must be 0, got %02x", data[0]);
    return FALSE;
  }
wrong_pilen:
  {
    GST_DEBUG_OBJECT (demux,
        "first two bits of program_info length must be 0, got %02x", data[0]);
    return FALSE;
  }
wrong_esilen:
  {
    GST_DEBUG_OBJECT (demux,
        "first two bits of ES_info length must be 0, got %02x", data[0]);
    g_array_free (stream->PMT.entries, TRUE);
    stream->PMT.entries = NULL;
    gst_mpeg_descriptor_free (stream->PMT.program_info);
    stream->PMT.program_info = NULL;
    return FALSE;
  }
}

/*
 * private_section() {
 *   table_id                                       8 uimsbf
 *   section_syntax_indicator                       1 bslbf
 *   private_indicator                              1 bslbf
 *   reserved                                       2 bslbf
 *   private_section_length                        12 uimsbf
 *   if (section_syntax_indicator == '0') {
 *     for ( i=0;i<N;i++) {
 *       private_data_byte                          8 bslbf
 *     }
 *   }
 *   else {
 *     table_id_extension                          16 uimsbf
 *     reserved                                     2 bslbf
 *     version_number                               5 uimsbf
 *     current_next_indicator                       1 bslbf
 *     section_number                               8 uimsbf
 *     last_section_number                          8 uimsbf
 *     for ( i=0;i<private_section_length-9;i++) {
 *       private_data_byte                          8 bslbf
 *     }
 *     CRC_32                                      32 rpchof
 *   }
 * }
 */
static FORCE_INLINE gboolean
gst_fluts_stream_parse_private_section (GstFluTSStream * stream,
    guint8 * data, guint datalen)
{
  GstFluTSDemux *demux;
  GstBuffer *buffer;
  demux = stream->demux;

  if (demux->check_crc)
    if (gst_fluts_demux_calc_crc32 (data, datalen) != 0)
      goto wrong_crc;

  /* just dump this down the pad */
  if (gst_pad_alloc_buffer (stream->pad, 0, datalen, NULL, &buffer) ==
      GST_FLOW_OK) {
#ifdef USE_LIBOIL
    oil_memcpy (buffer->data, data, datalen);
#else
    memcpy (buffer->data, data, datalen);
#endif
    gst_pad_push (stream->pad, buffer);
  }

  GST_DEBUG_OBJECT (demux, "parsing private section");
  return TRUE;

wrong_crc:
  {
    GST_DEBUG_OBJECT (demux, "wrong crc");
    return FALSE;
  }
}

/*
 * adaptation_field() {
 *   adaptation_field_length                              8 uimsbf
 *   if(adaptation_field_length >0) {
 *     discontinuity_indicator                            1 bslbf
 *     random_access_indicator                            1 bslbf
 *     elementary_stream_priority_indicator               1 bslbf
 *     PCR_flag                                           1 bslbf
 *     OPCR_flag                                          1 bslbf
 *     splicing_point_flag                                1 bslbf
 *     transport_private_data_flag                        1 bslbf
 *     adaptation_field_extension_flag                    1 bslbf
 *     if(PCR_flag == '1') {
 *       program_clock_reference_base                    33 uimsbf
 *       reserved                                         6 bslbf
 *       program_clock_reference_extension                9 uimsbf
 *     }
 *     if(OPCR_flag == '1') {
 *       original_program_clock_reference_base           33 uimsbf
 *       reserved                                         6 bslbf
 *       original_program_clock_reference_extension       9 uimsbf
 *     }
 *     if (splicing_point_flag == '1') {
 *       splice_countdown                                 8 tcimsbf
 *     }
 *     if(transport_private_data_flag == '1') {
 *       transport_private_data_length                    8 uimsbf
 *       for (i=0; i<transport_private_data_length;i++){
 *         private_data_byte                              8 bslbf
 *       }
 *     }
 *     if (adaptation_field_extension_flag == '1' ) {
 *       adaptation_field_extension_length                8 uimsbf
 *       ltw_flag                                         1 bslbf
 *       piecewise_rate_flag                              1 bslbf
 *       seamless_splice_flag                             1 bslbf
 *       reserved                                         5 bslbf
 *       if (ltw_flag == '1') {
 *         ltw_valid_flag                                 1 bslbf
 *         ltw_offset                                    15 uimsbf
 *       }
 *       if (piecewise_rate_flag == '1') {
 *         reserved                                       2 bslbf
 *         piecewise_rate                                22 uimsbf
 *       }
 *       if (seamless_splice_flag == '1'){
 *         splice_type                                    4 bslbf
 *         DTS_next_AU[32..30]                            3 bslbf
 *         marker_bit                                     1 bslbf
 *         DTS_next_AU[29..15]                           15 bslbf
 *         marker_bit                                     1 bslbf
 *         DTS_next_AU[14..0]                            15 bslbf
 *         marker_bit                                     1 bslbf
 *       }
 *       for ( i=0;i<N;i++) {
 *         reserved                                       8 bslbf
 *       }
 *     }
 *     for (i=0;i<N;i++){
 *       stuffing_byte                                    8 bslbf
 *     }
 *   }
 * }
 */
static FORCE_INLINE gboolean
gst_fluts_demux_parse_adaptation_field (GstFluTSStream * stream,
    const guint8 * data, guint data_len, guint * consumed)
{
  GstFluTSDemux *demux;
  guint8 length;
  guint8 *data_end;
  gint i;
  GstFluTSStream *pmt_stream;

  demux = stream->demux;

  data_end = ((guint8 *) data) + data_len;

  length = *data++;
  if (G_UNLIKELY (length > data_len))
    goto wrong_length;

  GST_DEBUG_OBJECT (demux, "parsing adaptation field, length %d", length);

  if (length > 0) {
    guint8 flags = *data++;

    GST_DEBUG_OBJECT (demux, "flags 0x%02x", flags);
    /* discontinuity flag */
    if (flags & 0x80) {
      GST_DEBUG_OBJECT (demux, "discontinuity flag set");
    }
    /* PCR_flag */
    if (flags & 0x10) {
      guint32 pcr1;
      guint16 pcr2;
      guint64 pcr, pcr_ext;
      gboolean valid_pcr = TRUE;

      pcr1 = GST_READ_UINT32_BE (data);
      pcr2 = GST_READ_UINT16_BE (data + 4);
      pcr = ((guint64) pcr1) << 1;
      pcr |= (pcr2 & 0x8000) >> 15;
      pcr_ext = (pcr2 & 0x01ff);
      if (pcr_ext)
        pcr = (pcr * 300 + pcr_ext % 300) / 300;
      GST_DEBUG_OBJECT (demux, "have PCR %" G_GUINT64_FORMAT " on PID 0x%04x "
          "and last pcr is %" G_GUINT64_FORMAT,
          pcr, stream->PID, stream->last_PCR);
      /* pcr has been converted into units of 90Khz ticks 
       * so assume discont if last pcr was > 900000 (10 second) lower */
      if (stream->last_PCR != -1 &&
          (pcr - stream->last_PCR > 900000 || pcr < stream->last_PCR)) {
        GstClockTimeDiff base_time_difference;

        GST_DEBUG_OBJECT (demux,
            "looks like we have a discont, this pcr should really be approx: %"
            G_GUINT64_FORMAT, stream->last_PCR + stream->last_PCR_difference);
        if (stream->discont_PCR == FALSE) {
          if (pcr > stream->last_PCR) {
            base_time_difference = -MPEGTIME_TO_GSTTIME ((pcr -
                    (stream->last_PCR + stream->last_PCR_difference)));
          } else {
            base_time_difference = MPEGTIME_TO_GSTTIME ((stream->last_PCR +
                    stream->last_PCR_difference) - pcr);
          }
          stream->discont_PCR = TRUE;
          stream->discont_difference = base_time_difference;
          valid_pcr = FALSE;
        } else {
          GstClockTimeDiff base_time_difference;

          /* need to update all pmt streams in case this pcr is pcr 
           * for multiple programs */
          int j;
          gboolean *pmts_checked = (gboolean *) & demux->pmts_checked;
          memset (pmts_checked, 0, sizeof (gboolean) * (FLUTS_MAX_PID + 1));

          for (j = 0; j < FLUTS_MAX_PID + 1; j++) {
            if (demux->streams[j] && demux->streams[j]->PMT_pid) {
              if (!pmts_checked[demux->streams[j]->PMT_pid]) {
                /* check if this is correct pcr for pmt */
                if (demux->streams[demux->streams[j]->PMT_pid] &&
                    stream->PID ==
                    demux->streams[demux->streams[j]->PMT_pid]->PMT.PCR_PID) {
                  /* checking the pcr discont is similar this second time
                   * if similar, update the es pids
                   * if not, assume it's a false discont due to corruption
                   * or other */
                  if (pcr > stream->last_PCR) {
                    base_time_difference = -MPEGTIME_TO_GSTTIME ((pcr -
                            (stream->last_PCR + stream->last_PCR_difference)));
                  } else {
                    base_time_difference =
                        MPEGTIME_TO_GSTTIME ((stream->last_PCR +
                            stream->last_PCR_difference) - pcr);
                  }
                  if ((base_time_difference - stream->discont_difference > 0 &&
                          base_time_difference - stream->discont_difference <
                          GST_SECOND * 10) ||
                      (stream->discont_difference - base_time_difference > 0 &&
                          stream->discont_difference - base_time_difference <
                          GST_SECOND * 10)) {
                    pmt_stream = demux->streams[demux->streams[j]->PMT_pid];
                    GST_DEBUG_OBJECT (demux, "Updating base_time on all es "
                        "pids belonging to PMT 0x%02x", stream->PMT_pid);
                    for (i = 0; i <= pmt_stream->PMT.entries->len; i++) {
                      GstFluTSPMTEntry *cur_entry =
                          &g_array_index (pmt_stream->PMT.entries,
                          GstFluTSPMTEntry, i);
                      GST_DEBUG_OBJECT (demux,
                          "Updating base time on " "pid 0x%02x by %"
                          G_GINT64_FORMAT, cur_entry->PID,
                          stream->discont_difference);
                      if (cur_entry->PID <= FLUTS_MAX_PID
                          && demux->streams[cur_entry->PID]) {
                        demux->streams[cur_entry->PID]->base_time +=
                            stream->discont_difference;
                      }
                    }
                  } else {
                    GST_DEBUG_OBJECT (demux, "last PCR discont looked to be "
                        "bogus: previous discont difference %" G_GINT64_FORMAT
                        " now %" G_GINT64_FORMAT, stream->discont_difference,
                        base_time_difference);
                    valid_pcr = FALSE;
                  }
                }
              }
              pmts_checked[demux->streams[j]->PMT_pid] = TRUE;
            }
          }

          stream->discont_PCR = FALSE;
          stream->discont_difference = 0;
        }
      } else if (stream->last_PCR != -1) {
        if (stream->discont_PCR) {
          GST_DEBUG_OBJECT (demux, "last PCR discont looked to be bogus");
          stream->discont_PCR = FALSE;
          stream->discont_difference = 0;
        }
        stream->last_PCR_difference = pcr - stream->last_PCR;
      }
      GST_DEBUG_OBJECT (demux,
          "valid pcr: %d last PCR difference: %" G_GUINT64_FORMAT, valid_pcr,
          stream->last_PCR_difference);
      if (valid_pcr) {

        stream->last_PCR = pcr;

        if (demux->clock && demux->clock_base != GST_CLOCK_TIME_NONE) {
          gdouble r_squared;
          GstFluTSStream *PMT_stream;

          /* for the reference start time we need to consult the PCR_PID of the
           * current PMT */
          PMT_stream = demux->streams[demux->current_PMT];
          if (PMT_stream->PMT.PCR_PID == stream->PID) {
            GST_LOG_OBJECT (demux,
                "internal %" GST_TIME_FORMAT " observation %" GST_TIME_FORMAT
                " pcr: %" G_GUINT64_FORMAT " base_pcr: %" G_GUINT64_FORMAT
                "pid: %d",
                GST_TIME_ARGS (gst_clock_get_internal_time (demux->clock)),
                GST_TIME_ARGS (MPEGTIME_TO_GSTTIME (pcr) -
                    MPEGTIME_TO_GSTTIME (stream->base_PCR) + stream->base_time +
                    demux->clock_base), pcr, stream->base_PCR, stream->PID);
            gst_clock_add_observation (demux->clock,
                gst_clock_get_internal_time (demux->clock),
                demux->clock_base + stream->base_time +
                MPEGTIME_TO_GSTTIME (pcr) -
                MPEGTIME_TO_GSTTIME (stream->base_PCR), &r_squared);
          }
        }
      }
      data += 6;
    }
    /* OPCR_flag */
    if (flags & 0x08) {
      guint32 opcr1;
      guint16 opcr2;
      guint64 opcr, opcr_ext;

      opcr1 = GST_READ_UINT32_BE (data);
      opcr2 = GST_READ_UINT16_BE (data + 4);
      opcr = ((guint64) opcr1) << 1;
      opcr |= (opcr2 & 0x8000) >> 15;
      opcr_ext = (opcr2 & 0x01ff);
      if (opcr_ext)
        opcr = (opcr * 300 + opcr_ext % 300) / 300;
      GST_DEBUG_OBJECT (demux, "have OPCR %" G_GUINT64_FORMAT " on PID 0x%04x",
          opcr, stream->PID);
      stream->last_OPCR = opcr;
      data += 6;
    }
    /* splicing_point_flag */
    if (flags & 0x04) {
      guint8 splice_countdown;

      splice_countdown = *data++;
      GST_DEBUG_OBJECT (demux, "have splicing point, countdown %d",
          splice_countdown);
    }
    /* transport_private_data_flag */
    if (flags & 0x02) {
      guint8 plength = *data++;

      if (data + plength > data_end)
        goto private_data_too_large;

      GST_DEBUG_OBJECT (demux, "have private data, length: %d", plength);
      data += plength;
    }
    /* adaptation_field_extension_flag */
    if (flags & 0x01) {
      GST_DEBUG_OBJECT (demux, "have field extension");
    }
  }

  *consumed = length + 1;
  return TRUE;

  /* ERRORS */
wrong_length:
  {
    GST_DEBUG_OBJECT (demux, "length %d > %d", length, data_len);
    return FALSE;
  }
private_data_too_large:
  {
    GST_DEBUG_OBJECT (demux, "have too large a private data length");
    return FALSE;
  }
}

/*
 * program_association_section() {
 *   table_id                               8 uimsbf   == 0x00
 *   section_syntax_indicator               1 bslbf    == 1
 *   '0'                                    1 bslbf    == 0
 *   reserved                               2 bslbf
 *   section_length                        12 uimsbf   == 00xxxxx...
 *   transport_stream_id                   16 uimsbf
 *   reserved                               2 bslbf
 *   version_number                         5 uimsbf
 *   current_next_indicator                 1 bslbf
 *   section_number                         8 uimsbf
 *   last_section_number                    8 uimsbf
 *   for (i=0; i<N;i++) {
 *     program_number                      16 uimsbf
 *     reserved                             3 bslbf
 *     if(program_number == '0') {
 *       network_PID                       13 uimsbf
 *     }
 *     else {
 *       program_map_PID                   13 uimsbf
 *     }
 *   }
 *   CRC_32                                32 rpchof
 * }
 */
static FORCE_INLINE gboolean
gst_fluts_stream_parse_pat (GstFluTSStream * stream,
    guint8 * data, guint datalen)
{
  GstFluTSDemux *demux;
  gint entries;
  guint32 CRC;
  guint version_number;
  guint8 current_next_indicator;
  GstFluTSPAT *PAT;

  demux = stream->demux;

  if (datalen < 8)
    return FALSE;

  if (*data++ != 0x00)
    goto wrong_id;
  if ((data[0] & 0xc0) != 0x80)
    goto wrong_sync;
  if ((data[0] & 0x0c) != 0x00)
    goto wrong_seclen;

  data += 2;
  GST_DEBUG_OBJECT (demux, "PAT section_length: %d", datalen - 3);

  if (demux->check_crc)
    if (gst_fluts_demux_calc_crc32 (data - 3, datalen) != 0)
      goto wrong_crc;

  PAT = &stream->PAT;

  version_number = (data[2] & 0x3e) >> 1;
  GST_DEBUG_OBJECT (demux, "PAT version_number: %d", version_number);
  if (version_number == PAT->version_number)
    goto same_version;

  current_next_indicator = (data[2] & 0x01);
  GST_DEBUG_OBJECT (demux, "PAT current_next_indicator %d",
      current_next_indicator);
  if (current_next_indicator == 0)
    goto not_yet_applicable;

  PAT->version_number = version_number;
  PAT->current_next_indicator = current_next_indicator;

  PAT->transport_stream_id = GST_READ_UINT16_BE (data);
  data += 3;
  GST_DEBUG_OBJECT (demux, "PAT stream_id: %d", PAT->transport_stream_id);

  PAT->section_number = *data++;
  PAT->last_section_number = *data++;

  GST_DEBUG_OBJECT (demux, "PAT current_next_indicator: %d",
      PAT->current_next_indicator);
  GST_DEBUG_OBJECT (demux, "PAT section_number: %d", PAT->section_number);
  GST_DEBUG_OBJECT (demux, "PAT last_section_number: %d",
      PAT->last_section_number);

  /* 5 bytes after section length and a 4 bytes CRC, 
   * the rest is 4 byte entries */
  entries = (datalen - 3 - 9) / 4;

  if (PAT->entries)
    g_array_free (PAT->entries, TRUE);
  PAT->entries =
      g_array_sized_new (FALSE, TRUE, sizeof (GstFluTSPATEntry), entries);

  while (entries--) {
    GstFluTSPATEntry entry;
    GstFluTSStream *PMT_stream;

    entry.program_number = GST_READ_UINT16_BE (data);
    data += 2;
    entry.PID = GST_READ_UINT16_BE (data);
    entry.PID &= 0x1fff;
    data += 2;

    /* get/create stream for PMT */
    PMT_stream = gst_fluts_demux_get_stream_for_PID (demux, entry.PID);
    if (PMT_stream->PID_type != PID_TYPE_PROGRAM_MAP) {
      /* set as program map */
      PMT_stream->PID_type = PID_TYPE_PROGRAM_MAP;
      /* initialise section filter */
      gst_section_filter_init (&PMT_stream->section_filter);
    }

    g_array_append_val (PAT->entries, entry);

    GST_DEBUG_OBJECT (demux, "  PAT program: %d, PID 0x%04x",
        entry.program_number, entry.PID);
  }
  CRC = GST_READ_UINT32_BE (data);
  GST_DEBUG_OBJECT (demux, "PAT CRC: 0x%08x", CRC);

  /* PAT has been updated, signal the change */
  g_object_notify ((GObject *) (demux), "pat-info");

  return TRUE;

  /* ERRORS */
wrong_crc:
  {
    GST_DEBUG_OBJECT (demux, "wrong crc");
    return FALSE;
  }
same_version:
  {
    GST_DEBUG_OBJECT (demux, "same version as existing PAT");
    return TRUE;
  }
not_yet_applicable:
  {
    GST_DEBUG_OBJECT (demux, "Ignoring PAT with current_next_indicator = 0");
    return TRUE;
  }
wrong_id:
  {
    GST_DEBUG_OBJECT (demux, "expected table_id == 0, got %02x", data[0]);
    return FALSE;
  }
wrong_sync:
  {
    GST_DEBUG_OBJECT (demux, "expected sync 10, got %02x", data[0]);
    return FALSE;
  }
wrong_seclen:
  {
    GST_DEBUG_OBJECT (demux,
        "first two bits of section length must be 0, got %02x", data[0]);
    return FALSE;
  }
}

static gboolean
gst_fluts_demux_is_PMT (GstFluTSDemux * demux, guint16 PID)
{
  GstFluTSStream *stream;
  GstFluTSPAT *PAT;
  gint i;

  /* get the PAT */
  stream = demux->streams[PID_PROGRAM_ASSOCIATION_TABLE];
  if (stream == NULL || stream->PAT.entries == NULL)
    return FALSE;

  PAT = &stream->PAT;

  for (i = 0; i < PAT->entries->len; i++) {
    GstFluTSPATEntry *entry;

    entry = &g_array_index (PAT->entries, GstFluTSPATEntry, i);
    if (!entry)
      continue;

    if (entry->PID == PID)
      return TRUE;
  }
  return FALSE;
}

static FORCE_INLINE GstFlowReturn
gst_fluts_stream_pes_buffer_flush (GstFluTSStream * stream)
{
  GstFlowReturn ret = GST_FLOW_OK;

  if (stream->pes_buffer) {
    GST_BUFFER_SIZE (stream->pes_buffer) = stream->pes_buffer_used;
    ret = gst_pes_filter_push (&stream->filter, stream->pes_buffer);
    if (ret == GST_FLOW_LOST_SYNC)
      stream->pes_buffer_in_sync = FALSE;
    stream->pes_buffer = NULL;
  }
  return ret;
}

static FORCE_INLINE GstFlowReturn
gst_fluts_stream_pes_buffer_push (GstFluTSStream * stream,
    const guint8 * in_data, guint in_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint8 *out_data;

  if (G_UNLIKELY (stream->pes_buffer
          && stream->pes_buffer_used + in_size > stream->pes_buffer_size)) {
    GST_DEBUG ("stream with PID 0x%04x have PES buffer full at %u bytes."
        " Flushing and growing the buffer",
        stream->PID, stream->pes_buffer_size);
    stream->pes_buffer_overflow = TRUE;
    if (stream->pes_buffer_size < (FLUTS_MAX_PES_BUFFER_SIZE >> 1))
      stream->pes_buffer_size <<= 1;

    ret = gst_fluts_stream_pes_buffer_flush (stream);
    if (ret == GST_FLOW_LOST_SYNC)
      goto done;
  }

  if (G_UNLIKELY (!stream->pes_buffer)) {
    /* set initial size of PES buffer */
    if (G_UNLIKELY (stream->pes_buffer_size == 0))
      stream->pes_buffer_size = FLUTS_MIN_PES_BUFFER_SIZE;

    stream->pes_buffer = gst_buffer_new_and_alloc (stream->pes_buffer_size);
    stream->pes_buffer_used = 0;
  }
  out_data = GST_BUFFER_DATA (stream->pes_buffer) + stream->pes_buffer_used;
#ifdef USE_LIBOIL
  oil_memcpy (out_data, in_data, in_size);
#else
  memcpy (out_data, in_data, in_size);
#endif
  stream->pes_buffer_used += in_size;
done:
  return ret;
}

static FORCE_INLINE GstFlowReturn
gst_fluts_demux_pes_buffer_flush (GstFluTSDemux * demux)
{
  gint i;
  GstFlowReturn ret = GST_FLOW_OK;

  for (i = 0; i < FLUTS_MAX_PID + 1; i++) {
    GstFluTSStream *stream = demux->streams[i];
    if (stream && stream->pad) {
      gst_fluts_stream_pes_buffer_flush (stream);
      stream->pes_buffer_in_sync = FALSE;
    }
  }
  return ret;
}

static FORCE_INLINE GstFlowReturn
gst_fluts_demux_push_fragment (GstFluTSStream * stream,
    const guint8 * in_data, guint in_size)
{
  GstFlowReturn ret;
  GstBuffer *es_buf = gst_buffer_new_and_alloc (in_size);
#ifdef USE_LIBOIL
  oil_memcpy (GST_BUFFER_DATA (es_buf), in_data, in_size);
#else
  memcpy (GST_BUFFER_DATA (es_buf), in_data, in_size);
#endif
  ret = gst_pes_filter_push (&stream->filter, es_buf);

  /* If PES filter return ok then PES fragment buffering 
   * can be enabled */
  if (ret == GST_FLOW_OK)
    stream->pes_buffer_in_sync = TRUE;
  else if (ret == GST_FLOW_LOST_SYNC)
    stream->pes_buffer_in_sync = FALSE;
  return ret;
}

/*
 * transport_packet(){
 *   sync_byte                                                               8 bslbf == 0x47
 *   transport_error_indicator                                               1 bslbf
 *   payload_unit_start_indicator                                            1 bslbf
 *   transport _priority                                                     1 bslbf
 *   PID                                                                    13 uimsbf
 *   transport_scrambling_control                                            2 bslbf
 *   adaptation_field_control                                                2 bslbf
 *   continuity_counter                                                      4 uimsbf
 *   if(adaptation_field_control=='10' || adaptation_field_control=='11'){
 *     adaptation_field()
 *   }
 *   if(adaptation_field_control=='01' || adaptation_field_control=='11') {
 *     for (i=0;i<N;i++){
 *       data_byte                                                           8 bslbf
 *     }
 *   }
 * }
 */
static FORCE_INLINE GstFlowReturn
gst_fluts_demux_parse_stream (GstFluTSDemux * demux, GstFluTSStream * stream,
    const guint8 * in_data, guint in_size)
{
  GstFlowReturn ret;
  gboolean transport_error_indicator;
  gboolean payload_unit_start_indicator;
  gboolean transport_priority;
  guint16 PID;
  guint8 transport_scrambling_control;
  guint8 adaptation_field_control;
  guint8 continuity_counter;
  const guint8 *data = in_data;
  guint datalen = in_size;

  transport_error_indicator = (data[0] & 0x80) == 0x80;
  payload_unit_start_indicator = (data[0] & 0x40) == 0x40;
  transport_priority = (data[0] & 0x20) == 0x20;
  PID = stream->PID;
  transport_scrambling_control = (data[2] & 0xc0) >> 6;
  adaptation_field_control = (data[2] & 0x30) >> 4;
  continuity_counter = data[2] & 0x0f;

  data += 3;
  datalen -= 3;

  GST_DEBUG_OBJECT (demux, "afc 0x%x, pusi %d, PID 0x%04x datalen %u",
      adaptation_field_control, payload_unit_start_indicator, PID, datalen);

  ret = GST_FLOW_OK;

  /* packets with adaptation_field_control == 0 must be skipped */
  if (adaptation_field_control == 0)
    goto skip;

  /* parse adaption field if any */
  if (adaptation_field_control & 0x2) {
    guint consumed;

    if (!gst_fluts_demux_parse_adaptation_field (stream, data,
            datalen, &consumed))
      goto done;

    if (datalen <= consumed)
      goto too_small;

    data += consumed;
    datalen -= consumed;
    GST_DEBUG_OBJECT (demux, "consumed: %u datalen: %u", consumed, datalen);
  }

  /* If this packet has a payload, handle it */
  if (adaptation_field_control & 0x1) {
    GST_DEBUG_OBJECT (demux, "Packet payload %d bytes, PID 0x%04x", datalen,
        PID);

    /* For unknown streams, check if the PID is in the partial PIDs
     * list as an elementary stream and override the type if so 
     */
    if (stream->PID_type == PID_TYPE_UNKNOWN) {
      if (fluts_is_elem_pid (demux, PID)) {
        GST_DEBUG_OBJECT (demux,
            "PID 0x%04x is an elementary stream in the PID list", PID);
        stream->PID_type = PID_TYPE_ELEMENTARY;
        stream->flags |= FLUTS_STREAM_FLAG_STREAM_TYPE_UNKNOWN;
        stream->base_time = 0;
        stream->last_time = 0;

        /* Clear any existing descriptor */
        if (stream->ES_info) {
          gst_mpeg_descriptor_free (stream->ES_info);
          stream->ES_info = NULL;
        }

        /* Initialise our PES filter */
        gst_pes_filter_init (&stream->filter, NULL, NULL);
        gst_pes_filter_set_callbacks (&stream->filter,
            (GstPESFilterData) gst_fluts_demux_data_cb,
            (GstPESFilterResync) gst_fluts_demux_resync_cb, stream);
      }
    }

    /* now parse based on the stream type */
    switch (stream->PID_type) {
      case PID_TYPE_PROGRAM_ASSOCIATION:
      case PID_TYPE_CONDITIONAL_ACCESS:
      case PID_TYPE_PROGRAM_MAP:
      case PID_TYPE_PRIVATE_SECTION:
      {
        GstBuffer *sec_buf;
        guint8 *section_data;
        guint16 section_length;
        guint8 pointer;

        /* do stuff with our section */
        if (payload_unit_start_indicator) {
          pointer = *data++;
          datalen -= 1;
          if (pointer >= datalen) {
            GST_DEBUG_OBJECT (demux, "pointer: 0x%02x too large", pointer);
            return GST_FLOW_OK;
          }
          data += pointer;
          datalen -= pointer;
        }

        /* FIXME: try to use data directly instead of creating a buffer and
           pushing in into adapter at section filter */
        sec_buf = gst_buffer_new_and_alloc (datalen);
#ifdef USE_LIBOIL
        oil_memcpy (GST_BUFFER_DATA (sec_buf), data, datalen);
#else
        memcpy (GST_BUFFER_DATA (sec_buf), data, datalen);
#endif
        if (gst_section_filter_push (&stream->section_filter,
                payload_unit_start_indicator, continuity_counter, sec_buf)) {
          GST_DEBUG_OBJECT (demux, "section finished");
          /* section ready */
          section_length = stream->section_filter.section_length;
          section_data =
              (guint8 *) gst_adapter_peek (stream->section_filter.adapter,
              section_length + 3);

          switch (stream->PID_type) {
            case PID_TYPE_PROGRAM_ASSOCIATION:
              gst_fluts_stream_parse_pat (stream, section_data,
                  section_length + 3);
              break;
            case PID_TYPE_CONDITIONAL_ACCESS:
              gst_fluts_stream_parse_cat (stream, section_data,
                  section_length + 3);
              break;
            case PID_TYPE_PROGRAM_MAP:
              gst_fluts_stream_parse_pmt (stream, section_data,
                  section_length + 3);
              break;
            case PID_TYPE_PRIVATE_SECTION:
              gst_fluts_stream_parse_private_section (stream, section_data,
                  section_length + 3);
              break;
          }

          gst_section_filter_clear (&stream->section_filter);

        } else {
          /* section still going, don't parse left */
          GST_DEBUG_OBJECT (demux, "section still going for PID 0x%04x", PID);
        }
        break;
      }
      case PID_TYPE_NULL_PACKET:
        GST_DEBUG_OBJECT (demux,
            "skipping PID 0x%04x, type 0x%04x (NULL packet)", PID,
            stream->PID_type);
        break;
      case PID_TYPE_UNKNOWN:
        GST_DEBUG_OBJECT (demux, "skipping unknown PID 0x%04x, type 0x%04x",
            PID, stream->PID_type);
        break;
      case PID_TYPE_ELEMENTARY:
      {
        if (payload_unit_start_indicator) {
          GST_DEBUG_OBJECT (demux, "new PES start for PID 0x%04x, used %u"
              "bytes of %u bytes in the PES buffer",
              PID, stream->pes_buffer_used, stream->pes_buffer_size);
          /* Flush buffered PES data */
          gst_fluts_stream_pes_buffer_flush (stream);
          gst_pes_filter_drain (&stream->filter);
          /* Resize the buffer to half if no overflow detected and
           * had been used less than half of it */
          if (stream->pes_buffer_overflow == FALSE
              && stream->pes_buffer_used < (stream->pes_buffer_size >> 1)) {
            stream->pes_buffer_size >>= 1;
            if (stream->pes_buffer_size < FLUTS_MIN_PES_BUFFER_SIZE)
              stream->pes_buffer_size = FLUTS_MIN_PES_BUFFER_SIZE;
            GST_DEBUG_OBJECT (demux, "PES buffer size reduced to %u bytes",
                stream->pes_buffer_size);
          }
          /* mark the stream not in sync to give a chance on PES filter to 
           * detect lost sync */
          stream->pes_buffer_in_sync = FALSE;
          stream->pes_buffer_overflow = FALSE;
        }
        GST_LOG_OBJECT (demux, "Elementary packet of size %u for PID 0x%04x",
            datalen, PID);

        if (datalen > 0) {
          if (!stream->pes_buffer_in_sync) {
            /* Push the first fragment to PES filter to have a chance to
             * detect GST_FLOW_LOST_SYNC.
             */
            GST_LOG_OBJECT (demux, "fragment directly pushed to PES filter");
            ret = gst_fluts_demux_push_fragment (stream, data, datalen);
          } else {
            /* Otherwhise we buffer the PES fragment */
            ret = gst_fluts_stream_pes_buffer_push (stream, data, datalen);
            /* If sync is lost here is due a pes_buffer_flush and we can try
             * to resync in the PES filter with the current fragment
             */
            if (ret == GST_FLOW_LOST_SYNC) {
              GST_LOG_OBJECT (demux, "resync, fragment pushed to PES filter");
              ret = gst_fluts_demux_push_fragment (stream, data, datalen);
            }
          }

          break;
        } else {
          GST_WARNING_OBJECT (demux, "overflow of datalen: %u so skipping",
              datalen);
          return GST_FLOW_OK;
        }

      }
    }
  }

done:
  return ret;

skip:
  {
    GST_DEBUG_OBJECT (demux, "skipping, adaptation_field_control == 0");
    return GST_FLOW_OK;
  }
too_small:
  {
    GST_DEBUG_OBJECT (demux, "skipping, adaptation_field consumed all data");
    return GST_FLOW_OK;
  }
}

static FORCE_INLINE GstFlowReturn
gst_fluts_demux_parse_transport_packet (GstFluTSDemux * demux,
    const guint8 * data)
{
  GstFlowReturn ret;
  guint16 PID;
  GstFluTSStream *stream;

  /* skip sync byte */
  data++;

  /* get PID */
  PID = ((data[0] & 0x1f) << 8) | data[1];

  /* get the stream. */
  stream = gst_fluts_demux_get_stream_for_PID (demux, PID);

  /* parse the stream */
  ret = gst_fluts_demux_parse_stream (demux, stream, data,
      FLUTS_NORMAL_TS_PACKETSIZE - 1);

  return ret;

  /* ERRORS */
}

static gboolean
gst_fluts_demux_send_event (GstFluTSDemux * demux, GstEvent * event)
{
  gint i;
  gboolean have_stream = FALSE, res = TRUE;

  for (i = 0; i < FLUTS_MAX_PID + 1; i++) {
    GstFluTSStream *stream = demux->streams[i];

    if (stream && stream->pad) {
      res &= gst_pad_push_event (stream->pad, gst_event_ref (event));
      have_stream = TRUE;
    }
  }
  gst_event_unref (event);

  return have_stream;
}

static gboolean
gst_fluts_demux_sink_event (GstPad * pad, GstEvent * event)
{
  GstFluTSDemux *demux = GST_FLUTS_DEMUX (gst_pad_get_parent (pad));
  gboolean res;

  GST_DEBUG_OBJECT (demux, "got event %s",
      gst_event_type_get_name (GST_EVENT_TYPE (event)));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_fluts_demux_send_event (demux, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_adapter_clear (demux->adapter);
      res = gst_fluts_demux_send_event (demux, event);
      break;
    case GST_EVENT_EOS:
      /* Flush buffered PES data */
      gst_fluts_demux_pes_buffer_flush (demux);
      /* Send the EOS event on each stream */
      if (!(res = gst_fluts_demux_send_event (demux, event))) {
        /* we have no streams */
        GST_ELEMENT_ERROR (demux, STREAM, TYPE_NOT_FOUND,
            (NULL), ("No valid streams found at EOS"));
      }
      break;
    case GST_EVENT_NEWSEGMENT:
      res = gst_fluts_demux_send_event (demux, event);
      break;
    default:
      res = gst_fluts_demux_send_event (demux, event);
      break;
  }
  gst_object_unref (demux);

  return TRUE;
}

static gboolean
gst_fluts_demux_provides_clock (GstElement * element)
{
  GstFluTSDemux *demux;
  GstQuery *query;
  gboolean is_live = FALSE;
  GstPad *peer;

  demux = GST_FLUTS_DEMUX (element);
  query = gst_query_new_latency ();
  peer = gst_pad_get_peer (demux->sinkpad);

  if (peer) {
    if (gst_pad_query (peer, query))
      gst_query_parse_latency (query, &is_live, NULL, NULL);
    gst_object_unref (peer);
  }
  gst_query_unref (query);

  return is_live;
}

static GstClock *
gst_fluts_demux_provide_clock (GstElement * element)
{
  GstFluTSDemux *demux = GST_FLUTS_DEMUX (element);

  if (gst_fluts_demux_provides_clock (element)) {
    if (demux->clock == NULL) {
      demux->clock = g_object_new (GST_TYPE_SYSTEM_CLOCK, "name",
          "FluTSClock", NULL);
      demux->clock_base = GST_CLOCK_TIME_NONE;
    }

    return demux->clock;
  }

  return NULL;
}

static gboolean
gst_fluts_demux_src_pad_query (GstPad * pad, GstQuery * query)
{
  GstFluTSDemux *demux = GST_FLUTS_DEMUX (gst_pad_get_parent (pad));
  gboolean res = FALSE;
  GstPad *peer;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      peer = gst_pad_get_peer (demux->sinkpad);
      if (peer) {
        res = gst_pad_query (peer, query);
        if (res) {
          gboolean is_live;
          GstClockTime min_latency, max_latency;

          gst_query_parse_latency (query, &is_live, &min_latency, &max_latency);
          if (is_live) {
            min_latency += TS_LATENCY * GST_MSECOND;
            if (max_latency != GST_CLOCK_TIME_NONE)
              max_latency += TS_LATENCY * GST_MSECOND;
          }

          gst_query_set_latency (query, is_live, min_latency, max_latency);
        }
        gst_object_unref (peer);
      }
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      gint64 duration;

      gst_query_parse_duration (query, &format, &duration);

      if (format == GST_FORMAT_BYTES) {
        res = FALSE;
      } else {
        res = gst_pad_query_default (pad, query);
      }

      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
  }

  return res;
}


static FORCE_INLINE gint
is_mpegts_sync (const guint8 * in_data, const guint8 * end_data,
    guint packetsize)
{
  guint ret = 0;
  if (G_LIKELY (IS_MPEGTS_SYNC (in_data)))
    return 100;

  if (in_data + packetsize < end_data - 5) {
    if (G_LIKELY (IS_MPEGTS_SYNC (in_data + packetsize)))
      ret += 50;
  }

  if (in_data[0] == 0x47) {
    ret += 25;

    if ((in_data[1] & 0x80) == 0x00)
      ret += 10;

    if ((in_data[3] & 0x10) == 0x10)
      ret += 5;
  }
  return ret;
}


static FORCE_INLINE guint
gst_fluts_demux_sync_scan (GstFluTSDemux * demux, const guint8 * in_data,
    guint size, guint * flush)
{
  guint sync_count = 0;
  const guint8 *end_scan = in_data + size - demux->packetsize;
  guint8 *ptr_data = (guint8 *) in_data;

  while (ptr_data <= end_scan && sync_count < LENGTH_SYNC_LUT) {
    /* if sync code is found try to store it in the LUT */
    guint chance = is_mpegts_sync (ptr_data, end_scan, demux->packetsize);
    if (G_LIKELY (chance > 50)) {
      /* skip paketsize bytes and try find next */
      guint8 *next_sync = ptr_data + demux->packetsize;
      if (next_sync < end_scan) {
        demux->sync_lut[sync_count] = ptr_data;
        sync_count++;
        ptr_data += demux->packetsize;
      } else
        goto done;

    } else {
      ptr_data++;
    }
  }
done:
  *flush = ptr_data - in_data;

  return sync_count;
}

static GstFlowReturn
gst_fluts_demux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFluTSDemux *demux = GST_FLUTS_DEMUX (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_OK;
  const guint8 *data;
  guint avail;
  guint flush = 0;
  gint i;
  guint sync_count;

  if (GST_BUFFER_IS_DISCONT (buffer)) {
    /* Flush buffered PES data */
    gst_fluts_demux_pes_buffer_flush (demux);
    gst_adapter_clear (demux->adapter);
  }
  /* first push the new buffer into the adapter */
  gst_adapter_push (demux->adapter, buffer);

  /* check if there's enough data to parse a packet */
  avail = gst_adapter_available (demux->adapter);
  if (G_UNLIKELY (avail < demux->packetsize))
    goto done;

  /* recover all data from adapter */
  data = gst_adapter_peek (demux->adapter, avail);

  /* scan for sync codes */
  sync_count = gst_fluts_demux_sync_scan (demux, data, avail, &flush);

  /* process all packets */
  for (i = 0; i < sync_count; i++) {
    ret = gst_fluts_demux_parse_transport_packet (demux, demux->sync_lut[i]);
    if (G_UNLIKELY (ret == GST_FLOW_LOST_SYNC)) {
      ret = GST_FLOW_OK;
      continue;
    }
    if (G_UNLIKELY (ret != GST_FLOW_OK)) {
      flush = demux->sync_lut[i] - data + demux->packetsize;
      flush = MIN (avail, flush);
      goto done;
    }
  }

done:
  /* flush processed data */
  if (flush) {
    GST_DEBUG_OBJECT (demux, "flushing %d/%d", flush, avail);
    gst_adapter_flush (demux->adapter, flush);
  }

  gst_object_unref (demux);

  return ret;
}

static GstStateChangeReturn
gst_fluts_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstFluTSDemux *demux = GST_FLUTS_DEMUX (element);
  GstStateChangeReturn result;


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      demux->adapter = gst_adapter_new ();
      demux->sync_lut = g_new0 (guint8 *, LENGTH_SYNC_LUT);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_fluts_demux_reset (demux);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      g_object_unref (demux->adapter);
      g_free (demux->sync_lut);
      break;
    default:
      break;
  }

  return result;
}

static GValueArray *
fluts_demux_build_pat_info (GstFluTSDemux * demux)
{
  GValueArray *vals = NULL;
  GstFluTSPAT *PAT;
  gint i;

  g_return_val_if_fail (demux->streams[0] != NULL, NULL);
  g_return_val_if_fail (demux->streams[0]->PID_type ==
      PID_TYPE_PROGRAM_ASSOCIATION, NULL);

  PAT = &(demux->streams[0]->PAT);
  vals = g_value_array_new (PAT->entries->len);

  for (i = 0; i < PAT->entries->len; i++) {
    GstFluTSPATEntry *cur_entry =
        &g_array_index (PAT->entries, GstFluTSPATEntry, i);
    GValue v = { 0, };
    FluTsPatInfo *info_obj;

    info_obj = fluts_pat_info_new (cur_entry->program_number, cur_entry->PID);

    g_value_init (&v, G_TYPE_OBJECT);
    g_value_take_object (&v, info_obj);
    g_value_array_append (vals, &v);
  }
  return vals;
}

static FluTsPmtInfo *
fluts_demux_build_pmt_info (GstFluTSDemux * demux, guint16 pmt_pid)
{
  FluTsPmtInfo *info_obj;
  GstFluTSPMT *PMT;
  gint i;

  g_return_val_if_fail (demux->streams[pmt_pid] != NULL, NULL);
  g_return_val_if_fail (demux->streams[pmt_pid]->PID_type ==
      PID_TYPE_PROGRAM_MAP, NULL);

  PMT = &(demux->streams[pmt_pid]->PMT);

  info_obj = fluts_pmt_info_new (PMT->program_number, PMT->PCR_PID,
      PMT->version_number);

  for (i = 0; i < PMT->entries->len; i++) {
    GstFluTSStream *stream;
    FluTsPmtStreamInfo *stream_info;
    GstFluTSPMTEntry *cur_entry =
        &g_array_index (PMT->entries, GstFluTSPMTEntry, i);

    stream = demux->streams[cur_entry->PID];
    stream_info =
        fluts_pmt_stream_info_new (cur_entry->PID, stream->stream_type);

    if (stream->ES_info) {
      int i;

      /* add languages */
      guint8 *iso639_languages =
          gst_mpeg_descriptor_find (stream->ES_info, DESC_ISO_639_LANGUAGE);
      if (iso639_languages) {
        for (i = 0; i < DESC_ISO_639_LANGUAGE_codes_n (iso639_languages); i++) {
          gchar *language_n = (gchar *)
              DESC_ISO_639_LANGUAGE_language_code_nth (iso639_languages, i);
          fluts_pmt_stream_info_add_language (stream_info,
              g_strndup (language_n, 3));
        }
      }

      for (i = 0; i < gst_mpeg_descriptor_n_desc (stream->ES_info); ++i) {
        guint8 *desc = gst_mpeg_descriptor_nth (stream->ES_info, i);

        /* add the whole descriptor, tag + length + DESC_LENGTH bytes */
        fluts_pmt_stream_info_add_descriptor (stream_info,
            (gchar *) desc, 2 + DESC_LENGTH (desc));
      }
    }
    fluts_pmt_info_add_stream (info_obj, stream_info);
  }
  return info_obj;
}

static void
gst_fluts_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFluTSDemux *demux = GST_FLUTS_DEMUX (object);
  gchar **pids;
  guint num_pids;
  int i;

  switch (prop_id) {
    case PROP_ES_PIDS:
      pids = g_strsplit (g_value_get_string (value), ":", -1);
      num_pids = g_strv_length (pids);
      if (num_pids > 0) {
        demux->elementary_pids = g_new0 (guint16, num_pids);
        demux->nb_elementary_pids = num_pids;
        for (i = 0; i < num_pids; i++) {
          demux->elementary_pids[i] = strtol (pids[i], NULL, 0);
          GST_INFO ("partial TS ES pid %d", demux->elementary_pids[i]);
        }
      }
      g_strfreev (pids);
      break;
    case PROP_CHECK_CRC:
      demux->check_crc = g_value_get_boolean (value);
      break;
    case PROP_PROGRAM_NUMBER:
      demux->program_number = g_value_get_int (value);
      break;
    case PROP_M2TS:
      demux->m2ts_mode = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_fluts_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFluTSDemux *demux = GST_FLUTS_DEMUX (object);
  int i;

  switch (prop_id) {
    case PROP_ES_PIDS:
      if (demux->nb_elementary_pids == 0) {
        g_value_set_static_string (value, "");
      } else {
        gchar **ts_pids;

        ts_pids = g_new0 (gchar *, demux->nb_elementary_pids + 1);
        for (i = 0; i < demux->nb_elementary_pids; i++) {
          ts_pids[i] = g_strdup_printf ("%d", demux->elementary_pids[i]);
        }

        g_value_set_string (value, g_strjoinv (":", ts_pids));
        g_strfreev (ts_pids);
      }
      break;
    case PROP_CHECK_CRC:
      g_value_set_boolean (value, demux->check_crc);
      break;
    case PROP_PROGRAM_NUMBER:
      g_value_set_int (value, demux->program_number);
      break;
    case PROP_PAT_INFO:
    {
      if (demux->streams[0] != NULL) {
        g_value_take_boxed (value, fluts_demux_build_pat_info (demux));
      }
      break;
    }
    case PROP_PMT_INFO:
    {
      if (demux->current_PMT != 0 && demux->streams[demux->current_PMT] != NULL) {
        g_value_take_object (value, fluts_demux_build_pmt_info (demux,
                demux->current_PMT));
      }
      break;
    }
    case PROP_M2TS:
      g_value_set_boolean (value, demux->m2ts_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_fluts_demux_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "mpegtsdemux",
          GST_RANK_PRIMARY, GST_TYPE_FLUTS_DEMUX))
    return FALSE;

  return TRUE;
}
