/* ASF parser plugin for GStreamer
 * Copyright (C) 2009 Thiago Santos <thiagoss@embedded.ufcg.edu.br>
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

#include <string.h>
#include "gstasfparse.h"

/* FIXME add this include
 * #include <gst/gst-i18n-plugin.h> */

GST_DEBUG_CATEGORY_STATIC (asfparse_debug);
#define GST_CAT_DEFAULT asfparse_debug

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-ms-asf, parsed = (boolean) true")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-ms-asf, parsed = (boolean) false")
    );

static GstStateChangeReturn gst_asf_parse_change_state (GstElement * element,
    GstStateChange transition);
static void gst_asf_parse_loop (GstPad * pad);

GST_BOILERPLATE (GstAsfParse, gst_asf_parse, GstElement, GST_TYPE_ELEMENT);

static void
gst_asf_parse_reset (GstAsfParse * asfparse)
{
  gst_adapter_clear (asfparse->adapter);
  gst_asf_file_info_reset (asfparse->asfinfo);
  asfparse->parse_state = ASF_PARSING_HEADERS;
  asfparse->headers_size = 0;
  asfparse->data_size = 0;
  asfparse->parsed_packets = 0;
  asfparse->offset = 0;
}

static gboolean
gst_asf_parse_sink_activate (GstPad * pad)
{
  if (gst_pad_check_pull_range (pad)) {
    return gst_pad_activate_pull (pad, TRUE);
  } else {
    return gst_pad_activate_push (pad, TRUE);
  }
}

static gboolean
gst_asf_parse_sink_activate_pull (GstPad * pad, gboolean active)
{
  if (active) {
    return gst_pad_start_task (pad, (GstTaskFunction) gst_asf_parse_loop, pad);
  } else {
    return gst_pad_stop_task (pad);
  }
}

static GstFlowReturn
gst_asf_parse_push (GstAsfParse * asfparse, GstBuffer * buf)
{
  gst_buffer_set_caps (buf, asfparse->outcaps);
  return gst_pad_push (asfparse->srcpad, buf);
}

static GstFlowReturn
gst_asf_parse_parse_data_object (GstAsfParse * asfparse, GstBuffer * buffer)
{
  GstByteReader *reader;
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 packet_count = 0;

  GST_DEBUG_OBJECT (asfparse, "Parsing data object");

  reader = gst_byte_reader_new_from_buffer (buffer);
  /* skip to packet count */
  if (!gst_byte_reader_skip (reader, 40))
    goto error;
  if (!gst_byte_reader_get_uint64_le (reader, &packet_count))
    goto error;

  if (asfparse->asfinfo->packets_count != packet_count) {
    GST_WARNING_OBJECT (asfparse, "File properties object and data object have "
        "different packets count, %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT,
        asfparse->asfinfo->packets_count, packet_count);
  } else {
    GST_DEBUG_OBJECT (asfparse, "Total packets: %" G_GUINT64_FORMAT,
        packet_count);
  }

  gst_byte_reader_free (reader);
  return gst_asf_parse_push (asfparse, buffer);

error:
  ret = GST_FLOW_ERROR;
  GST_ERROR_OBJECT (asfparse, "Error while parsing data object headers");
  gst_byte_reader_free (reader);
  return ret;
}

static GstFlowReturn
gst_asf_parse_parse_packet (GstAsfParse * asfparse, GstBuffer * buffer)
{
  GstAsfPacketInfo *packetinfo = asfparse->packetinfo;

  if (!gst_asf_parse_packet (buffer, packetinfo, FALSE,
          asfparse->asfinfo->packet_size))
    goto error;

  GST_DEBUG_OBJECT (asfparse, "Received packet of length %" G_GUINT32_FORMAT
      ", padding %" G_GUINT32_FORMAT ", send time %" G_GUINT32_FORMAT
      ", duration %" G_GUINT16_FORMAT " and %s keyframe(s)",
      packetinfo->packet_size, packetinfo->padding,
      packetinfo->send_time, packetinfo->duration,
      (packetinfo->has_keyframe) ? "with" : "without");

  /* set gstbuffer fields */
  if (!packetinfo->has_keyframe) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  }
  GST_BUFFER_TIMESTAMP (buffer) = ((GstClockTime) packetinfo->send_time)
      * GST_MSECOND;
  GST_BUFFER_DURATION (buffer) = ((GstClockTime) packetinfo->duration)
      * GST_MSECOND;

  return gst_asf_parse_push (asfparse, buffer);

