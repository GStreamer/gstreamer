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
#include "gst-validate-pipeline-monitor.h"
#include "gst-validate-reporter.h"
#include "gst-validate-utils.h"
#include "validate.h"
#include <string.h>
#include <stdarg.h>

/**
 * SECTION:gst-validate-pad-monitor
 * @short_description: Class that wraps a #GstPad for Validate checks
 *
 * TODO
 */
#define _GET_PAD_MONITOR(p) g_object_get_qdata ((GObject*) p, _Q_VALIDATE_MONITOR)
#define _SET_PAD_MONITOR(p,d) g_object_set_qdata ((GObject*) p, _Q_VALIDATE_MONITOR, d)

static GstValidateInterceptionReturn
gst_validate_pad_monitor_intercept_report (GstValidateReporter * reporter,
    GstValidateReport * report);

#define _do_init \
  G_IMPLEMENT_INTERFACE (GST_TYPE_VALIDATE_REPORTER, _reporter_iface_init)

static void
_reporter_iface_init (GstValidateReporterInterface * iface)
{
  iface->intercept_report = gst_validate_pad_monitor_intercept_report;
}

#define gst_validate_pad_monitor_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstValidatePadMonitor, gst_validate_pad_monitor,
    GST_TYPE_VALIDATE_MONITOR, _do_init);

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

#define PAD_PARENT_IS_SINK(m) \
    (GST_VALIDATE_MONITOR_GET_PARENT(m) ? \
        GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_SINK ( \
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

/* Structure used to store all seek-related information */
struct _GstValidatePadSeekData
{
  guint32 seqnum;
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
};

typedef struct
{
  GstClockTime timestamp;
  GstEvent *event;
} SerializedEventData;

static GstPad *
_get_actual_pad (GstPad * pad)
{
  pad = gst_object_ref (pad);

  while (GST_IS_PROXY_PAD (pad)) {
    GstPad *next_pad;

    if (GST_PAD_IS_SINK (pad)) {
      if (GST_IS_GHOST_PAD (pad))
        next_pad = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));
      else
        next_pad = GST_PAD (gst_proxy_pad_get_internal (GST_PROXY_PAD (pad)));
    } else {
      next_pad = gst_pad_get_peer (pad);
    }

    gst_object_unref (pad);
    if (!next_pad)
      return NULL;

    pad = next_pad;
  }

  return pad;
}

static gboolean
_find_master_report_on_pad (GstPad * pad, GstValidateReport * report)
{
  GstValidatePadMonitor *pad_monitor;
  GstValidateReport *prev_report;
  gboolean result = FALSE;
  GstPad *tmppad = pad;

  pad = _get_actual_pad (pad);
  if (pad == NULL) {
    GST_ERROR_OBJECT (tmppad, "Does not have a target yet");

    return FALSE;
  }

  pad_monitor = _GET_PAD_MONITOR (pad);

  /* For some reason this pad isn't monitored */
  if (pad_monitor == NULL)
    goto done;

  prev_report = gst_validate_reporter_get_report ((GstValidateReporter *)
      pad_monitor, report->issue->issue_id);

  if (prev_report) {
    if (prev_report->master_report)
      result = gst_validate_report_set_master_report (report,
          prev_report->master_report);
    else
      result = gst_validate_report_set_master_report (report, prev_report);
  }

done:
  gst_object_unref (pad);

  return result;
}

static gboolean
_find_master_report_for_sink_pad (GstValidatePadMonitor * pad_monitor,
    GstValidateReport * report)
{
  GstPad *peerpad;
  gboolean result = FALSE;
  GstPad *pad =
      GST_PAD_CAST (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (pad_monitor)));

  peerpad = gst_pad_get_peer (pad);
  gst_object_unref (pad);

  /* If the peer src pad already has a similar report no need to look
   * any further */
  if (peerpad && _find_master_report_on_pad (peerpad, report))
    result = TRUE;

  if (peerpad)
    gst_object_unref (peerpad);

  return result;
}

static gboolean
_find_master_report_for_src_pad (GstValidatePadMonitor * pad_monitor,
    GstValidateReport * report)
{
  GstIterator *iter;
  gboolean done;
  gboolean result = FALSE;
  GstPad *target =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (pad_monitor)));

  iter = gst_pad_iterate_internal_links (target);
  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
      {
        GstPad *pad = g_value_get_object (&value);

        if (_find_master_report_on_pad (pad, report)) {
          result = TRUE;
          done = TRUE;
        }

        g_value_reset (&value);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (target, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_object_unref (target);
  gst_iterator_free (iter);

  return result;
}

static GstValidateInterceptionReturn
_concatenate_issues (GstValidatePadMonitor * pad_monitor,
    GstValidateReport * report)
{
  GstPad *pad =
      GST_PAD_CAST (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (pad_monitor)));

  if (GST_PAD_IS_SINK (pad)
      && _find_master_report_for_sink_pad (pad_monitor, report)) {
    gst_object_unref (pad);
    return GST_VALIDATE_REPORTER_KEEP;
  } else if (GST_PAD_IS_SRC (pad)
      && _find_master_report_for_src_pad (pad_monitor, report)) {
    gst_object_unref (pad);
    return GST_VALIDATE_REPORTER_KEEP;
  }

  gst_object_unref (pad);
  return GST_VALIDATE_REPORTER_REPORT;
}

static GstValidateInterceptionReturn
gst_validate_pad_monitor_intercept_report (GstValidateReporter *
    reporter, GstValidateReport * report)
{
  GstValidateReporterInterface *iface_class, *old_iface_class;
  GstValidatePadMonitor *pad_monitor = GST_VALIDATE_PAD_MONITOR (reporter);
  GstValidateReportingDetails monitor_reporting_level;
  GstValidateInterceptionReturn ret;

  monitor_reporting_level =
      gst_validate_reporter_get_reporting_level (reporter);

  iface_class =
      G_TYPE_INSTANCE_GET_INTERFACE (reporter, GST_TYPE_VALIDATE_REPORTER,
      GstValidateReporterInterface);
  old_iface_class = g_type_interface_peek_parent (iface_class);

  old_iface_class->intercept_report (reporter, report);

  switch (monitor_reporting_level) {
    case GST_VALIDATE_SHOW_NONE:
      ret = GST_VALIDATE_REPORTER_DROP;
      break;
    case GST_VALIDATE_SHOW_UNKNOWN:
      ret = _concatenate_issues (pad_monitor, report);
      break;
    default:
      ret = GST_VALIDATE_REPORTER_REPORT;
      break;
  }

  gst_validate_report_set_reporting_level (report, monitor_reporting_level);
  return ret;
}

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
  g_free (serialized_event);
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

static gchar *
_get_event_string (GstEvent * event)
{
  const GstStructure *st;

  if ((st = gst_event_get_structure (event)))
    return gst_structure_to_string (st);
  else
    return g_strdup_printf ("%s", GST_EVENT_TYPE_NAME (event));
}

static void
_check_field_type (GstValidatePadMonitor * monitor,
    GstStructure * structure, gboolean mandatory, const gchar * field, ...)
{
  va_list var_args;
  GType type;
  gchar *joined_types = NULL;
  const gchar *rejected_types[5];
  gint rejected_types_index = 0;
  gchar *struct_str;

  if (!gst_structure_has_field (structure, field)) {
    if (mandatory) {
      gchar *str = gst_structure_to_string (structure);

      GST_VALIDATE_REPORT (monitor, CAPS_IS_MISSING_FIELD,
          "Field '%s' is missing from structure: %s", field, str);
      g_free (str);
    } else {
      GST_DEBUG_OBJECT (monitor, "Field %s is missing but is not mandatory",
          field);
    }
    return;
  }

  memset ((gchar **) rejected_types, 0, sizeof (rejected_types));
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
  struct_str = gst_structure_to_string (structure);
  GST_VALIDATE_REPORT (monitor, CAPS_FIELD_HAS_BAD_TYPE,
      "Field '%s' has wrong type %s in structure '%s'. Expected: %s", field,
      g_type_name (gst_structure_get_field_type (structure, field)), struct_str,
      joined_types);
  g_free (joined_types);
  g_free (struct_str);
}

static void
gst_validate_pad_monitor_check_raw_video_caps_complete (GstValidatePadMonitor *
    monitor, GstStructure * structure)
{
  _check_field_type (monitor, structure, TRUE, "width", G_TYPE_INT,
      GST_TYPE_INT_RANGE, 0);
  _check_field_type (monitor, structure, TRUE, "height", G_TYPE_INT,
      GST_TYPE_INT_RANGE, 0);
  _check_field_type (monitor, structure, TRUE, "framerate", GST_TYPE_FRACTION,
      GST_TYPE_FRACTION_RANGE, 0);
  _check_field_type (monitor, structure, FALSE, "pixel-aspect-ratio",
      GST_TYPE_FRACTION, GST_TYPE_FRACTION_RANGE, 0);
  _check_field_type (monitor, structure, TRUE, "format", G_TYPE_STRING,
      GST_TYPE_LIST);
}

