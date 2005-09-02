/* -*- Mode: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2000> Daniel Fischer <dan@f3c.com>
 *               <2004> Wim Taymans <wim@fluendo.com>
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
#include <gst/gst.h>
#include <string.h>
#include <libavc1394/avc1394.h>
#include <libavc1394/avc1394_vcr.h>
#include <libavc1394/rom1394.h>

#include "gstdv1394src.h"

GST_DEBUG_CATEGORY_STATIC (dv1394src_debug);
#define GST_CAT_DEFAULT (dv1394src_debug)

#define PAL_FRAMESIZE 144000
#define PAL_FRAMERATE 25

#define NTSC_FRAMESIZE 120000
#define NTSC_FRAMERATE 30

/* Filter signals and args */
enum
{
  SIGNAL_FRAME_DROPPED,
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_PORT		-1
#define DEFAULT_CHANNEL		63
#define DEFAULT_CONSECUTIVE	1
#define DEFAULT_SKIP		0
#define DEFAULT_DROP_INCOMPLETE	TRUE
#define DEFAULT_USE_AVC		TRUE
#define DEFAULT_GUID		0

enum
{
  ARG_0,
  ARG_PORT,
  ARG_CHANNEL,
  ARG_CONSECUTIVE,
  ARG_SKIP,
  ARG_DROP_INCOMPLETE,
  ARG_USE_AVC,
  ARG_GUID
};

static GstElementDetails gst_dv1394src_details =
GST_ELEMENT_DETAILS ("Firewire (1394) DV Source",
    "Source/Video",
    "Source for DV video data from firewire port",
    "Erik Walthinsen <omega@temple-baptist.com>\n"
    "Daniel Fischer <dan@f3c.com>\n" "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dv, "
        "format = (string) { NTSC, PAL }, " "systemstream = (boolean) true")
    );

static void gst_dv1394src_base_init (gpointer g_class);
static void gst_dv1394src_class_init (GstDV1394SrcClass * klass);
static void gst_dv1394src_init (GstDV1394Src * filter);
static void gst_dv1394src_dispose (GObject * obj);

static void gst_dv1394src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dv1394src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_dv1394src_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_dv1394src_create (GstPushSrc * psrc, GstBuffer ** buf);

#if 0
static const GstEventMask *gst_dv1394src_get_event_mask (GstPad * pad);
#endif
static gboolean gst_dv1394src_event (GstPad * pad, GstEvent * event);

#if 0
static const GstFormat *gst_dv1394src_get_formats (GstPad * pad);
#endif
static gboolean gst_dv1394src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);

static const GstQueryType *gst_dv1394src_get_query_types (GstPad * pad);
static gboolean gst_dv1394src_query (GstPad * pad, GstQuery * query);

static GstElementClass *parent_class = NULL;

static void gst_dv1394src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static guint gst_dv1394src_signals[LAST_SIGNAL] = { 0 };

GType
gst_dv1394src_get_type (void)
{
  static GType gst_dv1394src_type = 0;

  if (!gst_dv1394src_type) {
    static const GTypeInfo gst_dv1394src_info = {
      sizeof (GstDV1394Src),
      gst_dv1394src_base_init,
      NULL,
      (GClassInitFunc) gst_dv1394src_class_init,
      NULL,
      NULL,
      sizeof (GstDV1394Src),
      0,
      (GInstanceInitFunc) gst_dv1394src_init,
    };
    static const GInterfaceInfo urihandler_info = {
      gst_dv1394src_uri_handler_init,
      NULL,
      NULL,
    };

    gst_dv1394src_type =
        g_type_register_static (GST_TYPE_PUSH_SRC, "DV1394Src",
        &gst_dv1394src_info, 0);

    g_type_add_interface_static (gst_dv1394src_type, GST_TYPE_URI_HANDLER,
        &urihandler_info);

    GST_DEBUG_CATEGORY_INIT (dv1394src_debug, "dv1394src", 0,
        "DV firewire source");
  }
  return gst_dv1394src_type;
}

static void
gst_dv1394src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));

  gst_element_class_set_details (element_class, &gst_dv1394src_details);
}

