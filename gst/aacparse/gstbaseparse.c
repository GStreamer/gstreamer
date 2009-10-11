/* GStreamer
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>.
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
 * SECTION:gstbaseparse
 * @short_description: Base class for stream parsers
 * @see_also: #GstBaseTransform
 *
 * This base class is for parser elements that process data and splits it 
 * into separate audio/video/whatever frames.
 *
 * It provides for:
 * <itemizedlist>
 *   <listitem><para>One sinkpad and one srcpad</para></listitem>
 *   <listitem><para>Handles state changes</para></listitem>
 *   <listitem><para>Does flushing</para></listitem>
 *   <listitem><para>Push mode</para></listitem>
 *   <listitem><para>Pull mode</para></listitem>
 *   <listitem><para>Handles events (NEWSEGMENT/EOS/FLUSH)</para></listitem>
 *   <listitem><para>Handles seeking in both modes</para></listitem>
 *   <listitem><para>
 *        Handles POSITION/DURATION/SEEKING/FORMAT/CONVERT queries
 *   </para></listitem>
 * </itemizedlist>
 *
 * The purpose of this base class is to provide a basic functionality of
 * a parser and share a lot of rather complex code.
 *
 * Description of the parsing mechanism:
 * <orderedlist>
 * <listitem>
 *   <itemizedlist><title>Set-up phase</title>
 *   <listitem><para>
 *     GstBaseParse class calls @set_sink_caps to inform the subclass about
 *     incoming sinkpad caps. Subclass should set the srcpad caps accordingly.
 *   </para></listitem>
 *   <listitem><para>
 *     GstBaseParse calls @start to inform subclass that data processing is
 *     about to start now.
 *   </para></listitem>
 *   <listitem><para>
 *      At least in this point subclass needs to tell the GstBaseParse class
 *      how big data chunks it wants to receive (min_frame_size). It can do 
 *      this with @gst_base_parse_set_min_frame_size.
 *   </para></listitem>
 *   <listitem><para>
 *      GstBaseParse class sets up appropriate data passing mode (pull/push)
 *      and starts to process the data.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist>
 *   <title>Parsing phase</title>
 *     <listitem><para>
 *       GstBaseParse gathers at least min_frame_size bytes of data either 
 *       by pulling it from upstream or collecting buffers into internal
 *       #GstAdapter.
 *     </para></listitem>
 *     <listitem><para>
 *       A buffer of min_frame_size bytes is passed to subclass with
 *       @check_valid_frame. Subclass checks the contents and returns TRUE
 *       if the buffer contains a valid frame. It also needs to set the
 *       @framesize according to the detected frame size. If buffer didn't
 *       contain a valid frame, this call must return FALSE and optionally
 *       set the @skipsize value to inform base class that how many bytes
 *       it needs to skip in order to find a valid frame. The passed buffer
 *       is read-only.
 *     </para></listitem>
 *     <listitem><para>
 *       After valid frame is found, it will be passed again to subclass with
 *       @parse_frame call. Now subclass is responsible for parsing the
 *       frame contents and setting the buffer timestamp, duration and caps.
 *     </para></listitem>
 *     <listitem><para>
 *       Finally the buffer can be pushed downstream and parsing loop starts
 *       over again.
 *     </para></listitem>
 *     <listitem><para>
 *       During the parsing process GstBaseClass will handle both srcpad and
 *       sinkpad events. They will be passed to subclass if @event or
 *       @src_event callbacks have been provided.
 *     </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Shutdown phase</title>
 *   <listitem><para>
 *     GstBaseParse class calls @stop to inform the subclass that data
 *     parsing will be stopped.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * </orderedlist>
 *
 * Subclass is responsible for providing pad template caps for
 * source and sink pads. The pads need to be named "sink" and "src". It also 
 * needs to set the fixed caps on srcpad, when the format is ensured (e.g. 
 * when base class calls subclass' @set_sink_caps function).
 *
 * This base class uses GST_FORMAT_DEFAULT as a meaning of frames. So,
 * subclass conversion routine needs to know that conversion from
 * GST_FORMAT_TIME to GST_FORMAT_DEFAULT must return the
 * frame number that can be found from the given byte position.
 *
 * GstBaseParse uses subclasses conversion methods also for seeking. If
 * subclass doesn't provide @convert function, seeking will get disabled.
 *
 * Subclass @start and @stop functions will be called to inform the beginning
 * and end of data processing.
 *
 * Things that subclass need to take care of:
 * <itemizedlist>
 *   <listitem><para>Provide pad templates</para></listitem>
 *   <listitem><para>
 *      Fixate the source pad caps when appropriate
 *   </para></listitem>
 *   <listitem><para>
 *      Inform base class how big data chunks should be retrieved. This is
 *      done with @gst_base_parse_set_min_frame_size function.
 *   </para></listitem>
 *   <listitem><para>
 *      Examine data chunks passed to subclass with @check_valid_frame
 *      and tell if they contain a valid frame
 *   </para></listitem>
 *   <listitem><para>
 *      Set the caps and timestamp to frame that is passed to subclass with
 *      @parse_frame function.
 *   </para></listitem>
 *   <listitem><para>Provide conversion functions</para></listitem>
 *   <listitem><para>
 *      Update the duration information with @gst_base_parse_set_duration
 *   </para></listitem>
 * </itemizedlist>
 *
 */

/* TODO:
 *  - Better segment handling:
 *    - NEWSEGMENT for gaps
 *    - Not NEWSEGMENT starting at 0 but at first frame timestamp
 *  - GstIndex support
 *  - Seek table generation and subclass seek entry injection
 *  - Accurate seeking
 *  - In push mode provide a queue of adapter-"queued" buffers for upstream
 *    buffer metadata
 *  - Timestamp tracking and setting
 *  - Handle upstream timestamps
 *  - Queue buffers/events until caps are set
 *  - Bitrate tracking => inaccurate seeking, inaccurate duration calculation
 *  - Let subclass decide if frames outside the segment should be dropped
 *  - Send queries upstream
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "gstbaseparse.h"

GST_DEBUG_CATEGORY_STATIC (gst_base_parse_debug);
#define GST_CAT_DEFAULT gst_base_parse_debug

/* Supported formats */
static GstFormat fmtlist[] = {
  GST_FORMAT_DEFAULT,
  GST_FORMAT_BYTES,
  GST_FORMAT_TIME,
  0
};

#define GST_BASE_PARSE_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_BASE_PARSE, GstBaseParsePrivate))

struct _GstBaseParsePrivate
{
  GstActivateMode pad_mode;

  gint64 duration;
  GstFormat duration_fmt;

  guint min_frame_size;
  gboolean passthrough;

  gboolean discont;
  gboolean flushing;

  gint64 offset;

  GList *pending_events;

  GstBuffer *cache;
};

struct _GstBaseParseClassPrivate
{
  gpointer _padding;
};

static GstElementClass *parent_class = NULL;

static void gst_base_parse_base_init (gpointer g_class);
static void gst_base_parse_base_finalize (gpointer g_class);
static void gst_base_parse_class_init (GstBaseParseClass * klass);
static void gst_base_parse_init (GstBaseParse * parse,
    GstBaseParseClass * klass);

