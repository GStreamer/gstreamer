/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-pad-monitor.c - Validate PadMonitor class
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#  include "config.h"
#endif

#include "gst-validate-internal.h"
#include "gst-validate-pad-monitor.h"
#include "gst-validate-element-monitor.h"
#include "gst-validate-reporter.h"
#include <string.h>
#include <stdarg.h>

/**
 * SECTION:gst-validate-pad-monitor
 * @short_description: Class that wraps a #GstPad for Validate checks
 *
 * TODO
 */

#define gst_validate_pad_monitor_parent_class parent_class
G_DEFINE_TYPE (GstValidatePadMonitor, gst_validate_pad_monitor,
    GST_TYPE_VALIDATE_MONITOR);

#define PENDING_FIELDS "pending-fields"
#define AUDIO_TIMESTAMP_TOLERANCE (GST_MSECOND * 100)

#define PAD_PARENT_IS_DEMUXER(m) \
    (GST_VALIDATE_MONITOR_GET_PARENT(m) ? \
        GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_DEMUXER ( \
            GST_VALIDATE_MONITOR_GET_PARENT(m)) : \
        FALSE)

#define PAD_PARENT_IS_DECODER(m) \
    (GST_VALIDATE_MONITOR_GET_PARENT(m) ? \
        GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_DECODER ( \
            GST_VALIDATE_MONITOR_GET_PARENT(m)) : \
        FALSE)

#define PAD_PARENT_IS_ENCODER(m) \
    (GST_VALIDATE_MONITOR_GET_PARENT(m) ? \
        GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_ENCODER ( \
            GST_VALIDATE_MONITOR_GET_PARENT(m)) : \
        FALSE)


/*
 * Locking the parent should always be done before locking the
 * pad-monitor to prevent deadlocks in case another monitor from
 * another pad on the same element starts an operation that also
 * requires locking itself and some other monitors from internally
 * linked pads.
 *
 * An example:
 * An element has a sink and a src pad. Some test starts running at sinkpad
 * and it locks the parent, and then it locks itself. In case it needs to get
 * some information from the srcpad, it is able to lock the srcpad and get it
 * because the srcpad should never lock itself before locking the parent (which
 * it won't be able as sinkpad already locked it).
 *
 * As a side one, it is possible that srcpad locks itself without locking the
 * parent in case it wants to do a check that won't need to use other internally
 * linked pads (sinkpad). But in this case it might lock and unlock freely without
 * causing deadlocks.
 */
#define GST_VALIDATE_PAD_MONITOR_PARENT_LOCK(m)                  \
G_STMT_START {                                             \
  if (G_LIKELY (GST_VALIDATE_MONITOR_GET_PARENT (m))) {          \
    GST_VALIDATE_MONITOR_LOCK (GST_VALIDATE_MONITOR_GET_PARENT (m));   \
  } else {                                                 \
    GST_WARNING_OBJECT (m, "No parent found, can't lock"); \
  }                                                        \
} G_STMT_END

#define GST_VALIDATE_PAD_MONITOR_PARENT_UNLOCK(m)                  \
G_STMT_START {                                               \
  if (G_LIKELY (GST_VALIDATE_MONITOR_GET_PARENT (m))) {            \
    GST_VALIDATE_MONITOR_UNLOCK (GST_VALIDATE_MONITOR_GET_PARENT (m));   \
  } else {                                                   \
    GST_WARNING_OBJECT (m, "No parent found, can't unlock"); \
  }                                                          \
} G_STMT_END

typedef struct
{
  GstClockTime timestamp;
  GstEvent *event;
} SerializedEventData;

static void
debug_pending_event (GstPad * pad, GPtrArray * array)
{
  guint i, len;

  len = array->len;
  for (i = 0; i < len; i++) {
    SerializedEventData *data = g_ptr_array_index (array, i);
    GST_DEBUG_OBJECT (pad, "event #%d %" GST_TIME_FORMAT " %s %p",
        i, GST_TIME_ARGS (data->timestamp),
        GST_EVENT_TYPE_NAME (data->event), data->event);
  }
}

static void
_serialized_event_data_free (SerializedEventData * serialized_event)
{
  gst_event_unref (serialized_event->event);
  g_slice_free (SerializedEventData, serialized_event);
}

static gboolean gst_validate_pad_monitor_do_setup (GstValidateMonitor *
    monitor);
static GstElement *gst_validate_pad_monitor_get_element (GstValidateMonitor *
    monitor);
static void
gst_validate_pad_monitor_setcaps_pre (GstValidatePadMonitor * pad_monitor,
    GstCaps * caps);
static void gst_validate_pad_monitor_setcaps_post (GstValidatePadMonitor *
    pad_monitor, GstCaps * caps, gboolean ret);

#define PAD_IS_IN_PUSH_MODE(p) ((p)->mode == GST_PAD_MODE_PUSH)

static gboolean
_structure_is_raw_video (GstStructure * structure)
{
  return gst_structure_has_name (structure, "video/x-raw");
}

static gboolean
_structure_is_raw_audio (GstStructure * structure)
{
  return gst_structure_has_name (structure, "audio/x-raw");
}

static void
_check_field_type (GstValidatePadMonitor * monitor, GstStructure * structure,
    const gchar * field, ...)
{
  va_list var_args;
  GType type;
  gchar *joined_types = NULL;
  const gchar *rejected_types[5];
  gint rejected_types_index = 0;

  if (!gst_structure_has_field (structure, field)) {
    GST_VALIDATE_REPORT (monitor, CAPS_IS_MISSING_FIELD,
        "Field '%s' is missing from structure: %" GST_PTR_FORMAT, field,
        structure);
    return;
  }

  memset (rejected_types, 0, sizeof (rejected_types));
  va_start (var_args, field);
  while ((type = va_arg (var_args, GType)) != 0) {
    if (gst_structure_has_field_typed (structure, field, type)) {
      va_end (var_args);
      return;
    }
    rejected_types[rejected_types_index++] = g_type_name (type);
  }
  va_end (var_args);

  joined_types = g_strjoinv (" / ", (gchar **) rejected_types);
  GST_VALIDATE_REPORT (monitor, CAPS_FIELD_HAS_BAD_TYPE,
      "Field '%s' has wrong type %s in structure '%" GST_PTR_FORMAT
      "'. Expected: %s", field,
      g_type_name (gst_structure_get_field_type (structure, field)), structure,
      joined_types);
  g_free (joined_types);
}

static void
gst_validate_pad_monitor_check_raw_video_caps_complete (GstValidatePadMonitor *
    monitor, GstStructure * structure)
{
  _check_field_type (monitor, structure, "width", G_TYPE_INT,
      GST_TYPE_INT_RANGE, 0);
  _check_field_type (monitor, structure, "height", G_TYPE_INT,
      GST_TYPE_INT_RANGE, 0);
  _check_field_type (monitor, structure, "framerate", GST_TYPE_FRACTION,
      GST_TYPE_FRACTION_RANGE, 0);
  _check_field_type (monitor, structure, "pixel-aspect-ratio",
      GST_TYPE_FRACTION, GST_TYPE_FRACTION_RANGE, 0);
  _check_field_type (monitor, structure, "format", G_TYPE_STRING,
      GST_TYPE_LIST);
}

static void
gst_validate_pad_monitor_check_raw_audio_caps_complete (GstValidatePadMonitor *
    monitor, GstStructure * structure)
{
  gint channels;
  _check_field_type (monitor, structure, "format", G_TYPE_STRING, GST_TYPE_LIST,
      0);
  _check_field_type (monitor, structure, "layout", G_TYPE_STRING, GST_TYPE_LIST,
      0);
  _check_field_type (monitor, structure, "rate", G_TYPE_INT, GST_TYPE_LIST,
      GST_TYPE_INT_RANGE, 0);
  _check_field_type (monitor, structure, "channels", G_TYPE_INT, GST_TYPE_LIST,
      GST_TYPE_INT_RANGE, 0);
  if (gst_structure_get_int(structure, "channels", &channels)) {
    if (channels > 2)
      _check_field_type (monitor, structure, "channel-mask", GST_TYPE_BITMASK,
          GST_TYPE_LIST, 0);
  }
}

