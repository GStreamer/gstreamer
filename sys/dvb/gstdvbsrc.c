/* GStreamer DVB source
 * Copyright (C) 2006 Zaheer Abbas Merali <zaheerabbas at merali
 *                                         dot org>
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *     @Author: Reynaldo H. Verdejo Pinochet <r.verdejo@sisa.samsung.com>
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
 * SECTION:element-dvbsrc
 *
 * dvbsrc can be used to capture video from DVB cards, DVB-T, DVB-S or DVB-T.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch dvbsrc modulation="QAM 64" trans-mode=8k bandwidth=8 frequency=514000000 code-rate-lp=AUTO code-rate-hp=2/3 guard=4  hierarchy=0 ! mpegtsdemux name=demux ! queue max-size-buffers=0 max-size-time=0 ! mpeg2dec ! xvimagesink demux. ! queue max-size-buffers=0 max-size-time=0 ! mad ! alsasink
 * ]| Captures a full transport stream from dvb card 0 that is a DVB-T card at tuned frequency 514000000 with other parameters as seen in the pipeline and renders the first tv program on the transport stream.
 * |[
 * gst-launch dvbsrc modulation="QAM 64" trans-mode=8k bandwidth=8 frequency=514000000 code-rate-lp=AUTO code-rate-hp=2/3 guard=4  hierarchy=0 pids=100:256:257 ! mpegtsdemux name=demux ! queue max-size-buffers=0 max-size-time=0 ! mpeg2dec ! xvimagesink demux. ! queue max-size-buffers=0 max-size-time=0 ! mad ! alsasink
 * ]| Captures and renders a transport stream from dvb card 0 that is a DVB-T card for a program at tuned frequency 514000000 with PMT pid 100 and elementary stream pids of 256, 257 with other parameters as seen in the pipeline.
 * |[
 * gst-launch dvbsrc polarity="h" frequency=11302000 symbol-rate=27500 diseqc-source=0 pids=50:102:103 ! mpegtsdemux name=demux ! queue max-size-buffers=0 max-size-time=0 ! mpeg2dec ! xvimagesink demux. ! queue max-size-buffers=0 max-size-time=0 ! mad ! alsasink
 * ]| Captures and renders a transport stream from dvb card 0 that is a DVB-S card for a program at tuned frequency 11302000 Hz, symbol rate of 27500 kHz with PMT pid of 50 and elementary stream pids of 102 and 103.
 * </refsect2>
 */

/* 
 * History of DVB_API_VERSION 5 minor changes
 *
 * API Addition/changes in reverse order (most recent first)
 *
 * Minor 10 (statistics properties)
 *   DTV_STAT_*
 *   FE_SCALE_* 
 *
 * Minor 9
 *   DTV_LNA
 *   LNA_AUTO
 *
 * Minor 8
 *   FE_CAN_MULTISTREAM
 *   DTV_ISDBS_TS_ID_LEGACY (was DVB_ISDBS_TS_ID)
 *   DTV_DVBT2_PLP_ID_LEGACY (was DVB_DVBT2_PLP_ID)
 *   NO_STREAM_ID_FILTER
 *   INTERLEAVING_AUTO
 *
 * Minor 7 (DTMB Support)
 *   FEC_2_5
 *   QAM_4_NR
 *   TRANSMISSION_MODE_C1 / _C3780
 *   GUARD_INTERVAL_PN420 / _PN595 / _PN945
 *   INTERLEAVING_NONE / _240 / _720
 *   DTV_INTERLEAVING
 *   SYS_DTMB (Renamed from SYS_DMBTH but has safety #define)
 *
 * Minor 6 (ATSC-MH)
 *   DTV_ATSCMH_* (for those not defined in later versions)
 *
 * Somewhere in between 5 and 6:
 *   SYS_DVBC_ANNEX_A / _C (Safety #define for _AC => A)
 * 
 * Minor 5 (Note : minimum version we support according to configure.ac)
 *   DTV_ENUM_DELSYS
 *
 * Minor 4
 *   SYS_TURBO
 *
 * Minor 3 (DVB-T2)
 *   TRANSMISSION_MODE_1K / _16K / _32K
 *   BANDWIDTH_5_MHZ / _10_MHZ / _1_712_MHZ
 *   GUARD_INTERVAL_1_128 / _19_128 / _19_256
 *   DTV_DVBT2_PLP_ID (/!\ renamed in minor 8 !)
 *   SYS_DVBT2
 *
 * Minor 2
 *   FE_CAN_TURBO_FEC
 *   DTV_ISDBS_TS_ID (/!\ renamed in minor 8 !)
 *
 * Minor 1 (ISDB-T and ISDB-Tsb)
 *   TRANSMISSION_MODE_4K
 *   DTV_ISDBT_* (for those not defined in later version)
 *
 * Minor 0 : initial version
 */

/* We know we have at least DVB_API_VERSION >= 5 */
#define HAVE_V5_MINOR(minor) ((DVB_API_VERSION > 5) || \
			      (DVB_API_VERSION_MINOR >= (minor)))

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdvbsrc.h"
#include <gst/gst.h>
#include <gst/glib-compat-private.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "_stdint.h"

#include <unistd.h>

#include <linux/dvb/version.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

#include <gst/gst-i18n-plugin.h>

/* Before 5.6 we map A to AC */
#if !HAVE_V5_MINOR(6)
#define SYS_DVBC_ANNEX_A SYS_DVBC_ANNEX_AC
#endif

/* NO_STREAM_ID_FILTER introduced in minor 8 */
#ifndef NO_STREAM_ID_FILTER
#define NO_STREAM_ID_FILTER    (~0U)
#endif

/* DTV_STREAM_ID introduced in minor 8 (redefine) */
#ifndef DTV_STREAM_ID
#define DTV_STREAM_ID DTV_ISDBS_TS_ID
#endif

GST_DEBUG_CATEGORY_STATIC (gstdvbsrc_debug);
#define GST_CAT_DEFAULT (gstdvbsrc_debug)

#define SLOF (11700*1000UL)
#define LOF1 (9750*1000UL)
#define LOF2 (10600*1000UL)

#define NUM_DTV_PROPS 16

/* Signals */
enum
{
  SIGNAL_TUNING_START,
  SIGNAL_TUNING_DONE,
  SIGNAL_TUNING_FAIL,
  LAST_SIGNAL
};

/* Arguments */
enum
{
  ARG_0,
  ARG_DVBSRC_ADAPTER,
  ARG_DVBSRC_FRONTEND,
  ARG_DVBSRC_DISEQC_SRC,
  ARG_DVBSRC_FREQUENCY,
  ARG_DVBSRC_POLARITY,
  ARG_DVBSRC_PIDS,
  ARG_DVBSRC_SYM_RATE,
  ARG_DVBSRC_BANDWIDTH,
  ARG_DVBSRC_CODE_RATE_HP,
  ARG_DVBSRC_CODE_RATE_LP,
  ARG_DVBSRC_GUARD,
  ARG_DVBSRC_MODULATION,
  ARG_DVBSRC_TRANSMISSION_MODE,
  ARG_DVBSRC_HIERARCHY_INF,
  ARG_DVBSRC_TUNE,
  ARG_DVBSRC_INVERSION,
  ARG_DVBSRC_STATS_REPORTING_INTERVAL,
  ARG_DVBSRC_TIMEOUT,
  ARG_DVBSRC_TUNING_TIMEOUT,
  ARG_DVBSRC_DVB_BUFFER_SIZE,
  ARG_DVBSRC_DELSYS,
  ARG_DVBSRC_PILOT,
  ARG_DVBSRC_ROLLOFF,
  ARG_DVBSRC_STREAM_ID,
  ARG_DVBSRC_BANDWIDTH_HZ
};

#define DEFAULT_ADAPTER 0
#define DEFAULT_FRONTEND 0
#define DEFAULT_DISEQC_SRC -1   /* disabled */
#define DEFAULT_FREQUENCY 0
#define DEFAULT_POLARITY "H"
#define DEFAULT_PIDS "8192"     /* Special value meaning 'all' PIDs */
#define DEFAULT_SYMBOL_RATE 0
#define DEFAULT_BANDWIDTH_HZ 8000000
#define DEFAULT_BANDWIDTH BANDWIDTH_8_MHZ
#define DEFAULT_CODE_RATE_HP FEC_AUTO
#define DEFAULT_CODE_RATE_LP FEC_1_2
#define DEFAULT_GUARD GUARD_INTERVAL_1_16
#define DEFAULT_MODULATION QAM_16
#define DEFAULT_TRANSMISSION_MODE TRANSMISSION_MODE_8K
#define DEFAULT_HIERARCHY HIERARCHY_1
#define DEFAULT_INVERSION INVERSION_ON
#define DEFAULT_STATS_REPORTING_INTERVAL 100
#define DEFAULT_TIMEOUT 1000000 /* 1 second */
#define DEFAULT_TUNING_TIMEOUT 10 * GST_SECOND  /* 10 seconds */
#define DEFAULT_DVB_BUFFER_SIZE (10*188*1024)   /* kernel default is 8192 */
#define DEFAULT_BUFFER_SIZE 8192        /* not a property */
#define DEFAULT_DELSYS SYS_UNDEFINED
#define DEFAULT_PILOT PILOT_AUTO
#define DEFAULT_ROLLOFF ROLLOFF_AUTO
#define DEFAULT_STREAM_ID NO_STREAM_ID_FILTER

static void gst_dvbsrc_output_frontend_stats (GstDvbSrc * src);