error:
  GST_ERROR_OBJECT (asfparse, "Error while parsing data packet");
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_asf_parse_pull_headers (GstAsfParse * asfparse)
{
  GstBuffer *guid_and_size = NULL;
  GstBuffer *headers = NULL;
  guint64 size;
  GstFlowReturn ret;

  if ((ret = gst_pad_pull_range (asfparse->sinkpad, asfparse->offset,
              ASF_GUID_OBJSIZE_SIZE, &guid_and_size)) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (asfparse, "Failed to pull data from headers");
    goto leave;
  }
  asfparse->offset += ASF_GUID_OBJSIZE_SIZE;
  size = gst_asf_match_and_peek_obj_size (GST_BUFFER_DATA (guid_and_size),
      &(guids[ASF_HEADER_OBJECT_INDEX]));

  if (size == 0) {
    GST_ERROR_OBJECT (asfparse, "ASF starting identifier missing");
    goto leave;
  }

  if ((ret = gst_pad_pull_range (asfparse->sinkpad, asfparse->offset,
              size - ASF_GUID_OBJSIZE_SIZE, &headers)) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (asfparse, "Failed to pull data from headers");
    goto leave;
  }
  headers = gst_buffer_join (guid_and_size, headers);
  guid_and_size = NULL;
  asfparse->offset += size - ASF_GUID_OBJSIZE_SIZE;
  if (!gst_asf_parse_headers (headers, asfparse->asfinfo)) {
    goto leave;
  }
  return gst_asf_parse_push (asfparse, headers);

leave:
  if (headers)
    gst_buffer_unref (headers);
  if (guid_and_size)
    gst_buffer_unref (guid_and_size);
  return ret;
}

static GstFlowReturn
gst_asf_parse_pull_data_header (GstAsfParse * asfparse)
{
  GstBuffer *buf = NULL;
  GstFlowReturn ret;

  if ((ret = gst_pad_pull_range (asfparse->sinkpad, asfparse->offset,
              ASF_DATA_OBJECT_SIZE, &buf)) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (asfparse, "Failed to pull data header");
    return ret;
  }
  asfparse->offset += ASF_DATA_OBJECT_SIZE;
  asfparse->data_size = gst_asf_match_and_peek_obj_size (GST_BUFFER_DATA (buf),
      &(guids[ASF_DATA_OBJECT_INDEX]));
  if (asfparse->data_size == 0) {
    GST_ERROR_OBJECT (asfparse, "Unexpected object, was expecting data object");
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }

  return gst_asf_parse_parse_data_object (asfparse, buf);
}