static void
gst_validate_pad_monitor_check_raw_audio_caps_complete (GstValidatePadMonitor *
    monitor, GstStructure * structure)
{
  gint channels;
  _check_field_type (monitor, structure, TRUE, "format", G_TYPE_STRING,
      GST_TYPE_LIST, 0);
  _check_field_type (monitor, structure, TRUE, "layout", G_TYPE_STRING,
      GST_TYPE_LIST, 0);
  _check_field_type (monitor, structure, TRUE, "rate", G_TYPE_INT,
      GST_TYPE_LIST, GST_TYPE_INT_RANGE, 0);
  _check_field_type (monitor, structure, TRUE, "channels", G_TYPE_INT,
      GST_TYPE_LIST, GST_TYPE_INT_RANGE, 0);
  if (gst_structure_get_int (structure, "channels", &channels)) {
    if (channels > 2)
      _check_field_type (monitor, structure, TRUE, "channel-mask",
          GST_TYPE_BITMASK, GST_TYPE_LIST, 0);
  }
}

static void
gst_validate_pad_monitor_check_caps_complete (GstValidatePadMonitor * monitor,
    GstCaps * caps)
{
  GstStructure *structure;
  gint i;

  GST_DEBUG_OBJECT (monitor, "Checking caps %" GST_PTR_FORMAT, caps);

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
gst_validate_pad_monitor_get_othercaps (GstValidatePadMonitor * monitor,
    GstCaps * filter)
{
  GstCaps *caps = gst_caps_new_empty ();
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstCaps *peercaps;
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (monitor)));

  iter = gst_pad_iterate_internal_links (pad);
  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);

        /* TODO What would be the correct caps operation to merge the caps in
         * case one sink is internally linked to multiple srcs? */
        peercaps = gst_pad_peer_query_caps (otherpad, filter);
        if (peercaps)
          caps = gst_caps_merge (caps, peercaps);

        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        gst_caps_unref (caps);
        caps = gst_caps_new_empty ();
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  GST_DEBUG_OBJECT (pad, "Otherpad caps: %" GST_PTR_FORMAT, caps);

  gst_iterator_free (iter);
  gst_object_unref (pad);

  return caps;
}

static gboolean
_structure_is_video (GstStructure * structure)
{
  const gchar *name = gst_structure_get_name (structure);

  return g_strstr_len (name, 6, "video/")
      && g_strcmp0 (name, "video/quicktime") != 0;
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
  return (GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_DECODER (parent) ||
      GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_ENCODER (parent)) &&
      !GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_CONVERTER (parent);
}


/* Check if the field @f from @s2 (if present) is represented in @s1
 * Represented here means either equal or @s1's value is in a list/range
 * from @s2
 */
static gboolean
_structures_field_is_contained (GstStructure * s1, GstStructure * s2,
    gboolean mandatory, const gchar * f)
{
  const GValue *v1;
  const GValue *v2;

  v2 = gst_structure_get_value (s2, f);
  if (!v2)
    return TRUE;                /* nothing to compare to */

  v1 = gst_structure_get_value (s1, f);
  if (!v1)
    return !mandatory;

  if (!gst_value_is_fixed (v1))
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
_check_and_copy_structure_field (GstStructure * from, GstStructure * to,
    const gchar * name)
{
  if (gst_structure_has_field (from, name)) {
    gst_structure_set_value (to, name, gst_structure_get_value (from, name));
  }
}

static GstCaps *
gst_validate_pad_monitor_copy_caps_fields_into_caps (GstValidatePadMonitor *
    monitor, GstCaps * from_caps, GstCaps * into_caps)
{
  gint i, j, into_size, from_size;
  GstStructure *structure;
  GstCaps *res = gst_caps_new_empty ();

  into_size = gst_caps_get_size (into_caps);
  from_size = gst_caps_get_size (from_caps);

  for (i = 0; i < into_size; i++) {
    GstStructure *s = gst_caps_get_structure (into_caps, i);

    for (j = 0; j < from_size; j++) {
      GstStructure *new_structure = gst_structure_copy (s);

      structure = gst_caps_get_structure (from_caps, j);
      if (_structure_is_video (structure)) {
        _check_and_copy_structure_field (structure, new_structure, "width");
        _check_and_copy_structure_field (structure, new_structure, "height");
        _check_and_copy_structure_field (structure, new_structure, "framerate");
        _check_and_copy_structure_field (structure, new_structure,
            "pixel-aspect-ratio");
      } else if (_structure_is_audio (s)) {
        _check_and_copy_structure_field (structure, new_structure, "rate");
        _check_and_copy_structure_field (structure, new_structure, "channels");
      }

      gst_caps_append_structure (res, new_structure);
    }
  }
  return res;
}

static GstCaps *
gst_validate_pad_monitor_transform_caps (GstValidatePadMonitor * monitor,
    GstCaps * caps)
{
  GstCaps *othercaps;
  GstCaps *new_caps;
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstCaps *template_caps;
  GstPad *pad;


  GST_DEBUG_OBJECT (monitor, "Transform caps %" GST_PTR_FORMAT, caps);

  if (caps == NULL)
    return NULL;

  othercaps = gst_caps_new_empty ();

  pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (monitor)));
  iter = gst_pad_iterate_internal_links (pad);
  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);
        template_caps = gst_pad_get_pad_template_caps (otherpad);

        new_caps =
            gst_validate_pad_monitor_copy_caps_fields_into_caps (monitor, caps,
            template_caps);
        if (!gst_caps_is_empty (new_caps))
          gst_caps_append (othercaps, new_caps);
        else
          gst_caps_unref (new_caps);

        gst_caps_unref (template_caps);
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        gst_caps_unref (othercaps);
        othercaps = gst_caps_new_empty ();
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);

  GST_DEBUG_OBJECT (pad, "Transformed caps: %" GST_PTR_FORMAT, othercaps);
  gst_object_unref (pad);

  return othercaps;
}

static void
gst_validate_pad_monitor_check_caps_fields_proxied (GstValidatePadMonitor *
    monitor, GstCaps * caps, GstCaps * filter)
{
  GstStructure *structure;
  GstStructure *otherstructure;
  GstCaps *othercaps;
  GstCaps *otherfilter;
  gint i, j;

  if (!gst_validate_pad_monitor_pad_should_proxy_othercaps (monitor))
    return;

  otherfilter = gst_validate_pad_monitor_transform_caps (monitor, filter);
  othercaps = gst_validate_pad_monitor_get_othercaps (monitor, otherfilter);
  if (otherfilter)
    gst_caps_unref (otherfilter);

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
          if (_structures_field_is_contained (structure, otherstructure, TRUE,
                  "width")
              && _structures_field_is_contained (structure, otherstructure,
                  TRUE, "height")
              && _structures_field_is_contained (structure, otherstructure,
                  TRUE, "framerate")
              && _structures_field_is_contained (structure, otherstructure,
                  FALSE, "pixel-aspect-ratio")) {
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
          if (_structures_field_is_contained (structure, otherstructure, TRUE,
                  "rate")
              && _structures_field_is_contained (structure, otherstructure,
                  TRUE, "channels")) {
            found = TRUE;
            break;
          }
        }
      }
    }

    if (type_match && !found) {
      gchar *otherstruct_str = gst_structure_to_string (otherstructure),
          *caps_str = gst_caps_to_string (caps);

      GST_VALIDATE_REPORT (monitor, GET_CAPS_NOT_PROXYING_FIELDS,
          "Peer pad structure '%s' has no similar version "
          "on pad's caps '%s'", otherstruct_str, caps_str);

      g_free (otherstruct_str);
      g_free (caps_str);
    }
  }

  gst_caps_unref (othercaps);
}

static void
gst_validate_pad_monitor_check_late_serialized_events (GstValidatePadMonitor *
    monitor, GstClockTime ts)
{
  gint i;
  GstPad *pad;

  if (!GST_CLOCK_TIME_IS_VALID (ts))
    return;

  pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (monitor)));

  GST_DEBUG_OBJECT (pad, "Timestamp to check %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ts));

  for (i = 0; i < monitor->serialized_events->len; i++) {
    SerializedEventData *data =
        g_ptr_array_index (monitor->serialized_events, i);

    GST_DEBUG_OBJECT (pad, "Event #%d (%s) ts: %" GST_TIME_FORMAT,
        i, GST_EVENT_TYPE_NAME (data->event), GST_TIME_ARGS (data->timestamp));

    if (GST_CLOCK_TIME_IS_VALID (data->timestamp) && data->timestamp < ts) {
      gchar *event_str = _get_event_string (data->event);

      GST_VALIDATE_REPORT (monitor, SERIALIZED_EVENT_WASNT_PUSHED_IN_TIME,
          "Serialized event %s wasn't pushed before expected timestamp %"
          GST_TIME_FORMAT " on pad %s:%s", event_str,
          GST_TIME_ARGS (data->timestamp), GST_DEBUG_PAD_NAME (pad));

      g_free (event_str);
    } else {
      /* events should be ordered by ts */
      break;
    }
  }

  if (i) {
    debug_pending_event (pad, monitor->serialized_events);
    g_ptr_array_remove_range (monitor->serialized_events, 0, i);
  }

  gst_object_unref (pad);
}

static void
seek_data_free (GstValidatePadSeekData * data)
{
  g_free (data);
}

static GstValidatePadSeekData *
seek_data_for_seqnum (GstValidatePadMonitor * monitor, guint32 seqnum)
{
  GList *tmp;

  for (tmp = monitor->seeks; tmp; tmp = tmp->next) {
    GstValidatePadSeekData *data = (GstValidatePadSeekData *) tmp->data;
    if (data->seqnum == seqnum)
      return data;
  }

  return NULL;
}