#define GST_TYPE_DVBSRC_CODE_RATE (gst_dvbsrc_code_rate_get_type ())
static GType
gst_dvbsrc_code_rate_get_type (void)
{
  static GType dvbsrc_code_rate_type = 0;
  static GEnumValue code_rate_types[] = {
    {FEC_NONE, "NONE", "none"},
    {FEC_1_2, "1/2", "1/2"},
    {FEC_2_3, "2/3", "2/3"},
    {FEC_3_4, "3/4", "3/4"},
    {FEC_4_5, "4/5", "4/5"},
    {FEC_5_6, "5/6", "5/6"},
    {FEC_6_7, "6/7", "6/7"},
    {FEC_7_8, "7/8", "7/8"},
    {FEC_8_9, "8/9", "8/9"},
    {FEC_AUTO, "AUTO", "auto"},
    {0, NULL, NULL},
  };

  if (!dvbsrc_code_rate_type) {
    dvbsrc_code_rate_type =
        g_enum_register_static ("GstDvbSrcCode_Rate", code_rate_types);
  }
  return dvbsrc_code_rate_type;
}

#define GST_TYPE_DVBSRC_MODULATION (gst_dvbsrc_modulation_get_type ())
static GType
gst_dvbsrc_modulation_get_type (void)
{
  static GType dvbsrc_modulation_type = 0;
  static GEnumValue modulation_types[] = {
    {QPSK, "QPSK", "qpsk"},
    {QAM_16, "QAM 16", "qam-16"},
    {QAM_32, "QAM 32", "qam-32"},
    {QAM_64, "QAM 64", "qam-64"},
    {QAM_128, "QAM 128", "qam-128"},
    {QAM_256, "QAM 256", "qam-256"},
    {QAM_AUTO, "AUTO", "auto"},
    {VSB_8, "8VSB", "8vsb"},
    {VSB_16, "16VSB", "16vsb"},
    {PSK_8, "8PSK", "8psk"},
    {0, NULL, NULL},
  };

  if (!dvbsrc_modulation_type) {
    dvbsrc_modulation_type =
        g_enum_register_static ("GstDvbSrcModulation", modulation_types);
  }
  return dvbsrc_modulation_type;
}

#define GST_TYPE_DVBSRC_TRANSMISSION_MODE (gst_dvbsrc_transmission_mode_get_type ())
static GType
gst_dvbsrc_transmission_mode_get_type (void)
{
  static GType dvbsrc_transmission_mode_type = 0;
  static GEnumValue transmission_mode_types[] = {
    {TRANSMISSION_MODE_2K, "2K", "2k"},
    {TRANSMISSION_MODE_8K, "8K", "8k"},
    {TRANSMISSION_MODE_AUTO, "AUTO", "auto"},
    {0, NULL, NULL},
  };

  if (!dvbsrc_transmission_mode_type) {
    dvbsrc_transmission_mode_type =
        g_enum_register_static ("GstDvbSrcTransmission_Mode",
        transmission_mode_types);
  }
  return dvbsrc_transmission_mode_type;
}

#define GST_TYPE_DVBSRC_BANDWIDTH (gst_dvbsrc_bandwidth_get_type ())
static GType
gst_dvbsrc_bandwidth_get_type (void)
{
  static GType dvbsrc_bandwidth_type = 0;
  static GEnumValue bandwidth_types[] = {
    {BANDWIDTH_8_MHZ, "8", "8"},
    {BANDWIDTH_7_MHZ, "7", "7"},
    {BANDWIDTH_6_MHZ, "6", "6"},
    {BANDWIDTH_AUTO, "AUTO", "AUTO"},
    {0, NULL, NULL},
  };

  if (!dvbsrc_bandwidth_type) {
    dvbsrc_bandwidth_type =
        g_enum_register_static ("GstDvbSrcBandwidth", bandwidth_types);
  }
  return dvbsrc_bandwidth_type;
}

#define GST_TYPE_DVBSRC_GUARD (gst_dvbsrc_guard_get_type ())
static GType
gst_dvbsrc_guard_get_type (void)
{
  static GType dvbsrc_guard_type = 0;
  static GEnumValue guard_types[] = {
    {GUARD_INTERVAL_1_32, "32", "32"},
    {GUARD_INTERVAL_1_16, "16", "16"},
    {GUARD_INTERVAL_1_8, "8", "8"},
    {GUARD_INTERVAL_1_4, "4", "4"},
    {GUARD_INTERVAL_AUTO, "AUTO", "auto"},
    {0, NULL, NULL},
  };

  if (!dvbsrc_guard_type) {
    dvbsrc_guard_type = g_enum_register_static ("GstDvbSrcGuard", guard_types);
  }
  return dvbsrc_guard_type;
}

#define GST_TYPE_DVBSRC_HIERARCHY (gst_dvbsrc_hierarchy_get_type ())
static GType
gst_dvbsrc_hierarchy_get_type (void)
{
  static GType dvbsrc_hierarchy_type = 0;
  static GEnumValue hierarchy_types[] = {
    {HIERARCHY_NONE, "NONE", "none"},
    {HIERARCHY_1, "1", "1"},
    {HIERARCHY_2, "2", "2"},
    {HIERARCHY_4, "4", "4"},
    {HIERARCHY_AUTO, "AUTO", "auto"},
    {0, NULL, NULL},
  };

  if (!dvbsrc_hierarchy_type) {
    dvbsrc_hierarchy_type =
        g_enum_register_static ("GstDvbSrcHierarchy", hierarchy_types);
  }
  return dvbsrc_hierarchy_type;
}

#define GST_TYPE_DVBSRC_INVERSION (gst_dvbsrc_inversion_get_type ())
static GType
gst_dvbsrc_inversion_get_type (void)
{
  static GType dvbsrc_inversion_type = 0;
  static GEnumValue inversion_types[] = {
    {INVERSION_OFF, "OFF", "off"},
    {INVERSION_ON, "ON", "on"},
    {INVERSION_AUTO, "AUTO", "auto"},
    {0, NULL, NULL},
  };

  if (!dvbsrc_inversion_type) {
    dvbsrc_inversion_type =
        g_enum_register_static ("GstDvbSrcInversion", inversion_types);
  }
  return dvbsrc_inversion_type;
}

#define GST_TYPE_DVBSRC_DELSYS (gst_dvbsrc_delsys_get_type ())
static GType
gst_dvbsrc_delsys_get_type (void)
{
  static GType dvbsrc_delsys_type = 0;
  static GEnumValue delsys_types[] = {
    {SYS_UNDEFINED, "UNDEFINED", "undefined"},
    {SYS_DVBC_ANNEX_A, "DVB-C-A", "dvb-c-a"},
    {SYS_DVBC_ANNEX_B, "DVB-C-B", "dvb-c-b"},
    {SYS_DVBT, "DVB-T", "dvb-t"},
    {SYS_DSS, "DSS", "dss"},
    {SYS_DVBS, "DVB-S", "dvb-s"},
    {SYS_DVBS2, "DVB-S2", "dvb-s2"},
    {SYS_DVBH, "DVB-H", "dvb-h"},
    {SYS_ISDBT, "ISDB-T", "isdb-t"},
    {SYS_ISDBS, "ISDB-S", "isdb-s"},
    {SYS_ISDBC, "ISDB-C", "isdb-c"},
    {SYS_ATSC, "ATSC", "atsc"},
    {SYS_ATSCMH, "ATSC-MH", "atsc-mh"},
#if HAVE_V5_MINOR(7)
    {SYS_DTMB, "DTMB", "dtmb"},
#endif
    {SYS_CMMB, "CMMB", "cmmb"},
    {SYS_DAB, "DAB", "dab"},
    {SYS_DVBT2, "DVB-T2", "dvb-t2"},
    {SYS_TURBO, "TURBO", "turbo"},
#if HAVE_V5_MINOR(6)
    {SYS_DVBC_ANNEX_C, "DVB-C-C", "dvb-c-c"},
#endif
    {0, NULL, NULL},
  };

  if (!dvbsrc_delsys_type) {
    dvbsrc_delsys_type =
        g_enum_register_static ("GstDvbSrcDelsys", delsys_types);
  }
  return dvbsrc_delsys_type;
}

#define GST_TYPE_DVBSRC_PILOT (gst_dvbsrc_pilot_get_type ())
static GType
gst_dvbsrc_pilot_get_type (void)
{
  static GType dvbsrc_pilot_type = 0;
  static GEnumValue pilot_types[] = {
    {PILOT_ON, "ON", "on"},
    {PILOT_OFF, "OFF", "off"},
    {PILOT_AUTO, "AUTO", "auto"},
    {0, NULL, NULL},
  };

  if (!dvbsrc_pilot_type) {
    dvbsrc_pilot_type = g_enum_register_static ("GstDvbSrcPilot", pilot_types);
  }
  return dvbsrc_pilot_type;
}

#define GST_TYPE_DVBSRC_ROLLOFF (gst_dvbsrc_rolloff_get_type ())
static GType
gst_dvbsrc_rolloff_get_type (void)
{
  static GType dvbsrc_rolloff_type = 0;
  static GEnumValue rolloff_types[] = {
    {ROLLOFF_35, "35", "35"},
    {ROLLOFF_20, "20", "20"},
    {ROLLOFF_25, "25", "25"},
    {ROLLOFF_AUTO, "auto", "auto"},
    {0, NULL, NULL},
  };

  if (!dvbsrc_rolloff_type) {
    dvbsrc_rolloff_type =
        g_enum_register_static ("GstDvbSrcRolloff", rolloff_types);
  }
  return dvbsrc_rolloff_type;
}

