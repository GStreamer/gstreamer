/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstevent.c: GstEvent subsystem
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
 * SECTION:gstevent
 * @short_description: Structure describing events that are passed up and down
 *                     a pipeline
 * @see_also: #GstPad, #GstElement
 *
 * The event classes are used to construct and query events.
 *
 * Events are usually created with gst_event_new_*() which takes the extra
 * event parameters as arguments.
 * Events can be parsed with their respective gst_event_parse_*() functions.
 * The event should be unreffed with gst_event_unref().
 *
 * Events are passed between elements in parallel to the data stream. Some events
 * are serialized with buffers, others are not. Some events only travel downstream,
 * others only upstream. Some events can travel both upstream and downstream. 
 * 
 * The events are used to signal special conditions in the datastream such as EOS
 * or the start of a new segment. Events are also used to flush the pipeline of
 * any pending data.
 *
 * Most of the event API is used inside plugins. The application usually only 
 * constructs and uses the seek event API when it wants to perform a seek in the
 * pipeline. 

 * gst_event_new_seek() is usually used to create a seek event and it takes
 * the needed parameters for a seek event.
 * <example>
 * <title>performing a seek on a pipeline</title>
 *   <programlisting>
 *   GstEvent *event;
 *   gboolean result;
 *   ...
 *   // construct a seek event to play the media from second 2 to 5, flush
 *   // the pipeline to decrease latency.
 *   event = gst_event_new_seek (1.0, 
 *      GST_FORMAT_TIME, 
 *   	GST_SEEK_FLAG_FLUSH,
 *   	GST_SEEK_TYPE_SET, 2 * GST_SECOND,
 *   	GST_SEEK_TYPE_SET, 5 * GST_SECOND);
 *   ...
 *   result = gst_element_send_event (pipeline, event);
 *   if (!result)
 *     g_warning ("seek failed");
 *   ...
 *   </programlisting>
 * </example>
 *
 * Last reviewed on 2005-11-23 (0.9.5)
 */

#include <string.h>             /* memcpy */

#include "gst_private.h"

#include "gstinfo.h"
#include "gstevent.h"
#include "gstenumtypes.h"
#include "gstutils.h"

static void gst_event_init (GTypeInstance * instance, gpointer g_class);
static void gst_event_class_init (gpointer g_class, gpointer class_data);
static void gst_event_finalize (GstEvent * event);
static GstEvent *_gst_event_copy (GstEvent * event);

void
_gst_event_initialize (void)
{
  gst_event_get_type ();
}

typedef struct
{
  gint type;
  gchar *name;
  GQuark quark;
} GstEventQuarks;

static GstEventQuarks event_quarks[] = {
  {GST_EVENT_UNKNOWN, "unknown", 0},
  {GST_EVENT_FLUSH_START, "flush-start", 0},
  {GST_EVENT_FLUSH_STOP, "flush-stop", 0},
  {GST_EVENT_EOS, "eos", 0},
  {GST_EVENT_NEWSEGMENT, "newsegment", 0},
  {GST_EVENT_TAG, "tag", 0},
  {GST_EVENT_BUFFERSIZE, "buffersize", 0},
  {GST_EVENT_QOS, "qos", 0},
  {GST_EVENT_SEEK, "seek", 0},
  {GST_EVENT_NAVIGATION, "navigation", 0},
  {GST_EVENT_CUSTOM_UPSTREAM, "custom-upstream", 0},
  {GST_EVENT_CUSTOM_DOWNSTREAM, "custom-downstream", 0},
  {GST_EVENT_CUSTOM_DOWNSTREAM_OOB, "custom-downstream-oob", 0},
  {GST_EVENT_CUSTOM_BOTH, "custom-both", 0},
  {GST_EVENT_CUSTOM_BOTH_OOB, "custom-both-oob", 0},

  {0, NULL, 0}
};

/**
 * gst_event_type_get_name:
 * @type: the event type
 *
 * Get a printable name for the given event type. Do not modify or free.
 *
 * Returns: a reference to the static name of the event.
 */
const gchar *
gst_event_type_get_name (GstEventType type)
{
  gint i;

  for (i = 0; event_quarks[i].name; i++) {
    if (type == event_quarks[i].type)
      return event_quarks[i].name;
  }
  return "unknown";
}

/**
 * gst_event_type_to_quark:
 * @type: the event type
 *
 * Get the unique quark for the given event type.
 *
 * Returns: the quark associated with the event type
 */
