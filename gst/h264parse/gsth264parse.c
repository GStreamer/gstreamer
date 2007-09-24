/* GStreamer h264 parser
 * Copyright (C) 2005 Michal Benes <michal.benes@itonis.tv>
 *
 * gsth264parse.c:
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


#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gsth264parse.h"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264"));

GST_DEBUG_CATEGORY_STATIC (h264_parse_debug);
#define GST_CAT_DEFAULT h264_parse_debug

static const GstElementDetails gst_h264_parse_details =
GST_ELEMENT_DETAILS ("H264Parse",
    "Codec/Parser",
    "Parses raw h264 stream",
    "Michal Benes <michal.benes@itonis.tv>");

GST_BOILERPLATE (GstH264Parse, gst_h264_parse, GstElement, GST_TYPE_ELEMENT);

static void gst_h264_parse_finalize (GObject * object);
static GstFlowReturn gst_h264_parse_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_h264_parse_handle_event (GstPad * pad, GstEvent * event);

static void
gst_h264_parse_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details (gstelement_class, &gst_h264_parse_details);

  GST_DEBUG_CATEGORY_INIT (h264_parse_debug, "h264parse", 0, "h264 parser");
}

static void
gst_h264_parse_finalize (GObject * object)
{
  GstH264Parse *h264parse;

  h264parse = GST_H264PARSE (object);

  g_object_unref (h264parse->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_h264_parse_class_init (GstH264ParseClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_h264_parse_finalize);
}

static void
gst_h264_parse_init (GstH264Parse * h264parse, GstH264ParseClass * g_class)
{
  h264parse->sink = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_chain_function (h264parse->sink,
      GST_DEBUG_FUNCPTR (gst_h264_parse_chain));
  gst_pad_set_event_function (h264parse->sink,
      GST_DEBUG_FUNCPTR (gst_h264_parse_handle_event));
  gst_element_add_pad (GST_ELEMENT (h264parse), h264parse->sink);

  h264parse->src = gst_pad_new_from_static_template (&srctemplate, "src");
  gst_element_add_pad (GST_ELEMENT (h264parse), h264parse->src);

  h264parse->adapter = gst_adapter_new ();
}

static GstFlowReturn
gst_h264_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn res = GST_FLOW_OK;
  GstH264Parse *h264parse;

  h264parse = GST_H264PARSE (GST_PAD_PARENT (pad));

  if (!GST_PAD_CAPS (h264parse->src)) {
    GstCaps *caps;

    /* Forward sink caps if possible */
    caps = GST_PAD_CAPS (h264parse->sink);
    if (caps)
      gst_caps_ref (caps);

    /* Set default caps if the sink caps were not negotiated */
    if (!caps)
      caps = gst_caps_new_simple ("video/x-h264", NULL);

    /* Set source caps */
    if (!gst_pad_set_caps (h264parse->src, caps)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (h264parse),
          CORE, NEGOTIATION, (NULL), ("failed to set caps"));
      gst_caps_unref (caps);
      return GST_FLOW_ERROR;
    }
    gst_caps_unref (caps);
  }

  gst_adapter_push (h264parse->adapter, buffer);

  while (res == GST_FLOW_OK) {
    gint i;
    gint next_nalu_pos = -1;
    guint32 nalu_size;
    gint avail;
    const guint8 *data;

    avail = gst_adapter_available (h264parse->adapter);
    if (avail < 5)
      break;
    data = gst_adapter_peek (h264parse->adapter, avail);

    nalu_size = (data[0] << 24)
        + (data[1] << 16) + (data[2] << 8) + data[3];

    if (nalu_size == 1) {
      /* Bytestream format */
      /* Find next NALU header */
      for (i = 1; i < avail - 4; ++i) {
        if (data[i + 0] == 0 && data[i + 1] == 0 && data[i + 2] == 0
            && data[i + 3] == 1) {
          next_nalu_pos = i;
          break;
        }
      }
    } else {
      /* Packetized format */
      if (nalu_size + 4 <= avail)
        next_nalu_pos = nalu_size + 4;
    }

    /* Send packet */
    if (next_nalu_pos > 0) {
      GstBuffer *outbuf;

      outbuf = gst_buffer_new_and_alloc (next_nalu_pos);
      memcpy (GST_BUFFER_DATA (outbuf), data, next_nalu_pos);
      gst_adapter_flush (h264parse->adapter, next_nalu_pos);

      gst_buffer_set_caps (outbuf, GST_PAD_CAPS (h264parse->src));
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
      res = gst_pad_push (h264parse->src, outbuf);
    } else {
      break;                    /* NALU can not be parsed yet */
    }
  }

  return res;
}


static gboolean
gst_h264_parse_handle_event (GstPad * pad, GstEvent * event)
{
  GstH264Parse *h264parse;
  gboolean res;

  h264parse = GST_H264PARSE (gst_pad_get_parent (pad));

  res = gst_pad_event_default (pad, event);

  gst_object_unref (h264parse);
  return res;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "h264parse",
      GST_RANK_NONE, GST_TYPE_H264PARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "h264parse",
    "Element parsing raw h264 streams",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