static void gst_dvbsrc_finalize (GObject * object);
static void gst_dvbsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dvbsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_dvbsrc_create (GstPushSrc * element,
    GstBuffer ** buffer);

static gboolean gst_dvbsrc_start (GstBaseSrc * bsrc);
static gboolean gst_dvbsrc_stop (GstBaseSrc * bsrc);
static GstStateChangeReturn gst_dvbsrc_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_dvbsrc_unlock (GstBaseSrc * bsrc);
static gboolean gst_dvbsrc_unlock_stop (GstBaseSrc * bsrc);
static gboolean gst_dvbsrc_is_seekable (GstBaseSrc * bsrc);
static gboolean gst_dvbsrc_get_size (GstBaseSrc * src, guint64 * size);

static gboolean gst_dvbsrc_tune (GstDvbSrc * object);
static gboolean gst_dvbsrc_set_fe_params (GstDvbSrc * object,
    struct dtv_properties *props);
static gboolean gst_dvbsrc_tune_fe (GstDvbSrc * object);

static void gst_dvbsrc_set_pes_filters (GstDvbSrc * object);
static void gst_dvbsrc_unset_pes_filters (GstDvbSrc * object);
static inline int gst_dvbsrc_retry_ioctl (int fd, unsigned long req,
    void *data);

static GstStaticPadTemplate ts_src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("video/mpegts, "
        "mpegversion = (int) 2," "systemstream = (boolean) TRUE"));

/*
 ******************************
 *                            *
 *      GObject Related       *
 *            	              *
 *                            *
 ******************************
 */

#define gst_dvbsrc_parent_class parent_class
G_DEFINE_TYPE (GstDvbSrc, gst_dvbsrc, GST_TYPE_PUSH_SRC);

static guint gst_dvbsrc_signals[LAST_SIGNAL] = { 0 };

