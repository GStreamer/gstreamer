/* GStreamer
 * Copyright (C) <2007> Julien Moutte <julien@fluendo.com>
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

#include "mpeg4videoparse.h"

GST_DEBUG_CATEGORY_STATIC (mpeg4v_parse_debug);
#define GST_CAT_DEFAULT mpeg4v_parse_debug

/* elementfactory information */
static GstElementDetails mpeg4vparse_details =
GST_ELEMENT_DETAILS ("MPEG 4 video elementary stream parser",
    "Codec/Parser/Video",
    "Parses MPEG-4 Part 2 elementary video streams",
    "Julien Moutte <julien@fluendo.com>");

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) 4, "
        "parsed = (boolean) true, " "systemstream = (boolean) false")
    );

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) 4, "
        "parsed = (boolean) false, " "systemstream = (boolean) false")
    );

GST_BOILERPLATE (GstMpeg4VParse, gst_mpeg4vparse, GstElement, GST_TYPE_ELEMENT);

static void
gst_mpeg4vparse_align (GstMpeg4VParse * parse)
{
  guint flushed = 0;

  /* Searching for a start code */
  while (gst_adapter_available (parse->adapter) >= 4) {
    /* If we have enough data, ensure we're aligned to a start code */
    const guint8 *data = gst_adapter_peek (parse->adapter, 4);

    if (data[0] == 0 && data[1] == 0 && data[2] == 1) {
      GST_LOG_OBJECT (parse, "found start code with type %02X", data[3]);
      parse->state = PARSE_START_FOUND;
      break;
    } else {
      gst_adapter_flush (parse->adapter, 1);
      flushed++;
      parse->state = PARSE_NEED_START;
    }
  }

  if (G_UNLIKELY (flushed)) {
    GST_LOG_OBJECT (parse, "flushed %u bytes while aligning", flushed);
  }
}

static GstFlowReturn
gst_mpeg4vparse_drain (GstMpeg4VParse * parse)
{
  GstFlowReturn ret = GST_FLOW_OK;
  const guint8 *data = NULL;
  guint available = 0;

  available = gst_adapter_available (parse->adapter);
  data = gst_adapter_peek (parse->adapter, available);

  while (parse->offset < available - 4) {
    /* We generate packets based on VOP end code (next start code) */
    if (data[parse->offset] == 0 && data[parse->offset + 1] == 0 &&
        data[parse->offset + 2] == 1) {
      switch (parse->state) {
        case PARSE_START_FOUND:
          if (data[parse->offset + 3] == 0xB6) {
            GST_LOG_OBJECT (parse, "found VOP start marker at %u",
                parse->offset);
            parse->state = PARSE_VOP_FOUND;
          }
          /* Jump over it */
          parse->offset += 4;
          break;
        case PARSE_VOP_FOUND:
        {                       /* We were in a VOP already, any start code marks the end of it */
          GstBuffer *out_buf = gst_adapter_take_buffer (parse->adapter,
              parse->offset);

          GST_LOG_OBJECT (parse, "found VOP end marker at %u", parse->offset);
          if (out_buf) {
            gst_buffer_set_caps (out_buf, GST_PAD_CAPS (parse->srcpad));
            gst_pad_push (parse->srcpad, out_buf);
          }
          /* Restart now that we flushed data */
          parse->offset = 0;
          parse->state = PARSE_START_FOUND;
          available = gst_adapter_available (parse->adapter);
          data = gst_adapter_peek (parse->adapter, available);
          break;
        }
        default:
          GST_WARNING_OBJECT (parse, "unexpected parse state (%d)",
              parse->state);
          ret = GST_FLOW_UNEXPECTED;
          goto beach;
      }
    } else {                    /* Continue searching */
      parse->offset++;
    }
  }

beach:
  return ret;
}