static void
gst_validate_pad_monitor_dispose (GObject * object)
{
  GstValidatePadMonitor *monitor = GST_VALIDATE_PAD_MONITOR_CAST (object);
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (monitor)));

  if (pad) {
    if (monitor->pad_probe_id)
      gst_pad_remove_probe (pad, monitor->pad_probe_id);
    gst_object_unref (pad);
  }

  if (monitor->expected_segment)
    gst_event_unref (monitor->expected_segment);

  gst_structure_free (monitor->pending_setcaps_fields);
  g_ptr_array_unref (monitor->serialized_events);
  g_list_free_full (monitor->expired_events, (GDestroyNotify) gst_event_unref);
  g_list_free_full (monitor->all_bufs, (GDestroyNotify) gst_buffer_unref);
  gst_caps_replace (&monitor->last_caps, NULL);
  gst_caps_replace (&monitor->last_query_res, NULL);
  gst_caps_replace (&monitor->last_query_filter, NULL);
  gst_caps_replace (&monitor->last_refused_caps, NULL);

  g_list_free_full (monitor->seeks, (GDestroyNotify) seek_data_free);

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

/* Called when a pad is being flushed */
static void
gst_validate_pad_monitor_flush (GstValidatePadMonitor * pad_monitor)
{
  /* Note: Keep in the same order as in the GstValidatePadMonitor structure */

  gst_caps_replace (&pad_monitor->last_caps, NULL);
  pad_monitor->caps_is_audio = pad_monitor->caps_is_video =
      pad_monitor->caps_is_raw = FALSE;

  pad_monitor->first_buffer = TRUE;

  pad_monitor->has_segment = FALSE;
  pad_monitor->is_eos = FALSE;

  pad_monitor->pending_buffer_discont = TRUE;

  gst_event_replace (&pad_monitor->expected_segment, NULL);
  if (pad_monitor->serialized_events->len)
    g_ptr_array_remove_range (pad_monitor->serialized_events, 0,
        pad_monitor->serialized_events->len);
  g_list_free_full (pad_monitor->expired_events,
      (GDestroyNotify) gst_event_unref);
  pad_monitor->expired_events = NULL;

  gst_segment_init (&pad_monitor->segment, GST_FORMAT_BYTES);
  pad_monitor->current_timestamp = GST_CLOCK_TIME_NONE;
  pad_monitor->current_duration = GST_CLOCK_TIME_NONE;

  pad_monitor->timestamp_range_start = GST_CLOCK_TIME_NONE;
  pad_monitor->timestamp_range_end = GST_CLOCK_TIME_NONE;
}

/* Called when the pad monitor is initialized or when
 * the pad is deactivated */
static void
gst_validate_pad_monitor_reset (GstValidatePadMonitor * pad_monitor)
{
  gst_validate_pad_monitor_flush (pad_monitor);

  /* Note : For the entries that haven't been reset in _flush(), do
   * it here and keep in the same order as the GstValidatePadMonitor
   * structure */

  pad_monitor->pending_flush_stop = FALSE;
  pad_monitor->pending_newsegment_seqnum = GST_SEQNUM_INVALID;
  pad_monitor->pending_eos_seqnum = GST_SEQNUM_INVALID;

  if (pad_monitor->pending_setcaps_fields)
    gst_structure_free (pad_monitor->pending_setcaps_fields);
  pad_monitor->pending_setcaps_fields =
      gst_structure_new_empty (PENDING_FIELDS);
  if (pad_monitor->seeks)
    g_list_free_full (pad_monitor->seeks, (GDestroyNotify) seek_data_free);
  pad_monitor->current_seek = NULL;
  pad_monitor->seeks = NULL;

  /* FIXME : Why BYTES and not UNDEFINED ? */
  gst_segment_init (&pad_monitor->segment, GST_FORMAT_BYTES);

  pad_monitor->min_buf_freq = 0;
  pad_monitor->buffers_pushed = 0;
  pad_monitor->last_buffers_pushed = 0;
  pad_monitor->min_buf_freq_interval_ts = GST_CLOCK_TIME_NONE;
  pad_monitor->min_buf_freq_first_buffer_ts = GST_CLOCK_TIME_NONE;
  pad_monitor->min_buf_freq_start = GST_CLOCK_TIME_NONE;
}

static void
gst_validate_pad_monitor_init (GstValidatePadMonitor * pad_monitor)
{
  pad_monitor->serialized_events =
      g_ptr_array_new_with_free_func ((GDestroyNotify)
      _serialized_event_data_free);

  gst_validate_pad_monitor_reset (pad_monitor);
}

/**
 * gst_validate_pad_monitor_new:
 * @pad: (transfer none): a #GstPad to run Validate on
 * @runner: (transfer none): a #GstValidateRunner
 * @parent: (nullable): The parent of the new monitor
 *
 * Returns: (transfer full): A #GstValidatePadMonitor
 */
GstValidatePadMonitor *
gst_validate_pad_monitor_new (GstPad * pad, GstValidateRunner * runner,
    GstValidateElementMonitor * parent)
{
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);
  g_return_val_if_fail (runner != NULL, NULL);

  return g_object_new (GST_TYPE_VALIDATE_PAD_MONITOR,
      "object", pad, "validate-runner", runner, "validate-parent",
      parent, NULL);
}

static GstElement *
gst_validate_pad_monitor_get_element (GstValidateMonitor * monitor)
{
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (monitor)));
  GstElement *parent = GST_ELEMENT (gst_pad_get_parent (pad));

  gst_object_unref (pad);

  return parent;
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
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (monitor)));

  GST_DEBUG_OBJECT (pad,
      "Checking if timestamp %" GST_TIME_FORMAT " is in range: %"
      GST_TIME_FORMAT " - %" GST_TIME_FORMAT " for pad "
      "%s:%s with tolerance: %" GST_TIME_FORMAT, GST_TIME_ARGS (ts),
      GST_TIME_ARGS (monitor->timestamp_range_start),
      GST_TIME_ARGS (monitor->timestamp_range_end), GST_DEBUG_PAD_NAME (pad),
      GST_TIME_ARGS (tolerance));
  gst_object_unref (pad);

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
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (monitor)));

  if (!GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer))
      || !GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer))) {
    GST_DEBUG_OBJECT (pad,
        "Can't check buffer timestamps range as "
        "buffer has no valid timestamp/duration");
    goto done;
  }

  ts = GST_BUFFER_TIMESTAMP (buffer);
  ts_end = ts + GST_BUFFER_DURATION (buffer);

  iter = gst_pad_iterate_internal_links (pad);

  if (iter == NULL) {
    GST_WARNING_OBJECT (pad, "No iterator available");
    goto done;
  }

  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);
        GST_DEBUG_OBJECT (pad, "Checking pad %s:%s input timestamps",
            GST_DEBUG_PAD_NAME (otherpad));
        othermonitor = _GET_PAD_MONITOR (otherpad);
        if (!othermonitor)
          continue;

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
        GST_WARNING_OBJECT (pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);

  if (!has_one) {
    GST_DEBUG_OBJECT (pad, "Skipping timestamp in range check as no "
        "internal linked pad was found");
    goto done;
  }
  if (!found) {
    GST_VALIDATE_REPORT (monitor, BUFFER_TIMESTAMP_OUT_OF_RECEIVED_RANGE,
        "Timestamp %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT
        " is out of range of received input", GST_TIME_ARGS (ts),
        GST_TIME_ARGS (ts_end));
  }
done:
  if (pad)
    gst_object_unref (pad);
}

static void
gst_validate_pad_monitor_check_discont (GstValidatePadMonitor * pad_monitor,
    GstBuffer * buffer)
{
  if (pad_monitor->pending_buffer_discont) {
    if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))
      GST_VALIDATE_REPORT (pad_monitor, BUFFER_MISSING_DISCONT,
          "Buffer is missing a DISCONT flag");
    pad_monitor->pending_buffer_discont = FALSE;
  }
}

static void
gst_validate_pad_monitor_check_first_buffer (GstValidatePadMonitor *
    pad_monitor, GstBuffer * buffer)
{
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (pad_monitor)));

  if (G_UNLIKELY (pad_monitor->first_buffer)) {
    pad_monitor->first_buffer = FALSE;

    if (!pad_monitor->has_segment && PAD_IS_IN_PUSH_MODE (pad)) {
      GST_VALIDATE_REPORT (pad_monitor, BUFFER_BEFORE_SEGMENT,
          "Received buffer before Segment event");
    }

    GST_DEBUG_OBJECT (pad,
        "Checking first buffer (pts:%" GST_TIME_FORMAT " dts:%" GST_TIME_FORMAT
        ")", GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
        GST_TIME_ARGS (GST_BUFFER_DTS (buffer)));

  }

  gst_object_unref (pad);
}

static void
gst_validate_pad_monitor_check_eos (GstValidatePadMonitor *
    pad_monitor, GstBuffer * buffer)
{
  if (G_UNLIKELY (pad_monitor->is_eos)) {
    GST_VALIDATE_REPORT (pad_monitor, BUFFER_AFTER_EOS,
        "Received buffer %" GST_PTR_FORMAT " after EOS", buffer);
  }
}