GType
gst_base_parse_get_type (void)
{
  static GType base_parse_type = 0;

  if (!base_parse_type) {
    static const GTypeInfo base_parse_info = {
      sizeof (GstBaseParseClass),
      (GBaseInitFunc) gst_base_parse_base_init,
      (GBaseFinalizeFunc) gst_base_parse_base_finalize,
      (GClassInitFunc) gst_base_parse_class_init,
      NULL,
      NULL,
      sizeof (GstBaseParse),
      0,
      (GInstanceInitFunc) gst_base_parse_init,
    };

    base_parse_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstAacBaseParse", &base_parse_info, G_TYPE_FLAG_ABSTRACT);
  }
  return base_parse_type;
}

static void gst_base_parse_finalize (GObject * object);

static gboolean gst_base_parse_sink_activate (GstPad * sinkpad);
static gboolean gst_base_parse_sink_activate_push (GstPad * pad,
    gboolean active);
static gboolean gst_base_parse_sink_activate_pull (GstPad * pad,
    gboolean active);
static gboolean gst_base_parse_handle_seek (GstBaseParse * parse,
    GstEvent * event);

static gboolean gst_base_parse_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_base_parse_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_base_parse_query (GstPad * pad, GstQuery * query);
static gboolean gst_base_parse_sink_setcaps (GstPad * pad, GstCaps * caps);
static const GstQueryType *gst_base_parse_get_querytypes (GstPad * pad);

static GstFlowReturn gst_base_parse_chain (GstPad * pad, GstBuffer * buffer);
static void gst_base_parse_loop (GstPad * pad);

static gboolean gst_base_parse_check_frame (GstBaseParse * parse,
    GstBuffer * buffer, guint * framesize, gint * skipsize);

static GstFlowReturn gst_base_parse_parse_frame (GstBaseParse * parse,
    GstBuffer * buffer);

static gboolean gst_base_parse_sink_eventfunc (GstBaseParse * parse,
    GstEvent * event);

static gboolean gst_base_parse_src_eventfunc (GstBaseParse * parse,
    GstEvent * event);

static gboolean gst_base_parse_is_seekable (GstBaseParse * parse);

static void gst_base_parse_drain (GstBaseParse * parse);

static void
gst_base_parse_base_init (gpointer g_class)
{
  GstBaseParseClass *klass = GST_BASE_PARSE_CLASS (g_class);
  GstBaseParseClassPrivate *priv;

  GST_DEBUG_CATEGORY_INIT (gst_base_parse_debug, "aacbaseparse", 0,
      "baseparse element");

  /* TODO: Remove this once GObject supports class private data */
  priv = g_slice_new0 (GstBaseParseClassPrivate);
  if (klass->priv)
    memcpy (priv, klass->priv, sizeof (GstBaseParseClassPrivate));
  klass->priv = priv;
}

static void
gst_base_parse_base_finalize (gpointer g_class)
{
  GstBaseParseClass *klass = GST_BASE_PARSE_CLASS (g_class);

  g_slice_free (GstBaseParseClassPrivate, klass->priv);
  klass->priv = NULL;
}

static void
gst_base_parse_finalize (GObject * object)
{
  GstBaseParse *parse = GST_BASE_PARSE (object);

  g_mutex_free (parse->parse_lock);
  g_object_unref (parse->adapter);

  if (parse->pending_segment) {
    gst_event_replace (&parse->pending_segment, NULL);
  }
  if (parse->close_segment) {
    gst_event_replace (&parse->close_segment, NULL);
  }

  if (parse->priv->cache) {
    gst_buffer_unref (parse->priv->cache);
    parse->priv->cache = NULL;
  }

  g_list_foreach (parse->priv->pending_events, (GFunc) gst_mini_object_unref,
      NULL);
  g_list_free (parse->priv->pending_events);
  parse->priv->pending_events = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_base_parse_class_init (GstBaseParseClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (klass, sizeof (GstBaseParsePrivate));
  parent_class = g_type_class_peek_parent (klass);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_parse_finalize);

  /* Default handlers */
  klass->check_valid_frame = gst_base_parse_check_frame;
  klass->parse_frame = gst_base_parse_parse_frame;
  klass->event = gst_base_parse_sink_eventfunc;
  klass->src_event = gst_base_parse_src_eventfunc;
  klass->is_seekable = gst_base_parse_is_seekable;
}

static void
gst_base_parse_init (GstBaseParse * parse, GstBaseParseClass * bclass)
{
  GstPadTemplate *pad_template;

  GST_DEBUG_OBJECT (parse, "gst_base_parse_init");

  parse->priv = GST_BASE_PARSE_GET_PRIVATE (parse);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (bclass), "sink");
  g_return_if_fail (pad_template != NULL);
  parse->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_event_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_parse_sink_event));
  gst_pad_set_setcaps_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_parse_sink_setcaps));
  gst_pad_set_chain_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_parse_chain));
  gst_pad_set_activate_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_parse_sink_activate));
  gst_pad_set_activatepush_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_parse_sink_activate_push));
  gst_pad_set_activatepull_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_parse_sink_activate_pull));
  gst_element_add_pad (GST_ELEMENT (parse), parse->sinkpad);

  GST_DEBUG_OBJECT (parse, "sinkpad created");

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (bclass), "src");
  g_return_if_fail (pad_template != NULL);
  parse->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_pad_set_event_function (parse->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_parse_src_event));
  gst_pad_set_query_type_function (parse->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_parse_get_querytypes));
  gst_pad_set_query_function (parse->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_parse_query));
  gst_element_add_pad (GST_ELEMENT (parse), parse->srcpad);
  GST_DEBUG_OBJECT (parse, "src created");

  parse->parse_lock = g_mutex_new ();
  parse->adapter = gst_adapter_new ();
  parse->pending_segment = NULL;
  parse->close_segment = NULL;

  parse->priv->pad_mode = GST_ACTIVATE_NONE;
  parse->priv->duration = -1;
  parse->priv->min_frame_size = 1;
  parse->priv->passthrough = FALSE;
  parse->priv->discont = FALSE;
  parse->priv->flushing = FALSE;
  parse->priv->offset = 0;
  GST_DEBUG_OBJECT (parse, "init ok");
}



/**
 * gst_base_parse_check_frame:
 * @parse: #GstBaseParse.
 * @buffer: GstBuffer.
 * @framesize: This will be set to tell the found frame size in bytes.
 * @skipsize: Output parameter that tells how much data needs to be skipped
 *            in order to find the following frame header.
 *
 * Default callback for check_valid_frame.
 * 
 * Returns: Always TRUE.
 */
static gboolean
gst_base_parse_check_frame (GstBaseParse * parse,
    GstBuffer * buffer, guint * framesize, gint * skipsize)
{
  *framesize = GST_BUFFER_SIZE (buffer);
  *skipsize = 0;
  return TRUE;
}


/**
 * gst_base_parse_parse_frame:
 * @parse: #GstBaseParse.
 * @buffer: #GstBuffer.
 *
 * Default callback for parse_frame.
 */