/* initialize the plugin's class */
static void
gst_dvbsrc_class_init (GstDvbSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_dvbsrc_set_property;
  gobject_class->get_property = gst_dvbsrc_get_property;
  gobject_class->finalize = gst_dvbsrc_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_dvbsrc_change_state);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&ts_src_factory));

  gst_element_class_set_static_metadata (gstelement_class, "DVB Source",
      "Source/Video",
      "Digital Video Broadcast Source",
      "P2P-VCR, C-Lab, University of Paderborn,"
      "Zaheer Abbas Merali <zaheerabbas at merali dot org>");

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_dvbsrc_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_dvbsrc_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_dvbsrc_unlock);
  gstbasesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_dvbsrc_unlock_stop);
  gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR (gst_dvbsrc_is_seekable);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_dvbsrc_get_size);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_dvbsrc_create);

  g_object_class_install_property (gobject_class, ARG_DVBSRC_ADAPTER,
      g_param_spec_int ("adapter", "The adapter device number",
          "The DVB adapter device number (eg. 0 for adapter0)",
          0, 16, DEFAULT_ADAPTER, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_FRONTEND,
      g_param_spec_int ("frontend", "The frontend device number",
          "The frontend device number (eg. 0 for frontend0)",
          0, 16, DEFAULT_FRONTEND, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_FREQUENCY,
      g_param_spec_uint ("frequency", "frequency", "Frequency",
          0, G_MAXUINT, DEFAULT_FREQUENCY, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_POLARITY,
      g_param_spec_string ("polarity", "polarity",
          "(DVB-S/S2) Polarity [vhHV] (eg. V for Vertical)",
          DEFAULT_POLARITY, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_PIDS,
      g_param_spec_string ("pids", "pids",
          "Colon seperated list of pids (eg. 110:120)",
          DEFAULT_PIDS, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_SYM_RATE,
      g_param_spec_uint ("symbol-rate",
          "symbol rate",
          "(DVB-S/S2, DVB-C) Symbol rate in bauds",
          0, G_MAXUINT, DEFAULT_SYMBOL_RATE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_TUNE,
      g_param_spec_pointer ("tune",
          "tune", "Atomically tune to channel. (For Apps)", G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_DISEQC_SRC,
      g_param_spec_int ("diseqc-source",
          "diseqc source",
          "(DVB-S/S2) Selected DiSEqC source. Only needed if you have a "
          "DiSEqC switch. Otherwise leave at -1 (disabled)", -1, 7,
          DEFAULT_DISEQC_SRC, G_PARAM_READWRITE));

  /* DVB-T, additional properties */

  g_object_class_install_property (gobject_class, ARG_DVBSRC_BANDWIDTH_HZ,
      g_param_spec_uint ("bandwidth-hz", "bandwidth-hz",
          "(DVB-T) Bandwidth in Hz", 0, G_MAXUINT, DEFAULT_BANDWIDTH_HZ,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_BANDWIDTH,
      g_param_spec_enum ("bandwidth", "bandwidth",
          "(DVB-T) Bandwidth. Deprecated", GST_TYPE_DVBSRC_BANDWIDTH,
          DEFAULT_BANDWIDTH, G_PARAM_READWRITE));

  /* FIXME: DVB-C, DVB-S, DVB-S2 named it as innerFEC */
  g_object_class_install_property (gobject_class, ARG_DVBSRC_CODE_RATE_HP,
      g_param_spec_enum ("code-rate-hp",
          "code-rate-hp",
          "(DVB-T, DVB-S/S2 and DVB-C) High priority code rate",
          GST_TYPE_DVBSRC_CODE_RATE, DEFAULT_CODE_RATE_HP, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_CODE_RATE_LP,
      g_param_spec_enum ("code-rate-lp",
          "code-rate-lp",
          "(DVB-T) Low priority code rate",
          GST_TYPE_DVBSRC_CODE_RATE, DEFAULT_CODE_RATE_LP, G_PARAM_READWRITE));

  /* FIXME: should the property be called 'guard-interval' then? */
  g_object_class_install_property (gobject_class, ARG_DVBSRC_GUARD,
      g_param_spec_enum ("guard",
          "guard",
          "(DVB-T) Guard Interval",
          GST_TYPE_DVBSRC_GUARD, DEFAULT_GUARD, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_MODULATION,
      g_param_spec_enum ("modulation", "modulation",
          "(DVB-T and DVB-C) Modulation type",
          GST_TYPE_DVBSRC_MODULATION, DEFAULT_MODULATION, G_PARAM_READWRITE));

  /* FIXME: property should be named 'transmission-mode' */
  g_object_class_install_property (gobject_class,
      ARG_DVBSRC_TRANSMISSION_MODE,
      g_param_spec_enum ("trans-mode", "trans-mode",
          "(DVB-T) Transmission mode",
          GST_TYPE_DVBSRC_TRANSMISSION_MODE, DEFAULT_TRANSMISSION_MODE,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_HIERARCHY_INF,
      g_param_spec_enum ("hierarchy", "hierarchy",
          "(DVB-T) Hierarchy information",
          GST_TYPE_DVBSRC_HIERARCHY, DEFAULT_HIERARCHY, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_INVERSION,
      g_param_spec_enum ("inversion", "inversion",
          "(DVB-T and DVB-C) Inversion information",
          GST_TYPE_DVBSRC_INVERSION, DEFAULT_INVERSION, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      ARG_DVBSRC_STATS_REPORTING_INTERVAL,
      g_param_spec_uint ("stats-reporting-interval",
          "stats-reporting-interval",
          "The number of reads before reporting frontend stats",
          0, G_MAXUINT, DEFAULT_STATS_REPORTING_INTERVAL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_TIMEOUT,
      g_param_spec_uint64 ("timeout", "Timeout",
          "Post a message after timeout microseconds (0 = disabled)",
          0, G_MAXUINT64, DEFAULT_TIMEOUT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_TUNING_TIMEOUT,
      g_param_spec_uint64 ("tuning-timeout", "Tuning Timeout",
          "Milliseconds to wait before giving up tuning/locking on a signal",
          0, G_MAXUINT64, DEFAULT_TUNING_TIMEOUT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      ARG_DVBSRC_DVB_BUFFER_SIZE,
      g_param_spec_uint ("dvb-buffer-size",
          "dvb-buffer-size",
          "The kernel buffer size used by the DVB api",
          0, G_MAXUINT, DEFAULT_DVB_BUFFER_SIZE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_DELSYS,
      g_param_spec_enum ("delsys", "delsys", "Delivery System",
          GST_TYPE_DVBSRC_DELSYS, DEFAULT_DELSYS, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_PILOT,
      g_param_spec_enum ("pilot", "pilot", "Pilot (DVB-S2)",
          GST_TYPE_DVBSRC_PILOT, DEFAULT_PILOT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_ROLLOFF,
      g_param_spec_enum ("rolloff", "rolloff", "Rolloff (DVB-S2)",
          GST_TYPE_DVBSRC_ROLLOFF, DEFAULT_ROLLOFF, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_STREAM_ID,
      g_param_spec_int ("stream-id", "stream-id",
          "(DVB-T2 and DVB-S2 max 255, ISDB max 65535) Stream ID "
          "(-1 = disabled)", -1, 65535, DEFAULT_STREAM_ID, G_PARAM_READWRITE));

  /**
   * GstDvbSrc::tuning-start:
   * @gstdvbsrc: the element on which the signal is emitted
   *
   * Signal emited when the element first attempts to tune the
   * frontend tunner to a given frequency.
   */
  gst_dvbsrc_signals[SIGNAL_TUNING_START] =
      g_signal_new ("tuning-start", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  /**
   * GstDvbSrc::tuning-done:
   * @gstdvbsrc: the element on which the signal is emitted
   *
   * Signal emited when the tunner has successfully got a lock on a signal.
   */
  gst_dvbsrc_signals[SIGNAL_TUNING_DONE] =
      g_signal_new ("tuning-done", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  /**
   * GstDvbSrc::tuning-fail:
   * @gstdvbsrc: the element on which the signal is emitted
   *
   * Signal emited when the tunner failed to get a lock on the
   * signal.
   */
  gst_dvbsrc_signals[SIGNAL_TUNING_FAIL] =
      g_signal_new ("tuning-fail", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_dvbsrc_init (GstDvbSrc * object)
{
  int i = 0;

  GST_INFO_OBJECT (object, "gst_dvbsrc_init");
  GST_DEBUG_OBJECT (object, "Kernel DVB API version %d.%d", DVB_API_VERSION,
      DVB_API_VERSION_MINOR);

  /* We are a live source */
  gst_base_src_set_live (GST_BASE_SRC (object), TRUE);
  /* And we wanted timestamped output */
  gst_base_src_set_do_timestamp (GST_BASE_SRC (object), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (object), GST_FORMAT_TIME);

  object->fd_frontend = -1;
  object->fd_dvr = -1;

  for (i = 0; i < MAX_FILTERS; i++) {
    object->pids[i] = G_MAXUINT16;
    object->fd_filters[i] = -1;
  }
  /* PID 8192 on DVB gets the whole transport stream */
  object->pids[0] = 8192;
  object->dvb_buffer_size = DEFAULT_DVB_BUFFER_SIZE;
  object->adapter_number = DEFAULT_ADAPTER;
  object->frontend_number = DEFAULT_FRONTEND;
  object->diseqc_src = DEFAULT_DISEQC_SRC;
  object->send_diseqc = (DEFAULT_DISEQC_SRC != -1);
  object->tone = SEC_TONE_OFF;
  /* object->pol = DVB_POL_H; *//* set via G_PARAM_CONSTRUCT */
  object->sym_rate = DEFAULT_SYMBOL_RATE;
  object->bandwidth = DEFAULT_BANDWIDTH;
  object->code_rate_hp = DEFAULT_CODE_RATE_HP;
  object->code_rate_lp = DEFAULT_CODE_RATE_LP;
  object->guard_interval = DEFAULT_GUARD;
  object->modulation = DEFAULT_MODULATION;
  object->transmission_mode = DEFAULT_TRANSMISSION_MODE;
  object->hierarchy_information = DEFAULT_HIERARCHY;
  object->inversion = DEFAULT_INVERSION;
  object->stats_interval = DEFAULT_STATS_REPORTING_INTERVAL;
  object->delsys = DEFAULT_DELSYS;
  object->pilot = DEFAULT_PILOT;
  object->rolloff = DEFAULT_ROLLOFF;
  object->stream_id = DEFAULT_STREAM_ID;

  g_mutex_init (&object->tune_mutex);
  object->timeout = DEFAULT_TIMEOUT;
  object->tuning_timeout = DEFAULT_TUNING_TIMEOUT;
}

/**
 * This loop should be safe enough considering:
 *
 * 1.- EINTR suggest the next ioctl might succeed
 * 2.- It's highly unlikely you will end up spining
 *     before your entire system goes nuts due to
 *     the massive number of interrupts.
 *
 * We don't check for EAGAIN here cause we are opening
 * the frontend in blocking mode.
 */
static inline int
gst_dvbsrc_retry_ioctl (int fd, unsigned long req, void *data)
{
  int ret;

  do
    ret = ioctl (fd, req, data);
  while (ret == -1 && errno == EINTR);

  return ret;
}

static void
gst_dvbsrc_set_pids (GstDvbSrc * dvbsrc, const gchar * pid_string)
{
  if (!strcmp (pid_string, "8192")) {
    /* get the whole ts */
    int pid_count = 1;
    dvbsrc->pids[0] = 8192;
    while (pid_count < MAX_FILTERS) {
      dvbsrc->pids[pid_count++] = G_MAXUINT16;
    }
  } else {
    int pid = 0;
    int pid_count;
    gchar **pids;
    char **tmp;

    tmp = pids = g_strsplit (pid_string, ":", MAX_FILTERS);

    /* always add the PAT and CAT pids */
    dvbsrc->pids[0] = 0;
    dvbsrc->pids[1] = 1;

    pid_count = 2;
    while (*pids != NULL && pid_count < MAX_FILTERS) {
      pid = strtol (*pids, NULL, 0);
      if (pid > 1 && pid <= 8192) {
        GST_INFO_OBJECT (dvbsrc, "\tParsed Pid: %d", pid);
        dvbsrc->pids[pid_count] = pid;
        pid_count++;
      }
      pids++;
    }
    while (pid_count < MAX_FILTERS) {
      dvbsrc->pids[pid_count++] = G_MAXUINT16;
    }

    g_strfreev (tmp);
  }
  /* if we are in playing or paused, then set filters now */
  GST_INFO_OBJECT (dvbsrc, "checking if playing for setting pes filters");
  if (GST_ELEMENT (dvbsrc)->current_state == GST_STATE_PLAYING ||
      GST_ELEMENT (dvbsrc)->current_state == GST_STATE_PAUSED) {
    GST_INFO_OBJECT (dvbsrc, "Setting pes filters now");
    gst_dvbsrc_set_pes_filters (dvbsrc);
  }
}

static void
gst_dvbsrc_set_property (GObject * _object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDvbSrc *object;

  g_return_if_fail (GST_IS_DVBSRC (_object));
  object = GST_DVBSRC (_object);

  switch (prop_id) {
    case ARG_DVBSRC_ADAPTER:
      object->adapter_number = g_value_get_int (value);
      break;
    case ARG_DVBSRC_FRONTEND:
      object->frontend_number = g_value_get_int (value);
      break;
    case ARG_DVBSRC_DISEQC_SRC:
      if (object->diseqc_src != g_value_get_int (value)) {
        object->diseqc_src = g_value_get_int (value);
        object->send_diseqc = TRUE;
      }
      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_DISEQC_ID");
      break;
    case ARG_DVBSRC_FREQUENCY:
      object->freq = g_value_get_uint (value);
      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_FREQUENCY (%d Hz)",
          object->freq);
      break;
    case ARG_DVBSRC_POLARITY:
    {
      const char *s = NULL;

      s = g_value_get_string (value);
      if (s != NULL) {
        object->pol = (s[0] == 'h' || s[0] == 'H') ? DVB_POL_H : DVB_POL_V;
        GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_POLARITY");
        GST_INFO_OBJECT (object, "\t%s", (s[0] == 'h'
                || s[0] == 'H') ? "DVB_POL_H" : "DVB_POL_V");
      }
      break;
    }
    case ARG_DVBSRC_PIDS:
    {
      const gchar *pid_string;

      pid_string = g_value_get_string (value);
      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_PIDS %s", pid_string);
      if (pid_string)
        gst_dvbsrc_set_pids (object, pid_string);
      break;
    }
    case ARG_DVBSRC_SYM_RATE:
      object->sym_rate = g_value_get_uint (value);
      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_SYM_RATE to value %d",
          object->sym_rate);
      break;

    case ARG_DVBSRC_BANDWIDTH_HZ:
      object->bandwidth = g_value_get_uint (value);
      break;
    case ARG_DVBSRC_BANDWIDTH:
      switch (g_value_get_enum (value)) {
        case BANDWIDTH_8_MHZ:
          object->bandwidth = 8000000;
          break;
        case BANDWIDTH_7_MHZ:
          object->bandwidth = 7000000;
          break;
        case BANDWIDTH_6_MHZ:
          object->bandwidth = 6000000;
          break;
        default:
          /* we don't know which bandwidth are set */
          object->bandwidth = 0;
          break;
      }
      break;
    case ARG_DVBSRC_CODE_RATE_HP:
      object->code_rate_hp = g_value_get_enum (value);
      break;
    case ARG_DVBSRC_CODE_RATE_LP:
      object->code_rate_lp = g_value_get_enum (value);
      break;
    case ARG_DVBSRC_GUARD:
      object->guard_interval = g_value_get_enum (value);
      break;
    case ARG_DVBSRC_MODULATION:
      object->modulation = g_value_get_enum (value);
      break;
    case ARG_DVBSRC_TRANSMISSION_MODE:
      object->transmission_mode = g_value_get_enum (value);
      break;
    case ARG_DVBSRC_HIERARCHY_INF:
      object->hierarchy_information = g_value_get_enum (value);
      break;
    case ARG_DVBSRC_INVERSION:
      object->inversion = g_value_get_enum (value);
      break;
    case ARG_DVBSRC_TUNE:{
      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_TUNE");

      /* if we are in paused/playing state tune now, otherwise in ready
       * to paused state change */
      if (GST_STATE (object) > GST_STATE_READY) {
        g_mutex_lock (&object->tune_mutex);
        gst_dvbsrc_tune (object);
        g_mutex_unlock (&object->tune_mutex);
      }
      break;
    }
    case ARG_DVBSRC_STATS_REPORTING_INTERVAL:
      object->stats_interval = g_value_get_uint (value);
      object->stats_counter = 0;
      break;
    case ARG_DVBSRC_TIMEOUT:
      object->timeout = g_value_get_uint64 (value);
      break;
    case ARG_DVBSRC_TUNING_TIMEOUT:
      object->tuning_timeout = g_value_get_uint64 (value);
      break;
    case ARG_DVBSRC_DVB_BUFFER_SIZE:
      object->dvb_buffer_size = g_value_get_uint (value);
      break;
    case ARG_DVBSRC_DELSYS:
      object->delsys = g_value_get_enum (value);
      break;
    case ARG_DVBSRC_PILOT:
      object->pilot = g_value_get_enum (value);
      break;
    case ARG_DVBSRC_ROLLOFF:
      object->rolloff = g_value_get_enum (value);
      break;
    case ARG_DVBSRC_STREAM_ID:
      object->stream_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_dvbsrc_get_property (GObject * _object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDvbSrc *object;

  g_return_if_fail (GST_IS_DVBSRC (_object));
  object = GST_DVBSRC (_object);

  switch (prop_id) {
    case ARG_DVBSRC_ADAPTER:
      g_value_set_int (value, object->adapter_number);
      break;
    case ARG_DVBSRC_FRONTEND:
      g_value_set_int (value, object->frontend_number);
      break;
    case ARG_DVBSRC_FREQUENCY:
      g_value_set_uint (value, object->freq);
      break;
    case ARG_DVBSRC_POLARITY:
      if (object->pol == DVB_POL_H)
        g_value_set_static_string (value, "H");
      else
        g_value_set_static_string (value, "V");
      break;
    case ARG_DVBSRC_SYM_RATE:
      g_value_set_uint (value, object->sym_rate);
      break;
    case ARG_DVBSRC_DISEQC_SRC:
      g_value_set_int (value, object->diseqc_src);
      break;
    case ARG_DVBSRC_BANDWIDTH_HZ:
      g_value_set_uint (value, object->bandwidth);
      break;
    case ARG_DVBSRC_BANDWIDTH:{
      int tmp;
      if (object->bandwidth == 0)
        tmp = BANDWIDTH_AUTO;
      else if (object->bandwidth <= 6000000)
        tmp = BANDWIDTH_6_MHZ;
      else if (object->bandwidth <= 7000000)
        tmp = BANDWIDTH_7_MHZ;
      else if (object->bandwidth <= 8000000)
        tmp = BANDWIDTH_8_MHZ;
      else
        tmp = BANDWIDTH_AUTO;

      g_value_set_enum (value, tmp);
      break;
    }
    case ARG_DVBSRC_CODE_RATE_HP:
      g_value_set_enum (value, object->code_rate_hp);
      break;
    case ARG_DVBSRC_CODE_RATE_LP:
      g_value_set_enum (value, object->code_rate_lp);
      break;
    case ARG_DVBSRC_GUARD:
      g_value_set_enum (value, object->guard_interval);
      break;
    case ARG_DVBSRC_MODULATION:
      g_value_set_enum (value, object->modulation);
      break;
    case ARG_DVBSRC_TRANSMISSION_MODE:
      g_value_set_enum (value, object->transmission_mode);
      break;
    case ARG_DVBSRC_HIERARCHY_INF:
      g_value_set_enum (value, object->hierarchy_information);
      break;
    case ARG_DVBSRC_INVERSION:
      g_value_set_enum (value, object->inversion);
      break;
    case ARG_DVBSRC_STATS_REPORTING_INTERVAL:
      g_value_set_uint (value, object->stats_interval);
      break;
    case ARG_DVBSRC_TIMEOUT:
      g_value_set_uint64 (value, object->timeout);
      break;
    case ARG_DVBSRC_TUNING_TIMEOUT:
      g_value_set_uint64 (value, object->tuning_timeout);
      break;
    case ARG_DVBSRC_DVB_BUFFER_SIZE:
      g_value_set_uint (value, object->dvb_buffer_size);
      break;
    case ARG_DVBSRC_DELSYS:
      g_value_set_enum (value, object->delsys);
      break;
    case ARG_DVBSRC_PILOT:
      g_value_set_enum (value, object->pilot);
      break;
    case ARG_DVBSRC_ROLLOFF:
      g_value_set_enum (value, object->rolloff);
      break;
    case ARG_DVBSRC_STREAM_ID:
      g_value_set_int (value, object->stream_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static gboolean
gst_dvbsrc_close_devices (GstDvbSrc * object)
{
  gst_dvbsrc_unset_pes_filters (object);

  close (object->fd_dvr);
  object->fd_dvr = -1;
  close (object->fd_frontend);
  object->fd_frontend = -1;

  return TRUE;
}

static gboolean
gst_dvbsrc_check_delsys (struct dtv_property *prop, guchar delsys)
{
  int i;

  for (i = 0; i < prop->u.buffer.len; i++) {
    if (prop->u.buffer.data[i] == delsys)
      return TRUE;
  }
  GST_LOG ("Adapter does not suport delsys: %d", delsys);
  return FALSE;
}

static gboolean
gst_dvbsrc_open_frontend (GstDvbSrc * object, gboolean writable)
{
  struct dvb_frontend_info fe_info;
  struct dtv_properties props;
  struct dtv_property dvb_prop[1];
  gchar *frontend_dev;
  GstStructure *adapter_structure;
  char *adapter_name = NULL;

  frontend_dev = g_strdup_printf ("/dev/dvb/adapter%d/frontend%d",
      object->adapter_number, object->frontend_number);
  GST_INFO_OBJECT (object, "Using frontend device: %s", frontend_dev);

  /* open frontend */
  if ((object->fd_frontend =
          open (frontend_dev, writable ? O_RDWR : O_RDONLY)) < 0) {
    switch (errno) {
      case ENOENT:
        GST_ELEMENT_ERROR (object, RESOURCE, NOT_FOUND,
            (_("Device \"%s\" does not exist."), frontend_dev), (NULL));
        break;
      default:
        GST_ELEMENT_ERROR (object, RESOURCE, OPEN_READ_WRITE,
            (_("Could not open frontend device \"%s\"."), frontend_dev),
            GST_ERROR_SYSTEM);
        break;
    }

    g_free (frontend_dev);
    return FALSE;
  }

  GST_DEBUG_OBJECT (object, "Device opened, querying information");

  if (ioctl (object->fd_frontend, FE_GET_INFO, &fe_info) < 0) {
    GST_ELEMENT_ERROR (object, RESOURCE, SETTINGS,
        (_("Could not get settings from frontend device \"%s\"."),
            frontend_dev), GST_ERROR_SYSTEM);

    close (object->fd_frontend);
    g_free (frontend_dev);
    return FALSE;
  }

  GST_DEBUG_OBJECT (object, "check delivery systems");

  dvb_prop[0].cmd = DTV_ENUM_DELSYS;
  props.num = 1;
  props.props = dvb_prop;

  if (ioctl (object->fd_frontend, FE_GET_PROPERTY, &props) < 0) {
    GST_ELEMENT_ERROR (object, RESOURCE, SETTINGS,
        (_("Cannot enumerate delivery systems from frontend device \"%s\"."),
            frontend_dev), GST_ERROR_SYSTEM);

    close (object->fd_frontend);
    g_free (frontend_dev);
    return FALSE;
  }

  GST_DEBUG_OBJECT (object, "Got information about adapter : %s", fe_info.name);

  adapter_name = g_strdup (fe_info.name);

  adapter_structure = gst_structure_new ("dvb-adapter",
      "name", G_TYPE_STRING, adapter_name,
      /* Capability supported auto params */
      "auto-inversion", G_TYPE_BOOLEAN, fe_info.caps & FE_CAN_INVERSION_AUTO,
      "auto-qam", G_TYPE_BOOLEAN, fe_info.caps & FE_CAN_QAM_AUTO,
      "auto-transmission-mode", G_TYPE_BOOLEAN,
      fe_info.caps & FE_CAN_TRANSMISSION_MODE_AUTO, "auto-guard-interval",
      G_TYPE_BOOLEAN, fe_info.caps & FE_CAN_GUARD_INTERVAL_AUTO,
      "auto-hierarchy", G_TYPE_BOOLEAN, fe_info.caps & FE_CAN_HIERARCHY_AUTO,
      "auto-fec", G_TYPE_BOOLEAN, fe_info.caps & FE_CAN_FEC_AUTO, NULL);

  /* Capability delivery systems */
  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_DVBC_ANNEX_A)) {
    gst_structure_set (adapter_structure, "dvb-c-a", G_TYPE_STRING,
        "DVB-C ANNEX A", NULL);
    object->best_guess_delsys = SYS_DVBC_ANNEX_A;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_DVBC_ANNEX_B)) {
    gst_structure_set (adapter_structure, "dvb-c-b", G_TYPE_STRING,
        "DVB-C ANNEX C", NULL);
    object->best_guess_delsys = SYS_DVBC_ANNEX_B;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_DVBT)) {
    gst_structure_set (adapter_structure, "dvb-t", G_TYPE_STRING, "DVB-T",
        NULL);
    object->best_guess_delsys = SYS_DVBT;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_DSS)) {
    gst_structure_set (adapter_structure, "dss", G_TYPE_STRING, "DSS", NULL);
    object->best_guess_delsys = SYS_DSS;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_DVBS)) {
    gst_structure_set (adapter_structure, "dvb-s", G_TYPE_STRING, "DVB-S",
        NULL);
    object->best_guess_delsys = SYS_DVBS;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_DVBS2)) {
    gst_structure_set (adapter_structure, "dvb-s2", G_TYPE_STRING, "DVB-S2",
        NULL);
    object->best_guess_delsys = SYS_DVBS2;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_DVBH)) {
    gst_structure_set (adapter_structure, "dvb-h", G_TYPE_STRING, "DVB-H",
        NULL);
    object->best_guess_delsys = SYS_DVBH;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_ISDBT)) {
    gst_structure_set (adapter_structure, "isdb-t", G_TYPE_STRING, "ISDB-T",
        NULL);
    object->best_guess_delsys = SYS_ISDBT;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_ISDBS)) {
    gst_structure_set (adapter_structure, "isdb-s", G_TYPE_STRING, "ISDB-S",
        NULL);
    object->best_guess_delsys = SYS_ISDBS;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_ISDBC)) {
    gst_structure_set (adapter_structure, "isdb-c", G_TYPE_STRING, "ISDB-C",
        NULL);
    object->best_guess_delsys = SYS_ISDBC;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_ATSC)) {
    gst_structure_set (adapter_structure, "atsc", G_TYPE_STRING, "ATSC", NULL);
    object->best_guess_delsys = SYS_ATSC;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_ATSCMH)) {
    gst_structure_set (adapter_structure, "atsc-mh", G_TYPE_STRING, "ATSC-MH",
        NULL);
    object->best_guess_delsys = SYS_ATSCMH;
  }
#if HAVE_V5_MINOR(7)
  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_DTMB)) {
    gst_structure_set (adapter_structure, "dtmb", G_TYPE_STRING, "DTMB", NULL);
    object->best_guess_delsys = SYS_DTMB;
  }
#endif

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_CMMB)) {
    gst_structure_set (adapter_structure, "cmmb", G_TYPE_STRING, "CMMB", NULL);
    object->best_guess_delsys = SYS_CMMB;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_DAB)) {
    gst_structure_set (adapter_structure, "dab", G_TYPE_STRING, "DAB", NULL);
    object->best_guess_delsys = SYS_DAB;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_DVBT2)) {
    gst_structure_set (adapter_structure, "dvb-t2", G_TYPE_STRING, "DVB-T2",
        NULL);
    object->best_guess_delsys = SYS_DVBT2;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_TURBO)) {
    gst_structure_set (adapter_structure, "turbo", G_TYPE_STRING, "TURBO",
        NULL);
    object->best_guess_delsys = SYS_TURBO;
  }

  if (gst_dvbsrc_check_delsys (&dvb_prop[0], SYS_DVBC_ANNEX_C)) {
    gst_structure_set (adapter_structure, "dvb-c-c", G_TYPE_STRING,
        "DVB-C ANNEX C", NULL);
    object->best_guess_delsys = SYS_DVBC_ANNEX_C;
  }

  GST_INFO_OBJECT (object, "DVB card: %s ", adapter_name);
  GST_TRACE_OBJECT (object, "%s description: %" GST_PTR_FORMAT, adapter_name,
      adapter_structure);
  gst_element_post_message (GST_ELEMENT_CAST (object), gst_message_new_element
      (GST_OBJECT (object), adapter_structure));
  g_free (frontend_dev);
  g_free (adapter_name);

  return TRUE;
}

static gboolean
gst_dvbsrc_open_dvr (GstDvbSrc * object)
{
  gchar *dvr_dev;

  dvr_dev = g_strdup_printf ("/dev/dvb/adapter%d/dvr%d",
      object->adapter_number, object->frontend_number);
  GST_INFO_OBJECT (object, "Using dvr device: %s", dvr_dev);

  /* open DVR */
  if ((object->fd_dvr = open (dvr_dev, O_RDONLY | O_NONBLOCK)) < 0) {
    switch (errno) {
      case ENOENT:
        GST_ELEMENT_ERROR (object, RESOURCE, NOT_FOUND,
            (_("Device \"%s\" does not exist."), dvr_dev), (NULL));
        break;
      default:
        GST_ELEMENT_ERROR (object, RESOURCE, OPEN_READ,
            (_("Could not open file \"%s\" for reading."), dvr_dev),
            GST_ERROR_SYSTEM);
        break;
    }
    g_free (dvr_dev);
    return FALSE;
  }
  g_free (dvr_dev);

  GST_INFO_OBJECT (object, "Setting DVB kernel buffer size to %d ",
      object->dvb_buffer_size);
  if (ioctl (object->fd_dvr, DMX_SET_BUFFER_SIZE, object->dvb_buffer_size) < 0) {
    GST_INFO_OBJECT (object, "ioctl DMX_SET_BUFFER_SIZE failed (%d)", errno);
    return FALSE;
  }
  return TRUE;
}

static void
gst_dvbsrc_finalize (GObject * _object)
{
  GstDvbSrc *object;

  GST_DEBUG_OBJECT (_object, "gst_dvbsrc_finalize");

  g_return_if_fail (GST_IS_DVBSRC (_object));
  object = GST_DVBSRC (_object);

  /* freeing the mutex segfaults somehow */
  g_mutex_clear (&object->tune_mutex);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (_object);
}


/*
 ******************************
 *                            *
 *      Plugin Realization    *
 *                            *
 ******************************
 */



/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 */
gboolean
gst_dvbsrc_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gstdvbsrc_debug, "dvbsrc", 0, "DVB Source Element");

  return gst_element_register (plugin, "dvbsrc", GST_RANK_NONE,
      GST_TYPE_DVBSRC);
}

static GstFlowReturn
gst_dvbsrc_read_device (GstDvbSrc * object, int size, GstBuffer ** buffer)
{
  gint count = 0;
  gint ret_val = 0;
  GstBuffer *buf = gst_buffer_new_and_alloc (size);
  GstClockTime timeout = object->timeout * GST_USECOND;
  GstMapInfo map;

  g_return_val_if_fail (GST_IS_BUFFER (buf), GST_FLOW_ERROR);

  if (object->fd_dvr < 0)
    return GST_FLOW_ERROR;

  gst_buffer_map (buf, &map, GST_MAP_WRITE);
  while (count < size) {
    ret_val = gst_poll_wait (object->poll, timeout);
    GST_LOG_OBJECT (object, "select returned %d", ret_val);
    if (G_UNLIKELY (ret_val < 0)) {
      if (errno == EBUSY)
        goto stopped;
      else if (errno == EINTR)
        continue;
      else
        goto select_error;
    } else if (G_UNLIKELY (ret_val == 0)) {
      /* timeout, post element message */
      gst_element_post_message (GST_ELEMENT_CAST (object),
          gst_message_new_element (GST_OBJECT (object),
              gst_structure_new_empty ("dvb-read-failure")));
    } else {
      int nread = read (object->fd_dvr, map.data + count, size - count);

      if (G_UNLIKELY (nread < 0)) {
        GST_WARNING_OBJECT
            (object,
            "Unable to read from device: /dev/dvb/adapter%d/dvr%d (%d)",
            object->adapter_number, object->frontend_number, errno);
        gst_element_post_message (GST_ELEMENT_CAST (object),
            gst_message_new_element (GST_OBJECT (object),
                gst_structure_new_empty ("dvb-read-failure")));
      } else
        count = count + nread;
    }
  }
  gst_buffer_unmap (buf, &map);
  gst_buffer_resize (buf, 0, count);

  *buffer = buf;

  return GST_FLOW_OK;

stopped:
  {
    GST_DEBUG_OBJECT (object, "stop called");
    gst_buffer_unmap (buf, &map);
    gst_buffer_unref (buf);
    return GST_FLOW_FLUSHING;
  }
select_error:
  {
    GST_ELEMENT_ERROR (object, RESOURCE, READ, (NULL),
        ("select error %d: %s (%d)", ret_val, g_strerror (errno), errno));
    gst_buffer_unmap (buf, &map);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_dvbsrc_create (GstPushSrc * element, GstBuffer ** buf)
{
  gint buffer_size;
  GstFlowReturn retval = GST_FLOW_ERROR;
  GstDvbSrc *object;

  object = GST_DVBSRC (element);
  GST_LOG ("fd_dvr: %d", object->fd_dvr);

  buffer_size = DEFAULT_BUFFER_SIZE;

  /* device can not be tuned during read */
  g_mutex_lock (&object->tune_mutex);


  if (object->fd_dvr > -1) {
    /* --- Read TS from DVR device --- */
    GST_DEBUG_OBJECT (object, "Reading from DVR device");
    retval = gst_dvbsrc_read_device (object, buffer_size, buf);

    if (object->stats_interval != 0 &&
        ++object->stats_counter == object->stats_interval) {
      gst_dvbsrc_output_frontend_stats (object);
      object->stats_counter = 0;
    }
  }

  g_mutex_unlock (&object->tune_mutex);

  return retval;

}

static GstStateChangeReturn
gst_dvbsrc_change_state (GstElement * element, GstStateChange transition)
{
  GstDvbSrc *src;
  GstStateChangeReturn ret;

  src = GST_DVBSRC (element);
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* open frontend then close it again, just so caps sent */
      gst_dvbsrc_open_frontend (src, FALSE);
      if (src->fd_frontend) {
        close (src->fd_frontend);
      }
      break;
    default:
      break;
  }

  return ret;
}


static gboolean
gst_dvbsrc_start (GstBaseSrc * bsrc)
{
  GstDvbSrc *src = GST_DVBSRC (bsrc);

  gst_dvbsrc_open_frontend (src, TRUE);
  if (!gst_dvbsrc_tune (src)) {
    GST_ERROR_OBJECT (src, "Not able to lock on to the dvb channel");
    gst_dvbsrc_unset_pes_filters (src);
    close (src->fd_frontend);
    return FALSE;
  }
  if (!gst_dvbsrc_open_dvr (src)) {
    GST_ERROR_OBJECT (src, "Not able to open dvr_device");
    /* unset filters also */
    gst_dvbsrc_unset_pes_filters (src);
    close (src->fd_frontend);
    return FALSE;
  }
  if (!(src->poll = gst_poll_new (TRUE))) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE, (NULL),
        ("could not create an fdset: %s (%d)", g_strerror (errno), errno));
    /* unset filters also */
    gst_dvbsrc_unset_pes_filters (src);
    close (src->fd_frontend);
    return FALSE;
  } else {
    gst_poll_fd_init (&src->poll_fd_dvr);
    src->poll_fd_dvr.fd = src->fd_dvr;
    gst_poll_add_fd (src->poll, &src->poll_fd_dvr);
    gst_poll_fd_ctl_read (src->poll, &src->poll_fd_dvr, TRUE);
  }

  return TRUE;
}

static gboolean
gst_dvbsrc_stop (GstBaseSrc * bsrc)
{
  GstDvbSrc *src = GST_DVBSRC (bsrc);

  gst_dvbsrc_close_devices (src);
  if (src->poll) {
    gst_poll_free (src->poll);
    src->poll = NULL;
  }

  return TRUE;
}

static gboolean
gst_dvbsrc_unlock (GstBaseSrc * bsrc)
{
  GstDvbSrc *src = GST_DVBSRC (bsrc);

  gst_poll_set_flushing (src->poll, TRUE);
  return TRUE;
}

static gboolean
gst_dvbsrc_unlock_stop (GstBaseSrc * bsrc)
{
  GstDvbSrc *src = GST_DVBSRC (bsrc);

  gst_poll_set_flushing (src->poll, FALSE);
  return TRUE;
}

static gboolean
gst_dvbsrc_is_seekable (GstBaseSrc * bsrc)
{
  return FALSE;
}

static gboolean
gst_dvbsrc_get_size (GstBaseSrc * src, guint64 * size)
{
  return FALSE;
}

static void
gst_dvbsrc_output_frontend_stats (GstDvbSrc * src)
{
  fe_status_t status;
  uint16_t snr, _signal;
  uint32_t ber, uncorrected_blocks;
  GstMessage *message;
  GstStructure *structure;
  int fe_fd = src->fd_frontend;

  if (ioctl (fe_fd, FE_READ_STATUS, &status) ||
      ioctl (fe_fd, FE_READ_SIGNAL_STRENGTH, &_signal) ||
      ioctl (fe_fd, FE_READ_SNR, &snr) ||
      ioctl (fe_fd, FE_READ_BER, &ber) ||
      ioctl (fe_fd, FE_READ_UNCORRECTED_BLOCKS, &uncorrected_blocks)) {
    GST_WARNING_OBJECT (src, "Failed to get statistics from the device");
    return;
  }

  structure = gst_structure_new ("dvb-frontend-stats", "status", G_TYPE_INT,
      status, "signal", G_TYPE_INT, _signal, "snr", G_TYPE_INT, snr,
      "ber", G_TYPE_INT, ber, "unc", G_TYPE_INT, uncorrected_blocks,
      "lock", G_TYPE_BOOLEAN, status & FE_HAS_LOCK, NULL);
  message = gst_message_new_element (GST_OBJECT (src), structure);
  gst_element_post_message (GST_ELEMENT (src), message);
}

struct diseqc_cmd
{
  struct dvb_diseqc_master_cmd cmd;
  uint32_t wait;
};

static void
diseqc_send_msg (int fd, fe_sec_voltage_t v, struct diseqc_cmd *cmd,
    fe_sec_tone_mode_t t, fe_sec_mini_cmd_t b)
{
  if (ioctl (fd, FE_SET_TONE, SEC_TONE_OFF) == -1) {
    GST_ERROR ("Setting tone to off failed");
    return;
  }

  if (ioctl (fd, FE_SET_VOLTAGE, v) == -1) {
    GST_ERROR ("Setting voltage failed");
    return;
  }

  g_usleep (15 * 1000);
  GST_LOG ("diseqc: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x", cmd->cmd.msg[0],
      cmd->cmd.msg[1], cmd->cmd.msg[2], cmd->cmd.msg[3], cmd->cmd.msg[4],
      cmd->cmd.msg[5]);
  if (ioctl (fd, FE_DISEQC_SEND_MASTER_CMD, &cmd->cmd) == -1) {
    GST_ERROR ("Sending DiSEqC command failed");
    return;
  }

  g_usleep (cmd->wait * 1000);
  g_usleep (15 * 1000);

  if (ioctl (fd, FE_DISEQC_SEND_BURST, b) == -1) {
    GST_ERROR ("Sending burst failed");
    return;
  }

  g_usleep (15 * 1000);

  if (ioctl (fd, FE_SET_TONE, t) == -1) {
    GST_ERROR ("Setting tone failed");
    return;
  }
}


/* digital satellite equipment control,
 * specification is available from http://www.eutelsat.com/
 */
static void
diseqc (int secfd, int sat_no, int voltage, int tone)
{
  struct diseqc_cmd cmd = { {{0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00}, 4}, 0 };

  /* param: high nibble: reset bits, low nibble set bits,
   * bits are: option, position, polarizaion, band
   */
  cmd.cmd.msg[3] =
      0xf0 | (((sat_no * 4) & 0x0f) | (tone == SEC_TONE_ON ? 1 : 0) |
      (voltage == SEC_VOLTAGE_13 ? 0 : 2));
  /* send twice because some DiSEqC switches do not respond correctly the
   * first time */
  diseqc_send_msg (secfd, voltage, &cmd, tone,
      sat_no % 2 ? SEC_MINI_B : SEC_MINI_A);
  diseqc_send_msg (secfd, voltage, &cmd, tone,
      sat_no % 2 ? SEC_MINI_B : SEC_MINI_A);

}

inline static void
set_prop (struct dtv_property *props, int *n, guint32 cmd, guint32 data)
{
  if (*n == NUM_DTV_PROPS) {
    g_critical ("Index out of bounds");
  } else {
    props[*n].cmd = cmd;
    props[(*n)++].u.data = data;
  }
}

static gboolean
gst_dvbsrc_tune_fe (GstDvbSrc * object)
{
  GstPoll *poll_set;
  GstPollFD fe_fd;
  fe_status_t status;
  struct dtv_properties props;
  struct dtv_property dvb_prop[NUM_DTV_PROPS];
  GstClockTimeDiff elapsed_time, timeout_step = 500 * GST_MSECOND;
  GstClockTime start;

  GST_DEBUG_OBJECT (object, "Starting the frontend tuning process");

  if (object->fd_frontend < 0) {
    GST_INFO_OBJECT (object, "Frontend not open: tuning later");
    return FALSE;
  }

  GST_DEBUG_OBJECT (object, "check delivery systems");

  dvb_prop[0].cmd = DTV_ENUM_DELSYS;
  props.num = 1;
  props.props = dvb_prop;

  if (gst_dvbsrc_retry_ioctl (object->fd_frontend, FE_GET_PROPERTY, &props)) {
    GST_WARNING_OBJECT (object, "Error enumerating delsys: %s",
        g_strerror (errno));

    return FALSE;
  }

  /* If there's no delivery system set yet. Choose the
   * last from the list of frontend supported ones */
  if (object->delsys == SYS_UNDEFINED) {
    object->delsys = object->best_guess_delsys;
  } else if (!gst_dvbsrc_check_delsys (&dvb_prop[0], object->delsys)) {
    GST_WARNING_OBJECT (object, "Delsys fail %u", object->delsys);
    return FALSE;
  }

  gst_dvbsrc_unset_pes_filters (object);

  gst_poll_fd_init (&fe_fd);
  fe_fd.fd = object->fd_frontend;
  poll_set = gst_poll_new (TRUE);

  if (!gst_poll_add_fd (poll_set, &fe_fd)) {
    GST_WARNING_OBJECT (object, "Could not add frontend fd to poll set");
    goto fail;
  }

  gst_poll_fd_ctl_read (poll_set, &fe_fd, TRUE);

  memset (dvb_prop, 0, sizeof (dvb_prop));
  dvb_prop[0].cmd = DTV_CLEAR;

  if (gst_dvbsrc_retry_ioctl (object->fd_frontend, FE_SET_PROPERTY, &props)) {
    GST_WARNING_OBJECT (object, "Error resetting tuner: %s",
        g_strerror (errno));
  }

  memset (dvb_prop, 0, sizeof (dvb_prop));
  if (!gst_dvbsrc_set_fe_params (object, &props)) {
    GST_WARNING_OBJECT (object, "Could not set frontend params");
    goto fail;
  }

  GST_DEBUG_OBJECT (object, "Setting %d properties", props.num);
  if (gst_dvbsrc_retry_ioctl (object->fd_frontend, FE_SET_PROPERTY, &props)) {
    GST_WARNING_OBJECT (object, "Error tuning channel: %s (%d)",
        g_strerror (errno), errno);
    goto fail;
  }

  g_signal_emit (object, gst_dvbsrc_signals[SIGNAL_TUNING_START], 0);

  if (gst_dvbsrc_retry_ioctl (object->fd_frontend, FE_READ_STATUS, &status)) {
    GST_WARNING_OBJECT (object, "Failed querying frontend for tuning status:"
        " %s (%d)", g_strerror (errno), errno);
    goto fail_with_signal;
  }

  /* signal locking loop */
  elapsed_time = 0;
  start = gst_util_get_timestamp ();

  while (!(status & FE_HAS_LOCK) && elapsed_time <= object->tuning_timeout) {
    if (gst_poll_wait (poll_set, timeout_step) == -1)
      goto fail_with_signal;
    if (gst_dvbsrc_retry_ioctl (object->fd_frontend, FE_READ_STATUS, &status)) {
      GST_WARNING_OBJECT (object, "Failed querying frontend for tuning status"
          " %s (%d)", g_strerror (errno), errno);
      goto fail_with_signal;
    }
    gst_dvbsrc_output_frontend_stats (object);
    /* keep retrying forever if tuning_timeout = 0 */
    if (object->tuning_timeout)
      elapsed_time = GST_CLOCK_DIFF (start, gst_util_get_timestamp ());
    GST_LOG_OBJECT (object,
        "Tuning. Time elapsed %" G_GUINT64_FORMAT " Limit %" G_GUINT64_FORMAT,
        elapsed_time, object->tuning_timeout);
  }

  if (!(status & FE_HAS_LOCK)) {
    GST_WARNING_OBJECT (object,
        "Unable to lock on signal at desired frequency");
    goto fail_with_signal;
  }

  GST_LOG_OBJECT (object, "status == 0x%02x", status);

  g_signal_emit (object, gst_dvbsrc_signals[SIGNAL_TUNING_DONE], 0);
  GST_DEBUG_OBJECT (object, "Successfully set frontend tuning params");

  gst_poll_free (poll_set);
  return TRUE;

fail_with_signal:
  g_signal_emit (object, gst_dvbsrc_signals[SIGNAL_TUNING_FAIL], 0);
fail:
  GST_WARNING_OBJECT (object, "Could not tune to desired frequency");
  gst_poll_free (poll_set);
  return FALSE;
}

static gboolean
gst_dvbsrc_set_fe_params (GstDvbSrc * object, struct dtv_properties *props)
{
  fe_sec_voltage_t voltage;
  unsigned int freq = object->freq;
  unsigned int sym_rate = object->sym_rate * 1000;
  int inversion = object->inversion;
  int n;

  /* first 3 entries are reserved */
  n = 3;

  switch (object->delsys) {
    case SYS_DVBS:
    case SYS_DVBS2:
    case SYS_TURBO:
      if (freq > 2200000) {
        /* FIXME: Make SLOF/LOF1/LOF2 seteable props with a sane default.
         * These values shouldn't be fixed because not all universal LNBs
         * share the same parameters.
         *
         * this must be an absolute frequency */
        if (freq < SLOF) {
          freq -= LOF1;
          object->tone = SEC_TONE_OFF;
        } else {
          freq -= LOF2;
          object->tone = SEC_TONE_ON;
        }
      }

      inversion = INVERSION_AUTO;
      set_prop (props->props, &n, DTV_SYMBOL_RATE, sym_rate);
      set_prop (props->props, &n, DTV_INNER_FEC, object->code_rate_hp);

      GST_INFO_OBJECT (object,
          "Tuning DVB-S/DVB-S2/Turbo to L-Band:%u, Pol:%d, srate=%u, 22kHz=%s",
          freq, object->pol, sym_rate,
          object->tone == SEC_TONE_ON ? "on" : "off");

      if (object->pol == DVB_POL_H)
        voltage = SEC_VOLTAGE_18;
      else
        voltage = SEC_VOLTAGE_13;

      if (object->diseqc_src == -1 || object->send_diseqc == FALSE) {
        set_prop (props->props, &n, DTV_VOLTAGE, voltage);

        /* DTV_TONE not yet implemented
         * set_prop (fe_props_array, &n, DTV_TONE, object->tone) */
        if (ioctl (object->fd_frontend, FE_SET_TONE, object->tone) < 0) {
          GST_WARNING_OBJECT (object, "Couldn't set tone: %s",
              g_strerror (errno));
        }
      } else {
        GST_DEBUG_OBJECT (object, "Sending DISEqC");
        diseqc (object->fd_frontend, object->diseqc_src, voltage, object->tone);
        /* Once DiSEqC source is set, do not set it again until
         * app decides to change it
         * object->send_diseqc = FALSE; */
      }

      if ((object->delsys == SYS_DVBS2) || (object->delsys == SYS_TURBO))
        set_prop (props->props, &n, DTV_MODULATION, object->modulation);

      if (object->delsys == SYS_DVBS2) {
        set_prop (props->props, &n, DTV_PILOT, object->pilot);
        set_prop (props->props, &n, DTV_ROLLOFF, object->rolloff);
        set_prop (props->props, &n, DTV_STREAM_ID, object->stream_id);
      }
      break;
    case SYS_DVBT:
    case SYS_DVBT2:
      set_prop (props->props, &n, DTV_BANDWIDTH_HZ, object->bandwidth);
      set_prop (props->props, &n, DTV_CODE_RATE_HP, object->code_rate_hp);
      set_prop (props->props, &n, DTV_CODE_RATE_LP, object->code_rate_lp);
      set_prop (props->props, &n, DTV_MODULATION, object->modulation);
      set_prop (props->props, &n, DTV_TRANSMISSION_MODE,
          object->transmission_mode);
      set_prop (props->props, &n, DTV_GUARD_INTERVAL, object->guard_interval);
      set_prop (props->props, &n, DTV_HIERARCHY, object->hierarchy_information);
      if (object->delsys == SYS_DVBT2) {
        set_prop (props->props, &n, DTV_STREAM_ID, object->stream_id);
      }

      GST_INFO_OBJECT (object, "Tuning DVB-T/DVB_T2 to %d Hz", freq);
      break;
    case SYS_DVBC_ANNEX_A:
    case SYS_DVBC_ANNEX_B:
#if HAVE_V5_MINOR(6)
    case SYS_DVBC_ANNEX_C:
#endif
      GST_INFO_OBJECT (object, "Tuning DVB-C/ClearCable to %d, srate=%d",
          freq, sym_rate);

      set_prop (props->props, &n, DTV_MODULATION, object->modulation);
      if (object->delsys != SYS_DVBC_ANNEX_B) {
        set_prop (props->props, &n, DTV_INNER_FEC, object->code_rate_hp);
        set_prop (props->props, &n, DTV_SYMBOL_RATE, sym_rate);
      }
      break;
    case SYS_ATSC:
      GST_INFO_OBJECT (object, "Tuning ATSC to %d", freq);

      set_prop (props->props, &n, DTV_MODULATION, object->modulation);
      break;
    default:
      GST_ERROR_OBJECT (object, "Unknown frontend type %u", object->delsys);
      return FALSE;
  }

  set_prop (props->props, &n, DTV_TUNE, 0);
  props->num = n;
  /* set first three entries */
  n = 0;
  set_prop (props->props, &n, DTV_DELIVERY_SYSTEM, object->delsys);
  set_prop (props->props, &n, DTV_FREQUENCY, freq);
  set_prop (props->props, &n, DTV_INVERSION, inversion);

  return TRUE;
}

static gboolean
gst_dvbsrc_tune (GstDvbSrc * object)
{
  /* found in mail archive on linuxtv.org
   * What works well for us is:
   * - first establish a TS feed (i.e. tune the frontend and check for success)
   * - then set filters (PES/sections)
   * - then tell the MPEG decoder to start
   * - before tuning: first stop the MPEG decoder, then stop all filters
   */
  if (!gst_dvbsrc_tune_fe (object)) {
    GST_WARNING_OBJECT (object, "Unable to tune frontend");
    return FALSE;
  }

  gst_dvbsrc_set_pes_filters (object);

  return TRUE;
}


static void
gst_dvbsrc_unset_pes_filters (GstDvbSrc * object)
{
  int i = 0;

  GST_INFO_OBJECT (object, "clearing PES filter");

  for (i = 0; i < MAX_FILTERS; i++) {
    if (object->fd_filters[i] == -1)
      continue;
    close (object->fd_filters[i]);
    object->fd_filters[i] = -1;
  }
}

static void
gst_dvbsrc_set_pes_filters (GstDvbSrc * object)
{
  int *fd;
  int pid, i;
  struct dmx_pes_filter_params pes_filter;
  gchar *demux_dev = g_strdup_printf ("/dev/dvb/adapter%d/demux%d",
      object->adapter_number, object->frontend_number);

  GST_INFO_OBJECT (object, "Setting PES filter");

  for (i = 0; i < MAX_FILTERS; i++) {
    if (object->pids[i] == G_MAXUINT16)
      break;

    fd = &object->fd_filters[i];
    pid = object->pids[i];

    if (*fd >= 0)
      close (*fd);
    if ((*fd = open (demux_dev, O_RDWR)) < 0) {
      GST_ERROR_OBJECT (object, "Error opening demuxer: %s (%s)",
          g_strerror (errno), demux_dev);
      continue;
    }
    g_return_if_fail (*fd != -1);

    pes_filter.pid = pid;
    pes_filter.input = DMX_IN_FRONTEND;
    pes_filter.output = DMX_OUT_TS_TAP;
    pes_filter.pes_type = DMX_PES_OTHER;
    pes_filter.flags = DMX_IMMEDIATE_START;

    GST_INFO_OBJECT (object, "Setting pes-filter, pid = %d, type = %d",
        pes_filter.pid, pes_filter.pes_type);

    if (ioctl (*fd, DMX_SET_PES_FILTER, &pes_filter) < 0)
      GST_WARNING_OBJECT (object, "Error setting PES filter on %s: %s",
          demux_dev, g_strerror (errno));
  }

  g_free (demux_dev);
}