static void
gst_validate_pad_monitor_check_caps_complete (GstValidatePadMonitor * monitor,
    GstCaps * caps)
{
  GstStructure *structure;
  gint i;

  GST_DEBUG_OBJECT (monitor->pad, "Checking caps %" GST_PTR_FORMAT, caps);

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    if (_structure_is_raw_video (structure)) {
      gst_validate_pad_monitor_check_raw_video_caps_complete (monitor,
          structure);

    } else if (_structure_is_raw_audio (structure)) {
      gst_validate_pad_monitor_check_raw_audio_caps_complete (monitor,
          structure);
    }
  }
}

static GstCaps *
gst_validate_pad_monitor_get_othercaps (GstValidatePadMonitor * monitor)
{
  GstCaps *caps = gst_caps_new_empty ();
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstCaps *peercaps;

  iter =
      gst_pad_iterate_internal_links (GST_VALIDATE_PAD_MONITOR_GET_PAD
      (monitor));
  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);

        /* TODO What would be the correct caps operation to merge the caps in
         * case one sink is internally linked to multiple srcs? */
        peercaps = gst_pad_peer_query_caps (otherpad, NULL);
        if (peercaps)
          caps = gst_caps_merge (caps, peercaps);

        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        gst_caps_replace (&caps, gst_caps_new_empty ());
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (monitor->pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);

  GST_DEBUG_OBJECT (monitor->pad, "Otherpad caps: %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
_structure_is_video (GstStructure * structure)
{
  const gchar *name = gst_structure_get_name (structure);

  return g_strstr_len (name, 6, "video/")
      && strcmp (name, "video/quicktime") != 0;
}

static gboolean
_structure_is_audio (GstStructure * structure)
{
  const gchar *name = gst_structure_get_name (structure);

  return g_strstr_len (name, 6, "audio/") != NULL;
}

static gboolean
gst_validate_pad_monitor_pad_should_proxy_othercaps (GstValidatePadMonitor *
    monitor)
{
  GstValidateMonitor *parent = GST_VALIDATE_MONITOR_GET_PARENT (monitor);

  if (!parent)
    return FALSE;

  /* We only know how to handle othercaps checks for codecs so far */
  return GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_DECODER (parent) ||
      GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_ENCODER (parent);
}


/* Check if the field @f from @s2 (if present) is represented in @s1
 * Represented here means either equal or @s1's value is in a list/range
 * from @s2
 */
static gboolean
_structures_field_is_contained (GstStructure * s1, GstStructure * s2,
    const gchar * f)
{
  const GValue *v1;
  const GValue *v2;

  v2 = gst_structure_get_value (s2, f);
  if (!v2)
    return TRUE;                /* nothing to compare to */

  v1 = gst_structure_get_value (s1, f);
  if (!v1)
    return FALSE;

  if (!gst_value_is_fixed (v1) && !gst_value_is_fixed (v2))
    return TRUE;

  if (gst_value_compare (v1, v2) == GST_VALUE_EQUAL)
    return TRUE;

  if (GST_VALUE_HOLDS_LIST (v2)) {
    gint i;
    for (i = 0; i < gst_value_list_get_size (v2); i++) {
      const GValue *v2_subvalue = gst_value_list_get_value (v2, i);
      if (gst_value_compare (v1, v2_subvalue) == GST_VALUE_EQUAL)
        return TRUE;
    }
  }

  if (GST_VALUE_HOLDS_ARRAY (v2)) {
    gint i;
    for (i = 0; i < gst_value_array_get_size (v2); i++) {
      const GValue *v2_subvalue = gst_value_array_get_value (v2, i);
      if (gst_value_compare (v1, v2_subvalue) == GST_VALUE_EQUAL)
        return TRUE;
    }
  }

  if (GST_VALUE_HOLDS_INT_RANGE (v2)) {
    gint min, max;

    min = gst_value_get_int_range_min (v2);
    max = gst_value_get_int_range_max (v2);

    if (G_VALUE_HOLDS_INT (v1)) {
      gint v = g_value_get_int (v1);

      return v >= min && v <= max;
    } else {
      /* TODO compare int ranges with int ranges
       * or with lists if useful */
    }
  }

  if (GST_VALUE_HOLDS_FRACTION_RANGE (v2)) {
    const GValue *min, *max;

    min = gst_value_get_fraction_range_min (v2);
    max = gst_value_get_fraction_range_max (v2);

    if (GST_VALUE_HOLDS_FRACTION (v1)) {
      gint v_min = gst_value_compare (v1, min);
      gint v_max = gst_value_compare (v1, max);

      return (v_min == GST_VALUE_EQUAL || v_min == GST_VALUE_GREATER_THAN) &&
          (v_max == GST_VALUE_EQUAL || v_max == GST_VALUE_LESS_THAN);
    } else {
      /* TODO compare fraction ranges with fraction ranges
       * or with lists if useful */
    }
  }

  return FALSE;
}

static void
gst_validate_pad_monitor_check_caps_fields_proxied (GstValidatePadMonitor *
    monitor, GstCaps * caps)
{
  GstStructure *structure;
  GstStructure *otherstructure;
  GstCaps *othercaps;
  gint i, j;

  if (!gst_validate_pad_monitor_pad_should_proxy_othercaps (monitor))
    return;

  othercaps = gst_validate_pad_monitor_get_othercaps (monitor);

  for (i = 0; i < gst_caps_get_size (othercaps); i++) {
    gboolean found = FALSE;
    gboolean type_match = FALSE;

    otherstructure = gst_caps_get_structure (othercaps, i);

    /* look for a proxied version of 'otherstructure' */
    if (_structure_is_video (otherstructure)) {
      for (j = 0; j < gst_caps_get_size (caps); j++) {
        structure = gst_caps_get_structure (caps, j);
        if (_structure_is_video (structure)) {
          type_match = TRUE;
          if (_structures_field_is_contained (structure, otherstructure,
                  "width")
              && _structures_field_is_contained (structure, otherstructure,
                  "height")
              && _structures_field_is_contained (structure, otherstructure,
                  "framerate")
              && _structures_field_is_contained (structure, otherstructure,
                  "pixel-aspect-ratio")) {
            found = TRUE;
            break;
          }
        }
      }
    } else if (_structure_is_audio (otherstructure)) {
      for (j = 0; j < gst_caps_get_size (caps); j++) {
        structure = gst_caps_get_structure (caps, j);
        if (_structure_is_audio (structure)) {
          type_match = TRUE;
          if (_structures_field_is_contained (structure, otherstructure, "rate")
              && _structures_field_is_contained (structure, otherstructure,
                  "channels")) {
            found = TRUE;
            break;
          }
        }
      }
    }

    if (type_match && !found) {
      GST_VALIDATE_REPORT (monitor, GET_CAPS_NOT_PROXYING_FIELDS,
          "Peer pad structure '%" GST_PTR_FORMAT "' has no similar version "
          "on pad's caps '%" GST_PTR_FORMAT "'", otherstructure, caps);
    }
  }
}

static void
gst_validate_pad_monitor_check_late_serialized_events (GstValidatePadMonitor *
    monitor, GstClockTime ts)
{
  gint i;

  if (!GST_CLOCK_TIME_IS_VALID (ts))
    return;

  GST_DEBUG_OBJECT (monitor->pad, "Timestamp to check %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ts));

  for (i = 0; i < monitor->serialized_events->len; i++) {
    SerializedEventData *data =
        g_ptr_array_index (monitor->serialized_events, i);

    GST_DEBUG_OBJECT (monitor->pad, "Event #%d (%s) ts: %" GST_TIME_FORMAT,
        i, GST_EVENT_TYPE_NAME (data->event), GST_TIME_ARGS (data->timestamp));

    if (GST_CLOCK_TIME_IS_VALID (data->timestamp) && data->timestamp < ts) {
      GST_VALIDATE_REPORT (monitor, SERIALIZED_EVENT_WASNT_PUSHED_IN_TIME,
          "Serialized event %" GST_PTR_FORMAT " wasn't pushed before expected "
          "timestamp %" GST_TIME_FORMAT " on pad %s:%s", data->event,
          GST_TIME_ARGS (data->timestamp),
          GST_DEBUG_PAD_NAME (GST_VALIDATE_PAD_MONITOR_GET_PAD (monitor)));
    } else {
      /* events should be ordered by ts */
      break;
    }
  }

  if (i) {
    debug_pending_event (monitor->pad, monitor->serialized_events);
    g_ptr_array_remove_range (monitor->serialized_events, 0, i);
  }
}

static void
gst_validate_pad_monitor_dispose (GObject * object)
{
  GstValidatePadMonitor *monitor = GST_VALIDATE_PAD_MONITOR_CAST (object);
  GstPad *pad = GST_VALIDATE_PAD_MONITOR_GET_PAD (monitor);

  if (pad) {
    if (monitor->pad_probe_id)
      gst_pad_remove_probe (pad, monitor->pad_probe_id);
  }

  if (monitor->expected_segment)
    gst_event_unref (monitor->expected_segment);

  gst_structure_free (monitor->pending_setcaps_fields);
  g_ptr_array_unref (monitor->serialized_events);
  g_list_free_full (monitor->expired_events, (GDestroyNotify) gst_event_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_validate_pad_monitor_class_init (GstValidatePadMonitorClass * klass)
{
  GObjectClass *gobject_class;
  GstValidateMonitorClass *monitor_klass;

  gobject_class = G_OBJECT_CLASS (klass);
  monitor_klass = GST_VALIDATE_MONITOR_CLASS (klass);

  gobject_class->dispose = gst_validate_pad_monitor_dispose;

  monitor_klass->setup = gst_validate_pad_monitor_do_setup;
  monitor_klass->get_element = gst_validate_pad_monitor_get_element;
}

static void
gst_validate_pad_monitor_init (GstValidatePadMonitor * pad_monitor)
{
  pad_monitor->pending_setcaps_fields =
      gst_structure_new_empty (PENDING_FIELDS);
  pad_monitor->serialized_events =
      g_ptr_array_new_with_free_func ((GDestroyNotify)
      _serialized_event_data_free);
  pad_monitor->expired_events = NULL;
  gst_segment_init (&pad_monitor->segment, GST_FORMAT_BYTES);
  pad_monitor->first_buffer = TRUE;

  pad_monitor->timestamp_range_start = GST_CLOCK_TIME_NONE;
  pad_monitor->timestamp_range_end = GST_CLOCK_TIME_NONE;
}

/**
 * gst_validate_pad_monitor_new:
 * @pad: (transfer-none): a #GstPad to run Validate on
 */
GstValidatePadMonitor *
gst_validate_pad_monitor_new (GstPad * pad, GstValidateRunner * runner,
    GstValidateElementMonitor * parent)
{
  GstValidatePadMonitor *monitor = g_object_new (GST_TYPE_VALIDATE_PAD_MONITOR,
      "object", pad, "validate-runner", runner, "validate-parent",
      parent, NULL);

  if (GST_VALIDATE_PAD_MONITOR_GET_PAD (monitor) == NULL) {
    g_object_unref (monitor);
    return NULL;
  }
  return monitor;
}

static GstElement *
gst_validate_pad_monitor_get_element (GstValidateMonitor * monitor)
{
  GstPad *pad = GST_VALIDATE_PAD_MONITOR_GET_PAD (monitor);

  return GST_PAD_PARENT (pad);
}

static void
gst_validate_pad_monitor_event_overrides (GstValidatePadMonitor * pad_monitor,
    GstEvent * event)
{
  GList *iter;

  GST_VALIDATE_MONITOR_OVERRIDES_LOCK (pad_monitor);
  for (iter = GST_VALIDATE_MONITOR_OVERRIDES (pad_monitor).head; iter;
      iter = g_list_next (iter)) {
    GstValidateOverride *override = iter->data;

    gst_validate_override_event_handler (override,
        GST_VALIDATE_MONITOR_CAST (pad_monitor), event);
  }
  GST_VALIDATE_MONITOR_OVERRIDES_UNLOCK (pad_monitor);
}

static void
gst_validate_pad_monitor_buffer_overrides (GstValidatePadMonitor * pad_monitor,
    GstBuffer * buffer)
{
  GList *iter;

  GST_VALIDATE_MONITOR_OVERRIDES_LOCK (pad_monitor);
  for (iter = GST_VALIDATE_MONITOR_OVERRIDES (pad_monitor).head; iter;
      iter = g_list_next (iter)) {
    GstValidateOverride *override = iter->data;

    gst_validate_override_buffer_handler (override,
        GST_VALIDATE_MONITOR_CAST (pad_monitor), buffer);
  }
  GST_VALIDATE_MONITOR_OVERRIDES_UNLOCK (pad_monitor);
}

static void
gst_validate_pad_monitor_buffer_probe_overrides (GstValidatePadMonitor *
    pad_monitor, GstBuffer * buffer)
{
  GList *iter;

  GST_VALIDATE_MONITOR_OVERRIDES_LOCK (pad_monitor);
  for (iter = GST_VALIDATE_MONITOR_OVERRIDES (pad_monitor).head; iter;
      iter = g_list_next (iter)) {
    GstValidateOverride *override = iter->data;

    gst_validate_override_buffer_probe_handler (override,
        GST_VALIDATE_MONITOR_CAST (pad_monitor), buffer);
  }
  GST_VALIDATE_MONITOR_OVERRIDES_UNLOCK (pad_monitor);
}

static void
gst_validate_pad_monitor_query_overrides (GstValidatePadMonitor * pad_monitor,
    GstQuery * query)
{
  GList *iter;

  GST_VALIDATE_MONITOR_OVERRIDES_LOCK (pad_monitor);
  for (iter = GST_VALIDATE_MONITOR_OVERRIDES (pad_monitor).head; iter;
      iter = g_list_next (iter)) {
    GstValidateOverride *override = iter->data;

    gst_validate_override_query_handler (override,
        GST_VALIDATE_MONITOR_CAST (pad_monitor), query);
  }
  GST_VALIDATE_MONITOR_OVERRIDES_UNLOCK (pad_monitor);
}

static void
gst_validate_pad_monitor_setcaps_overrides (GstValidatePadMonitor * pad_monitor,
    GstCaps * caps)
{
  GList *iter;

  GST_VALIDATE_MONITOR_OVERRIDES_LOCK (pad_monitor);
  for (iter = GST_VALIDATE_MONITOR_OVERRIDES (pad_monitor).head; iter;
      iter = g_list_next (iter)) {
    GstValidateOverride *override = iter->data;

    gst_validate_override_setcaps_handler (override,
        GST_VALIDATE_MONITOR_CAST (pad_monitor), caps);
  }
  GST_VALIDATE_MONITOR_OVERRIDES_UNLOCK (pad_monitor);
}

/* FIXME : This is a bit dubious, what's the point of this check ? */
static gboolean
gst_validate_pad_monitor_timestamp_is_in_received_range (GstValidatePadMonitor *
    monitor, GstClockTime ts, GstClockTime tolerance)
{
  GST_DEBUG_OBJECT (monitor->pad, "Checking if timestamp %" GST_TIME_FORMAT
      " is in range: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT " for pad "
      "%s:%s with tolerance: %" GST_TIME_FORMAT, GST_TIME_ARGS (ts),
      GST_TIME_ARGS (monitor->timestamp_range_start),
      GST_TIME_ARGS (monitor->timestamp_range_end),
      GST_DEBUG_PAD_NAME (GST_VALIDATE_PAD_MONITOR_GET_PAD (monitor)),
      GST_TIME_ARGS (tolerance));
  return !GST_CLOCK_TIME_IS_VALID (monitor->timestamp_range_start) ||
      !GST_CLOCK_TIME_IS_VALID (monitor->timestamp_range_end) ||
      ((monitor->timestamp_range_start >= tolerance ?
          monitor->timestamp_range_start - tolerance : 0) <= ts
      && (ts >= tolerance ? ts - tolerance : 0) <=
      monitor->timestamp_range_end);
}

/* Iterates over internal links (sinkpads) to check that this buffer has
 * a timestamp that is in the range of the lastly received buffers */
static void
    gst_validate_pad_monitor_check_buffer_timestamp_in_received_range
    (GstValidatePadMonitor * monitor, GstBuffer * buffer,
    GstClockTime tolerance)
{
  GstClockTime ts;
  GstClockTime ts_end;
  GstIterator *iter;
  gboolean has_one = FALSE;
  gboolean found = FALSE;
  gboolean done;
  GstPad *otherpad;
  GstValidatePadMonitor *othermonitor;

  if (!GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer))
      || !GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer))) {
    GST_DEBUG_OBJECT (monitor->pad,
        "Can't check buffer timestamps range as "
        "buffer has no valid timestamp/duration");
    return;
  }
  ts = GST_BUFFER_TIMESTAMP (buffer);
  ts_end = ts + GST_BUFFER_DURATION (buffer);

  iter =
      gst_pad_iterate_internal_links (GST_VALIDATE_PAD_MONITOR_GET_PAD
      (monitor));

  if (iter == NULL) {
    GST_WARNING_OBJECT (GST_VALIDATE_PAD_MONITOR_GET_PAD (monitor),
        "No iterator available");
    return;
  }

  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);
        GST_DEBUG_OBJECT (monitor->pad, "Checking pad %s:%s input timestamps",
            GST_DEBUG_PAD_NAME (otherpad));
        othermonitor =
            g_object_get_data ((GObject *) otherpad, "validate-monitor");
        GST_VALIDATE_MONITOR_LOCK (othermonitor);
        if (gst_validate_pad_monitor_timestamp_is_in_received_range
            (othermonitor, ts, tolerance)
            &&
            gst_validate_pad_monitor_timestamp_is_in_received_range
            (othermonitor, ts_end, tolerance)) {
          done = TRUE;
          found = TRUE;
        }
        GST_VALIDATE_MONITOR_UNLOCK (othermonitor);
        g_value_reset (&value);
        has_one = TRUE;
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        has_one = FALSE;
        found = FALSE;
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (monitor->pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);

  if (!has_one) {
    GST_DEBUG_OBJECT (monitor->pad, "Skipping timestamp in range check as no "
        "internal linked pad was found");
    return;
  }
  if (!found) {
    GST_VALIDATE_REPORT (monitor, BUFFER_TIMESTAMP_OUT_OF_RECEIVED_RANGE,
        "Timestamp %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT
        " is out of range of received input", GST_TIME_ARGS (ts),
        GST_TIME_ARGS (ts_end));
  }
}

