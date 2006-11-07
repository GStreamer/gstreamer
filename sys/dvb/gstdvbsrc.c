/*
 *
 * GStreamer
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdvbsrc.h"
#include <gst/gst.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>
#include <string.h>
#include "_stdint.h"

#define _XOPEN_SOURCE 500
#include <unistd.h>

#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

#include "../../gst-libs/gst/gst-i18n-plugin.h"

GST_DEBUG_CATEGORY_STATIC (gstdvbsrc_debug);
#define GST_CAT_DEFAULT (gstdvbsrc_debug)

#define SLOF (11700*1000UL)
#define LOF1 (9750*1000UL)
#define LOF2 (10600*1000UL)


static GstElementDetails dvbsrc_details = {
  "DVB Source",
  "Source/Video",
  "Digital Video Broadcast Source",
  "P2P-VCR, C-Lab, University of Paderborn\n"
      "Zaheer Abbas Merali <zaheerabbas at merali dot org>"
};

/**
 * SECTION:element-dvbsrc
 *
 * <refsect2>
 * dvbsrc can be used to capture video from DVB cards, DVB-T, DVB-S or DVB-T.
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch dvbsrc modulation="QAM 64" trans-mode=8k bandwidth=8MHz freq=514000000 code-rate-lp=AUTO code-rate-hp=2/3 guard=4  hierarchy=0 ! flutsdemux crc-check=false name=demux ! queue max-size-buffers=0 max-size-time=0 ! flumpeg2vdec ! xvimagesink sync=false demux. ! queue max-size-buffers=0 max-size-time=0 ! flump3dec ! alsasink sync=false
 * </programlisting>
 * This pipeline captures a full transport stream from dvb card 0 that is a DVB-T card at tuned frequency 514000000 with other parameters as seen in the 
 * pipeline and outputs the first tv program on the transport stream.  The reason the sinks have to be set to have sync=false is due to bug #340482.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch dvbsrc modulation="QAM 64" trans-mode=8k bandwidth=8 freq=514000000 code-rate-lp=AUTO code-rate-hp=2/3 guard=4  hierarchy=0 pids=256:257 ! flutsdemux crc-check=false name=demux es-pids=256:257 ! queue max-size-buffers=0 max-size-time=0 ! flumpeg2vdec ! xvimagesink sync=false demux. ! queue max-size-buffers=0 max-size-time=0 ! flump3dec ! alsasink sync=false
 * </programlisting>
 * This pipeline captures a partial transport stream from dvb card 0 that is a DVB-T card for a program at tuned frequency 514000000 and pids of 256:257 with other parameters as seen in the pipeline and outputs the program with the pids 256 and 257.  The reason the sinks have to be set to
 * have sync=false is due to bug #340482.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch dvbsrc polarity="h" freq=11302000 srate=27500 diseqc-src=0 pids=102:103 ! queue max-size-buffers=0 max-size-time=0 ! flumpeg2vdec ! xvimagesink sync=false demux. ! queue max-size-buffers=0 max-size-time=0 ! flump3dec ! alsasink sync=false
 * </programlisting>
 * This pipeline captures a partial transport stream from dvb card 0 that is a DVB-S card for a program at tuned frequency 11302000 Hz, symbol rate of 27500 kHz and pids of 256:257 and outputs the program with the pids 256 and 257.  The reason the sinks have to be set to have sync=false is due to bug #340482.
 * </para>
 * </refsect2>
 */

/* Arguments */
enum
{
  ARG_0,
  ARG_DVBSRC_DEVICE,
  ARG_DVBSRC_DISEQC_SRC,
  ARG_DVBSRC_FREQ,
  ARG_DVBSRC_POL,
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
  ARG_DVBSRC_INVERSION
};

static void gst_dvbsrc_output_frontend_stats (GstDvbSrc * src);

