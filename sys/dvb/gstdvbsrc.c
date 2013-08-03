/* GStreamer DVB source
 * Copyright (C) 2006 Zaheer Abbas Merali <zaheerabbas at merali
 *                                         dot org>
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
 * gst-launch dvbsrc polarity="h" frequency=11302000 symbol-rate=27500 diseqc-src=0 pids=50:102:103 ! mpegtsdemux name=demux ! queue max-size-buffers=0 max-size-time=0 ! mpeg2dec ! xvimagesink demux. ! queue max-size-buffers=0 max-size-time=0 ! mad ! alsasink
 * ]| Captures and renders a transport stream from dvb card 0 that is a DVB-S card for a program at tuned frequency 11302000 Hz, symbol rate of 27500 kHz with PMT pid of 50 and elementary stream pids of 102 and 103.
 * </refsect2>
 */

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

GST_DEBUG_CATEGORY_STATIC (gstdvbsrc_debug);
#define GST_CAT_DEFAULT (gstdvbsrc_debug)

#define SLOF (11700*1000UL)
#define LOF1 (9750*1000UL)
#define LOF2 (10600*1000UL)

#define NUM_DTV_PROPS 16

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
  ARG_DVBSRC_DVB_BUFFER_SIZE
};

#define DEFAULT_ADAPTER 0
#define DEFAULT_FRONTEND 0
#define DEFAULT_DISEQC_SRC -1   /* disabled */
#define DEFAULT_FREQUENCY 0
#define DEFAULT_POLARITY "H"
#define DEFAULT_PIDS "8192"
#define DEFAULT_SYMBOL_RATE 0
#define DEFAULT_BANDWIDTH BANDWIDTH_7_MHZ
#define DEFAULT_CODE_RATE_HP FEC_AUTO
#define DEFAULT_CODE_RATE_LP FEC_1_2
#define DEFAULT_GUARD GUARD_INTERVAL_1_16
#define DEFAULT_MODULATION QAM_16
#define DEFAULT_TRANSMISSION_MODE TRANSMISSION_MODE_8K
#define DEFAULT_HIERARCHY HIERARCHY_1
#define DEFAULT_INVERSION INVERSION_ON
#define DEFAULT_STATS_REPORTING_INTERVAL 100
#define DEFAULT_TIMEOUT 1000000 /* 1 second */
#define DEFAULT_DVB_BUFFER_SIZE (10*188*1024)   /* Default is the same as the kernel default */
#define DEFAULT_BUFFER_SIZE 8192        /* not a property */

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
static void gst_dvbsrc_set_pes_filters (GstDvbSrc * object);
static void gst_dvbsrc_unset_pes_filters (GstDvbSrc * object);

