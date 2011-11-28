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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "bpp = (int) 24, "
        "depth = (int) 24, "
        "endianness = (int) 4321, "
        "framerate = (fraction) [1/1, 30/1], "
        "red_mask = (int) 16711680, "
        "green_mask = (int) 65280, "
        "blue_mask = (int) 255, "
        "width = (int) 320, "
        "height = (int) 240"
        ";video/x-raw-rgb, "
        "bpp = (int) 24, "
        "depth = (int) 24, "
        "endianness = (int) 4321, "
        "framerate = (fraction) [1/1, 30/1], "
        "red_mask = (int) 16711680, "
        "green_mask = (int) 65280, "
        "blue_mask = (int) 255, " "width = (int) 160, " "height = (int) 120")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-mimic")
    );


static gboolean gst_mim_enc_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_mim_enc_chain (GstPad * pad, GstBuffer * in);
static void gst_mim_enc_create_tcp_header (GstMimEnc * mimenc, GstBuffer * buf,
    guint32 payload_size, gboolean keyframe, gboolean paused);
static gboolean gst_mim_enc_event (GstPad * pad, GstEvent * event);

static GstStateChangeReturn
gst_mim_enc_change_state (GstElement * element, GstStateChange transition);

static void gst_mim_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mim_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

GST_BOILERPLATE (GstMimEnc, gst_mim_enc, GstElement, GST_TYPE_ELEMENT);

static void
gst_mim_enc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_mim_enc_set_property;
  gobject_class->get_property = gst_mim_enc_get_property;

  g_object_class_install_property (gobject_class, PROP_PAUSED_MODE,
      g_param_spec_boolean ("paused-mode", "Paused mode",
          "If enabled, empty frames will be generated every 4 seconds"
          " when no data is received",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_set_details_simple (element_class,
      "Mimic Encoder",
      "Codec/Encoder/Video",
      "MSN Messenger compatible Mimic video encoder element",
      "Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>,"
      "Olivier Crête <olivier.crete@collabora.co.uk");
}

static void
gst_mim_enc_class_init (GstMimEncClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_mim_enc_change_state);

  GST_DEBUG_CATEGORY_INIT (mimenc_debug, "mimenc", 0, "Mimic encoder plugin");
}

static void
gst_mim_enc_init (GstMimEnc * mimenc, GstMimEncClass * klass)
{
  mimenc->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_element_add_pad (GST_ELEMENT (mimenc), mimenc->sinkpad);
  gst_pad_set_setcaps_function (mimenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mim_enc_setcaps));
  gst_pad_set_chain_function (mimenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mim_enc_chain));
  gst_pad_set_event_function (mimenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mim_enc_event));

  mimenc->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (mimenc), mimenc->srcpad);

  mimenc->enc = NULL;

  gst_segment_init (&mimenc->segment, GST_FORMAT_UNDEFINED);

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

static gboolean
gst_mim_enc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstMimEnc *mimenc;
  GstStructure *structure;
  int ret = TRUE, height, width;

  mimenc = GST_MIM_ENC (gst_pad_get_parent (pad));
  g_return_val_if_fail (mimenc != NULL, FALSE);
  g_return_val_if_fail (GST_IS_MIM_ENC (mimenc), FALSE);

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &width);
  if (!ret) {
    GST_DEBUG_OBJECT (mimenc, "No width set");
    goto out;
  }
  ret = gst_structure_get_int (structure, "height", &height);
  if (!ret) {
    GST_DEBUG_OBJECT (mimenc, "No height set");
    goto out;
  }

  GST_OBJECT_LOCK (mimenc);

  if (width == 320 && height == 240)
    mimenc->res = MIMIC_RES_HIGH;
  else if (width == 160 && height == 120)
    mimenc->res = MIMIC_RES_LOW;
  else {
    GST_WARNING_OBJECT (mimenc, "Invalid resolution %dx%d", width, height);
    ret = FALSE;
    GST_OBJECT_UNLOCK (mimenc);
    goto out;
  }

  mimenc->width = (guint16) width;
  mimenc->height = (guint16) height;

  GST_DEBUG_OBJECT (mimenc, "Got info from caps w : %d, h : %d",
      mimenc->width, mimenc->height);

  if (!mimic_encoder_init (mimenc->enc, mimenc->res)) {
    GST_ERROR_OBJECT (mimenc, "mimic_encoder_init error");
    ret = FALSE;
    GST_OBJECT_UNLOCK (mimenc);
    goto out;
  }

  if (!mimic_get_property (mimenc->enc, "buffer_size", &mimenc->buffer_size)) {
    GST_ERROR_OBJECT (mimenc, "mimic_get_property(buffer_size) error");
    ret = FALSE;
  }

  GST_OBJECT_UNLOCK (mimenc);
