/*
 * GStreamer
 * Copyright (c) 2005 INdT.
 * Copyright (c) 2009 Collabora Ltd.
 * @author Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>
 * @author Philippe Khalaf <burger@speedy.org>
 * @author Olivier Crête <olivier.crete@collabora.co.uk>
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
 * SECTION:element-mimenc
 * @see_also: mimdec
 *
 * The MIMIC codec is used by MSN Messenger's webcam support. It creates the
 * TCP header for the MIMIC codec.
 *
 * When using it to communicate directly with MSN Messenger, if the sender
 * wants to stop sending, he has to send a special buffer every 4 seconds.
 * When the "paused-mode" property is set to TRUE, if the element receives no
 * buffer on its sink pad for 4 seconds, it will produced a special paused
 * frame and will continue doing so every 4 seconds until a new buffer is
 *u received on its sink pad.
 *
 * Its fourcc is ML20.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstmimenc.h"

GST_DEBUG_CATEGORY (mimenc_debug);
#define GST_CAT_DEFAULT (mimenc_debug)

#define MAX_INTERFRAMES 15

#define TCP_HEADER_SIZE 24

#define PAUSED_MODE_INTERVAL (4 * GST_SECOND)


enum
{
  PROP_0,
  PROP_PAUSED_MODE,
  PROP_LAST
};


static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format = (string) \"RGB\", "
        "framerate = (fraction) [1/1, 30/1], "
        "width = (int) 320, "
        "height = (int) 240;"
        "video/x-raw, format = (string) \"RGB\", "
        "framerate = (fraction) [1/1, 30/1], "
        "width = (int) 160, " "height = (int) 120")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-mimic")
    );


static gboolean gst_mim_enc_setcaps (GstMimEnc * mimenc, GstCaps * caps);
static GstFlowReturn gst_mim_enc_chain (GstPad * pad, GstObject * parent,
    GstBuffer * in);
static void gst_mim_enc_create_tcp_header (GstMimEnc * mimenc, guint8 * p,
    guint32 payload_size, GstClockTime ts, gboolean keyframe, gboolean paused);
static gboolean gst_mim_enc_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstStateChangeReturn
gst_mim_enc_change_state (GstElement * element, GstStateChange transition);

static void gst_mim_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mim_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

G_DEFINE_TYPE (GstMimEnc, gst_mim_enc, GST_TYPE_ELEMENT);


static void
gst_mim_enc_class_init (GstMimEncClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_mim_enc_set_property;
  gobject_class->get_property = gst_mim_enc_get_property;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_mim_enc_change_state);

  GST_DEBUG_CATEGORY_INIT (mimenc_debug, "mimenc", 0, "Mimic encoder plugin");


  g_object_class_install_property (gobject_class, PROP_PAUSED_MODE,
      g_param_spec_boolean ("paused-mode", "Paused mode",
          "If enabled, empty frames will be generated every 4 seconds"
          " when no data is received",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_static_metadata (gstelement_class,
      "Mimic Encoder",
      "Codec/Encoder/Video",
      "MSN Messenger compatible Mimic video encoder element",
      "Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>,"
      "Olivier Crête <olivier.crete@collabora.co.uk");
}

static void
gst_mim_enc_init (GstMimEnc * mimenc)
{
  mimenc->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (mimenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mim_enc_chain));
  gst_pad_set_event_function (mimenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mim_enc_event));
  gst_element_add_pad (GST_ELEMENT (mimenc), mimenc->sinkpad);

  mimenc->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (mimenc->srcpad);
  gst_element_add_pad (GST_ELEMENT (mimenc), mimenc->srcpad);

  mimenc->enc = NULL;

  gst_segment_init (&mimenc->segment, GST_FORMAT_TIME);

  mimenc->res = MIMIC_RES_HIGH;
  mimenc->buffer_size = -1;
  mimenc->width = 0;
  mimenc->height = 0;
  mimenc->frames = 0;
}

static void
gst_mim_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMimEnc *mimenc = GST_MIM_ENC (object);

  switch (prop_id) {
    case PROP_PAUSED_MODE:
      GST_OBJECT_LOCK (mimenc);
      mimenc->paused_mode = g_value_get_boolean (value);
      if (GST_STATE (object) == GST_STATE_PLAYING)
        GST_WARNING_OBJECT (object, "Tried to disable paused-mode in a playing"
            " encoder, it will not be stopped until it is paused");
      GST_OBJECT_UNLOCK (mimenc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mim_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMimEnc *mimenc = GST_MIM_ENC (object);

  switch (prop_id) {
    case PROP_PAUSED_MODE:
      GST_OBJECT_LOCK (mimenc);
      g_value_set_boolean (value, mimenc->paused_mode);
      GST_OBJECT_UNLOCK (mimenc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mim_enc_reset_locked (GstMimEnc * mimenc)
{
  if (mimenc->enc != NULL) {
    mimic_close (mimenc->enc);
    mimenc->enc = NULL;
    mimenc->buffer_size = -1;
    mimenc->frames = 0;
    mimenc->width = 0;
    mimenc->height = 0;
  }
  gst_event_replace (&mimenc->pending_segment, NULL);
}

static void
gst_mim_enc_reset (GstMimEnc * mimenc)
{
  GST_OBJECT_LOCK (mimenc);
  gst_mim_enc_reset_locked (mimenc);
  GST_OBJECT_UNLOCK (mimenc);
}

static gboolean
gst_mim_enc_setcaps (GstMimEnc * mimenc, GstCaps * caps)
{
  GstStructure *structure;
  int height, width;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "width", &width)) {
    GST_DEBUG_OBJECT (mimenc, "No width set");
    return FALSE;
  }
  if (!gst_structure_get_int (structure, "height", &height)) {
    GST_DEBUG_OBJECT (mimenc, "No height set");
    return FALSE;
  }

  GST_OBJECT_LOCK (mimenc);

  if (mimenc->width == width && mimenc->height == height) {
    ret = TRUE;
    goto out;
  }


  if (width == 320 && height == 240)
    mimenc->res = MIMIC_RES_HIGH;
  else if (width == 160 && height == 120)
    mimenc->res = MIMIC_RES_LOW;
  else {
    GST_WARNING_OBJECT (mimenc, "Invalid resolution %dx%d", width, height);
    goto out;
  }

  gst_mim_enc_reset_locked (mimenc);

  mimenc->width = (guint16) width;
  mimenc->height = (guint16) height;

  GST_DEBUG_OBJECT (mimenc, "Got info from caps w : %d, h : %d",
      mimenc->width, mimenc->height);

  mimenc->enc = mimic_open ();
  if (!mimenc->enc) {
    GST_ERROR_OBJECT (mimenc, "mimic_open failed");
    goto out;
  }

  if (!mimic_encoder_init (mimenc->enc, mimenc->res)) {
    GST_ERROR_OBJECT (mimenc, "mimic_encoder_init error");
    goto out;
  }

  if (!mimic_get_property (mimenc->enc, "buffer_size", &mimenc->buffer_size)) {
    GST_ERROR_OBJECT (mimenc, "mimic_get_property(buffer_size) error");
  }

  ret = TRUE;

out:
  GST_OBJECT_UNLOCK (mimenc);
  return ret;
}

static GstFlowReturn
gst_mim_enc_chain (GstPad * pad, GstObject * parent, GstBuffer * in)
{
  GstMimEnc *mimenc = GST_MIM_ENC (parent);
  GstBuffer *out = NULL;
  GstMapInfo in_map;
  GstMapInfo out_map;
  GstFlowReturn res = GST_FLOW_OK;
  gboolean keyframe;
  gint buffer_size;

  GST_OBJECT_LOCK (mimenc);

  gst_buffer_map (in, &in_map, GST_MAP_READ);

  out = gst_buffer_new_and_alloc (mimenc->buffer_size + TCP_HEADER_SIZE);
  gst_buffer_map (out, &out_map, GST_MAP_READWRITE);
  GST_BUFFER_TIMESTAMP (out) =
      gst_segment_to_running_time (&mimenc->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (in));
  mimenc->last_buffer = GST_BUFFER_TIMESTAMP (out);
  buffer_size = mimenc->buffer_size;
  keyframe = (mimenc->frames % MAX_INTERFRAMES) == 0 ? TRUE : FALSE;
  if (!mimic_encode_frame (mimenc->enc, in_map.data,
          out_map.data + TCP_HEADER_SIZE, &buffer_size, keyframe)) {
    gst_buffer_unmap (in, &in_map);
    gst_buffer_unmap (out, &out_map);
    gst_buffer_unref (out);
    GST_ELEMENT_ERROR (mimenc, STREAM, ENCODE, (NULL),
        ("mimic_encode_frame error"));
    res = GST_FLOW_ERROR;
    goto out_unlock;
  }
  gst_buffer_unmap (in, &in_map);

  if (!keyframe)
    GST_BUFFER_FLAG_SET (out, GST_BUFFER_FLAG_DELTA_UNIT);


  GST_LOG_OBJECT (mimenc, "incoming buf size %" G_GSIZE_FORMAT
      ", encoded size %" G_GSIZE_FORMAT, gst_buffer_get_size (in),
      gst_buffer_get_size (out));
  ++mimenc->frames;

  /* now let's create that tcp header */
  gst_mim_enc_create_tcp_header (mimenc, out_map.data, buffer_size,
      GST_BUFFER_TIMESTAMP (out), keyframe, FALSE);

  gst_buffer_unmap (out, &out_map);
  gst_buffer_resize (out, 0, buffer_size + TCP_HEADER_SIZE);

  if (G_UNLIKELY (mimenc->pending_segment)) {
    gst_pad_push_event (mimenc->srcpad, mimenc->pending_segment);
    mimenc->pending_segment = FALSE;
  }

  GST_OBJECT_UNLOCK (mimenc);

  res = gst_pad_push (mimenc->srcpad, out);

