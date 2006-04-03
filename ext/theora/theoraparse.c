/* GStreamer
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) 2006 Andy Wingo <wingo@pobox.com>
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
 * SECTION:element-theoraparse
 * @short_description: parses theora streams 
 * @see_also: theoradec, oggdemux, vorbisparse
 *
 * <refsect2>
 * <para>
 * The theoraparse element will parse the header packets of the Theora
 * stream and put them as the streamheader in the caps. This is used in the
 * multifdsink case where you want to stream live theora streams to multiple
 * clients, each client has to receive the streamheaders first before they can
 * consume the theora packets.
 * </para>
 * <para>
 * This element also makes sure that the buffers that it pushes out are properly
 * timestamped and that their offset and offset_end are set. The buffers that
 * vorbisparse outputs have all of the metadata that oggmux expects to receive,
 * which allows you to (for example) remux an ogg/theora file.
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch -v filesrc location=video.ogg ! oggdemux ! theoraparse ! fakesink
 * </programlisting>
 * This pipeline shows that the streamheader is set in the caps, and that each
 * buffer has the timestamp, duration, offset, and offset_end set.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch filesrc location=video.ogg ! oggdemux ! vorbisparse \
 *            ! oggmux ! filesink location=video-remuxed.ogg
 * </programlisting>
 * This pipeline shows remuxing. video-remuxed.ogg might not be exactly the same
 * as video.ogg, but they should produce exactly the same decoded data.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2006-04-01 (0.10.4.1)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gsttheoraparse.h"

static GstElementDetails theora_parse_details = {
  "TheoraParse",
  "Codec/Parser/Video",
  "parse raw theora streams",
  "Andy Wingo <wingo@pobox.com>"
};

static GstStaticPadTemplate theora_parse_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-theora")
    );

static GstStaticPadTemplate theora_parse_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-theora")
    );

GST_BOILERPLATE (GstTheoraParse, gst_theora_parse, GstElement,
    GST_TYPE_ELEMENT);

static GstFlowReturn theora_parse_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn theora_parse_change_state (GstElement * element,
    GstStateChange transition);
static gboolean theora_parse_sink_event (GstPad * pad, GstEvent * event);

static void
gst_theora_parse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&theora_parse_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&theora_parse_sink_factory));
  gst_element_class_set_details (element_class, &theora_parse_details);
}

static void
gst_theora_parse_class_init (GstTheoraParseClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->change_state = theora_parse_change_state;
}

static void
gst_theora_parse_init (GstTheoraParse * parse, GstTheoraParseClass * g_class)
{
  parse->sinkpad =
      gst_pad_new_from_static_template (&theora_parse_sink_factory, "sink");
  gst_pad_set_chain_function (parse->sinkpad, theora_parse_chain);
  gst_pad_set_event_function (parse->sinkpad, theora_parse_sink_event);
  gst_element_add_pad (GST_ELEMENT (parse), parse->sinkpad);

  parse->srcpad =
      gst_pad_new_from_static_template (&theora_parse_src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (parse), parse->srcpad);
}

static void
theora_parse_set_header_on_caps (GstTheoraParse * parse, GstCaps * caps)
{
  GstBuffer **bufs;
  GstStructure *structure;
  gint i;
  GValue array = { 0 };
  GValue value = { 0 };

  bufs = parse->streamheader;
  structure = gst_caps_get_structure (caps, 0);
  g_value_init (&array, GST_TYPE_ARRAY);

  for (i = 0; i < 3; i++) {
    g_assert (bufs[i]);
    bufs[i] = gst_buffer_make_metadata_writable (bufs[i]);
    GST_BUFFER_FLAG_SET (bufs[i], GST_BUFFER_FLAG_IN_CAPS);

    g_value_init (&value, GST_TYPE_BUFFER);
    gst_value_set_buffer (&value, bufs[i]);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);
  }

  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&array);
}

/* FIXME: copy from libtheora, theora should somehow make this available for seeking */
static int
_theora_ilog (unsigned int v)
{
  int ret = 0;

  while (v) {
    ret++;
    v >>= 1;
  }
  return (ret);
}

/* two tasks to do here: set the streamheader on the caps, and use libtheora to
   parse the headers */