static GstFlowReturn
gst_base_parse_parse_frame (GstBaseParse * parse, GstBuffer * buffer)
{
  /* FIXME: Could we even _try_ to do something clever here? */
  GST_BUFFER_TIMESTAMP (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  return GST_FLOW_OK;
}


/**
 * gst_base_parse_bytepos_to_time:
 * @parse: #GstBaseParse.
 * @bytepos: Position (in bytes) to be converted.
 * @pos_in_time: #GstClockTime pointer where the result is set.
 *
 * Convert given byte position into #GstClockTime format.
 * 
 * Returns: TRUE if conversion succeeded.
 */
static gboolean
gst_base_parse_bytepos_to_time (GstBaseParse * parse, gint64 bytepos,
    GstClockTime * pos_in_time)
{
  GstBaseParseClass *klass;
  gboolean res = FALSE;

  klass = GST_BASE_PARSE_GET_CLASS (parse);

  if (klass->convert) {
    res = klass->convert (parse, GST_FORMAT_BYTES, bytepos,
        GST_FORMAT_TIME, (gint64 *) pos_in_time);
  }
  return res;
}


/**
 * gst_base_parse_sink_event:
 * @pad: #GstPad that received the event.
 * @event: #GstEvent to be handled.
 *
 * Handler for sink pad events.
 *
 * Returns: TRUE if the event was handled.
 */
static gboolean
gst_base_parse_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseParse *parse;
  GstBaseParseClass *bclass;
  gboolean handled = FALSE;
  gboolean ret = TRUE;


  parse = GST_BASE_PARSE (gst_pad_get_parent (pad));
  bclass = GST_BASE_PARSE_GET_CLASS (parse);

  GST_DEBUG_OBJECT (parse, "handling event %d", GST_EVENT_TYPE (event));

  /* Cache all events except EOS, NEWSEGMENT and FLUSH_STOP if we have a
   * pending segment */
  if (parse->pending_segment && GST_EVENT_TYPE (event) != GST_EVENT_EOS
      && GST_EVENT_TYPE (event) != GST_EVENT_NEWSEGMENT
      && GST_EVENT_TYPE (event) != GST_EVENT_FLUSH_START
      && GST_EVENT_TYPE (event) != GST_EVENT_FLUSH_STOP) {
    parse->priv->pending_events =
        g_list_append (parse->priv->pending_events, event);
    ret = TRUE;
  } else {

    if (bclass->event)
      handled = bclass->event (parse, event);

    if (!handled)
      ret = gst_pad_event_default (pad, event);
  }

  gst_object_unref (parse);
  GST_DEBUG_OBJECT (parse, "event handled");
  return ret;
}


/**
 * gst_base_parse_sink_eventfunc:
 * @parse: #GstBaseParse.
 * @event: #GstEvent to be handled.
 *
 * Element-level event handler function.
 *
 * Returns: TRUE if the event was handled and not need forwarding.
 */
static gboolean
gst_base_parse_sink_eventfunc (GstBaseParse * parse, GstEvent * event)
{
  gboolean handled = FALSE;
  GstEvent **eventp;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start, stop, pos, offset = 0;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &pos);


      if (format == GST_FORMAT_BYTES) {
        GstClockTime seg_start, seg_stop, seg_pos;

        /* stop time is allowed to be open-ended, but not start & pos */
        seg_stop = GST_CLOCK_TIME_NONE;
        offset = pos;

        if (gst_base_parse_bytepos_to_time (parse, start, &seg_start) &&
            gst_base_parse_bytepos_to_time (parse, pos, &seg_pos)) {
          gst_event_unref (event);
          event = gst_event_new_new_segment_full (update, rate, applied_rate,
              GST_FORMAT_TIME, seg_start, seg_stop, seg_pos);
          format = GST_FORMAT_TIME;
          GST_DEBUG_OBJECT (parse, "Converted incoming segment to TIME. "
              "start = %" GST_TIME_FORMAT ", stop = %" GST_TIME_FORMAT
              ", pos = %" GST_TIME_FORMAT, GST_TIME_ARGS (seg_start),
              GST_TIME_ARGS (seg_stop), GST_TIME_ARGS (seg_pos));
        }
      }

      if (format != GST_FORMAT_TIME) {
        /* Unknown incoming segment format. Output a default open-ended 
         * TIME segment */
        gst_event_unref (event);
        event = gst_event_new_new_segment_full (update, rate, applied_rate,
            GST_FORMAT_TIME, 0, GST_CLOCK_TIME_NONE, 0);
      }

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &pos);

      gst_segment_set_newsegment_full (&parse->segment, update, rate,
          applied_rate, format, start, stop, pos);

      GST_DEBUG_OBJECT (parse, "Created newseg rate %g, applied rate %g, "
          "format %d, start = %" GST_TIME_FORMAT ", stop = %" GST_TIME_FORMAT
          ", pos = %" GST_TIME_FORMAT, rate, applied_rate, format,
          GST_TIME_ARGS (start), GST_TIME_ARGS (stop), GST_TIME_ARGS (pos));

      /* save the segment for later, right before we push a new buffer so that
       * the caps are fixed and the next linked element can receive
       * the segment. */
      eventp = &parse->pending_segment;
      gst_event_replace (eventp, event);
      gst_event_unref (event);
      handled = TRUE;

      /* but finish the current segment */
      GST_DEBUG_OBJECT (parse, "draining current segment");
      gst_base_parse_drain (parse);
      gst_adapter_clear (parse->adapter);
      parse->priv->offset = offset;
      break;
    }

    case GST_EVENT_FLUSH_START:
      parse->priv->flushing = TRUE;
      handled = gst_pad_push_event (parse->srcpad, event);
      /* Wait for _chain() to exit by taking the srcpad STREAM_LOCK */
      GST_PAD_STREAM_LOCK (parse->srcpad);
      GST_PAD_STREAM_UNLOCK (parse->srcpad);

      break;

    case GST_EVENT_FLUSH_STOP:
      gst_adapter_clear (parse->adapter);
      parse->priv->flushing = FALSE;
      parse->priv->discont = TRUE;
      break;

    case GST_EVENT_EOS:
      gst_base_parse_drain (parse);
      break;

    default:
      break;
  }

  return handled;
}


/**
 * gst_base_parse_src_event:
 * @pad: #GstPad that received the event.
 * @event: #GstEvent that was received.
 *
 * Handler for source pad events.
 *
 * Returns: TRUE if the event was handled.
 */
static gboolean
gst_base_parse_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseParse *parse;
  GstBaseParseClass *bclass;
  gboolean handled = FALSE;
  gboolean ret = TRUE;

  parse = GST_BASE_PARSE (gst_pad_get_parent (pad));
  bclass = GST_BASE_PARSE_GET_CLASS (parse);

  GST_DEBUG_OBJECT (parse, "event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  if (bclass->src_event)
    handled = bclass->src_event (parse, event);

  if (!handled)
    ret = gst_pad_event_default (pad, event);

  gst_object_unref (parse);
  return ret;
}


/**
 * gst_base_parse_src_eventfunc:
 * @parse: #GstBaseParse.
 * @event: #GstEvent that was received.
 *
 * Default srcpad event handler.
 *
 * Returns: TRUE if the event was handled and can be dropped.
 */