out:
  gst_buffer_unref (in);

  return res;

out_unlock:
  GST_OBJECT_UNLOCK (mimenc);
  goto out;
}

static void
gst_mim_enc_create_tcp_header (GstMimEnc * mimenc, guint8 * p,
    guint32 payload_size, GstClockTime ts, gboolean keyframe, gboolean paused)
{
  p[0] = 24;
  p[1] = paused ? 1 : 0;
  GST_WRITE_UINT16_LE (p + 2, mimenc->width);
  GST_WRITE_UINT16_LE (p + 4, mimenc->height);
  GST_WRITE_UINT16_LE (p + 6, keyframe ? 1 : 0);
  GST_WRITE_UINT32_LE (p + 8, payload_size);
  GST_WRITE_UINT32_LE (p + 12, paused ? 0 :
      GST_MAKE_FOURCC ('M', 'L', '2', '0'));
  GST_WRITE_UINT32_LE (p + 16, 0);
  GST_WRITE_UINT32_LE (p + 20, ts / GST_MSECOND);
}


static gboolean
gst_mim_enc_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMimEnc *mimenc = GST_MIM_ENC (parent);
  gboolean ret = TRUE;
  gboolean forward = TRUE;


  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&mimenc->segment, GST_FORMAT_UNDEFINED);
      gst_event_replace (&mimenc->pending_segment, NULL);
      break;
    case GST_EVENT_EOS:
      gst_mim_enc_reset (mimenc);
      break;
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);

      ret = gst_mim_enc_setcaps (mimenc, caps);

      forward = FALSE;
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      gst_event_copy_segment (event, &mimenc->segment);

      /* we need time for now */
      if (mimenc->segment.format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      gst_event_replace (&mimenc->pending_segment, event);
      forward = FALSE;
      break;

    }
      break;
    default:
      break;
  }

  if (forward)
    ret = gst_pad_event_default (pad, parent, event);
  else
    gst_event_unref (event);

