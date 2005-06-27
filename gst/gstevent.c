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

#include <string.h>             /* memcpy */

#include "gst_private.h"

#include "gstinfo.h"
#include "gstmemchunk.h"
#include "gstevent.h"
#include "gsttag.h"
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
  }
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
      if (GST_IS_TAG_LIST (event->event_data.structure.structure)) {
        gst_tag_list_free (event->event_data.structure.structure);
      } else {
        g_warning ("tag event %p didn't contain a valid tag list!", event);
        GST_ERROR ("tag event %p didn't contain a valid tag list!", event);
      }
      break;
    case GST_EVENT_NAVIGATION:
      gst_structure_free (event->event_data.structure.structure);
      break;
    default:
      break;
  }
}


static GstEvent *
_gst_event_copy (GstEvent * event)
{
  GstEvent *copy;

  copy = gst_event_new (event->type);

  copy->timestamp = event->timestamp;
  if (event->src) {
    copy->src = gst_object_ref (event->src);
  }

  memcpy (&copy->event_data, &event->event_data, sizeof (event->event_data));

  /* FIXME copy/ref additional fields */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
      copy->event_data.structure.structure =
          gst_tag_list_copy ((GstTagList *) event->event_data.structure.
          structure);
      break;
    case GST_EVENT_NAVIGATION:
      copy->event_data.structure.structure =
          gst_structure_copy (event->event_data.structure.structure);
    default:
      break;
  }

  return copy;
}

/**
 * gst_event_masks_contains:
 * @masks: The eventmask array to search
 * @mask: the event mask to find
 *
 * See if the given eventmask is inside the eventmask array.
 *
 * Returns: TRUE if the eventmask is found inside the array
 */
gboolean
gst_event_masks_contains (const GstEventMask * masks, GstEventMask * mask)
{
  g_return_val_if_fail (mask != NULL, FALSE);

  if (!masks)
    return FALSE;

  while (masks->type) {
    if (masks->type == mask->type &&
        (masks->flags & mask->flags) == mask->flags)
      return TRUE;

    masks++;
  }

  return FALSE;
}

/**
 * gst_event_new:
 * @type: The type of the new event
 *
 * Allocate a new event of the given type.
 *
 * Returns: A new event.
 */
GstEvent *
gst_event_new (GstEventType type)
{
  GstEvent *event;

  event = (GstEvent *) gst_mini_object_new (GST_TYPE_EVENT);

  GST_CAT_INFO (GST_CAT_EVENT, "creating new event type %d: %p", type, event);

  GST_EVENT_TYPE (event) = type;

  return event;
}

/**
 * gst_event_new_seek:
 * @type: The type of the seek event
 * @offset: The offset of the seek
 *
 * Allocate a new seek event with the given parameters.
 *
 * Returns: A new seek event.
 */
GstEvent *
gst_event_new_seek (GstSeekType type, gint64 offset)
{
  GstEvent *event;

  event = gst_event_new (GST_EVENT_SEEK);

  GST_EVENT_SEEK_TYPE (event) = type;
  GST_EVENT_SEEK_OFFSET (event) = offset;
  GST_EVENT_SEEK_ENDOFFSET (event) = -1;

  return event;
}

/**
 * gst_event_new_discontinuous_valist:
 * @new_media: A flag indicating a new media type starts
 * @format1: The format of the discont value
 * @var_args: more discont values and formats
 *
 * Allocate a new discontinuous event with the given format/value pairs. Note
 * that the values are of type gint64 - you may not use simple integers such
 * as "0" when calling this function, always cast them like "(gint64) 0".
 * Terminate the list with #GST_FORMAT_UNDEFINED.
 *
 * Returns: A new discontinuous event.
 */