static GstFlowReturn
gst_asf_parse_pull_packets (GstAsfParse * asfparse)
{
  GstFlowReturn ret;
  while (asfparse->asfinfo->broadcast ||
      asfparse->parsed_packets < asfparse->asfinfo->packets_count) {
    GstBuffer *packet = NULL;

    GST_DEBUG_OBJECT (asfparse, "Parsing packet %" G_GUINT64_FORMAT,
        asfparse->parsed_packets);

    /* get the packet */
    ret = gst_pad_pull_range (asfparse->sinkpad, asfparse->offset,
        asfparse->asfinfo->packet_size, &packet);
    if (ret != GST_FLOW_OK)
      return ret;
    asfparse->parsed_packets++;
    asfparse->offset += asfparse->asfinfo->packet_size;

    /* parse the packet */
    ret = gst_asf_parse_parse_packet (asfparse, packet);
    if (ret != GST_FLOW_OK)
      return ret;
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_asf_parse_pull_indexes (GstAsfParse * asfparse)
{
  GstBuffer *guid_and_size = NULL;
  GstBuffer *buf = NULL;
  guint64 obj_size;
  GstFlowReturn ret = GST_FLOW_OK;
  while (1) {
    ret = gst_pad_pull_range (asfparse->sinkpad, asfparse->offset,
        ASF_GUID_OBJSIZE_SIZE, &guid_and_size);
    if (ret != GST_FLOW_OK)
      break;
    /* we can peek at the object size */
    obj_size =
        gst_asf_match_and_peek_obj_size (GST_BUFFER_DATA (guid_and_size), NULL);
    if (obj_size == 0) {
      GST_ERROR_OBJECT (asfparse, "Incomplete object found");
      gst_buffer_unref (guid_and_size);
      ret = GST_FLOW_ERROR;
      break;
    }
    asfparse->offset += ASF_GUID_OBJSIZE_SIZE;

    /* pull the rest of the object */
    ret = gst_pad_pull_range (asfparse->sinkpad, asfparse->offset, obj_size,
        &buf);
    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (guid_and_size);
      break;
    }
    asfparse->offset += obj_size - ASF_GUID_OBJSIZE_SIZE;

    buf = gst_buffer_join (guid_and_size, buf);
    ret = gst_asf_parse_push (asfparse, buf);
    if (ret != GST_FLOW_OK)
      break;
  }
  return ret;
}

static void
gst_asf_parse_loop (GstPad * pad)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstAsfParse *asfparse = GST_ASF_PARSE_CAST (GST_OBJECT_PARENT (pad));

  GST_LOG_OBJECT (asfparse, "Processing data in loop function");
  switch (asfparse->parse_state) {
    case ASF_PARSING_HEADERS:
      GST_INFO_OBJECT (asfparse, "Starting to parse headers");
      ret = gst_asf_parse_pull_headers (asfparse);
      if (ret != GST_FLOW_OK)
        goto pause;
      asfparse->parse_state = ASF_PARSING_DATA;

    case ASF_PARSING_DATA:
      GST_INFO_OBJECT (asfparse, "Parsing data object headers");
      ret = gst_asf_parse_pull_data_header (asfparse);
      if (ret != GST_FLOW_OK)
        goto pause;
      asfparse->parse_state = ASF_PARSING_PACKETS;

    case ASF_PARSING_PACKETS:
      GST_INFO_OBJECT (asfparse, "Starting packet parsing");
      GST_INFO_OBJECT (asfparse, "Broadcast mode %s",
          asfparse->asfinfo->broadcast ? "on" : "off");
      ret = gst_asf_parse_pull_packets (asfparse);
      if (ret != GST_FLOW_OK)
        goto pause;

      /* test if all packets have been processed */
      if (!asfparse->asfinfo->broadcast &&
          asfparse->parsed_packets == asfparse->asfinfo->packets_count) {
        GST_INFO_OBJECT (asfparse,
            "All %" G_GUINT64_FORMAT " packets processed",
            asfparse->parsed_packets);
        asfparse->parse_state = ASF_PARSING_INDEXES;
      }

    case ASF_PARSING_INDEXES:
      /* we currently don't care about indexes, so just push them forward */
      GST_INFO_OBJECT (asfparse, "Starting indexes parsing");
      ret = gst_asf_parse_pull_indexes (asfparse);
      if (ret != GST_FLOW_OK)
        goto pause;
    default:
      break;
  }

pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_INFO_OBJECT (asfparse, "Pausing sinkpad task");
    gst_pad_pause_task (pad);

    if (ret == GST_FLOW_UNEXPECTED) {
      gst_pad_push_event (asfparse->srcpad, gst_event_new_eos ());
    } else if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_UNEXPECTED) {
      GST_ELEMENT_ERROR (asfparse, STREAM, FAILED,
          (NULL), ("streaming task paused, reason %s (%d)", reason, ret));
      gst_pad_push_event (asfparse->srcpad, gst_event_new_eos ());
    }
  }
}

