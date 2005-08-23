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
 * @short_description: Structure describing events that are passed up and down a pipeline
 * @see_also: #GstPad, #GstElement
 *
 * The event classes are used to construct and query events.
 *
 * Events are usually created with gst_event_new() which takes the event type as
 * an argument. Properties specific to the event can be set afterwards with the
 * provided macros. The event should be unreferenced with gst_event_unref().
 *
 * gst_event_new_seek() is a usually used to create a seek event and it takes
 * the needed parameters for a seek event. 
 *
 * gst_event_new_flush() creates a new flush event.
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

GType
gst_event_get_type (void)
{
  static GType _gst_event_type;

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

  GST_CAT_INFO (GST_CAT_EVENT, "freeing event %p", event);

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

  GST_CAT_INFO (GST_CAT_EVENT, "creating new event %p %d", event, type);

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
 * gst_event_new_newsegment:
 * @rate: a new rate for playback
 * @format: The format of the segment values
 * @start_val: the start value of the segment
 * @stop_val: the stop value of the segment
 * @base: base value for buffer timestamps.
 *
 * Allocate a new newsegment event with the given format/values tripplets.
 *
 * The newsegment event marks the range of buffers to be processed. All
 * data not within the segment range is not to be processed.
 *
 * The base time of the segment is used to convert the buffer timestamps
 * into the stream time again.
 *
 * After a newsegment event, the buffer stream time is calculated with:
 *
 *   TIMESTAMP(buf) - start_time + base
 *
 * Returns: A new newsegment event.
 */
GstEvent *
gst_event_new_newsegment (gdouble rate, GstFormat format,
    gint64 start_val, gint64 stop_val, gint64 base)
{
  if (format == GST_FORMAT_TIME) {
    GST_CAT_INFO (GST_CAT_EVENT,
        "creating newsegment rate %lf, format GST_FORMAT_TIME, "
        "start %" GST_TIME_FORMAT ", stop %" GST_TIME_FORMAT
        ", base %" GST_TIME_FORMAT,
        rate, GST_TIME_ARGS (start_val),
        GST_TIME_ARGS (stop_val), GST_TIME_ARGS (base));
  } else {
    GST_CAT_INFO (GST_CAT_EVENT,
        "creating newsegment rate %lf, format %d, "
        "start %lld, stop %lld, base %lld",
        rate, format, start_val, stop_val, base);
  }

  if (start_val != -1 && stop_val != -1)
    g_return_val_if_fail (start_val < stop_val, NULL);

  return gst_event_new_custom (GST_EVENT_NEWSEGMENT,
      gst_structure_new ("GstEventNewsegment", "rate", G_TYPE_DOUBLE, rate,
          "format", GST_TYPE_FORMAT, format,
          "start_val", G_TYPE_INT64, start_val,
          "stop_val", G_TYPE_INT64, stop_val,
          "base", G_TYPE_INT64, base, NULL));
}

/**
 * gst_event_parse_newsegment:
 * @event: The event to query
 * @rate: A pointer to the rate of the segment
 * @format: A pointer to the format of the newsegment values
 * @start_value: A pointer to store the start value in
 * @stop_value: A pointer to store the stop value in
 * @base: A pointer to store the base time in
 *
 * Get the start, stop and format in the newsegment event.
 */
void
gst_event_parse_newsegment (GstEvent * event, gdouble * rate,
    GstFormat * format, gint64 * start_value, gint64 * stop_value,
    gint64 * base)
{
  const GstStructure *structure;

  g_return_if_fail (GST_IS_EVENT (event));
  g_return_if_fail (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT);

  structure = gst_event_get_structure (event);
  if (rate)
    *rate = g_value_get_double (gst_structure_get_value (structure, "rate"));
  if (format)
    *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  if (start_value)
    *start_value =
        g_value_get_int64 (gst_structure_get_value (structure, "start_val"));
  if (stop_value)
    *stop_value =
        g_value_get_int64 (gst_structure_get_value (structure, "stop_val"));
  if (base)
    *base = g_value_get_int64 (gst_structure_get_value (structure, "base"));
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

/* filler event */
/**
 * gst_event_new_filler:
 *
 * Create a new dummy event that should be ignored.
 *
 * Returns: a new #GstEvent
 */
GstEvent *
gst_event_new_filler (void)
{
  return gst_event_new (GST_EVENT_FILLER);
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
 * @flags: The optional seek flags.
 * @cur_type: The type and flags for the new current position
 * @cur: The value of the new current position
 * @stop_type: The type and flags for the new stop position
 * @stop: The value of the new stop position
 *
 * Allocate a new seek event with the given parameters.
 *
 * The seek event configures playback of the pipeline from 
 * @cur to @stop at the speed given in @rate.
 * The @cur and @stop values are expressed in format @format.
 *
 * A @rate of 1.0 means normal playback rate, 2.0 means double speed.
 * Negatives values means backwards playback. A value of 0.0 for the
 * rate is not allowed.
 *
 * @cur_type and @stop_type specify how to adjust the current and stop
 * time, relative or absolute. A type of #GST_EVENT_TYPE_NONE means that
 * the position should not be updated.
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