static void
gst_validate_pad_monitor_update_buffer_data (GstValidatePadMonitor *
    pad_monitor, GstBuffer * buffer)
{
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (pad_monitor)));
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
  GST_DEBUG_OBJECT (pad, "Current stored range: %" GST_TIME_FORMAT
      " - %" GST_TIME_FORMAT,
      GST_TIME_ARGS (pad_monitor->timestamp_range_start),
      GST_TIME_ARGS (pad_monitor->timestamp_range_end));

  gst_object_unref (pad);
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
    monitor, GstObject * parent, GstFlowReturn ret)
{
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstPad *peerpad;
  GstState state, pending;
  GstFlowReturn aggregated = GST_FLOW_NOT_LINKED;
  gboolean found_a_pad = FALSE;
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (monitor)));

  iter = gst_pad_iterate_internal_links (pad);
  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);
        peerpad = gst_pad_get_peer (otherpad);
        if (peerpad) {
          found_a_pad = TRUE;
          aggregated =
              _combine_flows (aggregated,
              gst_pad_get_last_flow_return (peerpad));

          gst_object_unref (peerpad);
        }
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (pad, "Internal links pad iteration error");
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
    goto done;
  }

  if (aggregated == GST_FLOW_FLUSHING) {
    gst_element_get_state (GST_ELEMENT (parent), &state, &pending, 0);
    if (state < GST_STATE_PAUSED || pending < GST_STATE_PAUSED) {
      /* Aggregated is flushing, we might have been aggregating a combination
       * of pads that are not what was present on the element during the actual
       * data flow combination (pads might have been removed meanwhile) */

      goto done;
    }
  } else if (aggregated == GST_FLOW_OK || aggregated == GST_FLOW_EOS) {

    /* those are acceptable situations */
    if (GST_PAD_IS_FLUSHING (pad) && ret == GST_FLOW_FLUSHING) {
      /* pad is flushing, always acceptable to return flushing */
      goto done;
    }

    gst_element_get_state (GST_ELEMENT (parent), &state, &pending, 0);
    if (ret == GST_FLOW_FLUSHING && (state < GST_STATE_PAUSED
            || pending < GST_STATE_PAUSED)) {
      /* Element is being teared down, accept FLOW_FLUSHING */

      goto done;
    }

    if (monitor->is_eos && ret == GST_FLOW_EOS) {
      /* this element received eos and returned eos */
      goto done;
    }

    if (PAD_PARENT_IS_DEMUXER (monitor) && ret == GST_FLOW_EOS) {
      /* a demuxer can return EOS when the samples end */
      goto done;
    }
  }

  if (aggregated != ret) {
    GST_VALIDATE_REPORT (monitor, WRONG_FLOW_RETURN,
        "Wrong combined flow return %s(%d). Expected: %s(%d)",
        gst_flow_get_name (ret), ret, gst_flow_get_name (aggregated),
        aggregated);
  }

done:
  gst_object_unref (pad);
}

static void
    gst_validate_pad_monitor_otherpad_add_pending_serialized_event
    (GstValidatePadMonitor * monitor, GstEvent * event, GstClockTime last_ts)
{
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstValidatePadMonitor *othermonitor;
  GstPad *pad;


  if (!GST_EVENT_IS_SERIALIZED (event))
    return;

  pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (monitor)));
  iter = gst_pad_iterate_internal_links (pad);
  if (iter == NULL) {
    /* inputselector will return NULL if the sinkpad is not the active one .... */
    GST_FIXME_OBJECT (pad, "No iterator");
    gst_object_unref (pad);
    return;
  }

  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);
        othermonitor = _GET_PAD_MONITOR (otherpad);
        if (othermonitor) {
          SerializedEventData *data = g_new0 (SerializedEventData, 1);
          data->timestamp = last_ts;
          data->event = gst_event_ref (event);
          GST_VALIDATE_MONITOR_LOCK (othermonitor);
          GST_DEBUG_OBJECT (pad, "Storing for pad %s:%s event %p %s",
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
        GST_WARNING_OBJECT (pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
  gst_object_unref (pad);
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
  GstPad *pad;

  v = gst_structure_get_value (structure, field);
  pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (monitor)));

  if (v == NULL) {
    GST_DEBUG_OBJECT (pad, "Not adding pending field %s as it isn't "
        "present on structure %" GST_PTR_FORMAT, field, structure);
    gst_object_unref (pad);
    return;
  }

  iter = gst_pad_iterate_internal_links (pad);
  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);
        othermonitor = _GET_PAD_MONITOR (otherpad);
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
        GST_WARNING_OBJECT (pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
  gst_object_unref (pad);
}

static void
gst_validate_pad_monitor_otherpad_clear_pending_fields (GstValidatePadMonitor *
    monitor)
{
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstValidatePadMonitor *othermonitor;

  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (monitor)));

  iter = gst_pad_iterate_internal_links (pad);
  if (iter == NULL) {
    gst_object_unref (pad);
    GST_DEBUG_OBJECT (monitor, "No internally linked pad");

    return;
  }

  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);
        othermonitor = _GET_PAD_MONITOR (otherpad);
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
        GST_WARNING_OBJECT (pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_object_unref (pad);
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
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (monitor)));

  iter = gst_pad_iterate_internal_links (pad);
  if (iter == NULL) {
    GST_DEBUG_OBJECT (monitor, "No internally linked pad");
    gst_object_unref (pad);
    return;
  }

  done = FALSE;
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        otherpad = g_value_get_object (&value);
        if (!otherpad) {
          g_value_reset (&value);
          continue;
        }

        othermonitor = _GET_PAD_MONITOR (otherpad);
        if (!othermonitor) {
          g_value_reset (&value);
          continue;
        }

        GST_VALIDATE_MONITOR_LOCK (othermonitor);
        gst_event_replace (&othermonitor->expected_segment, event);
        GST_VALIDATE_MONITOR_UNLOCK (othermonitor);
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (pad, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
  gst_object_unref (pad);
}

/* common checks for both sink and src event functions */
static void
gst_validate_pad_monitor_common_event_check (GstValidatePadMonitor *
    pad_monitor, GstEvent * event)
{
  guint32 seqnum = gst_event_get_seqnum (event);

  if (seqnum == GST_SEQNUM_INVALID)
    GST_VALIDATE_REPORT (pad_monitor, EVENT_INVALID_SEQNUM,
        "Event %p (%s) has an invalid SEQNUM", event,
        GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    {
      if (pad_monitor->seeks) {
        GstValidatePadSeekData *seekdata =
            seek_data_for_seqnum (pad_monitor, seqnum);

        if (!seekdata)
          GST_VALIDATE_REPORT (pad_monitor, FLUSH_START_HAS_WRONG_SEQNUM,
              "Got: %" G_GUINT32_FORMAT " Expected: %" G_GUINT32_FORMAT, seqnum,
              ((GstValidatePadSeekData *) pad_monitor->seeks->data)->seqnum);
        else {
          if (!(seekdata->flags & GST_SEEK_FLAG_FLUSH)) {
            GST_VALIDATE_REPORT (pad_monitor, EVENT_FLUSH_START_UNEXPECTED,
                "Received flush-start for a non-flushing seek");
          }
        }
      }

      if (pad_monitor->pending_flush_stop) {
        GST_VALIDATE_REPORT (pad_monitor, EVENT_FLUSH_START_UNEXPECTED,
            "Received flush-start from when flush-stop was expected");
      }
      pad_monitor->pending_flush_stop = TRUE;
      /* Remove the current segment seekdata */
      if (pad_monitor->current_seek) {
        pad_monitor->seeks =
            g_list_remove (pad_monitor->seeks, pad_monitor->current_seek);
        seek_data_free (pad_monitor->current_seek);
        pad_monitor->current_seek = NULL;
      }
    }
      break;
    case GST_EVENT_FLUSH_STOP:
    {
      if (pad_monitor->seeks && !seek_data_for_seqnum (pad_monitor, seqnum)) {
        GST_VALIDATE_REPORT (pad_monitor, FLUSH_STOP_HAS_WRONG_SEQNUM,
            "Got: %" G_GUINT32_FORMAT " Expected: %" G_GUINT32_FORMAT, seqnum,
            ((GstValidatePadSeekData *) pad_monitor->seeks->data)->seqnum);
      }

      pad_monitor->pending_newsegment_seqnum = seqnum;
      pad_monitor->pending_eos_seqnum = seqnum;

      if (!pad_monitor->pending_flush_stop) {
        gchar *event_str = _get_event_string (event);

        GST_VALIDATE_REPORT (pad_monitor, EVENT_FLUSH_STOP_UNEXPECTED,
            "Unexpected flush-stop %s", event_str);
        g_free (event_str);
      }
      pad_monitor->pending_flush_stop = FALSE;

      /* Buffers following a FLUSH should have the DISCONT flag set */
      pad_monitor->pending_buffer_discont = TRUE;

      /* cleanup our data */
      gst_validate_pad_monitor_flush (pad_monitor);
    }
      break;
    default:
      break;
  }
}