static void
gst_validate_pad_monitor_check_first_buffer (GstValidatePadMonitor *
    pad_monitor, GstBuffer * buffer)
{
  if (G_UNLIKELY (pad_monitor->first_buffer)) {
    pad_monitor->first_buffer = FALSE;

    if (!pad_monitor->has_segment
        && PAD_IS_IN_PUSH_MODE (GST_VALIDATE_PAD_MONITOR_GET_PAD (pad_monitor)))
    {
      GST_VALIDATE_REPORT (pad_monitor, BUFFER_BEFORE_SEGMENT,
          "Received buffer before Segment event");
    }
    if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer))) {
      gint64 running_time = gst_segment_to_running_time (&pad_monitor->segment,
          pad_monitor->segment.format, GST_BUFFER_TIMESTAMP (buffer));
      if (running_time != 0) {
        GST_VALIDATE_REPORT (pad_monitor, FIRST_BUFFER_RUNNING_TIME_IS_NOT_ZERO,
            "First buffer running time is not 0, it is: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (running_time));
      }
    }
  }
}

static void
gst_validate_pad_monitor_update_buffer_data (GstValidatePadMonitor *
    pad_monitor, GstBuffer * buffer)
{
  pad_monitor->current_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  pad_monitor->current_duration = GST_BUFFER_DURATION (buffer);
  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer))) {
    if (GST_CLOCK_TIME_IS_VALID (pad_monitor->timestamp_range_start)) {
      pad_monitor->timestamp_range_start =
          MIN (pad_monitor->timestamp_range_start,
          GST_BUFFER_TIMESTAMP (buffer));
    } else {
      pad_monitor->timestamp_range_start = GST_BUFFER_TIMESTAMP (buffer);
    }

    if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer))) {
      GstClockTime endts =
          GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
      if (GST_CLOCK_TIME_IS_VALID (pad_monitor->timestamp_range_end)) {
        pad_monitor->timestamp_range_end =
            MAX (pad_monitor->timestamp_range_end, endts);
      } else {
        pad_monitor->timestamp_range_end = endts;
      }
    }
  }
  GST_DEBUG_OBJECT (pad_monitor->pad, "Current stored range: %" GST_TIME_FORMAT
      " - %" GST_TIME_FORMAT,
      GST_TIME_ARGS (pad_monitor->timestamp_range_start),
      GST_TIME_ARGS (pad_monitor->timestamp_range_end));
}