GQuark
gst_event_type_to_quark (GstEventType type)
{
  gint i;

  for (i = 0; event_quarks[i].name; i++) {
    if (type == event_quarks[i].type)
      return event_quarks[i].quark;
  }
  return 0;
}

/**
 * gst_event_type_get_flags:
 * @type: a #GstEventType
 *
 * Gets the #GstEventTypeFlags associated with @type.
 *
 * Returns: a #GstEventTypeFlags.
 */
GstEventTypeFlags
gst_event_type_get_flags (GstEventType type)
{
  GstEventTypeFlags ret;

  ret = type & ((1 << GST_EVENT_TYPE_SHIFT) - 1);

  return ret;
}

GType
gst_event_get_type (void)
{
  static GType _gst_event_type;
  int i;

  if (G_UNLIKELY (_gst_event_type == 0)) {
    static const GTypeInfo event_info = {
      sizeof (GstEventClass),
      NULL,
      NULL,
      gst_event_class_init,
      NULL,
      NULL,
      sizeof (GstEvent),
      0,
      gst_event_init,
      NULL
    };

    _gst_event_type = g_type_register_static (GST_TYPE_MINI_OBJECT,
        "GstEvent", &event_info, 0);

    for (i = 0; event_quarks[i].name; i++) {
      event_quarks[i].quark = g_quark_from_static_string (event_quarks[i].name);
    }
  }

  return _gst_event_type;
}

static void
gst_event_class_init (gpointer g_class, gpointer class_data)
{
  GstEventClass *event_class = GST_EVENT_CLASS (g_class);

  event_class->mini_object_class.copy =
      (GstMiniObjectCopyFunction) _gst_event_copy;
  event_class->mini_object_class.finalize =
      (GstMiniObjectFinalizeFunction) gst_event_finalize;
}

static void
gst_event_init (GTypeInstance * instance, gpointer g_class)
{
  GstEvent *event;

  event = GST_EVENT (instance);

  GST_EVENT_TIMESTAMP (event) = GST_CLOCK_TIME_NONE;
}

static void
gst_event_finalize (GstEvent * event)
{
  g_return_if_fail (event != NULL);
  g_return_if_fail (GST_IS_EVENT (event));

  GST_CAT_LOG (GST_CAT_EVENT, "freeing event %p type %s", event,
      gst_event_type_get_name (GST_EVENT_TYPE (event)));

  if (GST_EVENT_SRC (event)) {
    gst_object_unref (GST_EVENT_SRC (event));
    GST_EVENT_SRC (event) = NULL;
  }
  if (event->structure) {
    gst_structure_set_parent_refcount (event->structure, NULL);
    gst_structure_free (event->structure);
  }
}

static GstEvent *
_gst_event_copy (GstEvent * event)
{
  GstEvent *copy;

  copy = (GstEvent *) gst_mini_object_new (GST_TYPE_EVENT);

  GST_EVENT_TYPE (copy) = GST_EVENT_TYPE (event);
  GST_EVENT_TIMESTAMP (copy) = GST_EVENT_TIMESTAMP (event);

  if (GST_EVENT_SRC (event)) {
    GST_EVENT_SRC (copy) = gst_object_ref (GST_EVENT_SRC (event));
  }
  if (event->structure) {
    copy->structure = gst_structure_copy (event->structure);
    gst_structure_set_parent_refcount (copy->structure,
        &event->mini_object.refcount);
  }
  return copy;
}

static GstEvent *
gst_event_new (GstEventType type)
{
  GstEvent *event;

  event = (GstEvent *) gst_mini_object_new (GST_TYPE_EVENT);

  GST_CAT_DEBUG (GST_CAT_EVENT, "creating new event %p %s", event,
      gst_event_type_get_name (type));

  event->type = type;
  event->src = NULL;
  event->structure = NULL;

  return event;
}

/**
 * gst_event_new_custom:
 * @type: The type of the new event
 * @structure: The structure for the event. The event will take ownership of
 * the structure.
 *
 * Create a new custom-typed event. This can be used for anything not
 * handled by other event-specific functions to pass an event to another
 * element.
 *
 * Make sure to allocate an event type with the #GST_EVENT_MAKE_TYPE macro,
 * assigning a free number and filling in the correct direction and
 * serialization flags.
 *
 * New custom events can also be created by subclassing the event type if
 * needed.
 *
 * Returns: The new custom event.
 */