static gboolean
gst_base_parse_src_eventfunc (GstBaseParse * parse, GstEvent * event)
{
  gboolean handled = FALSE;
  GstBaseParseClass *bclass;

  bclass = GST_BASE_PARSE_GET_CLASS (parse);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      if (bclass->is_seekable (parse)) {
        handled = gst_base_parse_handle_seek (parse, event);
        gst_event_unref (event);
      }
      break;
    }
    default:
      break;
  }
  return handled;
}


/**
 * gst_base_parse_is_seekable:
 * @parse: #GstBaseParse.
 *
 * Default handler for is_seekable.
 *
 * Returns: Always TRUE.
 */
static gboolean
gst_base_parse_is_seekable (GstBaseParse * parse)
{
  return TRUE;
}


/**
 * gst_base_parse_handle_and_push_buffer:
 * @parse: #GstBaseParse.
 * @klass: #GstBaseParseClass.
 * @buffer: #GstBuffer.
 *
 * Parses the frame from given buffer and pushes it forward. Also performs
 * timestamp handling and checks the segment limits.
 *
 * This is called with srcpad STREAM_LOCK held.
 *
 * Returns: #GstFlowReturn
 */
static GstFlowReturn
gst_base_parse_handle_and_push_buffer (GstBaseParse * parse,
    GstBaseParseClass * klass, GstBuffer * buffer)
{
  GstFlowReturn ret;

  if (parse->priv->discont) {
    GST_DEBUG_OBJECT (parse, "marking DISCONT");
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    parse->priv->discont = FALSE;
  }

  ret = klass->parse_frame (parse, buffer);

  /* FIXME: Check the output buffer for any missing metadata,
   *        keep track of timestamp and calculate everything possible
   *        if not set already */

  /* First buffers are dropped, this means that the subclass needs more
   * frames to decide on the format and queues them internally */
  if (ret == GST_BASE_PARSE_FLOW_DROPPED && !GST_PAD_CAPS (parse->srcpad)) {
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  /* convert internal flow to OK and mark discont for the next buffer. */
  if (ret == GST_BASE_PARSE_FLOW_DROPPED) {
    parse->priv->discont = TRUE;
    ret = GST_FLOW_OK;

    gst_buffer_unref (buffer);

    return ret;
  } else if (ret != GST_FLOW_OK) {
    return ret;
  }

  return gst_base_parse_push_buffer (parse, buffer);
}

/**
 * gst_base_parse_push_buffer:
 * @parse: #GstBaseParse.
 * @buffer: #GstBuffer.
 *
 * Pushes the buffer downstream, sends any pending events and
 * does some timestamp and segment handling.
 *
 * This must be called with srcpad STREAM_LOCK held.
 *
 * Returns: #GstFlowReturn
 */
GstFlowReturn
gst_base_parse_push_buffer (GstBaseParse * parse, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime last_stop = GST_CLOCK_TIME_NONE;

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    last_stop = GST_BUFFER_TIMESTAMP (buffer);
  if (last_stop != GST_CLOCK_TIME_NONE && GST_BUFFER_DURATION_IS_VALID (buffer))
    last_stop += GST_BUFFER_DURATION (buffer);

  /* should have caps by now */
  g_return_val_if_fail (GST_PAD_CAPS (parse->srcpad), GST_FLOW_ERROR);

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (parse->srcpad));

  /* and should then also be linked downstream, so safe to send some events */
  if (parse->priv->pad_mode == GST_ACTIVATE_PULL) {
    if (G_UNLIKELY (parse->close_segment)) {
      GST_DEBUG_OBJECT (parse, "loop sending close segment");
      gst_pad_push_event (parse->srcpad, parse->close_segment);
      parse->close_segment = NULL;
    }

    if (G_UNLIKELY (parse->pending_segment)) {
      GST_DEBUG_OBJECT (parse, "loop push pending segment");
      gst_pad_push_event (parse->srcpad, parse->pending_segment);
      parse->pending_segment = NULL;
    }
  } else {
    if (G_UNLIKELY (parse->pending_segment)) {
      GST_DEBUG_OBJECT (parse, "chain pushing a pending segment");
      gst_pad_push_event (parse->srcpad, parse->pending_segment);
      parse->pending_segment = NULL;
    }
  }

  if (G_UNLIKELY (parse->priv->pending_events)) {
    GList *l;

    for (l = parse->priv->pending_events; l != NULL; l = l->next) {
      gst_pad_push_event (parse->srcpad, GST_EVENT (l->data));
    }
    g_list_free (parse->priv->pending_events);
    parse->priv->pending_events = NULL;
  }

  /* TODO: Add to seek table */

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer) &&
      GST_CLOCK_TIME_IS_VALID (parse->segment.stop) &&
      GST_BUFFER_TIMESTAMP (buffer) > parse->segment.stop) {
    GST_LOG_OBJECT (parse, "Dropped frame, after segment");
    gst_buffer_unref (buffer);
  } else if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer) &&
      GST_BUFFER_DURATION_IS_VALID (buffer) &&
      GST_CLOCK_TIME_IS_VALID (parse->segment.start) &&
      GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer)
      < parse->segment.start) {
    /* FIXME: subclass needs way to override the start as downstream might
     * need frames before for proper decoding */
    GST_LOG_OBJECT (parse, "Dropped frame, before segment");
    gst_buffer_unref (buffer);
  } else {
    ret = gst_pad_push (parse->srcpad, buffer);
    GST_LOG_OBJECT (parse, "frame (%d bytes) pushed: %d",
        GST_BUFFER_SIZE (buffer), ret);
  }

  /* Update current running segment position */
  if (ret == GST_FLOW_OK && last_stop != GST_CLOCK_TIME_NONE)
    gst_segment_set_last_stop (&parse->segment, GST_FORMAT_TIME, last_stop);

  return ret;
}


/**
 * gst_base_parse_drain:
 * @parse: #GstBaseParse.
 *
 * Drains the adapter until it is empty. It decreases the min_frame_size to
 * match the current adapter size and calls chain method until the adapter
 * is emptied or chain returns with error.
 */
static void
gst_base_parse_drain (GstBaseParse * parse)
{
  guint avail;

  GST_DEBUG_OBJECT (parse, "draining");

  for (;;) {
    avail = gst_adapter_available (parse->adapter);
    if (!avail)
      break;

    gst_base_parse_set_min_frame_size (parse, avail);
    if (gst_base_parse_chain (parse->sinkpad, NULL) != GST_FLOW_OK) {
      break;
    }

    /* nothing changed, maybe due to truncated frame; break infinite loop */
    if (avail == gst_adapter_available (parse->adapter)) {
      GST_DEBUG_OBJECT (parse, "no change during draining; flushing");
      gst_adapter_clear (parse->adapter);
    }
  }
}


/**
 * gst_base_parse_chain:
 * @pad: #GstPad.
 * @buffer: #GstBuffer.
 *
 * Returns: #GstFlowReturn.
 */