static GstFlowReturn
_combine_flows (GstFlowReturn ret1, GstFlowReturn ret2)
{
  if (ret1 == ret2)
    return ret1;
  if (ret1 <= GST_FLOW_NOT_NEGOTIATED)
    return ret1;
  if (ret2 <= GST_FLOW_NOT_NEGOTIATED)
    return ret2;
  if (ret1 == GST_FLOW_FLUSHING || ret2 == GST_FLOW_FLUSHING)
    return GST_FLOW_FLUSHING;
  if (ret1 == GST_FLOW_OK || ret2 == GST_FLOW_OK)
    return GST_FLOW_OK;
  return ret2;
}

static void
gst_validate_pad_monitor_check_aggregated_return (GstValidatePadMonitor *
    monitor, GstFlowReturn ret)
{
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstPad *peerpad;
  GstValidatePadMonitor *othermonitor;
  GstFlowReturn aggregated = GST_FLOW_NOT_LINKED;
  gboolean found_a_pad = FALSE;
  GstPad *pad = GST_VALIDATE_PAD_MONITOR_GET_PAD (monitor);

  iter = gst_pad_iterate_internal_links (pad);
  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);
        peerpad = gst_pad_get_peer (otherpad);
        if (peerpad) {
          othermonitor =
              g_object_get_data ((GObject *) peerpad, "validate-monitor");
          if (othermonitor) {
            found_a_pad = TRUE;
            GST_VALIDATE_MONITOR_LOCK (othermonitor);
            aggregated =
                _combine_flows (aggregated, othermonitor->last_flow_return);
            GST_VALIDATE_MONITOR_UNLOCK (othermonitor);
          }

          gst_object_unref (peerpad);
        }
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (monitor->pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
  if (!found_a_pad) {
    /* no peer pad found, nothing to do */
    return;
  }
  if (aggregated == GST_FLOW_OK || aggregated == GST_FLOW_EOS) {
    /* those are acceptable situations */

    if (GST_PAD_IS_FLUSHING (pad) && ret == GST_FLOW_FLUSHING) {
      /* pad is flushing, always acceptable to return flushing */
      return;
    }

    if (monitor->is_eos && ret == GST_FLOW_EOS) {
      /* this element received eos and returned eos */
      return;
    }

    if (PAD_PARENT_IS_DEMUXER (monitor) && ret == GST_FLOW_EOS) {
      /* a demuxer can return EOS when the samples end */
      return;
    }
  }

  if (aggregated != ret) {
    GST_VALIDATE_REPORT (monitor, WRONG_FLOW_RETURN,
        "Wrong combined flow return %s(%d). Expected: %s(%d)",
        gst_flow_get_name (ret), ret, gst_flow_get_name (aggregated),
        aggregated);
  }
}