GstEvent *
gst_event_new_custom (GstEventType type, GstStructure * structure)
{
  GstEvent *event;

  event = gst_event_new (type);
  if (structure) {
    gst_structure_set_parent_refcount (structure, &event->mini_object.refcount);
    event->structure = structure;
  }
  return event;
}

/**
 * gst_event_get_structure:
 * @event: The #GstEvent.
 *
 * Access the structure of the event.
 *
 * Returns: The structure of the event. The structure is still
 * owned by the event, which means that you should not free it and
 * that the pointer becomes invalid when you free the event.
 *
 * MT safe.
 */
const GstStructure *
gst_event_get_structure (GstEvent * event)
{
  g_return_val_if_fail (GST_IS_EVENT (event), NULL);

  return event->structure;
}

/**
 * gst_event_new_flush_start:
 *
 * Allocate a new flush start event. The flush start event can be send
 * upstream and downstream and travels out-of-bounds with the dataflow.
 * It marks pads as being in a WRONG_STATE to process more data.
 *
 * Elements unlock and blocking functions and exit their streaming functions
 * as fast as possible.
 *
 * This event is typically generated after a seek to minimize the latency
 * after the seek.
 *
 * Returns: A new flush start event.
 */
GstEvent *
gst_event_new_flush_start (void)
{
  return gst_event_new (GST_EVENT_FLUSH_START);
}

/**
 * gst_event_new_flush_stop:
 *
 * Allocate a new flush stop event. The flush start event can be send
 * upstream and downstream and travels out-of-bounds with the dataflow.
 * It is typically send after sending a FLUSH_START event to make the
 * pads accept data again.
 *
 * Elements can process this event synchronized with the dataflow since
 * the preceeding FLUSH_START event stopped the dataflow.
 *
 * This event is typically generated to complete a seek and to resume
 * dataflow.
 *
 * Returns: A new flush stop event.
 */
GstEvent *
gst_event_new_flush_stop (void)
{
  return gst_event_new (GST_EVENT_FLUSH_STOP);
}

/**
 * gst_event_new_eos:
 *
 * Create a new EOS event. The eos event can only travel downstream
 * synchronized with the buffer flow. Elements that receive the EOS
 * event on a pad can return UNEXPECTED as a GstFlowReturn when data
 * after the EOS event arrives.
 *
 * The EOS event will travel up to the sink elements in the pipeline
 * which will then post the GST_MESSAGE_EOS on the bus.
 *
 * When all sinks have posted an EOS message, the EOS message is
 * forwarded to the application.
 *
 * Returns: The new EOS event.
 */
GstEvent *
gst_event_new_eos (void)
{
  return gst_event_new (GST_EVENT_EOS);
}

/**
 * gst_event_new_new_segment:
 * @update: is this segment an update to a previous one
 * @rate: a new rate for playback
 * @format: The format of the segment values
 * @start: the start value of the segment
 * @stop: the stop value of the segment
 * @position: stream position
 *
 * Allocate a new newsegment event with the given format/values tripplets.
 *
 * The newsegment event marks the range of buffers to be processed. All
 * data not within the segment range is not to be processed. This can be
 * used intelligently by plugins to use more efficient methods of skipping
 * unneeded packets.
 *
 * The stream time of the segment is used to convert the buffer timestamps
 * into the stream time again, this is usually done in sinks to report the
 * current stream_time. @stream_time cannot be -1.
 *
 * @start cannot be -1, @stop can be -1. If there
 * is a valid @stop given, it must be greater or equal than @start.
 *
 * After a newsegment event, the buffer stream time is calculated with:
 *
 *   stream_time + (TIMESTAMP(buf) - start) * ABS (rate)
 *
 * Returns: A new newsegment event.
 */