out:
  gst_object_unref (mimenc);
  return ret;
}

static GstFlowReturn
gst_mim_enc_chain (GstPad * pad, GstBuffer * in)
{
  GstMimEnc *mimenc;
  GstBuffer *out_buf = NULL, *buf = NULL;
  guchar *data;
  gint buffer_size;
  GstFlowReturn res = GST_FLOW_OK;
  GstEvent *event = NULL;
  gboolean keyframe;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  mimenc = GST_MIM_ENC (gst_pad_get_parent (pad));

  g_return_val_if_fail (GST_IS_MIM_ENC (mimenc), GST_FLOW_ERROR);

  GST_OBJECT_LOCK (mimenc);

  if (mimenc->segment.format == GST_FORMAT_UNDEFINED) {
    GST_WARNING_OBJECT (mimenc, "No new-segment received,"
        " initializing segment with time 0..-1");
    gst_segment_init (&mimenc->segment, GST_FORMAT_TIME);
    gst_segment_set_newsegment (&mimenc->segment,
        FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0);
  }

  if (mimenc->enc == NULL) {
  }

  buf = in;
  data = GST_BUFFER_DATA (buf);

  out_buf = gst_buffer_new_and_alloc (mimenc->buffer_size + TCP_HEADER_SIZE);
  GST_BUFFER_TIMESTAMP (out_buf) =
      gst_segment_to_running_time (&mimenc->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (buf));
  mimenc->last_buffer = GST_BUFFER_TIMESTAMP (out_buf);
  buffer_size = mimenc->buffer_size;
  keyframe = (mimenc->frames % MAX_INTERFRAMES) == 0 ? TRUE : FALSE;
  if (!mimic_encode_frame (mimenc->enc, data,
          GST_BUFFER_DATA (out_buf) + TCP_HEADER_SIZE, &buffer_size,
          keyframe)) {
    gst_buffer_unref (out_buf);
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (mimenc, STREAM, ENCODE, (NULL),
        ("mimic_encode_frame error"));
    res = GST_FLOW_ERROR;
    goto out_unlock;
  }
  if (!keyframe)
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

  GST_BUFFER_SIZE (out_buf) = buffer_size + TCP_HEADER_SIZE;

  GST_LOG_OBJECT (mimenc, "incoming buf size %d, encoded size %d",
      GST_BUFFER_SIZE (buf), GST_BUFFER_SIZE (out_buf));
  ++mimenc->frames;

  /* now let's create that tcp header */
  gst_mim_enc_create_tcp_header (mimenc, out_buf, buffer_size, keyframe, FALSE);

  if (mimenc->need_newsegment) {
    event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0);
    mimenc->need_newsegment = FALSE;
  }

  GST_OBJECT_UNLOCK (mimenc);

  if (event) {
    if (!gst_pad_push_event (mimenc->srcpad, event))
      GST_WARNING_OBJECT (mimenc, "Failed to push NEWSEGMENT event");
  }

  res = gst_pad_push (mimenc->srcpad, out_buf);

out:
  if (buf)
    gst_buffer_unref (buf);
  gst_object_unref (mimenc);

  return res;

out_unlock:
  GST_OBJECT_UNLOCK (mimenc);
  goto out;
}