static gboolean gst_dvbsrc_frontend_status (GstDvbSrc * object);

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
          "The adapter device number (eg. 0 for adapter0)",
          0, 16, DEFAULT_ADAPTER, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_FRONTEND,
      g_param_spec_int ("frontend", "The frontend device number",
          "The frontend device number (eg. 0 for frontend0)",
          0, 16, DEFAULT_FRONTEND, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_FREQUENCY,
      g_param_spec_uint ("frequency", "frequency", "Frequency",
          0, G_MAXUINT, DEFAULT_FREQUENCY, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_POLARITY,
      g_param_spec_string ("polarity", "polarity", "Polarity [vhHV] (DVB-S)",
          DEFAULT_POLARITY, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_PIDS,
      g_param_spec_string ("pids", "pids",
          "Colon seperated list of pids (eg. 110:120)",
          DEFAULT_PIDS, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_SYM_RATE,
      g_param_spec_uint ("symbol-rate",
          "symbol rate",
          "Symbol Rate (DVB-S, DVB-C)",
          0, G_MAXUINT, DEFAULT_SYMBOL_RATE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_TUNE,
      g_param_spec_pointer ("tune",
          "tune", "Atomically tune to channel. (For Apps)", G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_DISEQC_SRC,
      g_param_spec_int ("diseqc-source",
          "diseqc source",
          "DISEqC selected source (-1 disabled) (DVB-S)",
          -1, 7, DEFAULT_DISEQC_SRC, G_PARAM_READWRITE));

  /* DVB-T, additional properties */

  g_object_class_install_property (gobject_class, ARG_DVBSRC_BANDWIDTH,
      g_param_spec_enum ("bandwidth",
          "bandwidth",
          "Bandwidth (DVB-T)", GST_TYPE_DVBSRC_BANDWIDTH, DEFAULT_BANDWIDTH,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_CODE_RATE_HP,
      g_param_spec_enum ("code-rate-hp",
          "code-rate-hp",
          "High Priority Code Rate (DVB-T, DVB-S and DVB-C)",
          GST_TYPE_DVBSRC_CODE_RATE, DEFAULT_CODE_RATE_HP, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_CODE_RATE_LP,
      g_param_spec_enum ("code-rate-lp",
          "code-rate-lp",
          "Low Priority Code Rate (DVB-T)",
          GST_TYPE_DVBSRC_CODE_RATE, DEFAULT_CODE_RATE_LP, G_PARAM_READWRITE));

  /* FIXME: should the property be called 'guard-interval' then? */
  g_object_class_install_property (gobject_class, ARG_DVBSRC_GUARD,
      g_param_spec_enum ("guard",
          "guard",
          "Guard Interval (DVB-T)",
          GST_TYPE_DVBSRC_GUARD, DEFAULT_GUARD, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_MODULATION,
      g_param_spec_enum ("modulation", "modulation",
          "Modulation (DVB-T and DVB-C)",
          GST_TYPE_DVBSRC_MODULATION, DEFAULT_MODULATION, G_PARAM_READWRITE));

  /* FIXME: property should be named 'transmission-mode' */
  g_object_class_install_property (gobject_class,
      ARG_DVBSRC_TRANSMISSION_MODE,
      g_param_spec_enum ("trans-mode", "trans-mode",
          "Transmission Mode (DVB-T)", GST_TYPE_DVBSRC_TRANSMISSION_MODE,
          DEFAULT_TRANSMISSION_MODE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_HIERARCHY_INF,
      g_param_spec_enum ("hierarchy", "hierarchy",
          "Hierarchy Information (DVB-T)",
          GST_TYPE_DVBSRC_HIERARCHY, DEFAULT_HIERARCHY, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_INVERSION,
      g_param_spec_enum ("inversion", "inversion",
          "Inversion Information (DVB-T and DVB-C)",
          GST_TYPE_DVBSRC_INVERSION, DEFAULT_INVERSION, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      ARG_DVBSRC_STATS_REPORTING_INTERVAL,
      g_param_spec_uint ("stats-reporting-interval",
          "stats-reporting-interval",
          "The number of reads before reporting frontend stats",
          0, G_MAXUINT, DEFAULT_STATS_REPORTING_INTERVAL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_TIMEOUT,
      g_param_spec_uint64 ("timeout", "Timeout",
          "Post a message after timeout microseconds (0 = disabled)", 0,
          G_MAXUINT64, DEFAULT_TIMEOUT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      ARG_DVBSRC_DVB_BUFFER_SIZE,
      g_param_spec_uint ("dvb-buffer-size",
          "dvb-buffer-size",
          "The kernel buffer size used by the DVB api",
          0, G_MAXUINT, DEFAULT_DVB_BUFFER_SIZE, G_PARAM_READWRITE));
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
  /* Pid 8192 on DVB gets the whole transport stream */
  object->pids[0] = 8192;
  object->dvb_buffer_size = DEFAULT_DVB_BUFFER_SIZE;
  object->adapter_number = DEFAULT_ADAPTER;
  object->frontend_number = DEFAULT_FRONTEND;
  object->diseqc_src = DEFAULT_DISEQC_SRC;
  object->send_diseqc = (DEFAULT_DISEQC_SRC != -1);
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

  g_mutex_init (&object->tune_mutex);
  object->timeout = DEFAULT_TIMEOUT;
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
      gchar *pid_string;

      pid_string = g_value_dup_string (value);
      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_PIDS %s", pid_string);
      if (!strcmp (pid_string, "8192")) {
        /* get the whole ts */
        int pid_count = 1;
        object->pids[0] = 8192;
        while (pid_count < MAX_FILTERS) {
          object->pids[pid_count++] = G_MAXUINT16;
        }
      } else {
        int pid = 0;
        int pid_count;
        gchar **pids;
        char **tmp;

        tmp = pids = g_strsplit (pid_string, ":", MAX_FILTERS);
        if (pid_string)
          g_free (pid_string);

        /* always add the PAT and CAT pids */
        object->pids[0] = 0;
        object->pids[1] = 1;

        pid_count = 2;
        while (*pids != NULL && pid_count < MAX_FILTERS) {
          pid = strtol (*pids, NULL, 0);
          if (pid > 1 && pid <= 8192) {
            GST_INFO_OBJECT (object, "\tParsed Pid: %d", pid);
            object->pids[pid_count] = pid;
            pid_count++;
          }
          pids++;
        }
        while (pid_count < MAX_FILTERS) {
          object->pids[pid_count++] = G_MAXUINT16;
        }

        g_strfreev (tmp);
      }
      /* if we are in playing or paused, then set filters now */
      GST_INFO_OBJECT (object, "checking if playing for setting pes filters");
      if (GST_ELEMENT (object)->current_state == GST_STATE_PLAYING ||
          GST_ELEMENT (object)->current_state == GST_STATE_PAUSED) {
        GST_INFO_OBJECT (object, "Setting pes filters now");
        gst_dvbsrc_set_pes_filters (object);
      }
    }
      break;
    case ARG_DVBSRC_SYM_RATE:
      object->sym_rate = g_value_get_uint (value);
      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_SYM_RATE to value %d",
          object->sym_rate);
      break;

    case ARG_DVBSRC_BANDWIDTH:
      object->bandwidth = g_value_get_enum (value);
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

      /* if we are in paused/playing state tune now, otherwise in ready to paused state change */
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
    case ARG_DVBSRC_DVB_BUFFER_SIZE:
      object->dvb_buffer_size = g_value_get_uint (value);
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
    case ARG_DVBSRC_BANDWIDTH:
      g_value_set_enum (value, object->bandwidth);
      break;
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
    case ARG_DVBSRC_DVB_BUFFER_SIZE:
      g_value_set_uint (value, object->dvb_buffer_size);
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
gst_dvbsrc_open_frontend (GstDvbSrc * object, gboolean writable)
{
  struct dvb_frontend_info fe_info;
  const char *adapter_desc = NULL;
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

    close (object->fd_frontend);
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

  GST_DEBUG_OBJECT (object, "Got information about adapter : %s", fe_info.name);

  adapter_name = g_strdup (fe_info.name);

  object->adapter_type = fe_info.type;
  switch (object->adapter_type) {
    case FE_QPSK:
      adapter_desc = "DVB-S";
      adapter_structure = gst_structure_new ("dvb-adapter",
          "type", G_TYPE_STRING, adapter_desc,
          "name", G_TYPE_STRING, adapter_name,
          "auto-fec", G_TYPE_BOOLEAN, fe_info.caps & FE_CAN_FEC_AUTO, NULL);
      break;
    case FE_QAM:
      adapter_desc = "DVB-C";
      adapter_structure = gst_structure_new ("dvb-adapter",
          "type", G_TYPE_STRING, adapter_desc,
          "name", G_TYPE_STRING, adapter_name,
          "auto-inversion", G_TYPE_BOOLEAN,
          fe_info.caps & FE_CAN_INVERSION_AUTO, "auto-qam", G_TYPE_BOOLEAN,
          fe_info.caps & FE_CAN_QAM_AUTO, "auto-fec", G_TYPE_BOOLEAN,
          fe_info.caps & FE_CAN_FEC_AUTO, NULL);
      break;
    case FE_OFDM:
      adapter_desc = "DVB-T";
      adapter_structure = gst_structure_new ("dvb-adapter",
          "type", G_TYPE_STRING, adapter_desc,
          "name", G_TYPE_STRING, adapter_name,
          "auto-inversion", G_TYPE_BOOLEAN,
          fe_info.caps & FE_CAN_INVERSION_AUTO, "auto-qam", G_TYPE_BOOLEAN,
          fe_info.caps & FE_CAN_QAM_AUTO, "auto-transmission-mode",
          G_TYPE_BOOLEAN, fe_info.caps & FE_CAN_TRANSMISSION_MODE_AUTO,
          "auto-guard-interval", G_TYPE_BOOLEAN,
          fe_info.caps & FE_CAN_GUARD_INTERVAL_AUTO, "auto-hierarchy",
          G_TYPE_BOOLEAN, fe_info.caps % FE_CAN_HIERARCHY_AUTO, "auto-fec",
          G_TYPE_BOOLEAN, fe_info.caps & FE_CAN_FEC_AUTO, NULL);
      break;
    case FE_ATSC:
      adapter_desc = "ATSC";
      adapter_structure = gst_structure_new ("dvb-adapter",
          "type", G_TYPE_STRING, adapter_desc,
          "name", G_TYPE_STRING, adapter_name, NULL);
      break;
    default:
      g_error ("Unknown frontend type: %d", object->adapter_type);
      adapter_structure = gst_structure_new ("dvb-adapter",
          "type", G_TYPE_STRING, "unknown", NULL);
  }

  GST_INFO_OBJECT (object, "DVB card: %s ", adapter_name);
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

  //g_object_get(G_OBJECT(object), "blocksize", &buffer_size, NULL);
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
    close (src->fd_frontend);
    return FALSE;
  }
  if (!gst_dvbsrc_frontend_status (src)) {
    /* unset filters also */
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

  ioctl (fe_fd, FE_READ_STATUS, &status);
  ioctl (fe_fd, FE_READ_SIGNAL_STRENGTH, &_signal);
  ioctl (fe_fd, FE_READ_SNR, &snr);
  ioctl (fe_fd, FE_READ_BER, &ber);
  ioctl (fe_fd, FE_READ_UNCORRECTED_BLOCKS, &uncorrected_blocks);

  structure = gst_structure_new ("dvb-frontend-stats", "status", G_TYPE_INT,
      status, "signal", G_TYPE_INT, _signal, "snr", G_TYPE_INT, snr,
      "ber", G_TYPE_INT, ber, "unc", G_TYPE_INT, uncorrected_blocks,
      "lock", G_TYPE_BOOLEAN, status & FE_HAS_LOCK, NULL);
  message = gst_message_new_element (GST_OBJECT (src), structure);
  gst_element_post_message (GST_ELEMENT (src), message);
}

static gboolean
gst_dvbsrc_frontend_status (GstDvbSrc * object)
{
  fe_status_t status = 0;
  gint i;

  GST_INFO_OBJECT (object, "gst_dvbsrc_frontend_status");

  if (object->fd_frontend < 0) {
    GST_ERROR_OBJECT (object,
        "Trying to get frontend status from not opened device!");
    return FALSE;
  } else
    GST_INFO_OBJECT (object, "fd-frontend: %d", object->fd_frontend);

  for (i = 0; i < 15; i++) {
    g_usleep (1000000);
    GST_INFO_OBJECT (object, ".");
    if (ioctl (object->fd_frontend, FE_READ_STATUS, &status) == -1) {
      GST_ERROR_OBJECT (object, "Failed reading frontend status.");
      return FALSE;
    }
    gst_dvbsrc_output_frontend_stats (object);
    if (status & FE_HAS_LOCK) {
      break;
    }
  }

  if (!(status & FE_HAS_LOCK)) {
    GST_INFO_OBJECT (object,
        "Not able to lock to the signal on the given frequency.");
    return FALSE;
  } else
    return TRUE;
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
    GST_ERROR ("Sending diseqc command failed");
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
  /* send twice because some diseqc switches do not respond correctly the
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
gst_dvbsrc_tune (GstDvbSrc * object)
{
  struct dtv_properties props;
  struct dtv_property dvb_prop[NUM_DTV_PROPS];
  fe_sec_voltage_t voltage;
  fe_status_t status;
  int n;
  int i;
  int j;
  unsigned int del_sys = SYS_UNDEFINED;
  unsigned int freq = object->freq;
  unsigned int sym_rate = object->sym_rate * 1000;
  unsigned int bandwidth;
  int inversion = object->inversion;

  /* found in mail archive on linuxtv.org
   * What works well for us is:
   * - first establish a TS feed (i.e. tune the frontend and check for success)
   * - then set filters (PES/sections)
   * - then tell the MPEG decoder to start
   * - before tuning: first stop the MPEG decoder, then stop all filters  
   */
  GST_INFO_OBJECT (object, "gst_dvbsrc_tune");

  if (object->fd_frontend < 0) {
    /* frontend not opened yet, tune later */
    GST_INFO_OBJECT (object, "Frontend not open: tuning later");
    return FALSE;
  }

  GST_DEBUG_OBJECT (object, "api version %d.%d", DVB_API_VERSION,
      DVB_API_VERSION_MINOR);

  gst_dvbsrc_unset_pes_filters (object);
  for (j = 0; j < 5; j++) {
    memset (dvb_prop, 0, sizeof (dvb_prop));
    dvb_prop[0].cmd = DTV_CLEAR;
    props.num = 1;
    props.props = dvb_prop;
    if (ioctl (object->fd_frontend, FE_SET_PROPERTY, &props) < 0) {
      g_warning ("Error resetting tuner: %s", strerror (errno));
    }
    /* First three entries are reserved */
    n = 3;
    switch (object->adapter_type) {
      case FE_QPSK:
        object->tone = SEC_TONE_OFF;
        if (freq > 2200000) {
          // this must be an absolute frequency
          if (freq < SLOF) {
            freq -= LOF1;
          } else {
            freq -= LOF2;
            object->tone = SEC_TONE_ON;
          }
        }

        inversion = INVERSION_AUTO;
        set_prop (dvb_prop, &n, DTV_SYMBOL_RATE, sym_rate);
        set_prop (dvb_prop, &n, DTV_INNER_FEC, object->code_rate_hp);
        /* TODO add dvbs2 */
        del_sys = SYS_DVBS;

        GST_INFO_OBJECT (object,
            "tuning DVB-S to L-Band:%u, Pol:%d, srate=%u, 22kHz=%s",
            freq, object->pol, sym_rate,
            object->tone == SEC_TONE_ON ? "on" : "off");

        if (object->pol == DVB_POL_H)
          voltage = SEC_VOLTAGE_18;
        else
          voltage = SEC_VOLTAGE_13;

        if (object->diseqc_src == -1 || object->send_diseqc == FALSE) {
          set_prop (dvb_prop, &n, DTV_VOLTAGE, voltage);

          // DTV_TONE not yet implemented
          // set_prop (fe_props_array, &n, DTV_TONE, object->tone)
        } else {
          GST_DEBUG_OBJECT (object, "Sending DISEqC");
          diseqc (object->fd_frontend, object->diseqc_src, voltage,
              object->tone);
          /* Once diseqc source is set, do not set it again until
           * app decides to change it */
          //object->send_diseqc = FALSE;
        }

        break;
      case FE_OFDM:
        del_sys = SYS_DVBT;
        bandwidth = 0;
        if (object->bandwidth != BANDWIDTH_AUTO) {
          /* Presumably not specifying bandwidth with s2api is equivalent
           * to BANDWIDTH_AUTO.
           */
          switch (object->bandwidth) {
            case BANDWIDTH_8_MHZ:
              bandwidth = 8000000;
              break;
            case BANDWIDTH_7_MHZ:
              bandwidth = 7000000;
              break;
            case BANDWIDTH_6_MHZ:
              bandwidth = 6000000;
              break;
            default:
              break;
          }
        }
        if (bandwidth) {
          set_prop (dvb_prop, &n, DTV_BANDWIDTH_HZ, bandwidth);
        }
        set_prop (dvb_prop, &n, DTV_CODE_RATE_HP, object->code_rate_hp);
        set_prop (dvb_prop, &n, DTV_CODE_RATE_LP, object->code_rate_lp);
        set_prop (dvb_prop, &n, DTV_MODULATION, object->modulation);
        set_prop (dvb_prop, &n, DTV_TRANSMISSION_MODE,
            object->transmission_mode);
        set_prop (dvb_prop, &n, DTV_GUARD_INTERVAL, object->guard_interval);
        set_prop (dvb_prop, &n, DTV_HIERARCHY, object->hierarchy_information);

        GST_INFO_OBJECT (object, "tuning DVB-T to %d Hz", freq);
        break;
      case FE_QAM:
        GST_INFO_OBJECT (object, "Tuning DVB-C to %d, srate=%d", freq,
            sym_rate);

        del_sys = SYS_DVBC_ANNEX_AC;
        set_prop (dvb_prop, &n, DTV_INNER_FEC, object->code_rate_hp);
        set_prop (dvb_prop, &n, DTV_MODULATION, object->modulation);
        set_prop (dvb_prop, &n, DTV_SYMBOL_RATE, sym_rate);
        break;
      case FE_ATSC:
        GST_INFO_OBJECT (object, "Tuning ATSC to %d", freq);
        del_sys = SYS_ATSC;
        set_prop (dvb_prop, &n, DTV_MODULATION, object->modulation);
        break;
      default:
        g_error ("Unknown frontend type: %d", object->adapter_type);

    }
    g_usleep (100000);
    /* now tune the frontend */
    set_prop (dvb_prop, &n, DTV_TUNE, 0);
    props.num = n;
    props.props = dvb_prop;
    /* set first three entries */
    n = 0;
    set_prop (dvb_prop, &n, DTV_DELIVERY_SYSTEM, del_sys);
    set_prop (dvb_prop, &n, DTV_FREQUENCY, freq);
    set_prop (dvb_prop, &n, DTV_INVERSION, inversion);

    GST_DEBUG_OBJECT (object, "Setting %d properties", props.num);
    if (ioctl (object->fd_frontend, FE_SET_PROPERTY, &props) < 0) {
      g_warning ("Error tuning channel: %s", strerror (errno));
    }
    for (i = 0; i < 50; i++) {
      g_usleep (100000);
      if (ioctl (object->fd_frontend, FE_READ_STATUS, &status) == -1) {
        perror ("FE_READ_STATUS");
        break;
      }
      GST_LOG_OBJECT (object, "status == 0x%02x", status);
      if (status & FE_HAS_LOCK)
        break;
    }
    if (status & FE_HAS_LOCK)
      break;
  }
  if (!(status & FE_HAS_LOCK))
    return FALSE;
  /* set pid filters */
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

    close (*fd);
    if ((*fd = open (demux_dev, O_RDWR)) < 0) {
      g_error ("Error opening demuxer: %s (%s)", strerror (errno), demux_dev);
      g_free (demux_dev);
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
          demux_dev, strerror (errno));
  }

  g_free (demux_dev);
}