static void
theora_parse_set_streamheader (GstTheoraParse * parse)
{
  GstCaps *caps;
  gint i;

  g_assert (!parse->streamheader_received);

  caps = gst_caps_make_writable (gst_pad_get_caps (parse->srcpad));
  theora_parse_set_header_on_caps (parse, caps);
  GST_DEBUG_OBJECT (parse, "here are the caps: %" GST_PTR_FORMAT, caps);
  gst_pad_set_caps (parse->srcpad, caps);
  gst_caps_unref (caps);

  for (i = 0; i < 3; i++) {
    ogg_packet packet;
    GstBuffer *buf;

    buf = parse->streamheader[i];
    gst_buffer_set_caps (buf, GST_PAD_CAPS (parse->srcpad));

    packet.packet = GST_BUFFER_DATA (buf);
    packet.bytes = GST_BUFFER_SIZE (buf);
    packet.granulepos = GST_BUFFER_OFFSET_END (buf);
    packet.packetno = i + 1;
    packet.e_o_s = 0;
    theora_decode_header (&parse->info, &parse->comment, &packet);
  }

  parse->fps_n = parse->info.fps_numerator;
  parse->fps_d = parse->info.fps_denominator;
  parse->shift = _theora_ilog (parse->info.keyframe_frequency_force - 1);

  parse->streamheader_received = TRUE;
}

static void
theora_parse_push_headers (GstTheoraParse * parse)
{
  gint i;

  if (!parse->streamheader_received)
    theora_parse_set_streamheader (parse);

  /* ignore return values, we pass along the result of pushing data packets only
   */
  for (i = 0; i < 3; i++)
    gst_pad_push (parse->srcpad, gst_buffer_ref (parse->streamheader[i]));

  parse->send_streamheader = FALSE;
}

static void
theora_parse_clear_queue (GstTheoraParse * parse)
{
  while (parse->buffer_queue->length) {
    GstBuffer *buf;

    buf = GST_BUFFER_CAST (g_queue_pop_head (parse->buffer_queue));
    gst_buffer_unref (buf);
  }
}

static gint64
make_granulepos (gint64 keyframe, gint64 frame, gint shift)
{
  if (keyframe == -1)
    keyframe = 0;

  g_return_val_if_fail (frame >= keyframe, -1);
  g_return_val_if_fail (frame - keyframe < 1 << shift, -1);

  return (keyframe << shift) + (frame - keyframe);
}

static void
parse_granulepos (gint64 granulepos, gint shift, gint64 * keyframe,
    gint64 * frame)
{
  *keyframe = granulepos >> shift;
  *frame = *keyframe + (granulepos & ((1 << shift) - 1));
}

static gboolean
is_keyframe (GstBuffer * buf)
{
  if (!GST_BUFFER_DATA (buf))
    return FALSE;
  if (!GST_BUFFER_SIZE (buf))
    return FALSE;
  return ((GST_BUFFER_DATA (buf)[0] & 0x40) == 0);
}

static GstFlowReturn
theora_parse_push_buffer (GstTheoraParse * parse, GstBuffer * buf,
    gint64 keyframe, gint64 frame)
{

  GstClockTime this_time, next_time;

  this_time = gst_util_uint64_scale_int (GST_SECOND * frame,
      parse->fps_d, parse->fps_n);

  next_time = gst_util_uint64_scale_int (GST_SECOND * (frame + 1),
      parse->fps_d, parse->fps_n);

  GST_BUFFER_OFFSET_END (buf) = make_granulepos (keyframe, frame, parse->shift);
  GST_BUFFER_OFFSET (buf) = this_time;
  GST_BUFFER_TIMESTAMP (buf) = this_time;
  GST_BUFFER_DURATION (buf) = next_time - this_time;

  gst_buffer_set_caps (buf, GST_PAD_CAPS (parse->srcpad));

  return gst_pad_push (parse->srcpad, buf);
}