static void
mark_pads_eos (GstValidatePadMonitor * pad_monitor)
{
  GstValidatePadMonitor *peer_monitor;
  GstPad *real_peer;
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (pad_monitor)));
  GstPad *peer = gst_pad_get_peer (pad);

  gst_object_unref (pad);
  pad_monitor->is_eos = TRUE;
  if (peer) {
    real_peer = _get_actual_pad (peer);
    peer_monitor = _GET_PAD_MONITOR (real_peer);
    if (peer_monitor)
      peer_monitor->is_eos = TRUE;
    gst_object_unref (peer);
    gst_object_unref (real_peer);
  }
}

static inline gboolean
_should_check_buffers (GstValidatePadMonitor * pad_monitor,
    gboolean force_checks)
{
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (pad_monitor)));
  GstValidateMonitor *monitor = GST_VALIDATE_MONITOR (pad_monitor);

  if (pad_monitor->first_buffer || force_checks) {
    if (pad_monitor->segment.rate != 1.0) {
      GST_INFO_OBJECT (pad_monitor, "We do not support buffer checking"
          " for trick modes");

      pad_monitor->check_buffers = FALSE;
    } else if (!PAD_PARENT_IS_DECODER (pad_monitor)) {
      GST_DEBUG_OBJECT (pad, "Not on a decoder => no buffer checking");

      pad_monitor->check_buffers = FALSE;
    } else if (GST_PAD_DIRECTION (pad) != GST_PAD_SINK) {
      GST_DEBUG_OBJECT (pad, "Not a sinkpad => no buffer checking");

      pad_monitor->check_buffers = FALSE;
    } else if (!pad_monitor->caps_is_video) {
      GST_DEBUG_OBJECT (pad, "Not working with video => no buffer checking");

      pad_monitor->check_buffers = FALSE;
    } else if (monitor->media_descriptor == NULL) {
      GST_DEBUG_OBJECT (pad, "No media_descriptor set => no buffer checking");

      pad_monitor->check_buffers = FALSE;
    } else if (!gst_validate_media_descriptor_detects_frames
        (monitor->media_descriptor)) {
      GST_DEBUG_OBJECT (pad,
          "No frame detection media descriptor => no buffer checking");
      pad_monitor->check_buffers = FALSE;
    } else if (pad_monitor->all_bufs == NULL &&
        !gst_validate_media_descriptor_get_buffers (monitor->media_descriptor,
            pad, NULL, &pad_monitor->all_bufs)) {

      GST_INFO_OBJECT (monitor,
          "The MediaInfo is marked as detecting frame, but getting frames"
          " from pad %" GST_PTR_FORMAT " did not work (some format conversion"
          " might be happening)", pad);

      pad_monitor->check_buffers = FALSE;
    } else {
      if (!pad_monitor->current_buf)
        pad_monitor->current_buf = pad_monitor->all_bufs;
      pad_monitor->check_buffers = TRUE;
    }
  }
  gst_object_unref (pad);

  return pad_monitor->check_buffers;
}

static void
gst_validate_monitor_find_next_buffer (GstValidatePadMonitor * pad_monitor)
{
  GList *tmp;
  gboolean passed_start = FALSE;

  if (!_should_check_buffers (pad_monitor, TRUE))
    return;

  for (tmp = g_list_last (pad_monitor->all_bufs); tmp; tmp = tmp->prev) {
    GstBuffer *cbuf = (GstBuffer *) tmp->data;
    GstClockTime ts =
        GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DTS (cbuf)) ? GST_BUFFER_DTS (cbuf)
        : GST_BUFFER_PTS (cbuf);

    if (!GST_CLOCK_TIME_IS_VALID (ts))
      continue;

    if (ts <= pad_monitor->segment.start)
      passed_start = TRUE;

    if (!passed_start)
      continue;

    if (!GST_BUFFER_FLAG_IS_SET (cbuf, GST_BUFFER_FLAG_DELTA_UNIT)) {
      break;
    }
  }

  if (tmp == NULL)
    pad_monitor->current_buf = pad_monitor->all_bufs;
  else
    pad_monitor->current_buf = tmp;
}

static void
post_segment_message (GstValidatePadMonitor * pad_monitor, GstPad * pad,
    const GstSegment * segment, guint32 seqnum)
{
  GstValidateMonitor *element_monitor =
      GST_VALIDATE_MONITOR_GET_PARENT (pad_monitor);
  GstElement *element;
  GstStructure *structure;
  GstMessage *msg;

  if (element_monitor == NULL)
    return;

  element = gst_validate_monitor_get_element (element_monitor);
  if (element == NULL)
    return;

  GST_DEBUG_OBJECT (pad,
      "Posting application message for seqnum:%" G_GUINT32_FORMAT " %"
      GST_SEGMENT_FORMAT, seqnum, segment);

  structure =
      gst_structure_new ("validate-segment", "segment", GST_TYPE_SEGMENT,
      segment, NULL);
  msg = gst_message_new_application ((GstObject *) element, structure);
  gst_message_set_seqnum (msg, seqnum);
  gst_element_post_message (element, msg);

  gst_object_unref (element);

  return;
}

/* Checks whether a segment is just an update of another,
 * That is to say that only the base and offset field differ and all
 * other fields are identical */
static gboolean
is_segment_update (GstSegment * a, const GstSegment * b)
{
  /* Note : We never care about the position field, it is only
   * used for internal usage by elements */
  if (a->rate == b->rate &&
      a->applied_rate == b->applied_rate &&
      a->format == b->format && a->time == b->time) {
    /* Changes in base/offset are considered updates */
    /* Updating the end position of a segment is an update */
    /* Updating the duration of a segment is an update */
    if (a->rate > 0.0) {
      if (a->start == b->start)
        return TRUE;
    } else {
      if (a->stop == b->stop)
        return TRUE;
    }
  }
  return FALSE;
}

static GstFlowReturn
gst_validate_pad_monitor_downstream_event_check (GstValidatePadMonitor *
    pad_monitor, GstObject * parent, GstEvent * event,
    GstPadEventFunction handler)
{
  GstFlowReturn ret = GST_FLOW_OK;
  const GstSegment *segment;
  guint32 seqnum = gst_event_get_seqnum (event);
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (pad_monitor)));

  gst_validate_pad_monitor_common_event_check (pad_monitor, event);

  /* pre checks */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
      /* Buffers following a STREAM_START should have the DISCONT flag set */
      pad_monitor->pending_buffer_discont = TRUE;
      break;
    case GST_EVENT_SEGMENT:
    {
      GstValidatePadSeekData *seekdata =
          seek_data_for_seqnum (pad_monitor, seqnum);

      /* parse segment data to be used if event is handled */
      gst_event_parse_segment (event, &segment);

      GST_DEBUG_OBJECT (pad,
          "Got segment seqnum:%" G_GUINT32_FORMAT " %" GST_SEGMENT_FORMAT,
          seqnum, segment);

      if (pad_monitor->pending_newsegment_seqnum != GST_SEQNUM_INVALID) {
        /* FIXME: Convert to more robust checks */
        if (pad_monitor->pending_newsegment_seqnum != seqnum) {
          GST_VALIDATE_REPORT (pad_monitor, SEGMENT_HAS_WRONG_SEQNUM,
              "Got: %u Expected: %u", seqnum,
              pad_monitor->pending_newsegment_seqnum);
        }
      }

      if (seekdata && seekdata != pad_monitor->current_seek) {
        /* Check for accurate seeks */
        if (seekdata->flags & GST_SEEK_FLAG_ACCURATE) {
          if (segment->time != seekdata->start)
            GST_VALIDATE_REPORT (pad_monitor, SEGMENT_HAS_WRONG_START,
                "After an accurate seek, got: %" GST_TIME_FORMAT
                " Expected: %" GST_TIME_FORMAT, GST_TIME_ARGS (segment->time),
                GST_TIME_ARGS (seekdata->start));
        }
      }

      pad_monitor->pending_eos_seqnum = seqnum;

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
                      segment->rate * segment->applied_rate))
                GST_VALIDATE_REPORT (pad_monitor, EVENT_NEW_SEGMENT_MISMATCH,
                    "Rate * applied_rate %f != expected %f",
                    segment->rate * segment->applied_rate,
                    exp_segment->rate * exp_segment->applied_rate);
              if (exp_segment->start != segment->start)
                GST_VALIDATE_REPORT (pad_monitor, EVENT_NEW_SEGMENT_MISMATCH,
                    "Start %" GST_TIME_FORMAT " != expected %" GST_TIME_FORMAT,
                    GST_TIME_ARGS (segment->start),
                    GST_TIME_ARGS (exp_segment->start));
              if (exp_segment->stop != segment->stop)
                GST_VALIDATE_REPORT (pad_monitor, EVENT_NEW_SEGMENT_MISMATCH,
                    "Stop %" GST_TIME_FORMAT " != expected %" GST_TIME_FORMAT,
                    GST_TIME_ARGS (segment->stop),
                    GST_TIME_ARGS (exp_segment->stop));
              if (exp_segment->position != segment->position)
                GST_VALIDATE_REPORT (pad_monitor, EVENT_NEW_SEGMENT_MISMATCH,
                    "Position %" GST_TIME_FORMAT " != expected %"
                    GST_TIME_FORMAT, GST_TIME_ARGS (segment->position),
                    GST_TIME_ARGS (exp_segment->position));
            }
          }
          gst_event_replace (&pad_monitor->expected_segment, NULL);
        }
      }

      /* Drop all expected seekdata from before this segment */
      if (seekdata) {
        while (pad_monitor->seeks && pad_monitor->seeks->data != seekdata) {
          GstValidatePadSeekData *tmp =
              (GstValidatePadSeekData *) pad_monitor->seeks->data;
          pad_monitor->seeks =
              g_list_delete_link (pad_monitor->seeks, pad_monitor->seeks);
          seek_data_free (tmp);
        }
      }
      pad_monitor->current_seek = seekdata;
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
      /* FIXME : This feels and looks wrong ... */
      if (pad_monitor->pending_eos_seqnum == GST_SEQNUM_INVALID) {
        GST_VALIDATE_REPORT (pad_monitor, EVENT_EOS_WITHOUT_SEGMENT,
            "EOS %" GST_PTR_FORMAT " received before a segment was received",
            event);
      } else if (pad_monitor->pending_eos_seqnum != seqnum) {
        GST_VALIDATE_REPORT (pad_monitor, EOS_HAS_WRONG_SEQNUM,
            "Got: %u. Expected: %u", seqnum, pad_monitor->pending_eos_seqnum);
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
    if (pad_monitor->event_full_func)
      ret = pad_monitor->event_full_func (pad, parent, event);
    else if (pad_monitor->event_func (pad, parent, event))
      ret = GST_FLOW_OK;
    else
      ret = GST_FLOW_ERROR;
  }
  GST_VALIDATE_PAD_MONITOR_PARENT_LOCK (pad_monitor);
  GST_VALIDATE_MONITOR_LOCK (pad_monitor);

  /* post checks */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      if (ret == GST_FLOW_OK) {
        /* If the new segment is not an update of the previous one, then
         * the following buffer should have the DISCONT flag set */
        if (!is_segment_update (&pad_monitor->segment, segment))
          pad_monitor->pending_buffer_discont = TRUE;
        if (!pad_monitor->has_segment
            && pad_monitor->segment.format != segment->format) {
          gst_segment_init (&pad_monitor->segment, segment->format);
        }
        gst_segment_copy_into (segment, &pad_monitor->segment);
        pad_monitor->has_segment = TRUE;
        gst_validate_monitor_find_next_buffer (pad_monitor);
        if (PAD_PARENT_IS_SINK (pad_monitor))
          post_segment_message (pad_monitor, pad, segment, seqnum);
      }
      break;
    case GST_EVENT_CAPS:{
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      gst_validate_pad_monitor_setcaps_post (pad_monitor, caps,
          ret == GST_FLOW_OK);
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
  gst_object_unref (pad);
  return ret;
}