static GstFlowReturn
gst_base_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBaseParseClass *bclass;
  GstBaseParse *parse;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf = NULL;
  GstBuffer *tmpbuf = NULL;
  guint fsize = 0;
  gint skip = -1;
  const guint8 *data;
  guint min_size;

  parse = GST_BASE_PARSE (GST_OBJECT_PARENT (pad));
  bclass = GST_BASE_PARSE_GET_CLASS (parse);

  if (G_LIKELY (buffer)) {
    GST_LOG_OBJECT (parse, "buffer size: %d, offset = %" G_GINT64_FORMAT,
        GST_BUFFER_SIZE (buffer), GST_BUFFER_OFFSET (buffer));
    if (G_UNLIKELY (parse->priv->passthrough)) {
      buffer = gst_buffer_make_metadata_writable (buffer);
      return gst_base_parse_push_buffer (parse, buffer);
    } else
      gst_adapter_push (parse->adapter, buffer);
  }

  /* Parse and push as many frames as possible */
  /* Stop either when adapter is empty or we are flushing */
  while (!parse->priv->flushing) {
    tmpbuf = gst_buffer_new ();

    /* Synchronization loop */
    for (;;) {
      GST_BASE_PARSE_LOCK (parse);
      min_size = parse->priv->min_frame_size;
      GST_BASE_PARSE_UNLOCK (parse);

      /* Collect at least min_frame_size bytes */
      if (gst_adapter_available (parse->adapter) < min_size) {
        GST_DEBUG_OBJECT (parse, "not enough data available (only %d bytes)",
            gst_adapter_available (parse->adapter));
        gst_buffer_unref (tmpbuf);
        goto done;
      }

      data = gst_adapter_peek (parse->adapter, min_size);
      GST_BUFFER_DATA (tmpbuf) = (guint8 *) data;
      GST_BUFFER_SIZE (tmpbuf) = min_size;
      GST_BUFFER_OFFSET (tmpbuf) = parse->priv->offset;
      GST_BUFFER_FLAG_SET (tmpbuf, GST_MINI_OBJECT_FLAG_READONLY);

      if (parse->priv->discont) {
        GST_DEBUG_OBJECT (parse, "marking DISCONT");
        GST_BUFFER_FLAG_SET (tmpbuf, GST_BUFFER_FLAG_DISCONT);
      }

      skip = -1;
      if (bclass->check_valid_frame (parse, tmpbuf, &fsize, &skip)) {
        if (gst_adapter_available (parse->adapter) < fsize) {
          GST_DEBUG_OBJECT (parse,
              "found valid frame but not enough data available (only %d bytes)",
              gst_adapter_available (parse->adapter));
          gst_buffer_unref (tmpbuf);
          goto done;
        }
        break;
      }
      if (skip > 0) {
        GST_LOG_OBJECT (parse, "finding sync, skipping %d bytes", skip);
        gst_adapter_flush (parse->adapter, skip);
        parse->priv->offset += skip;
      } else if (skip == -1) {
        /* subclass didn't touch this value. By default we skip 1 byte */
        GST_LOG_OBJECT (parse, "finding sync, skipping 1 byte");
        gst_adapter_flush (parse->adapter, 1);
        parse->priv->offset++;
      }
      /* There is a possibility that subclass set the skip value to zero.
         This means that it has probably found a frame but wants to ask
         more data (by increasing the min_size) to be sure of this. */
    }
    gst_buffer_unref (tmpbuf);
    tmpbuf = NULL;

    if (skip > 0) {
      /* Subclass found the sync, but still wants to skip some data */
      GST_LOG_OBJECT (parse, "skipping %d bytes", skip);
      gst_adapter_flush (parse->adapter, skip);
      parse->priv->offset += skip;
    }

    /* Grab lock to prevent a race with FLUSH_START handler */
    GST_PAD_STREAM_LOCK (parse->srcpad);

    /* FLUSH_START event causes the "flushing" flag to be set. In this
     * case we can leave the frame pushing loop */
    if (parse->priv->flushing) {
      GST_PAD_STREAM_UNLOCK (parse->srcpad);
      break;
    }

    /* FIXME: Would it be more efficient to make a subbuffer instead? */
    outbuf = gst_adapter_take_buffer (parse->adapter, fsize);

    /* Subclass may want to know the data offset */
    GST_BUFFER_OFFSET (outbuf) = parse->priv->offset;
    parse->priv->offset += fsize;

    ret = gst_base_parse_handle_and_push_buffer (parse, bclass, outbuf);
    GST_PAD_STREAM_UNLOCK (parse->srcpad);

    if (ret != GST_FLOW_OK) {
      GST_LOG_OBJECT (parse, "push returned %d", ret);
      break;
    }
  }

done:
  GST_LOG_OBJECT (parse, "chain leaving");
  return ret;
}

static GstFlowReturn
gst_base_parse_pull_range (GstBaseParse * parse, guint size,
    GstBuffer ** buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;

  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);

  /* Caching here actually makes much less difference than one would expect.
   * We do it mainly to avoid pulling buffers of 1 byte all the time */
  if (parse->priv->cache) {
    guint64 cache_offset = GST_BUFFER_OFFSET (parse->priv->cache);
    guint cache_size = GST_BUFFER_SIZE (parse->priv->cache);

    if (cache_offset <= parse->priv->offset &&
        (parse->priv->offset + size) < (cache_offset + cache_size)) {
      *buffer = gst_buffer_create_sub (parse->priv->cache,
          parse->priv->offset - cache_offset, size);
      GST_BUFFER_OFFSET (*buffer) = parse->priv->offset;
      return GST_FLOW_OK;
    }
    /* not enough data in the cache, free cache and get a new one */
    gst_buffer_unref (parse->priv->cache);
    parse->priv->cache = NULL;
  }

  /* refill the cache */
  ret =
      gst_pad_pull_range (parse->sinkpad, parse->priv->offset, MAX (size,
          64 * 1024), &parse->priv->cache);
  if (ret != GST_FLOW_OK) {
    parse->priv->cache = NULL;
    return ret;
  }

  if (GST_BUFFER_SIZE (parse->priv->cache) >= size) {
    *buffer = gst_buffer_create_sub (parse->priv->cache, 0, size);
    GST_BUFFER_OFFSET (*buffer) = parse->priv->offset;
    return GST_FLOW_OK;
  }

  /* Not possible to get enough data, try a last time with
   * requesting exactly the size we need */
  gst_buffer_unref (parse->priv->cache);
  parse->priv->cache = NULL;

  ret = gst_pad_pull_range (parse->sinkpad, parse->priv->offset, size,
      &parse->priv->cache);

  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (parse, "pull_range returned %d", ret);
    *buffer = NULL;
    return ret;
  }

  if (GST_BUFFER_SIZE (parse->priv->cache) < size) {
    GST_WARNING_OBJECT (parse, "Dropping short buffer at offset %"
        G_GUINT64_FORMAT ": wanted %u bytes, got %u bytes", parse->priv->offset,
        size, GST_BUFFER_SIZE (parse->priv->cache));

    gst_buffer_unref (parse->priv->cache);
    parse->priv->cache = NULL;

    *buffer = NULL;
    return GST_FLOW_UNEXPECTED;
  }

  *buffer = gst_buffer_create_sub (parse->priv->cache, 0, size);
  GST_BUFFER_OFFSET (*buffer) = parse->priv->offset;

  return GST_FLOW_OK;
}

/**
 * gst_base_parse_loop:
 * @pad: GstPad
 *
 * Loop that is used in pull mode to retrieve data from upstream.
 */