GstEvent *
gst_event_new_new_segment (gboolean update, gdouble rate, GstFormat format,
    gint64 start, gint64 stop, gint64 position)
{
  g_return_val_if_fail (rate != 0.0, NULL);

  if (format == GST_FORMAT_TIME) {
    GST_CAT_INFO (GST_CAT_EVENT,
        "creating newsegment update %d, rate %lf, format GST_FORMAT_TIME, "
        "start %" GST_TIME_FORMAT ", stop %" GST_TIME_FORMAT
        ", position %" GST_TIME_FORMAT,
        update, rate, GST_TIME_ARGS (start),
        GST_TIME_ARGS (stop), GST_TIME_ARGS (position));
  } else {
    GST_CAT_INFO (GST_CAT_EVENT,
        "creating newsegment update %d, rate %lf, format %d, "
        "start %" G_GINT64_FORMAT ", stop %" G_GINT64_FORMAT ", position %"
        G_GINT64_FORMAT, update, rate, format, start, stop, position);
  }
  g_return_val_if_fail (position != -1, NULL);
  g_return_val_if_fail (start != -1, NULL);
  if (stop != -1)
    g_return_val_if_fail (start <= stop, NULL);

  return gst_event_new_custom (GST_EVENT_NEWSEGMENT,
      gst_structure_new ("GstEventNewsegment",
          "update", G_TYPE_BOOLEAN, update,
          "rate", G_TYPE_DOUBLE, rate,
          "format", GST_TYPE_FORMAT, format,
          "start", G_TYPE_INT64, start,
          "stop", G_TYPE_INT64, stop,
          "position", G_TYPE_INT64, position, NULL));
}

/**
 * gst_event_parse_new_segment:
 * @event: The event to query
 * @update: A pointer to the update flag of the segment
 * @rate: A pointer to the rate of the segment
 * @format: A pointer to the format of the newsegment values
 * @start: A pointer to store the start value in
 * @stop: A pointer to store the stop value in
 * @position: A pointer to store the stream time in
 *
 * Get the format, start, stop and position in the newsegment event.
 */
void
gst_event_parse_new_segment (GstEvent * event, gboolean * update,
    gdouble * rate, GstFormat * format, gint64 * start,
    gint64 * stop, gint64 * position)
{
  const GstStructure *structure;

  g_return_if_fail (GST_IS_EVENT (event));
  g_return_if_fail (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT);

  structure = gst_event_get_structure (event);
  if (update)
    *update =
        g_value_get_boolean (gst_structure_get_value (structure, "update"));
  if (rate)
    *rate = g_value_get_double (gst_structure_get_value (structure, "rate"));
  if (format)
    *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  if (start)
    *start = g_value_get_int64 (gst_structure_get_value (structure, "start"));
  if (stop)
    *stop = g_value_get_int64 (gst_structure_get_value (structure, "stop"));
  if (position)
    *position =
        g_value_get_int64 (gst_structure_get_value (structure, "position"));
}

/**
 * gst_event_new_tag:
 * @taglist: metadata list
 *
 * Generates a metadata tag event from the given @taglist.
 *
 * Returns: a new #GstEvent
 */
GstEvent *
gst_event_new_tag (GstTagList * taglist)
{
  g_return_val_if_fail (taglist != NULL, NULL);

  return gst_event_new_custom (GST_EVENT_TAG, (GstStructure *) taglist);
}

/**
 * gst_event_parse_tag:
 * @event: a tag event
 * @taglist: pointer to metadata list
 *
 * Parses a tag @event and stores the results in the given @taglist location.
 */
void
gst_event_parse_tag (GstEvent * event, GstTagList ** taglist)
{
  g_return_if_fail (GST_IS_EVENT (event));
  g_return_if_fail (GST_EVENT_TYPE (event) == GST_EVENT_TAG);

  if (taglist)
    *taglist = (GstTagList *) event->structure;
}

/* buffersize event */
/**
 * gst_event_new_buffer_size:
 * @format: buffer format
 * @minsize: minimum buffer size
 * @maxsize: maximum buffer size
 * @async: thread behavior
 *
 * Create a new buffersize event. The event is sent downstream and notifies
 * elements that they should provide a buffer of the specified dimensions.
 *
 * When the async flag is set, a thread boundary is prefered.
 *
 * Returns: a new #GstEvent
 */
GstEvent *
gst_event_new_buffer_size (GstFormat format, gint64 minsize,
    gint64 maxsize, gboolean async)
{
  GST_CAT_INFO (GST_CAT_EVENT,
      "creating buffersize format %d, minsize %" G_GINT64_FORMAT
      ", maxsize %" G_GINT64_FORMAT ", async %d", format,
      minsize, maxsize, async);

  return gst_event_new_custom (GST_EVENT_BUFFERSIZE,
      gst_structure_new ("GstEventBufferSize",
          "format", GST_TYPE_FORMAT, format,
          "minsize", G_TYPE_INT64, minsize,
          "maxsize", G_TYPE_INT64, maxsize,
          "async", G_TYPE_BOOLEAN, async, NULL));
}