static void
    gst_validate_pad_monitor_otherpad_add_pending_serialized_event
    (GstValidatePadMonitor * monitor, GstEvent * event, GstClockTime last_ts)
{
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstValidatePadMonitor *othermonitor;

  if (!GST_EVENT_IS_SERIALIZED (event))
    return;

  iter =
      gst_pad_iterate_internal_links (GST_VALIDATE_PAD_MONITOR_GET_PAD
      (monitor));
  if (iter == NULL) {
    /* inputselector will return NULL if the sinkpad is not the active one .... */
    GST_FIXME_OBJECT (GST_VALIDATE_PAD_MONITOR_GET_PAD
        (monitor), "No iterator");
    return;
  }
  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);
        othermonitor =
            g_object_get_data ((GObject *) otherpad, "validate-monitor");
        if (othermonitor) {
          SerializedEventData *data = g_slice_new0 (SerializedEventData);
          data->timestamp = last_ts;
          data->event = gst_event_ref (event);
          GST_VALIDATE_MONITOR_LOCK (othermonitor);
          GST_DEBUG_OBJECT (monitor->pad, "Storing for pad %s:%s event %p %s",
              GST_DEBUG_PAD_NAME (otherpad), event,
              GST_EVENT_TYPE_NAME (event));
          g_ptr_array_add (othermonitor->serialized_events, data);
          debug_pending_event (otherpad, othermonitor->serialized_events);
          GST_VALIDATE_MONITOR_UNLOCK (othermonitor);
        }
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (monitor->pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
}

static void
gst_validate_pad_monitor_otherpad_add_pending_field (GstValidatePadMonitor *
    monitor, GstStructure * structure, const gchar * field)
{
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstValidatePadMonitor *othermonitor;
  const GValue *v;

  v = gst_structure_get_value (structure, field);
  if (v == NULL) {
    GST_DEBUG_OBJECT (monitor->pad, "Not adding pending field %s as it isn't "
        "present on structure %" GST_PTR_FORMAT, field, structure);
    return;
  }

  iter =
      gst_pad_iterate_internal_links (GST_VALIDATE_PAD_MONITOR_GET_PAD
      (monitor));
  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);
        othermonitor =
            g_object_get_data ((GObject *) otherpad, "validate-monitor");
        if (othermonitor) {
          GST_VALIDATE_MONITOR_LOCK (othermonitor);
          g_assert (othermonitor->pending_setcaps_fields != NULL);
          gst_structure_set_value (othermonitor->pending_setcaps_fields,
              field, v);
          GST_VALIDATE_MONITOR_UNLOCK (othermonitor);
        }
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (monitor->pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
}

static void
gst_validate_pad_monitor_otherpad_clear_pending_fields (GstValidatePadMonitor *
    monitor)
{
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstValidatePadMonitor *othermonitor;

  iter =
      gst_pad_iterate_internal_links (GST_VALIDATE_PAD_MONITOR_GET_PAD
      (monitor));
  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);
        othermonitor =
            g_object_get_data ((GObject *) otherpad, "validate-monitor");
        if (othermonitor) {
          GST_VALIDATE_MONITOR_LOCK (othermonitor);
          g_assert (othermonitor->pending_setcaps_fields != NULL);
          gst_structure_free (othermonitor->pending_setcaps_fields);
          othermonitor->pending_setcaps_fields =
              gst_structure_new_empty (PENDING_FIELDS);
          GST_VALIDATE_MONITOR_UNLOCK (othermonitor);
        }
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (monitor->pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
}

static void
gst_validate_pad_monitor_add_expected_newsegment (GstValidatePadMonitor *
    monitor, GstEvent * event)
{
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstValidatePadMonitor *othermonitor;

  iter =
      gst_pad_iterate_internal_links (GST_VALIDATE_PAD_MONITOR_GET_PAD
      (monitor));
  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);
        othermonitor =
            g_object_get_data ((GObject *) otherpad, "validate-monitor");
        GST_VALIDATE_MONITOR_LOCK (othermonitor);
        gst_event_replace (&othermonitor->expected_segment, event);
        GST_VALIDATE_MONITOR_UNLOCK (othermonitor);
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (monitor->pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
}

static void
gst_validate_pad_monitor_flush (GstValidatePadMonitor * pad_monitor)
{
  pad_monitor->current_timestamp = GST_CLOCK_TIME_NONE;
  pad_monitor->current_duration = GST_CLOCK_TIME_NONE;
  pad_monitor->timestamp_range_start = GST_CLOCK_TIME_NONE;
  pad_monitor->timestamp_range_end = GST_CLOCK_TIME_NONE;
  pad_monitor->has_segment = FALSE;
  pad_monitor->is_eos = FALSE;
  pad_monitor->last_flow_return = GST_FLOW_OK;
  gst_caps_replace (&pad_monitor->last_caps, NULL);
  pad_monitor->caps_is_audio = pad_monitor->caps_is_video = FALSE;

  g_list_free_full (pad_monitor->expired_events,
      (GDestroyNotify) gst_event_unref);
  pad_monitor->expired_events = NULL;

  if (pad_monitor->serialized_events->len)
    g_ptr_array_remove_range (pad_monitor->serialized_events, 0,
        pad_monitor->serialized_events->len);
}

/* common checks for both sink and src event functions */
static void
gst_validate_pad_monitor_common_event_check (GstValidatePadMonitor *
    pad_monitor, GstEvent * event)
{
  guint32 seqnum = gst_event_get_seqnum (event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    {
      if (pad_monitor->pending_flush_start_seqnum) {
        if (seqnum == pad_monitor->pending_flush_start_seqnum) {
          pad_monitor->pending_flush_start_seqnum = 0;
        } else {
          GST_VALIDATE_REPORT (pad_monitor, EVENT_HAS_WRONG_SEQNUM,
              "The expected flush-start seqnum should be the same as the "
              "one from the event that caused it (probably a seek). Got: %u."
              " Expected: %u", seqnum, pad_monitor->pending_flush_start_seqnum);
        }
      }

      if (pad_monitor->pending_flush_stop) {
        GST_VALIDATE_REPORT (pad_monitor, EVENT_FLUSH_START_UNEXPECTED,
            "Received flush-start from " " when flush-stop was expected");
      }
      pad_monitor->pending_flush_stop = TRUE;
    }
      break;
    case GST_EVENT_FLUSH_STOP:
    {
      if (pad_monitor->pending_flush_stop_seqnum) {
        if (seqnum == pad_monitor->pending_flush_stop_seqnum) {
          pad_monitor->pending_flush_stop_seqnum = 0;
        } else {
          GST_VALIDATE_REPORT (pad_monitor, EVENT_HAS_WRONG_SEQNUM,
              "The expected flush-stop seqnum should be the same as the "
              "one from the event that caused it (probably a seek). Got: %u."
              " Expected: %u", seqnum, pad_monitor->pending_flush_stop_seqnum);
        }
      }

      if (!pad_monitor->pending_flush_stop) {
        GST_VALIDATE_REPORT (pad_monitor, EVENT_FLUSH_STOP_UNEXPECTED,
            "Unexpected flush-stop %p" GST_PTR_FORMAT, event);
      }
      pad_monitor->pending_flush_stop = FALSE;

      /* cleanup our data */
      gst_validate_pad_monitor_flush (pad_monitor);
    }
      break;
    default:
      break;
  }
}