static GstValidatePadSeekData *
_store_seek_event_data (GstValidatePadMonitor * pad_monitor, GstEvent * event)
{
  GstValidatePadSeekData *data = g_new0 (GstValidatePadSeekData, 1);

  data->seqnum = gst_event_get_seqnum (event);
  gst_event_parse_seek (event, &data->rate, &data->format, &data->flags,
      &data->start_type, &data->start, &data->stop_type, &data->stop);

  pad_monitor->seeks = g_list_append (pad_monitor->seeks, data);

  return data;
}

static gboolean
gst_validate_pad_monitor_src_event_check (GstValidatePadMonitor * pad_monitor,
    GstObject * parent, GstEvent * event, GstPadEventFunction handler)
{
  gboolean ret = TRUE;
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (pad_monitor)));

  gst_validate_pad_monitor_common_event_check (pad_monitor, event);

  if (handler) {
    GstValidatePadSeekData *seekdata = NULL;

    GST_DEBUG_OBJECT (pad, "event %" GST_PTR_FORMAT, event);

    /* Safely store pending accurate seek values */
    if (GST_EVENT_TYPE (event) == GST_EVENT_SEEK)
      seekdata = _store_seek_event_data (pad_monitor, event);
    GST_VALIDATE_MONITOR_UNLOCK (pad_monitor);
    ret = pad_monitor->event_func (pad, parent, event);

    GST_VALIDATE_MONITOR_LOCK (pad_monitor);

    if (seekdata && !ret) {
      /* Remove failed seek from list */
      GST_LOG_OBJECT (pad, "Failed seek, removing stored seek data");
      pad_monitor->seeks = g_list_remove (pad_monitor->seeks, seekdata);
      g_free (seekdata);
    }
  }

  gst_object_unref (pad);
  return ret;
}

static gboolean
gst_validate_pad_monitor_check_right_buffer (GstValidatePadMonitor *
    pad_monitor, GstBuffer * buffer)
{
  gchar *checksum;
  GstBuffer *wanted_buf;
  GstMapInfo map, wanted_map;

  gboolean ret = TRUE;
  GstPad *pad;


  if (_should_check_buffers (pad_monitor, FALSE) == FALSE)
    return FALSE;

  pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (pad_monitor)));
  if (pad_monitor->current_buf == NULL) {
    GST_INFO_OBJECT (pad, "No current buffer one pad, Why?");
    gst_object_unref (pad);
    return FALSE;
  }

  wanted_buf = pad_monitor->current_buf->data;

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (wanted_buf)) &&
      GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (buffer)) &&
      GST_BUFFER_PTS (wanted_buf) != GST_BUFFER_PTS (buffer)) {

    GST_VALIDATE_REPORT (pad_monitor, WRONG_BUFFER,
        "buffer %" GST_PTR_FORMAT " PTS %" GST_TIME_FORMAT
        " different than expected: %" GST_TIME_FORMAT, buffer,
        GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
        GST_TIME_ARGS (GST_BUFFER_PTS (wanted_buf)));

    ret = FALSE;
  }

  if (GST_BUFFER_DTS (wanted_buf) != GST_BUFFER_DTS (buffer)) {
    GST_VALIDATE_REPORT (pad_monitor, WRONG_BUFFER,
        "buffer %" GST_PTR_FORMAT " DTS %" GST_TIME_FORMAT
        " different than expected: %" GST_TIME_FORMAT, buffer,
        GST_TIME_ARGS (GST_BUFFER_DTS (buffer)),
        GST_TIME_ARGS (GST_BUFFER_DTS (wanted_buf)));
    ret = FALSE;
  }

  if (GST_BUFFER_DURATION (wanted_buf) != GST_BUFFER_DURATION (buffer)) {
    GST_VALIDATE_REPORT (pad_monitor, WRONG_BUFFER,
        "buffer %" GST_PTR_FORMAT " DURATION %" GST_TIME_FORMAT
        " different than expected: %" GST_TIME_FORMAT, buffer,
        GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (wanted_buf)));
    ret = FALSE;
  }

  if (GST_BUFFER_FLAG_IS_SET (wanted_buf, GST_BUFFER_FLAG_DELTA_UNIT) !=
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
    GST_VALIDATE_REPORT (pad_monitor, WRONG_BUFFER,
        "buffer %" GST_PTR_FORMAT "  Delta unit is set to %s but expected %s",
        buffer, GST_BUFFER_FLAG_IS_SET (buffer,
            GST_BUFFER_FLAG_DELTA_UNIT) ? "True" : "False",
        GST_BUFFER_FLAG_IS_SET (wanted_buf,
            GST_BUFFER_FLAG_DELTA_UNIT) ? "True" : "False");
    ret = FALSE;
  }

  g_assert (gst_buffer_map (wanted_buf, &wanted_map, GST_MAP_READ));
  g_assert (gst_buffer_map (buffer, &map, GST_MAP_READ));

  checksum = g_compute_checksum_for_data (G_CHECKSUM_MD5,
      (const guchar *) map.data, map.size);

  if (g_strcmp0 ((gchar *) wanted_map.data, checksum)) {
    GST_VALIDATE_REPORT (pad_monitor, WRONG_BUFFER,
        "buffer %" GST_PTR_FORMAT " checksum %s different from expected: %s",
        buffer, checksum, wanted_map.data);
    ret = FALSE;
  }

  gst_buffer_unmap (wanted_buf, &wanted_map);
  gst_buffer_unmap (buffer, &map);
  g_free (checksum);
  gst_object_unref (pad);

  pad_monitor->current_buf = pad_monitor->current_buf->next;

  return ret;
}

static void
gst_validate_pad_monitor_check_return (GstValidatePadMonitor * pad_monitor,
    GstFlowReturn ret)
{
  GstValidateMonitor *parent = GST_VALIDATE_MONITOR (pad_monitor);

  if (ret != GST_FLOW_ERROR)
    return;

  while (GST_VALIDATE_MONITOR_GET_PARENT (parent))
    parent = GST_VALIDATE_MONITOR_GET_PARENT (parent);

  if (GST_IS_VALIDATE_PIPELINE_MONITOR (parent)) {
    GstValidatePipelineMonitor *m = GST_VALIDATE_PIPELINE_MONITOR (parent);

    GST_VALIDATE_MONITOR_LOCK (m);
    if (m->got_error == FALSE) {
      GST_VALIDATE_REPORT (pad_monitor, FLOW_ERROR_WITHOUT_ERROR_MESSAGE,
          "Pad return GST_FLOW_ERROR but no GST_MESSAGE_ERROR was received on"
          " the bus");

      /* Only report it the first time */
      m->got_error = TRUE;
    }
    GST_VALIDATE_MONITOR_UNLOCK (m);
  }
}