static GstFlowReturn
gst_asf_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAsfParse *asfparse;
  GstFlowReturn ret = GST_FLOW_OK;

  asfparse = GST_ASF_PARSE (GST_PAD_PARENT (pad));
  gst_adapter_push (asfparse->adapter, buffer);

  switch (asfparse->parse_state) {
    case ASF_PARSING_HEADERS:
      if (asfparse->headers_size == 0 &&
          gst_adapter_available (asfparse->adapter) >= ASF_GUID_OBJSIZE_SIZE) {

        /* we can peek at the object size */
        asfparse->headers_size =
            gst_asf_match_and_peek_obj_size (gst_adapter_peek
            (asfparse->adapter, ASF_GUID_OBJSIZE_SIZE),
            &(guids[ASF_HEADER_OBJECT_INDEX]));

        if (asfparse->headers_size == 0) {
          /* something is wrong, this probably ain't an ASF stream */
          GST_ERROR_OBJECT (asfparse, "ASF starting identifier missing");
          ret = GST_FLOW_ERROR;
          goto end;
        }
      }
      if (gst_adapter_available (asfparse->adapter) >= asfparse->headers_size) {
        GstBuffer *headers = gst_adapter_take_buffer (asfparse->adapter,
            asfparse->headers_size);
        if (gst_asf_parse_headers (headers, asfparse->asfinfo)) {
          ret = gst_asf_parse_push (asfparse, headers);
          asfparse->parse_state = ASF_PARSING_DATA;
        } else {
          ret = GST_FLOW_ERROR;
          GST_ERROR_OBJECT (asfparse, "Failed to parse headers");
        }
      }
      break;
    case ASF_PARSING_DATA:
      if (asfparse->data_size == 0 &&
          gst_adapter_available (asfparse->adapter) >= ASF_GUID_OBJSIZE_SIZE) {

        /* we can peek at the object size */
        asfparse->data_size =
            gst_asf_match_and_peek_obj_size (gst_adapter_peek
            (asfparse->adapter, ASF_GUID_OBJSIZE_SIZE),
            &(guids[ASF_DATA_OBJECT_INDEX]));

        if (asfparse->data_size == 0) {
          /* something is wrong */
          GST_ERROR_OBJECT (asfparse, "Unexpected object after headers, was "
              "expecting a data object");
          ret = GST_FLOW_ERROR;
          goto end;
        }
      }
      /* if we have received the full data object headers */
      if (gst_adapter_available (asfparse->adapter) >= ASF_DATA_OBJECT_SIZE) {
        ret = gst_asf_parse_parse_data_object (asfparse,
            gst_adapter_take_buffer (asfparse->adapter, ASF_DATA_OBJECT_SIZE));
        if (ret != GST_FLOW_OK) {
          goto end;
        }
        asfparse->parse_state = ASF_PARSING_PACKETS;
      }
      break;
    case ASF_PARSING_PACKETS:
      g_assert (asfparse->asfinfo->packet_size);
      while ((asfparse->asfinfo->broadcast ||
              asfparse->parsed_packets < asfparse->asfinfo->packets_count) &&
          gst_adapter_available (asfparse->adapter) >=
          asfparse->asfinfo->packet_size) {
        GstBuffer *packet = gst_adapter_take_buffer (asfparse->adapter,
            asfparse->asfinfo->packet_size);
        asfparse->parsed_packets++;
        ret = gst_asf_parse_parse_packet (asfparse, packet);
        if (ret != GST_FLOW_OK)
          goto end;
      }
      if (!asfparse->asfinfo->broadcast &&
          asfparse->parsed_packets >= asfparse->asfinfo->packets_count) {
        GST_INFO_OBJECT (asfparse, "Finished parsing packets");
        asfparse->parse_state = ASF_PARSING_INDEXES;
      }
      break;
    case ASF_PARSING_INDEXES:
      /* we currently don't care about any of those objects */
      if (gst_adapter_available (asfparse->adapter) >= ASF_GUID_OBJSIZE_SIZE) {
        guint64 obj_size;
        /* we can peek at the object size */
        obj_size = gst_asf_match_and_peek_obj_size (gst_adapter_peek
            (asfparse->adapter, ASF_GUID_OBJSIZE_SIZE), NULL);
        if (gst_adapter_available (asfparse->adapter) >= obj_size) {
          GST_DEBUG_OBJECT (asfparse, "Skiping object");
          ret = gst_asf_parse_push (asfparse,
              gst_adapter_take_buffer (asfparse->adapter, obj_size));
          if (ret != GST_FLOW_OK) {
            goto end;
          }
        }
      }
      break;
    default:
      break;
  }

end:
  return ret;
}

static void
gst_asf_parse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  gst_element_class_set_details_simple (element_class, "ASF parser",
      "Parser", "Parses ASF", "Thiago Santos <thiagoss@embedded.ufcg.edu.br>");

  GST_DEBUG_CATEGORY_INIT (asfparse_debug, "asfparse", 0,
      "Parser for ASF streams");
}

static void
gst_asf_parse_finalize (GObject * object)
{
  GstAsfParse *asfparse = GST_ASF_PARSE (object);
  gst_adapter_clear (asfparse->adapter);
  g_object_unref (G_OBJECT (asfparse->adapter));
  gst_caps_unref (asfparse->outcaps);
  gst_asf_file_info_free (asfparse->asfinfo);
  g_free (asfparse->packetinfo);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_asf_parse_class_init (GstAsfParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_asf_parse_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_asf_parse_change_state);
}

static void
gst_asf_parse_init (GstAsfParse * asfparse, GstAsfParseClass * klass)
{
  asfparse->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (asfparse->sinkpad, gst_asf_parse_chain);
  gst_pad_set_activate_function (asfparse->sinkpad,
      gst_asf_parse_sink_activate);
  gst_pad_set_activatepull_function (asfparse->sinkpad,
      gst_asf_parse_sink_activate_pull);
  gst_element_add_pad (GST_ELEMENT (asfparse), asfparse->sinkpad);

  asfparse->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (asfparse->srcpad);
  gst_element_add_pad (GST_ELEMENT (asfparse), asfparse->srcpad);

  asfparse->adapter = gst_adapter_new ();
  asfparse->outcaps = gst_caps_new_simple ("video/x-ms-asf", NULL);
  asfparse->asfinfo = gst_asf_file_info_new ();
  asfparse->packetinfo = g_new0 (GstAsfPacketInfo, 1);
  gst_asf_parse_reset (asfparse);
}

static GstStateChangeReturn
gst_asf_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstAsfParse *asfparse;
  GstStateChangeReturn ret;

  asfparse = GST_ASF_PARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_asf_parse_reset (asfparse);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

done:
  return ret;
}

gboolean
gst_asf_parse_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "asfparse",
      GST_RANK_NONE, GST_TYPE_ASF_PARSE);
}