static gboolean
gst_validate_pad_monitor_downstream_event_check (GstValidatePadMonitor *
    pad_monitor, GstObject * parent, GstEvent * event,
    GstPadEventFunction handler)
{
  gboolean ret = TRUE;
  const GstSegment *segment;
  guint32 seqnum = gst_event_get_seqnum (event);
  GstPad *pad = GST_VALIDATE_PAD_MONITOR_GET_PAD (pad_monitor);

  gst_validate_pad_monitor_common_event_check (pad_monitor, event);

  /* pre checks */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      /* parse segment data to be used if event is handled */
      gst_event_parse_segment (event, &segment);

      if (pad_monitor->pending_newsegment_seqnum) {
        if (pad_monitor->pending_newsegment_seqnum == seqnum) {
          pad_monitor->pending_newsegment_seqnum = 0;
        } else {
          /* TODO is this an error? could be a segment from the start
           * received just before the seek segment */
        }
      }

      /* got a segment, no need for EOS now */
      pad_monitor->pending_eos_seqnum = 0;

      if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
        gst_validate_pad_monitor_add_expected_newsegment (pad_monitor, event);
      } else {
        /* check if this segment is the expected one */
        if (pad_monitor->expected_segment) {
          const GstSegment *exp_segment;

          if (pad_monitor->expected_segment != event) {
            gst_event_parse_segment (pad_monitor->expected_segment,
                &exp_segment);
            if (segment->format == exp_segment->format) {
              if ((exp_segment->rate * exp_segment->applied_rate !=
                      segment->rate * segment->applied_rate)
                  || exp_segment->start != segment->start
                  || exp_segment->stop != segment->stop
                  || exp_segment->position != segment->position) {
                GST_VALIDATE_REPORT (pad_monitor, EVENT_NEW_SEGMENT_MISMATCH,
                    "Expected segment didn't match received segment event");
              }
            }
          }
          gst_event_replace (&pad_monitor->expected_segment, NULL);
        }
      }
      break;
    case GST_EVENT_CAPS:{
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      gst_validate_pad_monitor_setcaps_pre (pad_monitor, caps);
      break;
    }
    case GST_EVENT_EOS:
      pad_monitor->is_eos = TRUE;
      if (pad_monitor->pending_eos_seqnum &&
          pad_monitor->pending_eos_seqnum != seqnum) {
        GST_VALIDATE_REPORT (pad_monitor, EVENT_HAS_WRONG_SEQNUM,
            "The expected EOS seqnum should be the same as the "
            "one from the seek that caused it. Got: %u."
            " Expected: %u", seqnum, pad_monitor->pending_eos_seqnum);
      }
      /*
       * TODO add end of stream checks for
       *  - events not pushed
       *  - buffer data not pushed
       *  - pending events not received
       */
      break;

      /* both flushes are handled by the common event function */
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_TAG:
    case GST_EVENT_SINK_MESSAGE:
    default:
      break;
  }

  GST_VALIDATE_MONITOR_UNLOCK (pad_monitor);
  GST_VALIDATE_PAD_MONITOR_PARENT_UNLOCK (pad_monitor);
  gst_validate_pad_monitor_event_overrides (pad_monitor, event);
  if (handler) {
    gst_event_ref (event);
    ret = pad_monitor->event_func (pad, parent, event);
  }
  GST_VALIDATE_PAD_MONITOR_PARENT_LOCK (pad_monitor);
  GST_VALIDATE_MONITOR_LOCK (pad_monitor);

  /* post checks */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      if (ret) {
        if (!pad_monitor->has_segment
            && pad_monitor->segment.format != segment->format) {
          gst_segment_init (&pad_monitor->segment, segment->format);
        }
        gst_segment_copy_into (segment, &pad_monitor->segment);
        pad_monitor->has_segment = TRUE;
      }
      break;
    case GST_EVENT_CAPS:{
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      gst_validate_pad_monitor_setcaps_post (pad_monitor, caps, ret);
      break;
    }
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_EOS:
    case GST_EVENT_TAG:
    case GST_EVENT_SINK_MESSAGE:
    default:
      break;
  }

  if (handler)
    gst_event_unref (event);
  return ret;
}

static gboolean
gst_validate_pad_monitor_src_event_check (GstValidatePadMonitor * pad_monitor,
    GstObject * parent, GstEvent * event, GstPadEventFunction handler)
{
  gboolean ret = TRUE;
  gdouble rate;
  GstFormat format;
  gint64 start, stop;
  GstSeekFlags seek_flags;
  GstSeekType start_type, stop_type;
  guint32 seqnum = gst_event_get_seqnum (event);
  GstPad *pad = GST_VALIDATE_PAD_MONITOR_GET_PAD (pad_monitor);

  gst_validate_pad_monitor_common_event_check (pad_monitor, event);

  /* pre checks */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gst_event_parse_seek (event, &rate, &format, &seek_flags, &start_type,
          &start, &stop_type, &stop);
      /* upstream seek - store the seek event seqnum to check
       * flushes and newsegments share the same */

      /* TODO we might need to use a list as multiple seeks can be sent
       * before the flushes arrive here */
      if (seek_flags & GST_SEEK_FLAG_FLUSH) {
        pad_monitor->pending_flush_start_seqnum = seqnum;
        pad_monitor->pending_flush_stop_seqnum = seqnum;
      }
      pad_monitor->pending_newsegment_seqnum = seqnum;
      pad_monitor->pending_eos_seqnum = seqnum;
    }
      break;
      /* both flushes are handled by the common event handling function */
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_LATENCY:
    case GST_EVENT_STEP:
    case GST_EVENT_QOS:
    default:
      break;
  }

  if (handler) {
    GST_VALIDATE_MONITOR_UNLOCK (pad_monitor);
    gst_event_ref (event);
    ret = pad_monitor->event_func (pad, parent, event);
    GST_VALIDATE_MONITOR_LOCK (pad_monitor);
  }

  /* post checks */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_QOS:
    case GST_EVENT_SEEK:
    {
      if (ret == FALSE) {
        /* do not expect any of these events anymore */
        pad_monitor->pending_flush_start_seqnum = 0;
        pad_monitor->pending_flush_stop_seqnum = 0;
        pad_monitor->pending_newsegment_seqnum = 0;
        pad_monitor->pending_eos_seqnum = 0;
      }
    }
      break;
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_LATENCY:
    case GST_EVENT_STEP:
    default:
      break;
  }

  if (handler)
    gst_event_unref (event);
  return ret;
}

static GstFlowReturn
gst_validate_pad_monitor_chain_func (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstValidatePadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "validate-monitor");
  GstFlowReturn ret;

  GST_VALIDATE_PAD_MONITOR_PARENT_LOCK (pad_monitor);
  GST_VALIDATE_MONITOR_LOCK (pad_monitor);

  gst_validate_pad_monitor_check_first_buffer (pad_monitor, buffer);
  gst_validate_pad_monitor_update_buffer_data (pad_monitor, buffer);

  GST_VALIDATE_MONITOR_UNLOCK (pad_monitor);
  GST_VALIDATE_PAD_MONITOR_PARENT_UNLOCK (pad_monitor);

  gst_validate_pad_monitor_buffer_overrides (pad_monitor, buffer);

  ret = pad_monitor->chain_func (pad, parent, buffer);

  GST_VALIDATE_PAD_MONITOR_PARENT_LOCK (pad_monitor);
  GST_VALIDATE_MONITOR_LOCK (pad_monitor);

  pad_monitor->last_flow_return = ret;
  if (PAD_PARENT_IS_DEMUXER (pad_monitor))
    gst_validate_pad_monitor_check_aggregated_return (pad_monitor, ret);

  GST_VALIDATE_MONITOR_UNLOCK (pad_monitor);
  GST_VALIDATE_PAD_MONITOR_PARENT_UNLOCK (pad_monitor);

  return ret;
}

static gboolean
gst_validate_pad_monitor_event_is_tracked (GstValidatePadMonitor * monitor,
    GstEvent * event)
{
  if (!GST_EVENT_IS_SERIALIZED (event)) {
    return FALSE;
  }

  /* we don't track Tag events because they mutate too much and it is hard
   * to match a tag event pushed on a source pad with the one that was received
   * on a sink pad.
   * One idea would be to use seqnum, but it seems that it is undefined whether
   * seqnums should be maintained in tag events that are created from others
   * up to today. (2013-08-29)
   */
  if (GST_EVENT_TYPE (event) == GST_EVENT_TAG)
    return FALSE;

  return TRUE;
}

static gboolean
gst_validate_pad_monitor_sink_event_func (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstValidatePadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "validate-monitor");
  gboolean ret;

  GST_VALIDATE_PAD_MONITOR_PARENT_LOCK (pad_monitor);
  GST_VALIDATE_MONITOR_LOCK (pad_monitor);

  if (gst_validate_pad_monitor_event_is_tracked (pad_monitor, event)) {
    GstClockTime last_ts = GST_CLOCK_TIME_NONE;
    if (GST_CLOCK_TIME_IS_VALID (pad_monitor->current_timestamp)) {
      last_ts = pad_monitor->current_timestamp;
      if (GST_CLOCK_TIME_IS_VALID (pad_monitor->current_duration)) {
        last_ts += pad_monitor->current_duration;
      }
    }
    gst_validate_pad_monitor_otherpad_add_pending_serialized_event (pad_monitor,
        event, last_ts);
  }

  ret =
      gst_validate_pad_monitor_downstream_event_check (pad_monitor, parent,
      event, pad_monitor->event_func);

  GST_VALIDATE_MONITOR_UNLOCK (pad_monitor);
  GST_VALIDATE_PAD_MONITOR_PARENT_UNLOCK (pad_monitor);
  return ret;
}