static GstFlowReturn
gst_mpeg4vparse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstMpeg4VParse *parse = GST_MPEG4VIDEOPARSE (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (parse, "received buffer of %u bytes with ts %"
      GST_TIME_FORMAT " and offset %" G_GINT64_FORMAT, GST_BUFFER_SIZE (buffer),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_BUFFER_OFFSET (buffer));

  gst_adapter_push (parse->adapter, buffer);

  /* We need to get aligned on a start code */
  if (G_UNLIKELY (parse->state == PARSE_NEED_START)) {
    gst_mpeg4vparse_align (parse);
    /* No start code found in that buffer */
    if (G_UNLIKELY (parse->state == PARSE_NEED_START)) {
      GST_DEBUG_OBJECT (parse, "start code not found, need more data");
      goto beach;
    }
  }

  /* We need at least 8 bytes to find the next start code which marks the end
     of the one we just found */
  if (G_UNLIKELY (gst_adapter_available (parse->adapter) < 8)) {
    GST_DEBUG_OBJECT (parse, "start code found, need more data to find next");
    goto beach;
  }

  /* Drain the accumulated blocks frame per frame */
  ret = gst_mpeg4vparse_drain (parse);

beach:
  gst_object_unref (parse);

  return ret;
}

static gboolean
gst_mpeg4vparse_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  gboolean res = TRUE;
  GstCaps *out_caps = NULL;
  GstMpeg4VParse *parse = GST_MPEG4VIDEOPARSE (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (parse, "setcaps called with %" GST_PTR_FORMAT, caps);

  out_caps = gst_caps_new_simple ("video/mpeg",
      "mpegversion", G_TYPE_INT, 4,
      "systemstream", G_TYPE_BOOLEAN, FALSE,
      "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

  if (out_caps) {
    GST_DEBUG_OBJECT (parse, "setting downstream caps to %" GST_PTR_FORMAT,
        caps);
    res = gst_pad_set_caps (parse->srcpad, out_caps);
    gst_caps_unref (out_caps);
  }

  gst_object_unref (parse);

  return res;
}

static gboolean
gst_mpeg4vparse_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstMpeg4VParse *parse = GST_MPEG4VIDEOPARSE (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (parse, "handling event type %s",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (parse);

  return res;
}

static void
gst_mpeg4vparse_cleanup (GstMpeg4VParse * parse)
{
  if (parse->adapter) {
    gst_adapter_clear (parse->adapter);
  }

  parse->state = PARSE_NEED_START;
  parse->offset = 0;
}

static GstStateChangeReturn
gst_mpeg4vparse_change_state (GstElement * element, GstStateChange transition)
{
  GstMpeg4VParse *parse = GST_MPEG4VIDEOPARSE (element);
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_mpeg4vparse_cleanup (parse);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_mpeg4vparse_dispose (GObject * object)
{
  GstMpeg4VParse *parse = GST_MPEG4VIDEOPARSE (object);

  if (parse->adapter) {
    g_object_unref (parse->adapter);
    parse->adapter = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_mpeg4vparse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &mpeg4vparse_details);
}

static void
gst_mpeg4vparse_class_init (GstMpeg4VParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;
  gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_mpeg4vparse_dispose);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mpeg4vparse_change_state);
}

static void
gst_mpeg4vparse_init (GstMpeg4VParse * parse, GstMpeg4VParseClass * g_class)
{
  parse->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg4vparse_chain));
  gst_pad_set_event_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg4vparse_sink_event));
  gst_pad_set_setcaps_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg4vparse_sink_setcaps));
  gst_element_add_pad (GST_ELEMENT (parse), parse->sinkpad);

  parse->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (parse->srcpad);
  gst_element_add_pad (GST_ELEMENT (parse), parse->srcpad);

  parse->adapter = gst_adapter_new ();

  gst_mpeg4vparse_cleanup (parse);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (mpeg4v_parse_debug, "mpeg4videoparse", 0,
      "MPEG-4 video parser");

  if (!gst_element_register (plugin, "mpeg4videoparse", GST_RANK_SECONDARY,
          gst_mpeg4vparse_get_type ()))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpeg4videoparse",
    "MPEG-4 video parser",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