static void
gst_base_parse_loop (GstPad * pad)
{
  GstBaseParse *parse;
  GstBaseParseClass *klass;
  GstBuffer *buffer, *outbuf;
  gboolean ret = FALSE;
  guint fsize = 0, min_size;
  gint skip = 0;

  parse = GST_BASE_PARSE (gst_pad_get_parent (pad));
  klass = GST_BASE_PARSE_GET_CLASS (parse);

  /* TODO: Check if we reach segment stop limit */

  while (TRUE) {

    GST_BASE_PARSE_LOCK (parse);
    min_size = parse->priv->min_frame_size;
    GST_BASE_PARSE_UNLOCK (parse);

    ret = gst_base_parse_pull_range (parse, min_size, &buffer);

    if (ret == GST_FLOW_UNEXPECTED)
      goto eos;
    else if (ret != GST_FLOW_OK)
      goto need_pause;

    if (parse->priv->discont) {
      GST_DEBUG_OBJECT (parse, "marking DISCONT");
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    }

    skip = -1;
    if (klass->check_valid_frame (parse, buffer, &fsize, &skip)) {
      break;
    }
    if (skip > 0) {
      GST_LOG_OBJECT (parse, "finding sync, skipping %d bytes", skip);
      parse->priv->offset += skip;
    } else if (skip == -1) {
      GST_LOG_OBJECT (parse, "finding sync, skipping 1 byte");
      parse->priv->offset++;
    }
    GST_DEBUG_OBJECT (parse, "finding sync...");
    gst_buffer_unref (buffer);
  }

  if (fsize <= GST_BUFFER_SIZE (buffer)) {
    outbuf = gst_buffer_create_sub (buffer, 0, fsize);
    GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET (buffer);
    gst_buffer_unref (buffer);
  } else {
    gst_buffer_unref (buffer);
    ret = gst_base_parse_pull_range (parse, fsize, &outbuf);

    if (ret == GST_FLOW_UNEXPECTED)
      goto eos;
    else if (ret != GST_FLOW_OK)
      goto need_pause;
  }

  parse->priv->offset += fsize;

  /* Does the subclass want to skip too? */
  if (skip > 0)
    parse->priv->offset += skip;

  /* This always unrefs the outbuf, even if error occurs */
  ret = gst_base_parse_handle_and_push_buffer (parse, klass, outbuf);

  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (parse, "flow: %s", gst_flow_get_name (ret));
    if (GST_FLOW_IS_FATAL (ret)) {
      GST_ELEMENT_ERROR (parse, STREAM, FAILED, (NULL),
          ("streaming task paused, reason: %s", gst_flow_get_name (ret)));
      gst_pad_push_event (parse->srcpad, gst_event_new_eos ());
    }
    goto need_pause;
  }

  gst_object_unref (parse);
  return;

need_pause:
  {
    GST_LOG_OBJECT (parse, "pausing task");
    gst_pad_pause_task (pad);
    gst_object_unref (parse);
    return;
  }
eos:
  {
    GST_LOG_OBJECT (parse, "pausing task %d", ret);
    gst_pad_push_event (parse->srcpad, gst_event_new_eos ());
    gst_pad_pause_task (pad);
    gst_object_unref (parse);
    return;
  }
}


/**
 * gst_base_parse_sink_activate:
 * @sinkpad: #GstPad to be activated.
 *
 * Returns: TRUE if activation succeeded.
 */
static gboolean
gst_base_parse_sink_activate (GstPad * sinkpad)
{
  GstBaseParse *parse;
  gboolean result = TRUE;

  parse = GST_BASE_PARSE (gst_pad_get_parent (sinkpad));

  GST_DEBUG_OBJECT (parse, "sink activate");

  if (gst_pad_check_pull_range (sinkpad)) {
    GST_DEBUG_OBJECT (parse, "trying to activate in pull mode");
    result = gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    GST_DEBUG_OBJECT (parse, "trying to activate in push mode");
    result = gst_pad_activate_push (sinkpad, TRUE);
  }

  GST_DEBUG_OBJECT (parse, "sink activate return %d", result);
  gst_object_unref (parse);
  return result;
}


/**
 * gst_base_parse_activate:
 * @parse: #GstBaseParse.
 * @active: TRUE if element will be activated, FALSE if disactivated.
 *
 * Returns: TRUE if the operation succeeded.
 */
static gboolean
gst_base_parse_activate (GstBaseParse * parse, gboolean active)
{
  GstBaseParseClass *klass;
  gboolean result = FALSE;

  GST_DEBUG_OBJECT (parse, "activate");

  klass = GST_BASE_PARSE_GET_CLASS (parse);

  if (active) {
    if (parse->priv->pad_mode == GST_ACTIVATE_NONE && klass->start)
      result = klass->start (parse);

    GST_OBJECT_LOCK (parse);
    gst_segment_init (&parse->segment, GST_FORMAT_TIME);
    parse->priv->duration = -1;
    parse->priv->discont = FALSE;
    parse->priv->flushing = FALSE;
    parse->priv->offset = 0;

    if (parse->pending_segment)
      gst_event_unref (parse->pending_segment);

    parse->pending_segment =
        gst_event_new_new_segment (FALSE, parse->segment.rate,
        parse->segment.format,
        parse->segment.start, parse->segment.stop, parse->segment.last_stop);

    GST_OBJECT_UNLOCK (parse);
  } else {
    /* We must make sure streaming has finished before resetting things
     * and calling the ::stop vfunc */
    GST_PAD_STREAM_LOCK (parse->sinkpad);
    GST_PAD_STREAM_UNLOCK (parse->sinkpad);

    if (parse->priv->pad_mode != GST_ACTIVATE_NONE && klass->stop)
      result = klass->stop (parse);

    g_list_foreach (parse->priv->pending_events, (GFunc) gst_mini_object_unref,
        NULL);
    g_list_free (parse->priv->pending_events);
    parse->priv->pending_events = NULL;

    if (parse->priv->cache) {
      gst_buffer_unref (parse->priv->cache);
      parse->priv->cache = NULL;
    }

    parse->priv->pad_mode = GST_ACTIVATE_NONE;
  }
  GST_DEBUG_OBJECT (parse, "activate: %d", result);
  return result;
}


/**
 * gst_base_parse_sink_activate_push:
 * @pad: #GstPad to be (de)activated.
 * @active: TRUE when activating, FALSE when deactivating.
 *
 * Returns: TRUE if (de)activation succeeded.
 */
static gboolean
gst_base_parse_sink_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstBaseParse *parse;

  parse = GST_BASE_PARSE (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (parse, "sink activate push");

  result = gst_base_parse_activate (parse, active);

  if (result)
    parse->priv->pad_mode = active ? GST_ACTIVATE_PUSH : GST_ACTIVATE_NONE;

  GST_DEBUG_OBJECT (parse, "sink activate push: %d", result);

  gst_object_unref (parse);
  return result;
}


/**
 * gst_base_parse_sink_activate_pull:
 * @sinkpad: #GstPad to be (de)activated.
 * @active: TRUE when activating, FALSE when deactivating.
 *
 * Returns: TRUE if (de)activation succeeded.
 */