static gboolean
gst_validate_pad_monitor_src_event_func (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstValidatePadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "validate-monitor");
  gboolean ret;

  GST_VALIDATE_MONITOR_LOCK (pad_monitor);
  ret = gst_validate_pad_monitor_src_event_check (pad_monitor, parent, event,
      pad_monitor->event_func);
  GST_VALIDATE_MONITOR_UNLOCK (pad_monitor);
  return ret;
}

static gboolean
gst_validate_pad_monitor_query_func (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstValidatePadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "validate-monitor");
  gboolean ret;

  gst_validate_pad_monitor_query_overrides (pad_monitor, query);

  ret = pad_monitor->query_func (pad, parent, query);

  if (ret) {
    switch (GST_QUERY_TYPE (query)) {
      case GST_QUERY_CAPS:{
        GstCaps *res;
        /* We shouldn't need to lock the parent as this doesn't modify
         * other monitors, just does some peer_pad_caps */
        GST_VALIDATE_MONITOR_LOCK (pad_monitor);

        gst_query_parse_caps_result (query, &res);
        if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
          gst_validate_pad_monitor_check_caps_fields_proxied (pad_monitor, res);
        }
        GST_VALIDATE_MONITOR_UNLOCK (pad_monitor);
        break;
      }
      default:
        break;
    }
  }

  return ret;
}

static gboolean
gst_validate_pad_monitor_activatemode_func (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstValidatePadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "validate-monitor");
  gboolean ret = TRUE;

  /* TODO add overrides for activate func */

  if (pad_monitor->activatemode_func)
    ret = pad_monitor->activatemode_func (pad, parent, mode, active);
  if (ret && active == FALSE) {
    GST_VALIDATE_MONITOR_LOCK (pad_monitor);
    gst_validate_pad_monitor_flush (pad_monitor);
    GST_VALIDATE_MONITOR_UNLOCK (pad_monitor);
  }

  return ret;
}

static gboolean
gst_validate_pad_get_range_func (GstPad * pad, GstObject * parent,
    guint64 offset, guint size, GstBuffer ** buffer)
{
  GstValidatePadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "validate-monitor");
  gboolean ret;
  ret = pad_monitor->getrange_func (pad, parent, offset, size, buffer);
  return ret;
}

static gboolean
gst_validate_pad_monitor_buffer_probe (GstPad * pad, GstBuffer * buffer,
    gpointer udata)
{
  GstValidatePadMonitor *monitor = udata;

  GST_VALIDATE_PAD_MONITOR_PARENT_LOCK (monitor);
  GST_VALIDATE_MONITOR_LOCK (monitor);

  gst_validate_pad_monitor_check_first_buffer (monitor, buffer);
  gst_validate_pad_monitor_update_buffer_data (monitor, buffer);

  if (PAD_PARENT_IS_DECODER (monitor) || PAD_PARENT_IS_ENCODER (monitor)) {
    GstClockTime tolerance = 0;

    if (monitor->caps_is_audio)
      tolerance = AUDIO_TIMESTAMP_TOLERANCE;

    gst_validate_pad_monitor_check_buffer_timestamp_in_received_range (monitor,
        buffer, tolerance);
  }

  gst_validate_pad_monitor_check_late_serialized_events (monitor,
      GST_BUFFER_TIMESTAMP (buffer));

  /* a GstValidatePadMonitor parent must be a GstValidateElementMonitor */
  if (PAD_PARENT_IS_DECODER (monitor)) {

    /* should not push out of segment data */
    if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer)) &&
        GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer)) &&
        !gst_segment_clip (&monitor->segment, monitor->segment.format,
            GST_BUFFER_TIMESTAMP (buffer), GST_BUFFER_TIMESTAMP (buffer) +
            GST_BUFFER_DURATION (buffer), NULL, NULL)) {
      /* TODO is this a timestamp issue? */
      GST_VALIDATE_REPORT (monitor, BUFFER_IS_OUT_OF_SEGMENT,
          "buffer is out of segment and shouldn't be pushed. Timestamp: %"
          GST_TIME_FORMAT " - duration: %" GST_TIME_FORMAT ". Range: %"
          GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
          GST_TIME_ARGS (monitor->segment.start),
          GST_TIME_ARGS (monitor->segment.stop));
    }
  }

  GST_VALIDATE_MONITOR_UNLOCK (monitor);
  GST_VALIDATE_PAD_MONITOR_PARENT_UNLOCK (monitor);
  gst_validate_pad_monitor_buffer_probe_overrides (monitor, buffer);
  return TRUE;
}

static gboolean
gst_validate_pad_monitor_event_probe (GstPad * pad, GstEvent * event,
    gpointer udata)
{
  GstValidatePadMonitor *monitor = GST_VALIDATE_PAD_MONITOR_CAST (udata);
  gboolean ret;

  GST_VALIDATE_PAD_MONITOR_PARENT_LOCK (monitor);
  GST_VALIDATE_MONITOR_LOCK (monitor);

  GST_DEBUG_OBJECT (pad, "event %p %s", event, GST_EVENT_TYPE_NAME (event));

  if (GST_EVENT_IS_SERIALIZED (event)) {
    gint i;

    /* Detect if events the element received are being forwarded in the same order
     *
     * Several scenarios:
     * 1) The element pushes the event as-is
     * 2) The element consumes the event and does not forward it
     * 3) The element consumes the event and creates another one instead
     * 4) The element pushes other serialized event before pushing out the
     *    one it received
     *
     * For each pad we have two lists to track serialized events:
     *  1) We received on input and expect to see (serialized_events)
     *  2) We received on input but don't expect to see (expired_events)
     *
     * To detect events that are pushed in a different order from the one they were
     * received in we check that:
     *
     * For each event being outputted:
     *   If it is in the expired_events list:
     *     RAISE WARNING
     *   If it is in the serialized_events list:
     *     If there are other events that were received before:
     *        Put those events on the expired_events list
     *     Remove that event and any previous ones from the serialized_events list
     *
     * Clear expired events list when flushing or on pad deactivation
     *
     */

    if (g_list_find (monitor->expired_events, event)) {
      /* If it's the expired events, we've failed */
      GST_WARNING_OBJECT (pad, "Did not expect event %p %s", event,
          GST_EVENT_TYPE_NAME (event));
      GST_VALIDATE_REPORT (monitor, EVENT_SERIALIZED_OUT_OF_ORDER,
          "Serialized event was pushed out of order: %" GST_PTR_FORMAT, event);
      monitor->expired_events = g_list_remove (monitor->expired_events, event);
      gst_event_unref (event);  /* remove the ref that was on the list */
    } else if (monitor->serialized_events->len) {
      for (i = 0; i < monitor->serialized_events->len; i++) {
        SerializedEventData *next_event =
            g_ptr_array_index (monitor->serialized_events, i);
        GST_DEBUG_OBJECT (pad, "Checking against stored event #%d: %p %s", i,
            next_event->event, GST_EVENT_TYPE_NAME (next_event->event));

        if (event == next_event->event
            || GST_EVENT_TYPE (event) == GST_EVENT_TYPE (next_event->event)) {
          /* We have found our event */
          GST_DEBUG_OBJECT (pad, "Found matching event");

          while (monitor->serialized_events->len > i
              && GST_EVENT_TYPE (event) == GST_EVENT_TYPE (next_event->event)) {
            /* Swallow all expected events of the same type */
            g_ptr_array_remove_index (monitor->serialized_events, i);
            next_event = g_ptr_array_index (monitor->serialized_events, i);
          }

          /* Move all previous events to expired events */
          if (G_UNLIKELY (i > 0)) {
            GST_DEBUG_OBJECT (pad,
                "Moving previous expected events to expired list");
            while (i--) {
              next_event = g_ptr_array_index (monitor->serialized_events, 0);
              monitor->expired_events =
                  g_list_append (monitor->expired_events,
                  gst_event_ref (next_event->event));
              g_ptr_array_remove_index (monitor->serialized_events, 0);
            }
          }
          debug_pending_event (pad, monitor->serialized_events);
          break;
        }
      }
    }
  }

  /* This so far is just like an event that is flowing downstream,
   * so we do the same checks as a sinkpad event handler */
  ret =
      gst_validate_pad_monitor_downstream_event_check (monitor, NULL, event,
      NULL);
  GST_VALIDATE_MONITOR_UNLOCK (monitor);
  GST_VALIDATE_PAD_MONITOR_PARENT_UNLOCK (monitor);

  return ret;
}