static void
gst_dv1394src_class_init (GstDV1394SrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_PUSH_SRC);

  gobject_class->set_property = gst_dv1394src_set_property;
  gobject_class->get_property = gst_dv1394src_get_property;
  gobject_class->dispose = gst_dv1394src_dispose;

  gst_dv1394src_signals[SIGNAL_FRAME_DROPPED] =
      g_signal_new ("frame-dropped", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDV1394SrcClass, frame_dropped),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PORT,
      g_param_spec_int ("port", "Port", "Port number (-1 automatic)",
          -1, 16, DEFAULT_PORT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CHANNEL,
      g_param_spec_int ("channel", "Channel", "Channel number for listening",
          0, 64, DEFAULT_CHANNEL, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CONSECUTIVE,
      g_param_spec_int ("consecutive", "consecutive frames",
          "send n consecutive frames after skipping", 1, G_MAXINT,
          DEFAULT_CONSECUTIVE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SKIP,
      g_param_spec_int ("skip", "skip frames", "skip n frames",
          0, G_MAXINT, DEFAULT_SKIP, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DROP_INCOMPLETE,
      g_param_spec_boolean ("drop_incomplete", "drop_incomplete",
          "drop incomplete frames", DEFAULT_DROP_INCOMPLETE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_USE_AVC,
      g_param_spec_boolean ("use-avc", "Use AV/C", "Use AV/C VTR control",
          DEFAULT_USE_AVC, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_GUID,
      g_param_spec_uint64 ("guid", "GUID",
          "select one of multiple DV devices by its GUID. use a hexadecimal "
          "like 0xhhhhhhhhhhhhhhhh. (0 = no guid)", 0, G_MAXUINT64,
          DEFAULT_GUID, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_dv1394src_change_state;

  gstbasesrc_class->negotiate = NULL;
  gstpushsrc_class->create = gst_dv1394src_create;
}

static void
gst_dv1394src_init (GstDV1394Src * dv1394src)
{
  GstPad *srcpad = GST_BASE_SRC_PAD (dv1394src);

  gst_base_src_set_live (GST_BASE_SRC (dv1394src), TRUE);
  gst_pad_use_fixed_caps (srcpad);

  gst_pad_set_event_function (srcpad, gst_dv1394src_event);
  gst_pad_set_query_function (srcpad, gst_dv1394src_query);
  gst_pad_set_query_type_function (srcpad, gst_dv1394src_get_query_types);

#if 0
  gst_pad_set_event_mask_function (dv1394src->srcpad,
      gst_dv1394src_get_event_mask);
  gst_pad_set_convert_function (dv1394src->srcpad, gst_dv1394src_convert);
  gst_pad_set_formats_function (dv1394src->srcpad, gst_dv1394src_get_formats);
#endif

  dv1394src->dv_lock = g_mutex_new ();
  dv1394src->port = DEFAULT_PORT;
  dv1394src->channel = DEFAULT_CHANNEL;

  dv1394src->consecutive = DEFAULT_CONSECUTIVE;
  dv1394src->skip = DEFAULT_SKIP;
  dv1394src->drop_incomplete = DEFAULT_DROP_INCOMPLETE;
  dv1394src->use_avc = DEFAULT_USE_AVC;
  dv1394src->guid = DEFAULT_GUID;

  /* initialized when first header received */
  dv1394src->frame_size = 0;

  dv1394src->buf = NULL;
  dv1394src->frame = NULL;
  dv1394src->frame_sequence = 0;
}

static void
gst_dv1394src_dispose (GObject * obj)
{
  GstDV1394Src *dv1394src;

  dv1394src = GST_DV1394SRC (obj);

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_dv1394src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDV1394Src *filter = GST_DV1394SRC (object);

  switch (prop_id) {
    case ARG_PORT:
      filter->port = g_value_get_int (value);
      break;
    case ARG_CHANNEL:
      filter->channel = g_value_get_int (value);
      break;
    case ARG_SKIP:
      filter->skip = g_value_get_int (value);
      break;
    case ARG_CONSECUTIVE:
      filter->consecutive = g_value_get_int (value);
      break;
    case ARG_DROP_INCOMPLETE:
      filter->drop_incomplete = g_value_get_boolean (value);
      break;
    case ARG_USE_AVC:
      filter->use_avc = g_value_get_boolean (value);
      break;
    case ARG_GUID:
      filter->guid = g_value_get_uint64 (value);
      break;
    default:
      break;
  }
}

static void
gst_dv1394src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDV1394Src *filter = GST_DV1394SRC (object);

  switch (prop_id) {
    case ARG_PORT:
      g_value_set_int (value, filter->port);
      break;
    case ARG_CHANNEL:
      g_value_set_int (value, filter->channel);
      break;
    case ARG_SKIP:
      g_value_set_int (value, filter->skip);
      break;
    case ARG_CONSECUTIVE:
      g_value_set_int (value, filter->consecutive);
      break;
    case ARG_DROP_INCOMPLETE:
      g_value_set_boolean (value, filter->drop_incomplete);
      break;
    case ARG_USE_AVC:
      g_value_set_boolean (value, filter->use_avc);
      break;
    case ARG_GUID:
      g_value_set_uint64 (value, filter->guid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static int
gst_dv1394src_iso_receive (raw1394handle_t handle, int channel, size_t len,
    quadlet_t * data)
{
  GstDV1394Src *dv1394src = GST_DV1394SRC (raw1394_get_userdata (handle));

  if (len > 16) {
    /*
       the following code taken from kino-0.51 (Dan Dennedy/Charles Yates)
       Kindly relicensed under the LGPL. See the commit log for version 1.6 of
       this file in CVS.
     */
    unsigned char *p = (unsigned char *) &data[3];
    int section_type = p[0] >> 5;       /* section type is in bits 5 - 7 */
    int dif_sequence = p[1] >> 4;       /* dif sequence number is in bits 4 - 7 */
    int dif_block = p[2];

    /* if we are at the beginning of a frame, 
       we set buf=frame, and alloc a new buffer for frame
     */

    if (section_type == 0 && dif_sequence == 0) {       // dif header
      if (!dv1394src->negotiated) {
        GstCaps *caps;

        // figure format (NTSC/PAL)
        if (p[3] & 0x80) {
          // PAL
          dv1394src->frame_size = PAL_FRAMESIZE;
          dv1394src->frame_rate = PAL_FRAMERATE;
          GST_DEBUG ("PAL data");
          caps = gst_caps_new_simple ("video/x-dv",
              "format", G_TYPE_STRING, "PAL",
              "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
        } else {
          // NTSC (untested)
          dv1394src->frame_size = NTSC_FRAMESIZE;
          dv1394src->frame_rate = NTSC_FRAMERATE;
          GST_DEBUG
              ("NTSC data [untested] - please report success/failure to <dan@f3c.com>");
          caps = gst_caps_new_simple ("video/x-dv",
              "format", G_TYPE_STRING, "NTSC",
              "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
        }
        gst_pad_set_caps (GST_BASE_SRC_PAD (dv1394src), caps);
        gst_caps_unref (caps);
        dv1394src->negotiated = TRUE;
      }
      // drop last frame when not complete
      if (!dv1394src->drop_incomplete
          || dv1394src->bytes_in_frame == dv1394src->frame_size) {
        dv1394src->buf = dv1394src->frame;
      } else {
        GST_INFO_OBJECT (GST_ELEMENT (dv1394src), "incomplete frame dropped");
        g_signal_emit (G_OBJECT (dv1394src),
            gst_dv1394src_signals[SIGNAL_FRAME_DROPPED], 0);
        if (dv1394src->frame) {
          gst_buffer_unref (dv1394src->frame);
        }
      }
      dv1394src->frame = NULL;
      if ((dv1394src->frame_sequence + 1) % (dv1394src->skip +
              dv1394src->consecutive) < dv1394src->consecutive) {
        //GstFormat format;
        GstBuffer *buf;

        buf = gst_buffer_new_and_alloc (dv1394src->frame_size);
#if 0
        /* fill in default offset */
        format = GST_FORMAT_DEFAULT;
        gst_dv1394src_query (dv1394src->srcpad, GST_QUERY_POSITION, &format,
            &GST_BUFFER_OFFSET (buf));
        /* fill in timestamp */
        format = GST_FORMAT_TIME;
        gst_dv1394src_query (dv1394src->srcpad, GST_QUERY_POSITION, &format,
            &GST_BUFFER_TIMESTAMP (buf));
        /* fill in duration by converting one frame to time */
        gst_dv1394src_convert (dv1394src->srcpad, GST_FORMAT_DEFAULT, 1,
            &format, &GST_BUFFER_DURATION (buf));
#endif

        dv1394src->frame = buf;
      }
      dv1394src->frame_sequence++;
      dv1394src->bytes_in_frame = 0;
    }

    if (dv1394src->frame != NULL) {
      guint8 *data = GST_BUFFER_DATA (dv1394src->frame);

      switch (section_type) {
        case 0:                /* 1 Header block */
          /* p[3] |= 0x80; // hack to force PAL data */
          memcpy (data + dif_sequence * 150 * 80, p, 480);
          break;

        case 1:                /* 2 Subcode blocks */
          memcpy (data + dif_sequence * 150 * 80 + (1 + dif_block) * 80, p,
              480);
          break;

        case 2:                /* 3 VAUX blocks */
          memcpy (data + dif_sequence * 150 * 80 + (3 + dif_block) * 80, p,
              480);
          break;

        case 3:                /* 9 Audio blocks interleaved with video */
          memcpy (data + dif_sequence * 150 * 80 + (6 + dif_block * 16) * 80, p,
              480);
          break;

        case 4:                /* 135 Video blocks interleaved with audio */
          memcpy (data + dif_sequence * 150 * 80 + (7 + (dif_block / 15) +
                  dif_block) * 80, p, 480);
          break;

        default:               /* we can't handle any other data */
          break;
      }
      dv1394src->bytes_in_frame += 480;
    }
  }

  return 0;
}

static int
gst_dv1394src_bus_reset (raw1394handle_t handle, unsigned int generation)
{
  GST_INFO_OBJECT (GST_DV1394SRC (raw1394_get_userdata (handle)),
      "have bus reset");
  return 0;
}

static GstFlowReturn
gst_dv1394src_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstDV1394Src *dv1394src = GST_DV1394SRC (psrc);
  GstCaps *caps;

  GST_DV_LOCK (dv1394src);
  dv1394src->buf = NULL;
  while (dv1394src->buf == NULL)
    raw1394_loop_iterate (dv1394src->handle);

  caps = gst_pad_get_caps (GST_BASE_SRC_PAD (psrc));
  gst_buffer_set_caps (dv1394src->buf, caps);
  gst_caps_unref (caps);

  *buf = dv1394src->buf;
  GST_DV_UNLOCK (dv1394src);

  return GST_FLOW_OK;
}

static int
gst_dv1394src_discover_avc_node (GstDV1394Src * src)
{
  int node = -1;
  int i, j = 0;
  int m = src->num_ports;

  if (src->port >= 0) {
    /* search on explicit port */
    j = src->port;
    m = j + 1;
  }

  /* loop over all our ports */
  for (; j < m && node == -1; j++) {
    raw1394handle_t handle;
    gint n_ports;
    struct raw1394_portinfo pinf[16];

    /* open the port */
    handle = raw1394_new_handle ();
    if (!handle) {
      g_warning ("raw1394 - failed to get handle: %s.\n", strerror (errno));
      continue;
    }
    if ((n_ports = raw1394_get_port_info (handle, pinf, 16)) < 0) {
      g_warning ("raw1394 - failed to get port info: %s.\n", strerror (errno));
      goto next;
    }

    /* tell raw1394 which host adapter to use */
    if (raw1394_set_port (handle, j) < 0) {
      g_warning ("raw1394 - failed to set set port: %s.\n", strerror (errno));
      goto next;
    }

    /* now loop over all the nodes */
    for (i = 0; i < raw1394_get_nodecount (handle); i++) {
      /* are we looking for an explicit GUID */
      if (src->guid != 0) {
        if (src->guid == rom1394_get_guid (handle, i)) {
          node = i;
          src->port = j;
          break;
        }
      } else {
        rom1394_directory rom_dir;

        /* select first AV/C Tape Reccorder Player node */
        if (rom1394_get_directory (handle, i, &rom_dir) < 0) {
          g_warning ("error reading config rom directory for node %d\n", i);
          continue;
        }
        if ((rom1394_get_node_type (&rom_dir) == ROM1394_NODE_TYPE_AVC) &&
            avc1394_check_subunit_type (handle, i, AVC1394_SUBUNIT_TYPE_VCR)) {
          node = i;
          src->port = j;
          break;
        }
      }
    }
  next:
    raw1394_destroy_handle (handle);
  }
  return node;
}

static GstStateChangeReturn
gst_dv1394src_change_state (GstElement * element, GstStateChange transition)
{
  GstDV1394Src *dv1394src;
  GstStateChangeReturn ret;

  dv1394src = GST_DV1394SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* create a handle */
      if ((dv1394src->handle = raw1394_new_handle ()) == NULL)
        goto no_handle;

      /* set this plugin as the user data */
      raw1394_set_userdata (dv1394src->handle, dv1394src);

      /* get number of ports */
      if ((dv1394src->num_ports =
              raw1394_get_port_info (dv1394src->handle, dv1394src->pinfo,
                  16)) == 0)
        goto no_ports;

      if (dv1394src->use_avc || dv1394src->port == -1) {
        /* discover AVC and optionally the port */
        dv1394src->avc_node = gst_dv1394src_discover_avc_node (dv1394src);
      }

      /* configure our port now */
      if (raw1394_set_port (dv1394src->handle, dv1394src->port) < 0)
        goto cannot_set_port;

      /* set the callbacks */
      raw1394_set_iso_handler (dv1394src->handle, dv1394src->channel,
          gst_dv1394src_iso_receive);
      raw1394_set_bus_reset_handler (dv1394src->handle,
          gst_dv1394src_bus_reset);

      GST_DEBUG_OBJECT (dv1394src, "successfully opened up 1394 connection");
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (raw1394_start_iso_rcv (dv1394src->handle, dv1394src->channel) < 0)
        goto cannot_start;

      if (dv1394src->use_avc) {
        /* start the VCR */
        if (!avc1394_vcr_is_recording (dv1394src->handle, dv1394src->avc_node)
            && avc1394_vcr_is_playing (dv1394src->handle, dv1394src->avc_node)
            != AVC1394_VCR_OPERAND_PLAY_FORWARD) {
          avc1394_vcr_play (dv1394src->handle, dv1394src->avc_node);
        }
      }
      break;
    default:
      break;
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* we need to lock here as the _create function has to be completed.
       * The base source will not call the _create() function again. */
      GST_DV_LOCK (dv1394src);
      raw1394_stop_iso_rcv (dv1394src->handle, dv1394src->channel);
      if (dv1394src->use_avc) {
        /* pause the VCR */
        if (!avc1394_vcr_is_recording (dv1394src->handle, dv1394src->avc_node)
            && (avc1394_vcr_is_playing (dv1394src->handle, dv1394src->avc_node)
                != AVC1394_VCR_OPERAND_PLAY_FORWARD_PAUSE)) {
          avc1394_vcr_pause (dv1394src->handle, dv1394src->avc_node);
        }
      }
      GST_DV_UNLOCK (dv1394src);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      dv1394src->negotiated = FALSE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (dv1394src->use_avc) {
        /* stop the VCR */
        avc1394_vcr_stop (dv1394src->handle, dv1394src->avc_node);
      }
      raw1394_destroy_handle (dv1394src->handle);
      break;
    default:
      break;
  }

  return ret;

no_handle:
  {
    GST_ELEMENT_ERROR (dv1394src, RESOURCE, NOT_FOUND, (NULL),
        ("can't get raw1394 handle"));
    return GST_STATE_CHANGE_FAILURE;
  }
no_ports:
  {
    GST_ELEMENT_ERROR (dv1394src, RESOURCE, NOT_FOUND, (NULL),
        ("no ports available for raw1394"));
    return GST_STATE_CHANGE_FAILURE;
  }
cannot_set_port:
  {
    GST_ELEMENT_ERROR (dv1394src, RESOURCE, SETTINGS, (NULL),
        ("can't set 1394 port %d", dv1394src->port));
    return GST_STATE_CHANGE_FAILURE;
  }
cannot_start:
  {
    GST_ELEMENT_ERROR (dv1394src, RESOURCE, READ, (NULL),
        ("can't start 1394 iso receive"));
    return GST_STATE_CHANGE_FAILURE;
  }
}


#if 0
static const GstEventMask *
gst_dv1394src_get_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {0,}
  };

  return masks;
}
#endif

static gboolean
gst_dv1394src_event (GstPad * pad, GstEvent * event)
{
  return FALSE;
}

#if 0
static const GstFormat *
gst_dv1394src_get_formats (GstPad * pad)
{
  static GstFormat formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,
    0
  };

  return formats;
}
#endif

static gboolean
gst_dv1394src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  GstDV1394Src *src;

  src = GST_DV1394SRC (gst_pad_get_parent (pad));

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          src_value *= src->frame_size;
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * src->frame_rate / GST_SECOND;
          break;
        default:
          goto not_supported;
      }
      break;
    case GST_FORMAT_BYTES:
      src_value /= src->frame_size;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * src->frame_size;
          break;
        case GST_FORMAT_TIME:
          if (src->frame_rate != 0)
            *dest_value = src_value * GST_SECOND / src->frame_rate;
          else
            goto not_supported;
          break;
        default:
          goto not_supported;
      }
      break;
    default:
      goto not_supported;
  }

  gst_object_unref (src);
  return TRUE;

not_supported:
  {
    gst_object_unref (src);
    return FALSE;
  }
}

static const GstQueryType *
gst_dv1394src_get_query_types (GstPad * pad)
{
  static const GstQueryType src_query_types[] = {
    GST_QUERY_CONVERT,
    GST_QUERY_POSITION,
    0
  };

  return src_query_types;
}

static gboolean
gst_dv1394src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstDV1394Src *src;

  src = GST_DV1394SRC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 current;

      gst_query_parse_position (query, &format, NULL, NULL);

      /* bring our current frame to the requested format */
      res = gst_pad_query_convert (pad,
          GST_FORMAT_DEFAULT, src->frame_sequence, &format, &current);

      gst_query_set_position (query, format, current, -1);
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_dv1394src_convert (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        goto not_supported;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      goto not_supported;
  }

  gst_object_unref (src);
  return TRUE;

not_supported:
  {
    gst_object_unref (src);
    return FALSE;
  }
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint
gst_dv1394src_uri_get_type (void)
{
  return GST_URI_SRC;
}
static gchar **
gst_dv1394src_uri_get_protocols (void)
{
  static gchar *protocols[] = { "dv", NULL };

  return protocols;
}
static const gchar *
gst_dv1394src_uri_get_uri (GstURIHandler * handler)
{
  GstDV1394Src *gst_dv1394src = GST_DV1394SRC (handler);

  return gst_dv1394src->uri;
}

static gboolean
gst_dv1394src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gchar *protocol, *location;
  gboolean ret;

  ret = TRUE;

  GstDV1394Src *gst_dv1394src = GST_DV1394SRC (handler);

  protocol = gst_uri_get_protocol (uri);
  if (strcmp (protocol, "dv") != 0) {
    g_free (protocol);
    return FALSE;
  }
  g_free (protocol);

  location = gst_uri_get_location (uri);
  gst_dv1394src->port = strtol (location, NULL, 10);
  g_free (location);

  return ret;
}

static void
gst_dv1394src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_dv1394src_uri_get_type;
  iface->get_protocols = gst_dv1394src_uri_get_protocols;
  iface->get_uri = gst_dv1394src_uri_get_uri;
  iface->set_uri = gst_dv1394src_uri_set_uri;
}
