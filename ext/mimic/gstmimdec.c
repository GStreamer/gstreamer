/*
 * GStreamer
 * Copyright (c) 2005 INdT.
 * @author Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>
 * @author Rob Taylor <robtaylor@fastmail.fm>
 * @author Philippe Khalaf <burger@speedy.org>
 * @author Ole André Vadla Ravnås <oleavr@gmail.com>
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
 * SECTION:element-mimdec
 * @see_also: mimenc
 *
 * The MIMIC codec is used by MSN Messenger's webcam support. It consumes the
 * TCP header for the MIMIC codec.
 *
 * Its fourcc is ML20.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstmimdec.h"

GST_DEBUG_CATEGORY (mimdec_debug);
#define GST_CAT_DEFAULT (mimdec_debug)

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-mimic")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format= (string) \"RGB\", "
        "framerate = (fraction) 0/1, width = (int) 320, height = (int) 240;"
        "video/x-raw, format= (string) \"RGB\", "
        "framerate = (fraction) 0/1, width = (int) 160, height = (int) 120")
    );

static void gst_mim_dec_finalize (GObject * object);

static GstFlowReturn gst_mim_dec_chain (GstPad * pad, GstObject * parent,
    GstBuffer * in);
static GstStateChangeReturn
gst_mim_dec_change_state (GstElement * element, GstStateChange transition);

static gboolean gst_mim_dec_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);


G_DEFINE_TYPE (GstMimDec, gst_mim_dec, GST_TYPE_ELEMENT);

static void
gst_mim_dec_class_init (GstMimDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_mim_dec_change_state);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (gstelement_class,
      "Mimic Decoder",
      "Codec/Decoder/Video",
      "MSN Messenger compatible Mimic video decoder element",
      "Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>, "
      "Rob Taylor <robtaylor@fastmail.fm>, "
      "Philippe Khalaf <burger@speedy.org>, "
      "Ole André Vadla Ravnås <oleavr@gmail.com>,"
      "Olivier Crête <olivier.crete@collabora.co.uk");

  gobject_class->finalize = gst_mim_dec_finalize;

  GST_DEBUG_CATEGORY_INIT (mimdec_debug, "mimdec", 0, "Mimic decoder plugin");
}

static void
gst_mim_dec_init (GstMimDec * mimdec)
{
  mimdec->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_use_fixed_caps (mimdec->sinkpad);
  gst_pad_set_chain_function (mimdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mim_dec_chain));
  gst_pad_set_event_function (mimdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mim_dec_sink_event));
  gst_element_add_pad (GST_ELEMENT (mimdec), mimdec->sinkpad);

  mimdec->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (mimdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (mimdec), mimdec->srcpad);

  mimdec->adapter = gst_adapter_new ();

  mimdec->dec = NULL;
  mimdec->buffer_size = -1;
}

static void
gst_mim_dec_finalize (GObject * object)
{
  GstMimDec *mimdec = GST_MIM_DEC (object);

  gst_adapter_clear (mimdec->adapter);
  g_object_unref (mimdec->adapter);
  G_OBJECT_CLASS (gst_mim_dec_parent_class)->finalize (object);
}

static GstFlowReturn
gst_mim_dec_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstMimDec *mimdec = GST_MIM_DEC (parent);
  GstBuffer *out_buf;
  const guchar *header, *frame_body;
  guint32 fourcc;
  guint16 header_size;
  gint width, height;
  GstCaps *caps;
  GstFlowReturn res = GST_FLOW_OK;
  GstClockTime in_time = GST_BUFFER_TIMESTAMP (buf);
  GstEvent *event = NULL;
  gboolean result = TRUE;
  guint32 payload_size;
  guint32 current_ts;
  GstMapInfo map;

  gst_adapter_push (mimdec->adapter, buf);


  /* do we have enough bytes to read a header */
  while (gst_adapter_available (mimdec->adapter) >= 24) {
    header = gst_adapter_map (mimdec->adapter, 24);
    header_size = header[0];
    if (header_size != 24) {
      gst_adapter_unmap (mimdec->adapter);
      gst_adapter_flush (mimdec->adapter, 24);
      GST_ELEMENT_ERROR (mimdec, STREAM, DECODE, (NULL),
          ("invalid frame: header size %d incorrect", header_size));
      return GST_FLOW_ERROR;
    }

    if (header[1] == 1) {
      /* This is a a paused frame, skip it */
      gst_adapter_unmap (mimdec->adapter);
      gst_adapter_flush (mimdec->adapter, 24);
      continue;
    }

    fourcc = GUINT32_FROM_LE (*((guint32 *) (header + 12)));
    if (GST_MAKE_FOURCC ('M', 'L', '2', '0') != fourcc) {
      gst_adapter_unmap (mimdec->adapter);
      gst_adapter_flush (mimdec->adapter, 24);
      GST_ELEMENT_ERROR (mimdec, STREAM, WRONG_TYPE, (NULL),
          ("invalid frame: unknown FOURCC code 0x%" G_GINT32_MODIFIER "x",
              fourcc));
      return GST_FLOW_ERROR;
    }

    payload_size = GUINT32_FROM_LE (*((guint32 *) (header + 8)));

    current_ts = GUINT32_FROM_LE (*((guint32 *) (header + 20)));

    gst_adapter_unmap (mimdec->adapter);

    GST_LOG_OBJECT (mimdec, "Got packet, payload size %d", payload_size);

    if (gst_adapter_available (mimdec->adapter) < payload_size + 24)
      return GST_FLOW_OK;

    /* We have a whole packet and have read the header, lets flush it out */
    gst_adapter_flush (mimdec->adapter, 24);

    frame_body = gst_adapter_map (mimdec->adapter, payload_size);

    if (mimdec->buffer_size < 0) {
      /* Check if its a keyframe, otherwise skip it */
      if (GUINT32_FROM_LE (*((guint32 *) (frame_body + 12))) != 0) {
        gst_adapter_unmap (mimdec->adapter);
        gst_adapter_flush (mimdec->adapter, payload_size);
        return GST_FLOW_OK;
      }

      if (!mimic_decoder_init (mimdec->dec, frame_body)) {
        gst_adapter_unmap (mimdec->adapter);
        gst_adapter_flush (mimdec->adapter, payload_size);
        GST_ELEMENT_ERROR (mimdec, LIBRARY, INIT, (NULL),
            ("mimic_decoder_init error"));
        return GST_FLOW_ERROR;
      }

      if (!mimic_get_property (mimdec->dec, "buffer_size",
              &mimdec->buffer_size)) {
        gst_adapter_unmap (mimdec->adapter);
        gst_adapter_flush (mimdec->adapter, payload_size);
        GST_ELEMENT_ERROR (mimdec, LIBRARY, INIT, (NULL),
            ("mimic_get_property('buffer_size') error"));
        return GST_FLOW_ERROR;
      }

      mimic_get_property (mimdec->dec, "width", &width);
      mimic_get_property (mimdec->dec, "height", &height);
      GST_DEBUG_OBJECT (mimdec,
          "Initialised decoder with %d x %d payload size %d buffer_size %d",
          width, height, payload_size, mimdec->buffer_size);
      caps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "RGB",
          "framerate", GST_TYPE_FRACTION, 0, 1,
          "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);
      gst_pad_set_caps (mimdec->srcpad, caps);
      gst_caps_unref (caps);
    }


    if (mimdec->need_segment) {
      GstSegment segment;

      gst_segment_init (&segment, GST_FORMAT_TIME);

      if (GST_CLOCK_TIME_IS_VALID (in_time))
        segment.start = in_time;
      else
        segment.start = current_ts * GST_MSECOND;
      event = gst_event_new_segment (&segment);
    }
    mimdec->need_segment = FALSE;

    if (event)
      result = gst_pad_push_event (mimdec->srcpad, event);
    event = NULL;

    if (!result) {
      GST_WARNING_OBJECT (mimdec, "gst_pad_push_event failed");
      return GST_FLOW_ERROR;
    }


    out_buf = gst_buffer_new_allocate (NULL, mimdec->buffer_size, NULL);
    gst_buffer_map (out_buf, &map, GST_MAP_READWRITE);

    if (!mimic_decode_frame (mimdec->dec, frame_body, map.data)) {
      GST_WARNING_OBJECT (mimdec, "mimic_decode_frame error\n");

      gst_adapter_flush (mimdec->adapter, payload_size);

      gst_buffer_unmap (out_buf, &map);
      gst_buffer_unref (out_buf);
      GST_ELEMENT_ERROR (mimdec, STREAM, DECODE, (NULL),
          ("mimic_decode_frame error"));
      return GST_FLOW_ERROR;
    }
    gst_buffer_unmap (out_buf, &map);
    gst_adapter_flush (mimdec->adapter, payload_size);

    if (GST_CLOCK_TIME_IS_VALID (in_time))
      GST_BUFFER_TIMESTAMP (out_buf) = in_time;
    else
      GST_BUFFER_TIMESTAMP (out_buf) = current_ts * GST_MSECOND;

    res = gst_pad_push (mimdec->srcpad, out_buf);

    if (res != GST_FLOW_OK)
      break;
  }

  return res;
}

static GstStateChangeReturn
gst_mim_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstMimDec *mimdec;
  GstStateChangeReturn ret;

  mimdec = GST_MIM_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      mimdec->buffer_size = -1;
      mimdec->dec = mimic_open ();
      if (!mimdec->dec) {
        GST_ERROR_OBJECT (mimdec, "mimic_open failed");
        return GST_STATE_CHANGE_FAILURE;
      }
      mimdec->need_segment = TRUE;
      gst_adapter_clear (mimdec->adapter);

      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_mim_dec_parent_class)->change_state (element,
      transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;


  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (mimdec->dec != NULL) {
        mimic_close (mimdec->dec);
        mimdec->dec = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_mim_dec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = TRUE;
  gboolean forward = TRUE;
  GstMimDec *mimdec = GST_MIM_DEC (parent);

  /*
   * Ignore upstream segment event, its EVIL, we should implement
   * proper seeking instead
   */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      forward = FALSE;
      break;
    case GST_EVENT_STREAM_START:
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_EOS:
      gst_adapter_clear (mimdec->adapter);
      break;
    default:
      break;
  }

  if (forward)
    res = gst_pad_event_default (pad, parent, event);
  else
    gst_event_unref (event);

  return res;
}