#define GST_TYPE_DVBSRC_CODE_RATE (gst_dvbsrc_code_rate_get_type ())
static GType
gst_dvbsrc_code_rate_get_type (void)
{
  static GType dvbsrc_code_rate_type = 0;
  static GEnumValue code_rate_types[] = {
    {FEC_NONE, "NONE", "NONE"},
    {FEC_1_2, "1/2", "1/2"},
    {FEC_2_3, "2/3", "2/3"},
    {FEC_3_4, "3/4", "3/4"},
    {FEC_4_5, "4/5", "4/5"},
    {FEC_5_6, "5/6", "5/6"},
    {FEC_6_7, "6/7", "6/7"},
    {FEC_7_8, "7/8", "7/8"},
    {FEC_8_9, "8/9", "8/9"},
    {FEC_AUTO, "AUTO", ""},
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
    {QPSK, "QPSK", "QPSK"},
    {QAM_16, "QAM 16", "QAM 16"},
    {QAM_32, "QAM 32", "QAM 32"},
    {QAM_64, "QAM 64", "QAM 64"},
    {QAM_128, "QAM 128", "QAM 128"},
    {QAM_256, "QAM 256", "QAM 256"},
    {QAM_AUTO, "AUTO", "AUTO"},
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
    {TRANSMISSION_MODE_2K, "2k", "2k"},
    {TRANSMISSION_MODE_8K, "8k", "8k"},
    {TRANSMISSION_MODE_AUTO, "AUTO", "AUTO"},
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
    {GUARD_INTERVAL_AUTO, "AUTO", "AUTO"},
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
    {HIERARCHY_NONE, "NONE", "NONE"},
    {HIERARCHY_1, "1", "1"},
    {HIERARCHY_2, "2", "2"},
    {HIERARCHY_4, "4", "4"},
    {HIERARCHY_AUTO, "AUTO", "AUTO"},
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
    {INVERSION_AUTO, "AUTO", "AUTO"},
    {INVERSION_ON, "ON", "ON"},
    {INVERSION_AUTO, "OFF", "OFF"},
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
static gboolean gst_dvbsrc_unlock (GstBaseSrc * bsrc);
static gboolean gst_dvbsrc_is_seekable (GstBaseSrc * bsrc);
static gboolean gst_dvbsrc_get_size (GstBaseSrc * src, guint64 * size);

static gboolean gst_dvbsrc_tune (GstDvbSrc * object);
static void gst_dvbsrc_set_pes_filter (GstDvbSrc * object);
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

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gstdvbsrc_debug, "dvbsrc", 0, "DVB Source Element");

GST_BOILERPLATE_FULL (GstDvbSrc, gst_dvbsrc, GstPushSrc,
    GST_TYPE_PUSH_SRC, _do_init);

static void
gst_dvbsrc_base_init (gpointer gclass)
{
  GstDvbSrcClass *klass = (GstDvbSrcClass *) gclass;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&ts_src_factory));

  gst_element_class_set_details (element_class, &dvbsrc_details);
}


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

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_dvbsrc_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_dvbsrc_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_dvbsrc_unlock);
  gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR (gst_dvbsrc_is_seekable);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_dvbsrc_get_size);

  gstpushsrc_class->create = gst_dvbsrc_create;

  g_object_class_install_property (gobject_class, ARG_DVBSRC_DEVICE,
      g_param_spec_string ("device",
          "device",
          "The device directory", "/dev/dvb/adapter0", G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_FREQ,
      g_param_spec_int ("freq",
          "freq", "Frequency", 0, G_MAXINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_POL,
      g_param_spec_string ("pol",
          "pol", "Polarity [vhHV] (DVB-S)", "h", G_PARAM_READWRITE));


  g_object_class_install_property (gobject_class, ARG_DVBSRC_PIDS,
      g_param_spec_string ("pids",
          "pids",
          "Colon seperated list of pids (eg. 110:120)",
          "8192", G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_SYM_RATE,
      g_param_spec_int ("srate",
          "srate",
          "Symbol Rate (DVB-S, DVB-C)",
          0, G_MAXINT, DEFAULT_SYMBOL_RATE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_TUNE,
      g_param_spec_pointer ("tune",
          "tune", "Atomically tune to channel. (For Apps)", G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_DISEQC_SRC,
      g_param_spec_int ("diseqc_src",
          "diseqc_src",
          "DISEqC selected source (-1 disabled) (DVB-S)",
          -1, 7, DEFAULT_DISEQC_SRC, G_PARAM_READWRITE));

  /* DVB-T, additional properties */

  g_object_class_install_property (gobject_class, ARG_DVBSRC_BANDWIDTH,
      g_param_spec_enum ("bandwidth",
          "bandwidth",
          "Bandwidth (DVB-T)", GST_TYPE_DVBSRC_BANDWIDTH, 1, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_CODE_RATE_HP,
      g_param_spec_enum ("code-rate-hp",
          "code-rate-hp",
          "High Priority Code Rate (DVB-T)",
          GST_TYPE_DVBSRC_CODE_RATE, 1, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_CODE_RATE_LP,
      g_param_spec_enum ("code-rate-lp",
          "code-rate-lp",
          "Low Priority Code Rate (DVB-T)",
          GST_TYPE_DVBSRC_CODE_RATE, 1, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_GUARD,
      g_param_spec_enum ("guard",
          "guard",
          "Guard Interval (DVB-T)",
          GST_TYPE_DVBSRC_GUARD, 1, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_MODULATION,
      g_param_spec_enum ("modulation",
          "modulation",
          "Modulation (DVB-T)",
          GST_TYPE_DVBSRC_MODULATION, 1, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
      ARG_DVBSRC_TRANSMISSION_MODE,
      g_param_spec_enum ("trans-mode",
          "trans-mode",
          "Transmission Mode (DVB-T)",
          GST_TYPE_DVBSRC_TRANSMISSION_MODE, 1, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, ARG_DVBSRC_HIERARCHY_INF,
      g_param_spec_enum ("hierarchy",
          "hierarchy",
          "Hierarchy Information (DVB-T)",
          GST_TYPE_DVBSRC_HIERARCHY, 1, G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, ARG_DVBSRC_INVERSION,
      g_param_spec_enum ("inversion",
          "inversion",
          "Inversion Information (DVB-T)",
          GST_TYPE_DVBSRC_INVERSION, 1, G_PARAM_WRITABLE));

}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_dvbsrc_init (GstDvbSrc * object, GstDvbSrcClass * klass)
{
  int i = 0;

  GST_INFO_OBJECT (object, "gst_dvbsrc_init");

  /* We are a live source */
  gst_base_src_set_live (GST_BASE_SRC (object), TRUE);

  object->fd_frontend = -1;
  object->fd_dvr = -1;

  for (i = 0; i < MAX_FILTERS; i++) {
    object->pids[i] = 0;
    object->fd_filters[i] = -1;
  }
  /* Pid 8192 on DVB gets the whole transport stream */
  object->pids[0] = 8192;

  /* Setting standard devices */
  object->device = g_strdup (DEFAULT_DEVICE);
  object->frontend_dev = g_strconcat (object->device, "/frontend0", NULL);
  object->demux_dev = g_strconcat (object->device, "/demux0", NULL);
  object->dvr_dev = g_strconcat (object->device, "/dvr0", NULL);

  object->sym_rate = DEFAULT_SYMBOL_RATE;
  object->diseqc_src = DEFAULT_DISEQC_SRC;
  object->send_diseqc = FALSE;

  object->tune_mutex = g_mutex_new ();
}


static void
gst_dvbsrc_set_property (GObject * _object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDvbSrc *object;

  g_return_if_fail (GST_IS_DVBSRC (_object));
  object = GST_DVBSRC (_object);

  switch (prop_id) {
    case ARG_DVBSRC_DEVICE:
    {
      char delim_str[] = "/\0";

      if (object->device != NULL)
        g_free (object->device);
      object->device = g_value_dup_string (value);

      if (g_str_has_suffix (object->device, "/"))
        delim_str[0] = '\0';

      object->frontend_dev =
          g_strconcat (object->device, delim_str, "frontend0", NULL);
      object->demux_dev =
          g_strconcat (object->device, delim_str, "demux0", NULL);
      object->dvr_dev = g_strconcat (object->device, delim_str, "dvr0", NULL);
    }
      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_DEVICE");
      break;
    case ARG_DVBSRC_DISEQC_SRC:
      if (object->diseqc_src != g_value_get_int (value)) {
        object->diseqc_src = g_value_get_int (value);
        object->send_diseqc = TRUE;
      }
      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_DISEQC_ID");
      break;
    case ARG_DVBSRC_FREQ:
      object->freq = g_value_get_int (value);
      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_FREQ");
      break;
    case ARG_DVBSRC_POL:
    {
      const char *s = NULL;

      s = g_value_get_string (value);
      if (s != NULL)
        object->pol = (s[0] == 'h' || s[0] == 'H') ? DVB_POL_H : DVB_POL_V;
    }
      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_POL");
      break;
    case ARG_DVBSRC_PIDS:
    {
      int pid = 0;
      int pid_count = 0;
      gchar *pid_string;
      gchar **pids;
      char **tmp;

      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_PIDS");
      pid_string = g_value_dup_string (value);
      tmp = pids = g_strsplit (pid_string, ":", MAX_FILTERS);
      while (*pids != NULL && pid_count < MAX_FILTERS) {
        pid = strtol (*pids, NULL, 0);
        if (pid > 0 && pid <= 8192) {
          GST_INFO_OBJECT (object, "Parsed Pid: %d\n", pid);
          object->pids[pid_count] = pid;
          pid_count++;
        }
        pids++;
      }
      g_strfreev (tmp);
    }
      break;
    case ARG_DVBSRC_SYM_RATE:
      object->sym_rate = g_value_get_int (value);
      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_SYM_RATE to value %d",
          g_value_get_int (value));
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
    case ARG_DVBSRC_TUNE:
      GST_INFO_OBJECT (object, "Set Property: ARG_DVBSRC_TUNE");
      /* if we are in paused/playing state tune now, otherwise in ready to paused state change */
      if (gst_element_get_state
          (GST_ELEMENT (object), NULL, NULL,
              GST_CLOCK_TIME_NONE) > GST_STATE_READY) {
        g_mutex_lock (object->tune_mutex);
        gst_dvbsrc_tune (object);
        g_mutex_unlock (object->tune_mutex);
      }
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
    case ARG_DVBSRC_DEVICE:
      g_value_set_string (value, object->device);
      break;
    case ARG_DVBSRC_FREQ:
      g_value_set_int (value, object->freq);
      break;
    case ARG_DVBSRC_POL:
      if (object->pol == DVB_POL_H)
        g_value_set_string (value, "H");
      else
        g_value_set_string (value, "V");
      break;
    case ARG_DVBSRC_SYM_RATE:
      g_value_set_int (value, object->sym_rate);
      break;
    case ARG_DVBSRC_DISEQC_SRC:
      g_value_set_int (value, object->diseqc_src);
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
gst_dvbsrc_open_frontend (GstDvbSrc * object)
{
  struct dvb_frontend_info fe_info;
  char *adapter_desc = NULL;

  GST_INFO_OBJECT (object, "Using frontend device: %s", object->frontend_dev);
  GST_INFO_OBJECT (object, "Using dvr device:  %s", object->dvr_dev);

  /* open frontend */
  if ((object->fd_frontend = open (object->frontend_dev, O_RDWR)) < 0) {
    switch (errno) {
      case ENOENT:
        GST_ELEMENT_ERROR (object, RESOURCE, NOT_FOUND,
            (_("Device \"%s\" does not exist."), object->frontend_dev), (NULL));
        break;
      default:
        GST_ELEMENT_ERROR (object, RESOURCE, OPEN_READ_WRITE,
            (_("Could not open frontend device \"%s\"."), object->frontend_dev),
            GST_ERROR_SYSTEM);
        break;
    }

    close (object->fd_dvr);

    return FALSE;
  }

  if (ioctl (object->fd_frontend, FE_GET_INFO, &fe_info) < 0) {
    GST_ELEMENT_ERROR (object, RESOURCE, SETTINGS,
        (_("Could not get settings from frontend device \"%s\"."),
            object->frontend_dev), GST_ERROR_SYSTEM);

    close (object->fd_dvr);
    close (object->fd_frontend);

    return FALSE;
  }

  object->adapter_type = fe_info.type;
  switch (object->adapter_type) {
    case FE_QPSK:
      adapter_desc = "DVB-S";
      break;
    case FE_QAM:
      adapter_desc = "DVB-C";
      break;
    case FE_OFDM:
      adapter_desc = "DVB-T";
      break;
    default:
      g_error ("Unknown frontend type: %d", object->adapter_type);
  }

  /*g_signal_emit (G_OBJECT (object), gst_dvbsrc_signals[ADAPTER_TYPE_SIGNAL],
     0, object->adapter_type); */

  GST_INFO_OBJECT (object, "DVB card: %s ", fe_info.name);
  return TRUE;
}

static gboolean
gst_dvbsrc_open_dvr (GstDvbSrc * object)
{
  /* open DVR */
  if ((object->fd_dvr = open (object->dvr_dev, O_RDONLY | O_NONBLOCK)) < 0) {
    switch (errno) {
      case ENOENT:
        GST_ELEMENT_ERROR (object, RESOURCE, NOT_FOUND,
            (_("Device \"%s\" does not exist."), object->dvr_dev), (NULL));
        break;
      default:
        GST_ELEMENT_ERROR (object, RESOURCE, OPEN_READ,
            (_("Could not open file \"%s\" for reading."), object->dvr_dev),
            GST_ERROR_SYSTEM);
        break;
    }
    return FALSE;
  }
  GST_INFO_OBJECT (object, "Setting buffer size");
  if (ioctl (object->fd_dvr, DMX_SET_BUFFER_SIZE, 1024 * 1024) < 0) {
    GST_INFO_OBJECT (object, "DMX_SET_BUFFER_SIZE failed");
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

  g_free (object->frontend_dev);
  g_free (object->demux_dev);

  /* freeing the mutex segfaults somehow */
  g_mutex_free (object->tune_mutex);
}


/*
 ******************************
 *                            *
 *      Plugin Realisation    *
 *                            *
 ******************************
 */



/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "dvbsrc", GST_RANK_NONE,
      GST_TYPE_DVBSRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dvbsrc",
    "DVB Source", plugin_init, VERSION, "LGPL", "", "University of Paderborn");


static GstBuffer *
read_device (int fd, const char *fd_name, int size)
{
  int count = 0;
  struct pollfd pfd[1];
  int ret_val = 0;
  guint attempts = 0;
  const int TIMEOUT = 100;

  GstBuffer *buf = gst_buffer_new_and_alloc (size);

  g_return_val_if_fail (GST_IS_BUFFER (buf), NULL);

  if (fd < 0) {
    return NULL;
  }

  pfd[0].fd = fd;
  pfd[0].events = POLLIN;

  while (count < size) {
    ret_val = poll (pfd, 1, TIMEOUT);
    if (ret_val > 0) {
      if (pfd[0].revents & POLLIN) {
        int tmp = 0;

        tmp = read (fd, GST_BUFFER_DATA (buf) + count, size - count);
        if (tmp < 0) {
          GST_WARNING ("Unable to read from device: %s (%d)", fd_name, errno);
          attempts += 1;
          if (attempts % 10 == 0) {
            GST_WARNING
                ("Unable to read from device after %u attempts: %s",
                attempts, fd_name);
            gst_dvbsrc_output_frontend_stats (src);
          }

        } else
          count = count + tmp;
      } else {
        fprintf (stderr, "revents = %d\n", pfd[0].revents);
      }
    } else if (ret_val == 0) {  // poll timeout
      attempts += 1;
      GST_INFO ("Reading from device %s timedout (%d)", fd_name, attempts);

      if (attempts % 10 == 0) {
        GST_WARNING ("Unable to read after %u attempts from device: %s (%d)",
            attempts, fd_name, errno);
        gst_dvbsrc_output_frontend_stats (src);
      }
    } else if (errno == -EINTR) {       // poll interrupted
      ;
    }

  }

  GST_BUFFER_SIZE (buf) = count;
  GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
  return buf;
}

static GstFlowReturn
gst_dvbsrc_create (GstPushSrc * element, GstBuffer ** buf)
{
  static int quality_signal_rate = 0;
  gint buffer_size;
  GstFlowReturn retval = GST_FLOW_ERROR;

  GstDvbSrc *object = NULL;

  g_return_val_if_fail (GST_IS_DVBSRC (element), GST_FLOW_ERROR);
  object = GST_DVBSRC (element);
  GST_LOG ("buf: 0x%x fd_dvr: %d", buf, object->fd_dvr);

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);
  //g_object_get(G_OBJECT(object), "blocksize", &buffer_size, NULL);
  buffer_size = DEFAULT_BUFFER_SIZE;

  /* device can not be tuned during read */
  g_mutex_lock (object->tune_mutex);


  if (object->fd_dvr > -1) {
    /* --- Read TS from DVR device --- */
    GST_DEBUG_OBJECT (object, "Reading from DVR device");
    *buf = read_device (object->fd_dvr, object->dvr_dev, buffer_size);
    if (*buf != NULL) {
      GstCaps *caps;

      retval = GST_FLOW_OK;

      caps = gst_pad_get_caps (GST_BASE_SRC_PAD (object));
      gst_buffer_set_caps (*buf, caps);
      gst_caps_unref (caps);

      /* Every now and then signal signal quality */
      if (quality_signal_rate == 100) {
        gst_dvbsrc_output_frontend_stats (object);
        quality_signal_rate = 0;
      } else {
        quality_signal_rate++;
      }
    } else {
      GST_DEBUG_OBJECT (object, "Failed to read from device");
    }
  }

  g_mutex_unlock (object->tune_mutex);
  return retval;

}

static gboolean
gst_dvbsrc_start (GstBaseSrc * bsrc)
{
  GstDvbSrc *src = GST_DVBSRC (bsrc);

  gst_dvbsrc_open_frontend (src);
  gst_dvbsrc_tune (src);
  if (!gst_dvbsrc_frontend_status (src)) {
    return FALSE;
  }
  if (!gst_dvbsrc_open_dvr (src)) {
    GST_ERROR_OBJECT (src, "Not able to open dvr_device");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_dvbsrc_stop (GstBaseSrc * bsrc)
{
  GstDvbSrc *src = GST_DVBSRC (bsrc);

  gst_dvbsrc_close_devices (src);
  return TRUE;
}

static gboolean
gst_dvbsrc_unlock (GstBaseSrc * bsrc)
{
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

  GST_INFO_OBJECT (object, "gst_dvbsrc_frontend_status\n");

  if (object->fd_frontend < 0) {
    GST_ERROR_OBJECT (object,
        "Trying to get frontend status from not opened device!");
    return FALSE;
  } else
    GST_INFO_OBJECT (object, "fd-frontend: %d", object->fd_frontend);

  for (i = 0; i < 15; i++) {
    usleep (1000000);
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
        "Not able to lock to the signal on the given frequency.\n");
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
  if (ioctl (fd, FE_SET_TONE, SEC_TONE_OFF) == -1)
    perror ("FE_SET_TONE failed");

  if (ioctl (fd, FE_SET_VOLTAGE, v) == -1)
    perror ("FE_SET_VOLTAGE failed");

  usleep (15 * 1000);

  if (ioctl (fd, FE_DISEQC_SEND_MASTER_CMD, &cmd->cmd) == -1)
    perror ("FE_DISEQC_SEND_MASTER_CMD failed");

  usleep (cmd->wait * 1000);
  usleep (15 * 1000);

  if (ioctl (fd, FE_DISEQC_SEND_BURST, b) == -1)
    perror ("FE_DISEQC_SEND_BURST failed");

  usleep (15 * 1000);

  if (ioctl (fd, FE_SET_TONE, t) == -1)
    perror ("FE_SET_TONE failed");
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

  diseqc_send_msg (secfd, voltage, &cmd, tone,
      (sat_no / 4) % 2 ? SEC_MINI_B : SEC_MINI_A);

}


static gboolean
gst_dvbsrc_tune (GstDvbSrc * object)
{
  struct dvb_frontend_parameters feparams;
  fe_sec_voltage_t voltage;

  unsigned int freq = object->freq;
  unsigned int sym_rate = object->sym_rate * 1000;

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

  gst_dvbsrc_unset_pes_filters (object);

  switch (object->adapter_type) {
    case FE_QPSK:

      object->tone = SEC_TONE_OFF;
      if (freq > 2200000) {
        // this must be an absolute frequency
        if (freq < SLOF) {
          feparams.frequency = (freq - LOF1);
          if (object->tone < 0)
            object->tone = SEC_TONE_OFF;
        } else {
          feparams.frequency = (freq - LOF2);
          if (object->tone < 0)
            object->tone = SEC_TONE_ON;
        }
      } else {
        // this is an L-Band frequency
        feparams.frequency = freq;
        object->tone = SEC_TONE_OFF;
      }
      GST_INFO_OBJECT (object,
          "tuning DVB-S to L-Band:%u, Pol:%d, srate=%u, 22kHz=%s",
          feparams.frequency, object->pol, sym_rate,
          object->tone == SEC_TONE_ON ? "on" : "off");

      feparams.inversion = INVERSION_AUTO;
      feparams.u.qpsk.symbol_rate = sym_rate;
      feparams.u.qpsk.fec_inner = FEC_AUTO;

      if (object->pol == DVB_POL_H)
        voltage = SEC_VOLTAGE_18;
      else
        voltage = SEC_VOLTAGE_13;

      if (object->diseqc_src == -1 || object->send_diseqc == FALSE) {
        if (ioctl (object->fd_frontend, FE_SET_VOLTAGE, voltage) < 0) {
          g_warning ("Unable to set voltage on dvb frontend device");
        }

        if (ioctl (object->fd_frontend, FE_SET_TONE, object->tone) < 0) {
          g_warning ("Error setting tone: %s", strerror (errno));
        }
      } else {
        GST_DEBUG_OBJECT (object, "Sending DISEqC");
        diseqc (object->fd_frontend, object->diseqc_src, voltage, object->tone);
        /* Once diseqc source is set, do not set it again until
         * app decides to change it */
        object->send_diseqc = FALSE;
      }

      break;
    case FE_OFDM:
      feparams.frequency = freq;
      feparams.u.ofdm.bandwidth = object->bandwidth;
      feparams.u.ofdm.code_rate_HP = object->code_rate_hp;
      feparams.u.ofdm.code_rate_LP = object->code_rate_lp;
      feparams.u.ofdm.constellation = object->modulation;
      feparams.u.ofdm.transmission_mode = object->transmission_mode;
      feparams.u.ofdm.guard_interval = object->guard_interval;
      feparams.u.ofdm.hierarchy_information = object->hierarchy_information;
      feparams.inversion = object->inversion;

      GST_INFO_OBJECT (object, "tuning DVB-T to %d Hz\n", freq);
      break;
    case FE_QAM:
      GST_INFO_OBJECT (object, "Tuning DVB-C to %d, srate=%d", freq, sym_rate);
      feparams.frequency = freq;
      feparams.inversion = INVERSION_OFF;
      feparams.u.qam.fec_inner = FEC_AUTO;
      feparams.u.qam.modulation = object->modulation;
      feparams.u.qam.symbol_rate = sym_rate;
      break;
    default:
      g_error ("Unknown frontend type: %d", object->adapter_type);

  }
  usleep (100000);

  /* now tune the frontend */
  if (ioctl (object->fd_frontend, FE_SET_FRONTEND, &feparams) < 0) {
    g_warning ("Error tuning channel: %s", strerror (errno));
  }

  /* set pid filters */
  gst_dvbsrc_set_pes_filter (object);

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
gst_dvbsrc_set_pes_filter (GstDvbSrc * object)
{
  int *fd;
  int pid, i;
  struct dmx_pes_filter_params pes_filter;

  GST_INFO_OBJECT (object, "Setting PES filter");

  for (i = 0; i < MAX_FILTERS; i++) {
    if (object->pids[i] == 0)
      break;

    fd = &object->fd_filters[i];
    pid = object->pids[i];

    close (*fd);
    if ((*fd = open (object->demux_dev, O_RDWR)) < 0)
      g_error ("Error opening demuxer: %s (%s)", strerror (errno),
          object->demux_dev);

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
          object->demux_dev, strerror (errno));
  }
  /* always have PAT in the filter if we haven't used all our filter slots */
  if (object->pids[0] != 8192 && i < MAX_FILTERS) {
    /* pid 8192 means get whole ts */
    pes_filter.pid = 0;
    pes_filter.input = DMX_IN_FRONTEND;
    pes_filter.output = DMX_OUT_TS_TAP;
    pes_filter.pes_type = DMX_PES_OTHER;
    pes_filter.flags = DMX_IMMEDIATE_START;

    fd = &object->fd_filters[i];
    close (*fd);
    if ((*fd = open (object->demux_dev, O_RDWR)) < 0) {
      GST_WARNING_OBJECT ("Error opening demuxer: %s (%s)",
          strerror (errno), object->demux_dev);
    } else {
      GST_INFO_OBJECT (object, "Setting pes-filter, pid = %d, type = %d",
          pes_filter.pid, pes_filter.pes_type);

      if (ioctl (*fd, DMX_SET_PES_FILTER, &pes_filter) < 0)
        GST_WARNING_OBJECT (object, "Error setting PES filter on %s: %s",
            object->demux_dev, strerror (errno));
    }
  }


}