done:

  return ret;

newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (mimenc, "received non TIME newsegment");
    gst_event_unref (event);
    ret = FALSE;
    goto done;
  }
}

static void
paused_mode_task (gpointer data)
{
  GstMimEnc *mimenc = GST_MIM_ENC (data);
  GstClockTime now;
  GstClockTimeDiff diff;
  GstFlowReturn ret;

  GST_OBJECT_LOCK (mimenc);

  if (!GST_ELEMENT_CLOCK (mimenc)) {
    GST_OBJECT_UNLOCK (mimenc);
    GST_ERROR_OBJECT (mimenc, "Element has no clock");
    gst_pad_pause_task (mimenc->srcpad);
    return;
  }

  if (mimenc->stop_paused_mode) {
    GST_OBJECT_UNLOCK (mimenc);
    goto stop_task;
  }

  now = gst_clock_get_time (GST_ELEMENT_CLOCK (mimenc));

  diff = now - GST_ELEMENT_CAST (mimenc)->base_time - mimenc->last_buffer;
  if (diff < 0)
    diff = 0;

  if (diff > 3.95 * GST_SECOND) {
    GstBuffer *buffer;
    GstMapInfo out_map;

    buffer = gst_buffer_new_and_alloc (TCP_HEADER_SIZE);
    gst_buffer_map (buffer, &out_map, GST_MAP_WRITE);
    GST_BUFFER_TIMESTAMP (buffer) = mimenc->last_buffer + PAUSED_MODE_INTERVAL;
    gst_mim_enc_create_tcp_header (mimenc, out_map.data, 0,
        GST_BUFFER_TIMESTAMP (buffer), FALSE, TRUE);
    gst_buffer_unmap (buffer, &out_map);

    mimenc->last_buffer += PAUSED_MODE_INTERVAL;

    GST_OBJECT_UNLOCK (mimenc);
    GST_LOG_OBJECT (mimenc, "Haven't had an incoming buffer in 4 seconds,"
        " sending out a pause frame");

    ret = gst_pad_push (mimenc->srcpad, buffer);
    if (ret < 0) {
      GST_WARNING_OBJECT (mimenc, "Error pushing paused header: %s",
          gst_flow_get_name (ret));
      goto stop_task;
    }
  } else {
    GstClockTime next_stop;
    GstClockID id;

    next_stop = now + (PAUSED_MODE_INTERVAL - MIN (diff, PAUSED_MODE_INTERVAL));

    id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK (mimenc), next_stop);

    if (mimenc->stop_paused_mode) {
      GST_OBJECT_UNLOCK (mimenc);
      goto stop_task;
    }

    mimenc->clock_id = id;
    GST_OBJECT_UNLOCK (mimenc);

    gst_clock_id_wait (id, NULL);

    GST_OBJECT_LOCK (mimenc);
    mimenc->clock_id = NULL;
    GST_OBJECT_UNLOCK (mimenc);

    gst_clock_id_unref (id);
  }
  return;