static gboolean
gst_base_parse_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  gboolean result = FALSE;
  GstBaseParse *parse;

  parse = GST_BASE_PARSE (gst_pad_get_parent (sinkpad));

  GST_DEBUG_OBJECT (parse, "activate pull");

  result = gst_base_parse_activate (parse, active);

  if (result) {
    if (active) {
      result &= gst_pad_start_task (sinkpad,
          (GstTaskFunction) gst_base_parse_loop, sinkpad);
    } else {
      result &= gst_pad_stop_task (sinkpad);
    }
  }

  if (result)
    parse->priv->pad_mode = active ? GST_ACTIVATE_PULL : GST_ACTIVATE_NONE;

  GST_DEBUG_OBJECT (parse, "sink activate pull: %d", result);

  gst_object_unref (parse);
  return result;
}


/**
 * gst_base_parse_set_duration:
 * @parse: #GstBaseParse.
 * @fmt: #GstFormat.
 * @duration: duration value.
 *
 * Sets the duration of the currently playing media. Subclass can use this
 * when it notices a change in the media duration.
 */
void
gst_base_parse_set_duration (GstBaseParse * parse,
    GstFormat fmt, gint64 duration)
{
  g_return_if_fail (parse != NULL);

  GST_BASE_PARSE_LOCK (parse);
  if (duration != parse->priv->duration) {
    GstMessage *m;

    m = gst_message_new_duration (GST_OBJECT (parse), fmt, duration);
    gst_element_post_message (GST_ELEMENT (parse), m);

    /* TODO: what about duration tag? */
  }
  parse->priv->duration = duration;
  parse->priv->duration_fmt = fmt;
  GST_DEBUG_OBJECT (parse, "set duration: %" G_GINT64_FORMAT, duration);
  GST_BASE_PARSE_UNLOCK (parse);
}


/**
 * gst_base_parse_set_min_frame_size:
 * @parse: #GstBaseParse.
 * @min_size: Minimum size of the data that this base class should give to
 *            subclass.
 *
 * Subclass can use this function to tell the base class that it needs to
 * give at least #min_size buffers.
 */
void
gst_base_parse_set_min_frame_size (GstBaseParse * parse, guint min_size)
{
  g_return_if_fail (parse != NULL);

  GST_BASE_PARSE_LOCK (parse);
  parse->priv->min_frame_size = min_size;
  GST_LOG_OBJECT (parse, "set frame_min_size: %d", min_size);
  GST_BASE_PARSE_UNLOCK (parse);
}

/**
 * gst_base_transform_set_passthrough:
 * @trans: the #GstBaseTransform to set
 * @passthrough: boolean indicating passthrough mode.
 *
 * Set passthrough mode for this filter by default. This is mostly
 * useful for filters that do not care about negotiation.
 *
 * Always TRUE for filters which don't implement either a transform
 * or transform_ip method.
 *
 * MT safe.
 */
void
gst_base_parse_set_passthrough (GstBaseParse * parse, gboolean passthrough)
{
  g_return_if_fail (parse != NULL);

  GST_BASE_PARSE_LOCK (parse);
  parse->priv->passthrough = passthrough;
  GST_LOG_OBJECT (parse, "set passthrough: %d", passthrough);
  GST_BASE_PARSE_UNLOCK (parse);
}


/**
 * gst_base_parse_get_querytypes:
 * @pad: GstPad
 *
 * Returns: A table of #GstQueryType items describing supported query types.
 */
static const GstQueryType *
gst_base_parse_get_querytypes (GstPad * pad)
{
  static const GstQueryType list[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_FORMATS,
    GST_QUERY_SEEKING,
    GST_QUERY_CONVERT,
    0
  };

  return list;
}


/**
 * gst_base_parse_query:
 * @pad: #GstPad.
 * @query: #GstQuery.
 *
 * Returns: TRUE on success.
 */
static gboolean
gst_base_parse_query (GstPad * pad, GstQuery * query)
{
  GstBaseParse *parse;
  GstBaseParseClass *klass;
  gboolean res = FALSE;

  parse = GST_BASE_PARSE (GST_PAD_PARENT (pad));
  klass = GST_BASE_PARSE_GET_CLASS (parse);

  /* If subclass doesn't provide conversion function we can't reply
     to the query either */
  if (!klass->convert) {
    return FALSE;
  }

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      gint64 dest_value;
      GstFormat format;

      GST_DEBUG_OBJECT (parse, "position query");

      gst_query_parse_position (query, &format, NULL);

      g_mutex_lock (parse->parse_lock);

      if (format == GST_FORMAT_BYTES) {
        dest_value = parse->priv->offset;
        res = TRUE;
      } else if (format == parse->segment.format &&
          GST_CLOCK_TIME_IS_VALID (parse->segment.last_stop)) {
        dest_value = parse->segment.last_stop;
        res = TRUE;
      } else {
        /* priv->offset is updated in both PUSH/PULL modes */
        res = klass->convert (parse, GST_FORMAT_BYTES, parse->priv->offset,
            format, &dest_value);
      }
      g_mutex_unlock (parse->parse_lock);

      if (res)
        gst_query_set_position (query, format, dest_value);
      else
        res = gst_pad_query_default (pad, query);

      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      gint64 dest_value;

      GST_DEBUG_OBJECT (parse, "duration query");

      gst_query_parse_duration (query, &format, NULL);

      g_mutex_lock (parse->parse_lock);

      if (format == GST_FORMAT_BYTES) {
        res = gst_pad_query_peer_duration (parse->sinkpad, &format,
            &dest_value);
      } else if (parse->priv->duration != -1 &&
          format == parse->priv->duration_fmt) {
        dest_value = parse->priv->duration;
        res = TRUE;
      } else if (parse->priv->duration != -1) {
        res = klass->convert (parse, parse->priv->duration_fmt,
            parse->priv->duration, format, &dest_value);
      }

      g_mutex_unlock (parse->parse_lock);

      if (res)
        gst_query_set_duration (query, format, dest_value);
      else
        res = gst_pad_query_default (pad, query);
      break;
    }
    case GST_QUERY_SEEKING:
    {
      GstFormat fmt;
      gboolean seekable = FALSE;

      GST_DEBUG_OBJECT (parse, "seeking query");

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);

      if (fmt != GST_FORMAT_TIME) {
        return gst_pad_query_default (pad, query);
      }

      seekable = klass->is_seekable (parse);

      /* TODO: could this duration be calculated/converted if subclass
         hasn't given it? */
      gst_query_set_seeking (query, GST_FORMAT_TIME, seekable, 0,
          (parse->priv->duration == -1) ?
          GST_CLOCK_TIME_NONE : parse->priv->duration);

      GST_DEBUG_OBJECT (parse, "seekable: %d", seekable);
      res = TRUE;
      break;
    }
    case GST_QUERY_FORMATS:
      gst_query_set_formatsv (query, 3, fmtlist);
      res = TRUE;
      break;

    case GST_QUERY_CONVERT:
    {
      GstFormat src_format, dest_format;
      gint64 src_value, dest_value;

      gst_query_parse_convert (query, &src_format, &src_value,
          &dest_format, &dest_value);

      /* FIXME: hm? doesn't make sense 
       * We require all those values to be given
       if (src_format && src_value && dest_format && dest_value ) { */
      res = klass->convert (parse, src_format, src_value,
          dest_format, &dest_value);
      if (res) {
        gst_query_set_convert (query, src_format, src_value,
            dest_format, dest_value);
      }
      /*} */
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  return res;
}