static GstFlowReturn
gst_validate_pad_monitor_chain_func (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstValidatePadMonitor *pad_monitor = _GET_PAD_MONITOR (pad);
  GstFlowReturn ret;

  GST_VALIDATE_PAD_MONITOR_PARENT_LOCK (pad_monitor);
  GST_VALIDATE_MONITOR_LOCK (pad_monitor);

  gst_validate_pad_monitor_check_discont (pad_monitor, buffer);
  gst_validate_pad_monitor_check_right_buffer (pad_monitor, buffer);
  gst_validate_pad_monitor_check_first_buffer (pad_monitor, buffer);
  gst_validate_pad_monitor_update_buffer_data (pad_monitor, buffer);
  gst_validate_pad_monitor_check_eos (pad_monitor, buffer);

  GST_VALIDATE_MONITOR_UNLOCK (pad_monitor);
  GST_VALIDATE_PAD_MONITOR_PARENT_UNLOCK (pad_monitor);

  gst_validate_pad_monitor_buffer_overrides (pad_monitor, buffer);

  ret = pad_monitor->chain_func (pad, parent, buffer);

  gst_validate_pad_monitor_check_return (pad_monitor, ret);

  GST_VALIDATE_PAD_MONITOR_PARENT_LOCK (pad_monitor);
  GST_VALIDATE_MONITOR_LOCK (pad_monitor);

  if (ret == GST_FLOW_EOS) {
    mark_pads_eos (pad_monitor);
  }
  if (PAD_PARENT_IS_DEMUXER (pad_monitor))
    gst_validate_pad_monitor_check_aggregated_return (pad_monitor, parent, ret);

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

static GstFlowReturn
gst_validate_pad_monitor_sink_event_full_func (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstValidatePadMonitor *pad_monitor = _GET_PAD_MONITOR (pad);
  GstFlowReturn ret;

  GST_VALIDATE_PAD_MONITOR_PARENT_LOCK (pad_monitor);
  GST_VALIDATE_MONITOR_LOCK (pad_monitor);

  GST_DEBUG_OBJECT (pad, "event %p %s", event, GST_EVENT_TYPE_NAME (event));

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
gst_validate_pad_monitor_sink_event_func (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  if (gst_validate_pad_monitor_sink_event_full_func (pad, parent,
          event) == GST_FLOW_OK)
    return TRUE;
  return FALSE;
}

static gboolean
gst_validate_pad_monitor_src_event_func (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstValidatePadMonitor *pad_monitor = _GET_PAD_MONITOR (pad);
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
  GstValidatePadMonitor *pad_monitor = _GET_PAD_MONITOR (pad);
  gboolean ret;

  gst_validate_pad_monitor_query_overrides (pad_monitor, query);
  ret = pad_monitor->query_func (pad, parent, query);

  if (ret) {
    switch (GST_QUERY_TYPE (query)) {
      case GST_QUERY_ACCEPT_CAPS:
      {
        gboolean result;

        gst_caps_replace (&pad_monitor->last_refused_caps, NULL);
        gst_query_parse_accept_caps_result (query, &result);
        if (!result) {
          GstCaps *refused_caps;

          gst_query_parse_accept_caps (query, &refused_caps);
          pad_monitor->last_refused_caps = gst_caps_copy (refused_caps);

        }

        break;
      }
      case GST_QUERY_CAPS:{
        GstCaps *res;
        GstCaps *filter;

        /* We shouldn't need to lock the parent as this doesn't modify
         * other monitors, just does some peer_pad_caps */
        GST_VALIDATE_MONITOR_LOCK (pad_monitor);

        gst_query_parse_caps (query, &filter);
        gst_query_parse_caps_result (query, &res);

        gst_caps_replace (&pad_monitor->last_query_res, NULL);
        gst_caps_replace (&pad_monitor->last_query_filter, NULL);
        pad_monitor->last_query_res =
            res ? gst_caps_copy (res) : gst_caps_ref (GST_CAPS_NONE);
        pad_monitor->last_query_filter =
            filter ? gst_caps_copy (filter) : gst_caps_ref (GST_CAPS_NONE);

        if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
          gst_validate_pad_monitor_check_caps_fields_proxied (pad_monitor, res,
              filter);
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
  GstValidatePadMonitor *pad_monitor = _GET_PAD_MONITOR (pad);
  gboolean ret = TRUE;

  /* TODO add overrides for activate func */
  GST_DEBUG_OBJECT (pad, "active:%d", active);

  if (pad_monitor->activatemode_func)
    ret = pad_monitor->activatemode_func (pad, parent, mode, active);
  if (ret && active == FALSE) {
    GST_VALIDATE_MONITOR_LOCK (pad_monitor);
    gst_validate_pad_monitor_reset (pad_monitor);
    GST_VALIDATE_MONITOR_UNLOCK (pad_monitor);
  }

  return ret;
}

static GstFlowReturn
gst_validate_pad_monitor_get_range_func (GstPad * pad, GstObject * parent,
    guint64 offset, guint length, GstBuffer ** buffer)
{
  GstValidatePadMonitor *pad_monitor = _GET_PAD_MONITOR (pad);

  if (pad_monitor->get_range_func) {
    GstPad *peer = gst_pad_get_peer (pad);
    GstTask *task = NULL;
    GThread *thread = NULL;

    if (peer) {
      GST_OBJECT_LOCK (peer);
      task = GST_PAD_TASK (peer);
      if (task && GST_TASK_STATE (task) == GST_TASK_STARTED) {
        GST_OBJECT_LOCK (task);
        /* Only doing pointer comparison, no need to hold a ref */
        thread = task->thread;
        GST_OBJECT_UNLOCK (task);
      }
      GST_OBJECT_UNLOCK (peer);

      if (thread && thread != g_thread_self ()) {
        GST_VALIDATE_REPORT (pad_monitor, PULL_RANGE_FROM_WRONG_THREAD,
            "Pulling from wrong thread, expected pad thread: %p, got %p",
            task->thread, g_thread_self ());
      }

      gst_object_unref (peer);
    }

    return pad_monitor->get_range_func (pad, parent, offset, length, buffer);
  }

  return GST_FLOW_NOT_SUPPORTED;

}

/* The interval between two buffer frequency checks */
#define BUF_FREQ_CHECK_INTERVAL (GST_SECOND)

static void
gst_validate_pad_monitor_check_buffer_freq (GstValidatePadMonitor * monitor,
    GstPad * pad)
{
  GstClockTime ts;

  if (!GST_PAD_IS_SRC (pad))
    return;

  if (!monitor->min_buf_freq)
    return;

  ts = gst_util_get_timestamp ();
  monitor->buffers_pushed++;

  /* Same logic as in fpsdisplaysink to compute the buffer frequency */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID
          (monitor->min_buf_freq_first_buffer_ts))) {
    monitor->min_buf_freq_first_buffer_ts = ts;
    monitor->min_buf_freq_interval_ts = ts;
    return;
  }

  if (GST_CLOCK_DIFF (monitor->min_buf_freq_interval_ts,
          ts) > BUF_FREQ_CHECK_INTERVAL) {
    guint time_diff;
    gdouble fps;

    time_diff = (gdouble) (ts - monitor->min_buf_freq_interval_ts) / GST_SECOND;
    fps =
        (gdouble) (monitor->buffers_pushed -
        monitor->last_buffers_pushed) / time_diff;

    if (fps < monitor->min_buf_freq) {
      if (GST_CLOCK_TIME_IS_VALID (monitor->min_buf_freq_start) &&
          GST_CLOCK_DIFF (monitor->min_buf_freq_first_buffer_ts,
              ts) < monitor->min_buf_freq_start) {
        GST_DEBUG_OBJECT (pad,
            "buffer frequency is too low (%.2f) but ignore for now (buffer-frequency-start =%"
            GST_TIME_FORMAT ")", fps,
            GST_TIME_ARGS (monitor->min_buf_freq_start));
      } else {
        GST_VALIDATE_REPORT (monitor, CONFIG_BUFFER_FREQUENCY_TOO_LOW,
            "Buffers are not pushed fast enough on this pad: %.2f/sec (minimum: %.2f)",
            fps, monitor->min_buf_freq);
      }
    }

    monitor->last_buffers_pushed = monitor->buffers_pushed;
    monitor->min_buf_freq_interval_ts = ts;
  }
}

static gboolean
gst_validate_pad_monitor_buffer_probe (GstPad * pad, GstBuffer * buffer,
    gpointer udata, gboolean pull_mode)
{
  GstValidatePadMonitor *monitor = udata;

  GST_VALIDATE_PAD_MONITOR_PARENT_LOCK (monitor);
  GST_VALIDATE_MONITOR_LOCK (monitor);

  if (!pull_mode)
    gst_validate_pad_monitor_check_discont (monitor, buffer);
  gst_validate_pad_monitor_check_first_buffer (monitor, buffer);
  gst_validate_pad_monitor_update_buffer_data (monitor, buffer);
  gst_validate_pad_monitor_check_eos (monitor, buffer);

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
        ((!gst_segment_clip (&monitor->segment, monitor->segment.format,
                    GST_BUFFER_TIMESTAMP (buffer),
                    GST_BUFFER_TIMESTAMP (buffer) +
                    GST_BUFFER_DURATION (buffer), NULL, NULL)) ||
            /* In the case of raw data, buffers should be strictly contained inside the
             * segment */
            (monitor->caps_is_raw &&
                GST_BUFFER_PTS (buffer) + GST_BUFFER_DURATION (buffer) <
                monitor->segment.start))
        ) {
      /* TODO is this a timestamp issue? */
      GST_VALIDATE_REPORT (monitor, BUFFER_IS_OUT_OF_SEGMENT,
          "buffer is out of segment and shouldn't be pushed. Timestamp: %"
          GST_TIME_FORMAT " - Duration: %" GST_TIME_FORMAT ". Range: %"
          GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
          GST_TIME_ARGS (monitor->segment.start),
          GST_TIME_ARGS (monitor->segment.stop));
    }
  }

  gst_validate_pad_monitor_check_buffer_freq (monitor, pad);

  GST_VALIDATE_MONITOR_UNLOCK (monitor);
  GST_VALIDATE_PAD_MONITOR_PARENT_UNLOCK (monitor);
  gst_validate_pad_monitor_buffer_probe_overrides (monitor, buffer);
  return TRUE;
}

static void
gst_validate_pad_monitor_event_probe (GstPad * pad, GstEvent * event,
    gpointer udata)
{
  GstValidatePadMonitor *monitor = GST_VALIDATE_PAD_MONITOR_CAST (udata);
  guint32 seqnum = gst_event_get_seqnum (event);

  GST_VALIDATE_PAD_MONITOR_PARENT_LOCK (monitor);
  GST_VALIDATE_MONITOR_LOCK (monitor);

  GST_DEBUG_OBJECT (pad, "event %p %s seqnum:%" G_GUINT32_FORMAT, event,
      GST_EVENT_TYPE_NAME (event), seqnum);

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
      gchar *event_str = _get_event_string (event);
      /* If it's the expired events, we've failed */
      GST_WARNING_OBJECT (pad, "Did not expect event %p %s", event,
          GST_EVENT_TYPE_NAME (event));
      GST_VALIDATE_REPORT (monitor, EVENT_SERIALIZED_OUT_OF_ORDER,
          "Serialized event was pushed out of order: %s", event_str);

      g_free (event_str);
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
  gst_validate_pad_monitor_downstream_event_check (monitor, NULL, event, NULL);
  GST_VALIDATE_MONITOR_UNLOCK (monitor);
  GST_VALIDATE_PAD_MONITOR_PARENT_UNLOCK (monitor);
}

static GstPadProbeReturn
gst_validate_pad_monitor_pad_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer udata)
{
  if (info->type & GST_PAD_PROBE_TYPE_BUFFER)
    gst_validate_pad_monitor_buffer_probe (pad, info->data, udata,
        GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_PULL);
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

  if (g_str_has_prefix (gst_structure_get_name (structure), "audio/x-raw") ||
      g_str_has_prefix (gst_structure_get_name (structure), "video/x-raw")) {
    pad_monitor->caps_is_raw = TRUE;
  } else {
    pad_monitor->caps_is_raw = FALSE;
  }
}