static GstPadProbeReturn
gst_validate_pad_monitor_pad_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer udata)
{
  if (info->type & GST_PAD_PROBE_TYPE_BUFFER)
    gst_validate_pad_monitor_buffer_probe (pad, info->data, udata);
  else if (info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)
    gst_validate_pad_monitor_event_probe (pad, info->data, udata);

  return GST_PAD_PROBE_OK;
}

static void
gst_validate_pad_monitor_update_caps_info (GstValidatePadMonitor * pad_monitor,
    GstCaps * caps)
{
  GstStructure *structure;

  g_return_if_fail (gst_caps_is_fixed (caps));

  pad_monitor->caps_is_audio = FALSE;
  pad_monitor->caps_is_video = FALSE;

  structure = gst_caps_get_structure (caps, 0);
  if (g_str_has_prefix (gst_structure_get_name (structure), "audio/")) {
    pad_monitor->caps_is_audio = TRUE;
  } else if (g_str_has_prefix (gst_structure_get_name (structure), "video/")) {
    pad_monitor->caps_is_video = TRUE;
  }
}

static void
gst_validate_pad_monitor_setcaps_pre (GstValidatePadMonitor * pad_monitor,
    GstCaps * caps)
{
  GstStructure *structure;

  /* Check if caps are identical to last caps and complain if so
   * Only checked for sink pads as src pads might push the same caps
   * multiple times during unlinked/autoplugging scenarios */
  if (GST_PAD_IS_SINK (GST_VALIDATE_PAD_MONITOR_GET_PAD (pad_monitor)) &&
      pad_monitor->last_caps
      && gst_caps_is_equal (caps, pad_monitor->last_caps)) {
    GST_VALIDATE_REPORT (pad_monitor, EVENT_CAPS_DUPLICATE, "%" GST_PTR_FORMAT,
        caps);
  }

  gst_validate_pad_monitor_check_caps_complete (pad_monitor, caps);

  if (caps) {
    structure = gst_caps_get_structure (caps, 0);
    if (gst_structure_n_fields (pad_monitor->pending_setcaps_fields)) {
      gint i;
      for (i = 0;
          i < gst_structure_n_fields (pad_monitor->pending_setcaps_fields);
          i++) {
        const gchar *name =
            gst_structure_nth_field_name (pad_monitor->pending_setcaps_fields,
            i);
        const GValue *v = gst_structure_get_value (structure, name);
        const GValue *otherv =
            gst_structure_get_value (pad_monitor->pending_setcaps_fields, name);

        if (v == NULL) {
          GST_VALIDATE_REPORT (pad_monitor, CAPS_EXPECTED_FIELD_NOT_FOUND,
              "Field %s is missing from setcaps caps '%" GST_PTR_FORMAT "'",
              name, caps);
        } else if (gst_value_compare (v, otherv) != GST_VALUE_EQUAL) {
          GST_VALIDATE_REPORT (pad_monitor, CAPS_FIELD_UNEXPECTED_VALUE,
              "Field %s from setcaps caps '%" GST_PTR_FORMAT "' is different "
              "from expected value in caps '%" GST_PTR_FORMAT "'", name, caps,
              pad_monitor->pending_setcaps_fields);
        }
      }
    }

    if (gst_validate_pad_monitor_pad_should_proxy_othercaps (pad_monitor)) {
      if (_structure_is_video (structure)) {
        gst_validate_pad_monitor_otherpad_add_pending_field (pad_monitor,
            structure, "width");
        gst_validate_pad_monitor_otherpad_add_pending_field (pad_monitor,
            structure, "height");
        gst_validate_pad_monitor_otherpad_add_pending_field (pad_monitor,
            structure, "framerate");
        gst_validate_pad_monitor_otherpad_add_pending_field (pad_monitor,
            structure, "pixel-aspect-ratio");
      } else if (_structure_is_audio (structure)) {
        gst_validate_pad_monitor_otherpad_add_pending_field (pad_monitor,
            structure, "rate");
        gst_validate_pad_monitor_otherpad_add_pending_field (pad_monitor,
            structure, "channels");
      }
    }
  }

  gst_structure_free (pad_monitor->pending_setcaps_fields);
  pad_monitor->pending_setcaps_fields =
      gst_structure_new_empty (PENDING_FIELDS);

  gst_validate_pad_monitor_setcaps_overrides (pad_monitor, caps);
}

static void
gst_validate_pad_monitor_setcaps_post (GstValidatePadMonitor * pad_monitor,
    GstCaps * caps, gboolean ret)
{
  if (!ret)
    gst_validate_pad_monitor_otherpad_clear_pending_fields (pad_monitor);
  else {
    if (pad_monitor->last_caps) {
      gst_caps_unref (pad_monitor->last_caps);
    }
    pad_monitor->last_caps = gst_caps_ref (caps);
    gst_validate_pad_monitor_update_caps_info (pad_monitor, caps);
  }
}

static gboolean
gst_validate_pad_monitor_do_setup (GstValidateMonitor * monitor)
{
  GstValidatePadMonitor *pad_monitor = GST_VALIDATE_PAD_MONITOR_CAST (monitor);
  GstPad *pad;
  if (!GST_IS_PAD (GST_VALIDATE_MONITOR_GET_OBJECT (monitor))) {
    GST_WARNING_OBJECT (monitor, "Trying to create pad monitor with other "
        "type of object");
    return FALSE;
  }

  pad = GST_VALIDATE_PAD_MONITOR_GET_PAD (pad_monitor);

  if (g_object_get_data ((GObject *) pad, "validate-monitor")) {
    GST_WARNING_OBJECT (pad_monitor,
        "Pad already has a validate-monitor associated");
    return FALSE;
  }

  g_object_set_data ((GObject *) pad, "validate-monitor", pad_monitor);

  pad_monitor->pad = pad;

  pad_monitor->event_func = GST_PAD_EVENTFUNC (pad);
  pad_monitor->query_func = GST_PAD_QUERYFUNC (pad);
  pad_monitor->activatemode_func = GST_PAD_ACTIVATEMODEFUNC (pad);
  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {

    pad_monitor->chain_func = GST_PAD_CHAINFUNC (pad);
    if (pad_monitor->chain_func)
      gst_pad_set_chain_function (pad, gst_validate_pad_monitor_chain_func);

    gst_pad_set_event_function (pad, gst_validate_pad_monitor_sink_event_func);
  } else {
    pad_monitor->getrange_func = GST_PAD_GETRANGEFUNC (pad);
    if (pad_monitor->getrange_func)
      gst_pad_set_getrange_function (pad, gst_validate_pad_get_range_func);

    gst_pad_set_event_function (pad, gst_validate_pad_monitor_src_event_func);

    /* add buffer/event probes */
    pad_monitor->pad_probe_id =
        gst_pad_add_probe (pad,
        GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM |
        GST_PAD_PROBE_TYPE_EVENT_FLUSH,
        (GstPadProbeCallback) gst_validate_pad_monitor_pad_probe, pad_monitor,
        NULL);
  }
  gst_pad_set_query_function (pad, gst_validate_pad_monitor_query_func);
  gst_pad_set_activatemode_function (pad,
      gst_validate_pad_monitor_activatemode_func);

  gst_validate_reporter_set_name (GST_VALIDATE_REPORTER (monitor),
      g_strdup_printf ("%s:%s", GST_DEBUG_PAD_NAME (pad)));

  if (G_UNLIKELY (GST_PAD_PARENT (pad) == NULL))
    GST_FIXME ("Saw a pad not belonging to any object");

  return TRUE;
}