stop_task:

  gst_pad_pause_task (mimenc->srcpad);
}

static GstStateChangeReturn
gst_mim_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstMimEnc *mimenc = GST_MIM_ENC (element);
  GstStateChangeReturn ret;
  gboolean paused_mode;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_OBJECT_LOCK (mimenc);
      gst_segment_init (&mimenc->segment, GST_FORMAT_UNDEFINED);
      gst_event_replace (&mimenc->pending_segment, NULL);
      mimenc->last_buffer = GST_CLOCK_TIME_NONE;
      GST_OBJECT_UNLOCK (mimenc);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_OBJECT_LOCK (mimenc);
      if (mimenc->clock_id)
        gst_clock_id_unschedule (mimenc->clock_id);
      mimenc->stop_paused_mode = TRUE;
      GST_OBJECT_UNLOCK (mimenc);

      gst_pad_pause_task (mimenc->srcpad);

      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_mim_enc_parent_class)->change_state (element,
      transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_OBJECT_LOCK (mimenc);
      mimenc->stop_paused_mode = FALSE;
      paused_mode = mimenc->paused_mode;
      if (paused_mode) {
        if (!GST_ELEMENT_CLOCK (mimenc)) {
          GST_OBJECT_UNLOCK (mimenc);
          GST_ELEMENT_ERROR (mimenc, RESOURCE, FAILED,
              ("Using paused-mode requires a clock, but no clock was provided"
                  " to the element"), (NULL));
          return GST_STATE_CHANGE_FAILURE;
        }
        if (mimenc->last_buffer == GST_CLOCK_TIME_NONE)
          mimenc->last_buffer = gst_clock_get_time (GST_ELEMENT_CLOCK (mimenc))
              - GST_ELEMENT_CAST (mimenc)->base_time;
      }
      GST_OBJECT_UNLOCK (mimenc);
      if (paused_mode) {
        if (!gst_pad_start_task (mimenc->srcpad, paused_mode_task, mimenc,
                NULL)) {
          ret = GST_STATE_CHANGE_FAILURE;
          GST_ERROR_OBJECT (mimenc, "Can not start task");
        }
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_mim_enc_reset (mimenc);
      break;

    default:
      break;
  }

  return ret;
}