/**
 * gst_base_parse_handle_seek:
 * @parse: #GstBaseParse.
 * @event: #GstEvent.
 *
 * Returns: TRUE if seek succeeded.
 */
static gboolean
gst_base_parse_handle_seek (GstBaseParse * parse, GstEvent * event)
{
  GstBaseParseClass *klass;
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type = GST_SEEK_TYPE_NONE, stop_type;
  gboolean flush, update, res = TRUE;
  gint64 cur, stop, seekpos;
  GstSegment seeksegment = { 0, };
  GstFormat dstformat;

  klass = GST_BASE_PARSE_GET_CLASS (parse);

  gst_event_parse_seek (event, &rate, &format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  /* no negative rates yet */
  if (rate < 0.0)
    goto negative_rate;

  if (cur_type != GST_SEEK_TYPE_SET)
    goto wrong_type;

  /* For any format other than TIME, see if upstream handles
   * it directly or fail. For TIME, try upstream, but do it ourselves if
   * it fails upstream */
  if (format != GST_FORMAT_TIME) {
    return gst_pad_push_event (parse->sinkpad, event);
  } else {
    gst_event_ref (event);
    if (gst_pad_push_event (parse->sinkpad, event)) {
      gst_event_unref (event);
      return TRUE;
    }
  }

  /* get flush flag */
  flush = flags & GST_SEEK_FLAG_FLUSH;

  dstformat = GST_FORMAT_BYTES;
  if (!gst_pad_query_convert (parse->srcpad, format, cur, &dstformat, &seekpos)) {
    GST_DEBUG_OBJECT (parse, "conversion failed");
    return FALSE;
  }

  GST_DEBUG_OBJECT (parse,
      "seek position %" G_GINT64_FORMAT " in bytes: %" G_GINT64_FORMAT, cur,
      seekpos);

  if (parse->priv->pad_mode == GST_ACTIVATE_PULL) {
    gint64 last_stop;

    GST_DEBUG_OBJECT (parse, "seek in PULL mode");

    if (flush) {
      if (parse->srcpad) {
        GST_DEBUG_OBJECT (parse, "sending flush start");
        gst_pad_push_event (parse->srcpad, gst_event_new_flush_start ());
      }
    } else {
      gst_pad_pause_task (parse->sinkpad);
    }

    /* we should now be able to grab the streaming thread because we stopped it
     * with the above flush/pause code */
    GST_PAD_STREAM_LOCK (parse->sinkpad);

    /* save current position */
    last_stop = parse->segment.last_stop;
    GST_DEBUG_OBJECT (parse, "stopped streaming at %" G_GINT64_FORMAT,
        last_stop);

    /* copy segment, we need this because we still need the old
     * segment when we close the current segment. */
    memcpy (&seeksegment, &parse->segment, sizeof (GstSegment));

    GST_DEBUG_OBJECT (parse, "configuring seek");
    gst_segment_set_seek (&seeksegment, rate, format, flags,
        cur_type, cur, stop_type, stop, &update);

    /* figure out the last position we need to play. If it's configured (stop !=
     * -1), use that, else we play until the total duration of the file */
    if ((stop = seeksegment.stop) == -1)
      stop = seeksegment.duration;

    parse->priv->offset = seekpos;

    /* prepare for streaming again */
    if (flush) {
      GST_DEBUG_OBJECT (parse, "sending flush stop");
      gst_pad_push_event (parse->srcpad, gst_event_new_flush_stop ());
    } else {
      if (parse->close_segment)
        gst_event_unref (parse->close_segment);

      parse->close_segment = gst_event_new_new_segment (TRUE,
          parse->segment.rate, parse->segment.format,
          parse->segment.accum, parse->segment.last_stop, parse->segment.accum);

      /* keep track of our last_stop */
      seeksegment.accum = parse->segment.last_stop;

      GST_DEBUG_OBJECT (parse, "Created close seg format %d, "
          "start = %" GST_TIME_FORMAT ", stop = %" GST_TIME_FORMAT
          ", pos = %" GST_TIME_FORMAT, format,
          GST_TIME_ARGS (parse->segment.accum),
          GST_TIME_ARGS (parse->segment.last_stop),
          GST_TIME_ARGS (parse->segment.accum));
    }

    memcpy (&parse->segment, &seeksegment, sizeof (GstSegment));

    /* store the newsegment event so it can be sent from the streaming thread. */
    if (parse->pending_segment)
      gst_event_unref (parse->pending_segment);

    /* This will be sent later in _loop() */
    parse->pending_segment =
        gst_event_new_new_segment (FALSE, parse->segment.rate,
        parse->segment.format,
        parse->segment.last_stop, stop, parse->segment.last_stop);

    GST_DEBUG_OBJECT (parse, "Created newseg format %d, "
        "start = %" GST_TIME_FORMAT ", stop = %" GST_TIME_FORMAT
        ", pos = %" GST_TIME_FORMAT, format,
        GST_TIME_ARGS (parse->segment.last_stop),
        GST_TIME_ARGS (stop), GST_TIME_ARGS (parse->segment.last_stop));

    /* mark discont if we are going to stream from another position. */
    if (last_stop != parse->segment.last_stop) {
      GST_DEBUG_OBJECT (parse,
          "mark DISCONT, we did a seek to another position");
      parse->priv->discont = TRUE;
    }

    /* Start streaming thread if paused */
    gst_pad_start_task (parse->sinkpad,
        (GstTaskFunction) gst_base_parse_loop, parse->sinkpad);

    GST_PAD_STREAM_UNLOCK (parse->sinkpad);
  } else {
    GstEvent *new_event;
    /* The only thing we need to do in PUSH-mode is to send the
       seek event (in bytes) to upstream. Segment / flush handling happens
       in corresponding src event handlers */
    GST_DEBUG_OBJECT (parse, "seek in PUSH mode");
    new_event = gst_event_new_seek (rate, GST_FORMAT_BYTES, flush,
        GST_SEEK_TYPE_SET, seekpos, stop_type, stop);

    res = gst_pad_push_event (parse->sinkpad, new_event);
  }

done:
  return res;

  /* ERRORS */
negative_rate:
  {
    GST_DEBUG_OBJECT (parse, "negative playback rates are not supported yet.");
    res = FALSE;
    goto done;
  }
wrong_type:
  {
    GST_DEBUG_OBJECT (parse, "unsupported seek type.");
    res = FALSE;
    goto done;
  }
}


/**
 * gst_base_parse_sink_setcaps:
 * @pad: #GstPad.
 * @caps: #GstCaps.
 *
 * Returns: TRUE if caps were accepted.
 */
static gboolean
gst_base_parse_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseParse *parse;
  GstBaseParseClass *klass;
  gboolean res = TRUE;

  parse = GST_BASE_PARSE (GST_PAD_PARENT (pad));
  klass = GST_BASE_PARSE_GET_CLASS (parse);

  GST_DEBUG_OBJECT (parse, "caps: %" GST_PTR_FORMAT, caps);

  if (klass->set_sink_caps)
    res = klass->set_sink_caps (parse, caps);

  parse->negotiated = res;
  return res && gst_pad_set_caps (pad, caps);
}