static void
gst_validate_pad_monitor_setcaps_pre (GstValidatePadMonitor * pad_monitor,
    GstCaps * caps)
{
  GstStructure *structure;
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (pad_monitor)));

  /* Check if caps are identical to last caps and complain if so
   * Only checked for sink pads as src pads might push the same caps
   * multiple times during unlinked/autoplugging scenarios */
  if (GST_PAD_IS_SINK (pad) && pad_monitor->last_caps
      && gst_caps_is_equal (caps, pad_monitor->last_caps)) {
    gchar *caps_str = gst_caps_to_string (caps);

    GST_VALIDATE_REPORT (pad_monitor, EVENT_CAPS_DUPLICATE, "%s", caps_str);
    g_free (caps_str);

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
          gchar *caps_str = gst_caps_to_string (caps);

          GST_VALIDATE_REPORT (pad_monitor, CAPS_EXPECTED_FIELD_NOT_FOUND,
              "Field %s is missing from setcaps caps '%s'", name, caps_str);
          g_free (caps_str);
        } else if (gst_value_compare (v, otherv) != GST_VALUE_EQUAL) {
          gchar *caps_str = gst_caps_to_string (caps),
              *pending_setcaps_fields_str =
              gst_structure_to_string (pad_monitor->pending_setcaps_fields);


          GST_VALIDATE_REPORT (pad_monitor, CAPS_FIELD_UNEXPECTED_VALUE,
              "Field %s from setcaps caps '%s' is different "
              "from expected value in caps '%s'", name, caps_str,
              pending_setcaps_fields_str);

          g_free (pending_setcaps_fields_str);
          g_free (caps_str);
        }
      }
    }

    if (GST_PAD_IS_SINK (pad) &&
        gst_validate_pad_monitor_pad_should_proxy_othercaps (pad_monitor)) {
      if (_structure_is_video (structure)) {
        GST_DEBUG_OBJECT (pad,
            "Adding video common pending fields to other pad: %" GST_PTR_FORMAT,
            structure);
        gst_validate_pad_monitor_otherpad_add_pending_field (pad_monitor,
            structure, "width");
        gst_validate_pad_monitor_otherpad_add_pending_field (pad_monitor,
            structure, "height");
        gst_validate_pad_monitor_otherpad_add_pending_field (pad_monitor,
            structure, "framerate");
        gst_validate_pad_monitor_otherpad_add_pending_field (pad_monitor,
            structure, "pixel-aspect-ratio");
      } else if (_structure_is_audio (structure)) {
        GST_DEBUG_OBJECT (pad,
            "Adding audio common pending fields to other pad: %" GST_PTR_FORMAT,
            structure);
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
  gst_object_unref (pad);

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

static void
gst_validate_pad_monitor_get_min_buffer_frequency (GstValidatePadMonitor *
    monitor, GstPad * pad)
{
  GList *config, *l;

  if (!GST_PAD_IS_SRC (pad))
    return;

  config = gst_validate_plugin_get_config (NULL);
  for (l = config; l != NULL; l = g_list_next (l)) {
    GstStructure *s = l->data;
    gdouble min_buf_freq;
    const gchar *pad_name;
    GstElement *element = NULL;

    if (!gst_structure_get_double (s, "min-buffer-frequency", &min_buf_freq)) {
      gint max_int;

      if (!gst_structure_get_int (s, "min-buffer-frequency", &max_int))
        goto next;

      min_buf_freq = max_int;
    }

    pad_name = gst_structure_get_string (s, "name");
    if (!pad_name)
      pad_name = "src";

    if (g_strcmp0 (GST_PAD_NAME (pad), pad_name))
      goto next;

    element = gst_pad_get_parent_element (pad);

    if (!gst_validate_element_matches_target (element, s))
      goto next;

    monitor->min_buf_freq = min_buf_freq;

    gst_validate_utils_get_clocktime (s, "buffer-frequency-start",
        &monitor->min_buf_freq_start);

    GST_DEBUG_OBJECT (pad, "pad has a minimum buffer frequency of %f",
        min_buf_freq);
  next:
    g_clear_object (&element);
  }
}

static gboolean
gst_validate_pad_monitor_do_setup (GstValidateMonitor * monitor)
{
  GstValidatePadMonitor *pad_monitor = GST_VALIDATE_PAD_MONITOR_CAST (monitor);
  GstPad *pad = (gpointer) gst_validate_monitor_get_target (monitor);

  if (!GST_IS_PAD (pad)) {
    GST_WARNING_OBJECT (monitor, "Trying to create pad monitor with other "
        "type of object");
    gst_object_unref (pad);
    return FALSE;
  }

  if (_GET_PAD_MONITOR (pad)) {
    GST_WARNING_OBJECT (pad_monitor,
        "Pad already has a validate-monitor associated");
    gst_object_unref (pad);
    return FALSE;
  }

  _SET_PAD_MONITOR (pad, pad_monitor);

  pad_monitor->event_func = GST_PAD_EVENTFUNC (pad);
  pad_monitor->event_full_func = GST_PAD_EVENTFULLFUNC (pad);
  pad_monitor->query_func = GST_PAD_QUERYFUNC (pad);
  pad_monitor->activatemode_func = GST_PAD_ACTIVATEMODEFUNC (pad);
  pad_monitor->get_range_func = GST_PAD_GETRANGEFUNC (pad);
  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {

    pad_monitor->chain_func = GST_PAD_CHAINFUNC (pad);
    if (pad_monitor->chain_func)
      gst_pad_set_chain_function (pad, gst_validate_pad_monitor_chain_func);

    if (pad_monitor->event_full_func)
      gst_pad_set_event_full_function (pad,
          gst_validate_pad_monitor_sink_event_full_func);
    else
      gst_pad_set_event_function (pad,
          gst_validate_pad_monitor_sink_event_func);
  } else {
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

  if (GST_PAD_IS_SRC (pad)) {
    gst_pad_set_getrange_function (pad,
        gst_validate_pad_monitor_get_range_func);
  }

  gst_validate_reporter_set_name (GST_VALIDATE_REPORTER (monitor),
      g_strdup_printf ("%s:%s", GST_DEBUG_PAD_NAME (pad)));

  if (G_UNLIKELY (GST_PAD_PARENT (pad) == NULL))
    GST_FIXME ("Saw a pad not belonging to any object");

  gst_validate_pad_monitor_get_min_buffer_frequency (pad_monitor, pad);

  gst_object_unref (pad);
  return TRUE;
}