/**
 * gst_event_parse_buffer_size:
 * @event: The event to query
 * @format: A pointer to store the format in
 * @minsize: A pointer to store the minsize in
 * @maxsize: A pointer to store the maxsize in
 * @async: A pointer to store the async-flag in
 *
 * Get the format, minsize, maxsize and async-flag in the buffersize event.
 */
void
gst_event_parse_buffer_size (GstEvent * event, GstFormat * format,
    gint64 * minsize, gint64 * maxsize, gboolean * async)
{
  const GstStructure *structure;

  g_return_if_fail (GST_IS_EVENT (event));
  g_return_if_fail (GST_EVENT_TYPE (event) == GST_EVENT_BUFFERSIZE);

  structure = gst_event_get_structure (event);
  if (format)
    *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  if (minsize)
    *minsize =
        g_value_get_int64 (gst_structure_get_value (structure, "minsize"));
  if (maxsize)
    *maxsize =
        g_value_get_int64 (gst_structure_get_value (structure, "maxsize"));
  if (async)
    *async = g_value_get_boolean (gst_structure_get_value (structure, "async"));
}

/**
 * gst_event_new_qos:
 * @proportion: the proportion of the qos message
 * @diff: The time difference of the last Clock sync
 * @timestamp: The timestamp of the buffer
 *
 * Allocate a new qos event with the given values.
 * The QOS event is generated in an element that wants an upstream
 * element to either reduce or increase its rate because of
 * high/low CPU load.
 *
 * proportion is the requested adjustment in datarate, 1.0 is the normal
 * datarate, 0.75 means increase datarate by 75%, 1.5 is 150%. Negative
 * values request a slow down, so -0.75 means a decrease by 75%.
 *
 * diff is the difference against the clock in stream time of the last
 * buffer that caused the element to generate the QOS event.
 *
 * timestamp is the timestamp of the last buffer that cause the element
 * to generate the QOS event.
 *
 * Returns: A new QOS event.
 */
GstEvent *
gst_event_new_qos (gdouble proportion, GstClockTimeDiff diff,
    GstClockTime timestamp)
{
  GST_CAT_INFO (GST_CAT_EVENT,
      "creating qos proportion %lf, diff %" GST_TIME_FORMAT
      ", timestamp %" GST_TIME_FORMAT, proportion,
      GST_TIME_ARGS (diff), GST_TIME_ARGS (timestamp));

  return gst_event_new_custom (GST_EVENT_QOS,
      gst_structure_new ("GstEventQOS",
          "proportion", G_TYPE_DOUBLE, proportion,
          "diff", G_TYPE_INT64, diff,
          "timestamp", G_TYPE_UINT64, timestamp, NULL));
}

/**
 * gst_event_parse_qos:
 * @event: The event to query
 * @proportion: A pointer to store the proportion in
 * @diff: A pointer to store the diff in
 * @timestamp: A pointer to store the timestamp in
 *
 * Get the proportion, diff and timestamp in the qos event.
 */
void
gst_event_parse_qos (GstEvent * event, gdouble * proportion,
    GstClockTimeDiff * diff, GstClockTime * timestamp)
{
  const GstStructure *structure;

  g_return_if_fail (GST_IS_EVENT (event));
  g_return_if_fail (GST_EVENT_TYPE (event) == GST_EVENT_QOS);

  structure = gst_event_get_structure (event);
  if (proportion)
    *proportion =
        g_value_get_double (gst_structure_get_value (structure, "proportion"));
  if (diff)
    *diff = g_value_get_int64 (gst_structure_get_value (structure, "diff"));
  if (timestamp)
    *timestamp =
        g_value_get_uint64 (gst_structure_get_value (structure, "timestamp"));
}