static void
gst_mim_enc_create_tcp_header (GstMimEnc * mimenc, GstBuffer * buf,
    guint32 payload_size, gboolean keyframe, gboolean paused)
{
  /* 24 bytes */
  guchar *p = (guchar *) GST_BUFFER_DATA (buf);

  p[0] = 24;
  p[1] = paused ? 1 : 0;
  GST_WRITE_UINT16_LE (p + 2, mimenc->width);
  GST_WRITE_UINT16_LE (p + 4, mimenc->height);
  GST_WRITE_UINT16_LE (p + 6, keyframe ? 1 : 0);
  GST_WRITE_UINT32_LE (p + 8, payload_size);
  GST_WRITE_UINT32_LE (p + 12, paused ? 0 :
      GST_MAKE_FOURCC ('M', 'L', '2', '0'));
  GST_WRITE_UINT32_LE (p + 16, 0);
  GST_WRITE_UINT32_LE (p + 20, GST_BUFFER_TIMESTAMP (buf) / GST_MSECOND);
}

static gboolean
gst_mim_enc_event (GstPad * pad, GstEvent * event)
{
  GstMimEnc *mimenc = GST_MIM_ENC (gst_pad_get_parent (pad));
  gboolean ret = TRUE;
  gboolean forward = TRUE;


  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate,
          &format, &start, &stop, &time);

      /* we need time for now */
      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      GST_DEBUG_OBJECT (mimenc,
          "newsegment: update %d, rate %g, arate %g, start %" GST_TIME_FORMAT
          ", stop %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT,
          update, rate, arate, GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (time));

      /* now configure the values, we need these to time the release of the
       * buffers on the srcpad. */
      GST_OBJECT_LOCK (mimenc);
      gst_segment_set_newsegment_full (&mimenc->segment, update,
          rate, arate, format, start, stop, time);
      GST_OBJECT_UNLOCK (mimenc);
      forward = FALSE;
      break;

    }
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_OBJECT_LOCK (mimenc);
      gst_segment_init (&mimenc->segment, GST_FORMAT_UNDEFINED);
      mimenc->need_newsegment = TRUE;
      GST_OBJECT_UNLOCK (mimenc);
      break;
    default:
      break;
  }

  if (forward)
    ret = gst_pad_push_event (mimenc->srcpad, event);
  else
    gst_event_unref (event);

done:
  gst_object_unref (mimenc);

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
    GstEvent *event = NULL;

    buffer = gst_buffer_new_and_alloc (TCP_HEADER_SIZE);
    GST_BUFFER_TIMESTAMP (buffer) = mimenc->last_buffer + PAUSED_MODE_INTERVAL;
    gst_mim_enc_create_tcp_header (mimenc, buffer, 0, FALSE, TRUE);

    mimenc->last_buffer += PAUSED_MODE_INTERVAL;

    if (mimenc->need_newsegment) {
      event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0);
      mimenc->need_newsegment = FALSE;
    }

    GST_OBJECT_UNLOCK (mimenc);
    GST_LOG_OBJECT (mimenc, "Haven't had an incoming buffer in 4 seconds,"
        " sending out a pause frame");

    if (event) {
      if (!gst_pad_push_event (mimenc->srcpad, event))
        GST_WARNING_OBJECT (mimenc, "Failed to push NEWSEGMENT event");
    }
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
    case GST_STATE_CHANGE_NULL_TO_READY:
      mimenc->enc = mimic_open ();
      if (!mimenc) {
        GST_ERROR_OBJECT (mimenc, "mimic_open failed");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;

    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_OBJECT_LOCK (mimenc);
      gst_segment_init (&mimenc->segment, GST_FORMAT_UNDEFINED);
      mimenc->last_buffer = GST_CLOCK_TIME_NONE;
      mimenc->need_newsegment = TRUE;
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

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

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
        if (!gst_pad_start_task (mimenc->srcpad, paused_mode_task, mimenc)) {
          ret = GST_STATE_CHANGE_FAILURE;
          GST_ERROR_OBJECT (mimenc, "Can not start task");
        }
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_OBJECT_LOCK (element);
      if (mimenc->enc != NULL) {
        mimic_close (mimenc->enc);
        mimenc->enc = NULL;
        mimenc->buffer_size = -1;
        mimenc->frames = 0;
      }
      GST_OBJECT_UNLOCK (element);
      break;

    default:
      break;
  }

  return ret;
}