GstEvent *
gst_event_new_discontinuous_valist (gdouble rate, GstFormat format1,
    va_list var_args)
{
  GstEvent *event;
  gint count = 0;

  event = gst_event_new (GST_EVENT_DISCONTINUOUS);
  GST_EVENT_DISCONT_RATE (event) = rate;

  while (format1 != GST_FORMAT_UNDEFINED && count < 8) {

    GST_EVENT_DISCONT_OFFSET (event, count).format =
        format1 & GST_SEEK_FORMAT_MASK;
    GST_EVENT_DISCONT_OFFSET (event, count).start_value =
        va_arg (var_args, gint64);
    GST_EVENT_DISCONT_OFFSET (event, count).end_value =
        va_arg (var_args, gint64);

    format1 = va_arg (var_args, GstFormat);

    count++;
  }

  GST_EVENT_DISCONT_OFFSET_LEN (event) = count;

  return event;
}

/**
 * gst_event_new_discontinuous:
 * @new_media: A flag indicating a new media type starts
 * @format1: The format of the discont value
 * @...: more discont values and formats
 *
 * Allocate a new discontinuous event with the given format/value pairs. Note
 * that the values are of type gint64 - you may not use simple integers such
 * as "0" when calling this function, always cast them like "(gint64) 0".
 * Terminate the list with #GST_FORMAT_UNDEFINED.
 *
 * Returns: A new discontinuous event.
 */
GstEvent *
gst_event_new_discontinuous (gdouble rate, GstFormat format1, ...)
{
  va_list var_args;
  GstEvent *event;

  va_start (var_args, format1);

  event = gst_event_new_discontinuous_valist (rate, format1, var_args);

  va_end (var_args);

  return event;
}

/**
 * gst_event_discont_get_value:
 * @event: The event to query
 * @format: The format of the discontinuous value
 * @value: A pointer to the value
 *
 * Get the value for the given format in the discontinous event.
 *
 * Returns: TRUE if the discontinuous event carries the specified
 * format/value pair.
 */
gboolean
gst_event_discont_get_value (GstEvent * event, GstFormat format,
    gint64 * start_value, gint64 * end_value)
{
  gint i, n;

  g_return_val_if_fail (event != NULL, FALSE);

  n = GST_EVENT_DISCONT_OFFSET_LEN (event);

  for (i = 0; i < n; i++) {
    if (GST_EVENT_DISCONT_OFFSET (event, i).format == format) {
      if (start_value)
        *start_value = GST_EVENT_DISCONT_OFFSET (event, i).start_value;
      if (end_value)
        *end_value = GST_EVENT_DISCONT_OFFSET (event, i).end_value;
      return TRUE;
    }
  }

  return FALSE;
}


/**
 * gst_event_new_size:
 * @format: The format of the size value
 * @value: The value of the size event
 *
 * Create a new size event with the given values.
 *
 * Returns: The new size event.
 */
GstEvent *
gst_event_new_size (GstFormat format, gint64 value)
{
  GstEvent *event;

  event = gst_event_new (GST_EVENT_SIZE);

  GST_EVENT_SIZE_FORMAT (event) = format;
  GST_EVENT_SIZE_VALUE (event) = value;

  return event;
}


/**
 * gst_event_new_segment_seek:
 * @type: The type of the seek event
 * @start: The start offset of the seek
 * @stop: The stop offset of the seek
 *
 * Allocate a new segment seek event with the given parameters. 
 *
 * Returns: A new segment seek event.
 */
GstEvent *
gst_event_new_segment_seek (GstSeekType type, gint64 start, gint64 stop)
{
  GstEvent *event;

  g_return_val_if_fail (start < stop || stop == -1, NULL);

  event = gst_event_new (GST_EVENT_SEEK);

  GST_EVENT_SEEK_TYPE (event) = type;
  GST_EVENT_SEEK_OFFSET (event) = start;
  GST_EVENT_SEEK_ENDOFFSET (event) = stop;

  return event;
}

/**
 * gst_event_new_flush:
 * @done: Indicates the end of the flush
 *
 * Allocate a new flush event.
 *
 * Returns: A new flush event.
 */
GstEvent *
gst_event_new_flush (gboolean done)
{
  GstEvent *event;

  event = gst_event_new (GST_EVENT_FLUSH);
  GST_EVENT_FLUSH_DONE (event) = done;

  return event;
}