/**
 * gst_event_new_seek:
 * @rate: The new playback rate
 * @format: The format of the seek values
 * @flags: The optional seek flags
 * @cur_type: The type and flags for the new current position
 * @cur: The value of the new current position
 * @stop_type: The type and flags for the new stop position
 * @stop: The value of the new stop position
 *
 * Allocate a new seek event with the given parameters.
 *
 * The seek event configures playback of the pipeline from
 * @cur to @stop at the speed given in @rate, also called a segment.
 * The @cur and @stop values are expressed in format @format.
 *
 * A @rate of 1.0 means normal playback rate, 2.0 means double speed.
 * Negatives values means backwards playback. A value of 0.0 for the
 * rate is not allowed.
 *
 * @cur_type and @stop_type specify how to adjust the current and stop
 * time, relative or absolute. A type of #GST_SEEK_TYPE_NONE means that
 * the position should not be updated. The currently configured playback
 * segment can be queried with #GST_QUERY_SEGMENT.
 *
 * Note that updating the @cur position will actually move the current
 * playback pointer to that new position. It is not possible to seek 
 * relative to the current playing position, to do this, pause the pipeline,
 * get the current position and perform a GST_SEEK_TYPE_SET to the desired
 * position.
 *
 * Returns: A new seek event.
 */
GstEvent *
gst_event_new_seek (gdouble rate, GstFormat format, GstSeekFlags flags,
    GstSeekType cur_type, gint64 cur, GstSeekType stop_type, gint64 stop)
{
  if (format == GST_FORMAT_TIME) {
    GST_CAT_INFO (GST_CAT_EVENT,
        "creating seek rate %lf, format TIME, flags %d, "
        "cur_type %d, cur %" GST_TIME_FORMAT ", "
        "stop_type %d, stop %" GST_TIME_FORMAT,
        rate, flags, cur_type, GST_TIME_ARGS (cur),
        stop_type, GST_TIME_ARGS (stop));
  } else {
    GST_CAT_INFO (GST_CAT_EVENT,
        "creating seek rate %lf, format %d, flags %d, "
        "cur_type %d, cur %" G_GINT64_FORMAT ", "
        "stop_type %d, stop %" G_GINT64_FORMAT,
        rate, format, flags, cur_type, cur, stop_type, stop);
  }

  return gst_event_new_custom (GST_EVENT_SEEK,
      gst_structure_new ("GstEventSeek", "rate", G_TYPE_DOUBLE, rate,
          "format", GST_TYPE_FORMAT, format,
          "flags", GST_TYPE_SEEK_FLAGS, flags,
          "cur_type", GST_TYPE_SEEK_TYPE, cur_type,
          "cur", G_TYPE_INT64, cur,
          "stop_type", GST_TYPE_SEEK_TYPE, stop_type,
          "stop", G_TYPE_INT64, stop, NULL));
}

/**
 * gst_event_parse_seek:
 * @event: a seek event
 * @rate: result location for the rate
 * @format: result location for the stream format
 * @flags:  result location for the #GstSeekFlags
 * @cur_type: result location for the #GstSeekType of the current position
 * @cur: result location for the current postion expressed in @format
 * @stop_type:  result location for the #GstSeekType of the stop position
 * @stop: result location for the stop postion expressed in @format
 *
 * Parses a seek @event and stores the results in the given result locations.
 */
void
gst_event_parse_seek (GstEvent * event, gdouble * rate, GstFormat * format,
    GstSeekFlags * flags,
    GstSeekType * cur_type, gint64 * cur,
    GstSeekType * stop_type, gint64 * stop)
{
  const GstStructure *structure;

  g_return_if_fail (GST_IS_EVENT (event));
  g_return_if_fail (GST_EVENT_TYPE (event) == GST_EVENT_SEEK);

  structure = gst_event_get_structure (event);
  if (rate)
    *rate = g_value_get_double (gst_structure_get_value (structure, "rate"));
  if (format)
    *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  if (flags)
    *flags = g_value_get_flags (gst_structure_get_value (structure, "flags"));
  if (cur_type)
    *cur_type =
        g_value_get_enum (gst_structure_get_value (structure, "cur_type"));
  if (cur)
    *cur = g_value_get_int64 (gst_structure_get_value (structure, "cur"));
  if (stop_type)
    *stop_type =
        g_value_get_enum (gst_structure_get_value (structure, "stop_type"));
  if (stop)
    *stop = g_value_get_int64 (gst_structure_get_value (structure, "stop"));
}

/**
 * gst_event_new_navigation:
 * @structure: description of the event
 *
 * Create a new navigation event from the given description.
 *
 * Returns: a new #GstEvent
 */
GstEvent *
gst_event_new_navigation (GstStructure * structure)
{
  g_return_val_if_fail (structure != NULL, NULL);

  return gst_event_new_custom (GST_EVENT_NAVIGATION, structure);
}