static GstFlowReturn
theora_parse_drain_queue_prematurely (GstTheoraParse * parse)
{
  GstFlowReturn ret = GST_FLOW_OK;

  /* got an EOS event, make sure to push out any buffers that were in the queue
   * -- won't normally be the case, but this catches the
   * didn't-get-a-granulepos-on-the-last-packet case. Assuming a continuous
   * stream. */

  while (!g_queue_is_empty (parse->buffer_queue)) {
    GstBuffer *buf;

    buf = GST_BUFFER_CAST (g_queue_pop_head (parse->buffer_queue));

    parse->prev_frame++;

    if (is_keyframe (buf))
      /* we have a keyframe */
      parse->prev_keyframe = parse->prev_frame;

    ret = theora_parse_push_buffer (parse, buf, parse->prev_keyframe,
        parse->prev_frame);

    if (ret != GST_FLOW_OK)
      goto done;
  }

done:
  return ret;
}

static GstFlowReturn
theora_parse_drain_queue (GstTheoraParse * parse, gint64 granulepos)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gint64 keyframe, frame;

  parse_granulepos (granulepos, parse->shift, &keyframe, &frame);

  parse->prev_frame = MAX (parse->prev_frame,
      frame - g_queue_get_length (parse->buffer_queue));

  while (!g_queue_is_empty (parse->buffer_queue)) {
    GstBuffer *buf;

    parse->prev_frame++;
    g_assert (parse->prev_frame >= 0);

    buf = GST_BUFFER_CAST (g_queue_pop_head (parse->buffer_queue));

    if (is_keyframe (buf))
      /* we have a keyframe */
      parse->prev_keyframe = parse->prev_frame;

    ret = theora_parse_push_buffer (parse, buf, parse->prev_keyframe,
        parse->prev_frame);

    if (ret != GST_FLOW_OK)
      goto done;
  }

done:
  return ret;
}

static GstFlowReturn
theora_parse_queue_buffer (GstTheoraParse * parse, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;

  buf = gst_buffer_make_metadata_writable (buf);

  g_queue_push_tail (parse->buffer_queue, buf);

  if (GST_BUFFER_OFFSET_END_IS_VALID (buf))
    ret = theora_parse_drain_queue (parse, GST_BUFFER_OFFSET_END (buf));

  return ret;
}

static GstFlowReturn
theora_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret;
  GstBuffer *buf;
  GstTheoraParse *parse;

  parse = GST_THEORA_PARSE (gst_pad_get_parent (pad));

  buf = GST_BUFFER (buffer);
  parse->packetno++;

  if (parse->packetno <= 3) {
    /* if 1 <= packetno <= 3, it's streamheader,
     * so put it on the streamheader list and return */
    parse->streamheader[parse->packetno - 1] = buf;
    ret = GST_FLOW_OK;
  } else {
    if (parse->send_streamheader)
      theora_parse_push_headers (parse);

    ret = theora_parse_queue_buffer (parse, buf);
  }

  gst_object_unref (parse);

  return ret;
}

static gboolean
theora_parse_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret;
  GstTheoraParse *parse;

  parse = GST_THEORA_PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      theora_parse_clear_queue (parse);
      parse->prev_keyframe = -1;
      parse->prev_frame = -1;
      ret = gst_pad_event_default (pad, event);
      break;
    case GST_EVENT_EOS:
      theora_parse_drain_queue_prematurely (parse);
      ret = gst_pad_event_default (pad, event);
      break;
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (parse);

  return ret;
}

static GstStateChangeReturn
theora_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstTheoraParse *parse = GST_THEORA_PARSE (element);
  GstStateChangeReturn ret;
  gint i;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      theora_info_init (&parse->info);
      theora_comment_init (&parse->comment);
      parse->packetno = 0;
      parse->send_streamheader = TRUE;
      parse->buffer_queue = g_queue_new ();
      parse->prev_keyframe = -1;
      parse->prev_frame = -1;
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      theora_info_clear (&parse->info);
      theora_comment_clear (&parse->comment);
      theora_parse_clear_queue (parse);
      g_queue_free (parse->buffer_queue);
      parse->buffer_queue = NULL;
      for (i = 0; i < 3; i++) {
        if (parse->streamheader[i]) {
          gst_buffer_unref (parse->streamheader[i]);
          parse->streamheader[i] = NULL;
        }
      }
      parse->streamheader_received = FALSE;
      break;
    default:
      break;
  }

  return ret;
}
