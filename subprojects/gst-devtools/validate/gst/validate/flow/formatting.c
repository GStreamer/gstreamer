/* GStreamer
 *
 * Copyright (C) 2018-2019 Igalia S.L.
 * Copyright (C) 2018 Metrological Group B.V.
 *  Author: Alicia Boya García <aboya@igalia.com>
 *
 * formatting.c: Functions used by validateflow to get string
 * representations of buffers.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "formatting.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>
#include <stdio.h>
#include <glib/gprintf.h>

#include "../../gst/validate/gst-validate-utils.h"

typedef void (*Uint64Formatter) (gchar * dest, guint64 time);
G_LOCK_DEFINE (checksums_as_id_lock);
static GstStructure *checksums_as_id = NULL;

#define CONSTIFY(strv) ((const gchar * const *) strv)

static gboolean
use_field (const gchar * field, gchar ** logged, gchar ** ignored)
{
  if (logged)
    return g_strv_contains (CONSTIFY (logged), field);

  if (ignored)
    return !g_strv_contains (CONSTIFY (ignored), field);

  return TRUE;
}


void
format_time (gchar * dest_str, guint64 time)
{
  if (GST_CLOCK_TIME_IS_VALID (time)) {
    g_sprintf (dest_str, "%" GST_TIME_FORMAT, GST_TIME_ARGS (time));
  } else {
    strcpy (dest_str, "none");
  }
}

static void
format_number (gchar * dest_str, guint64 number)
{
  g_sprintf (dest_str, "%" G_GUINT64_FORMAT, number);
}

gchar *
validate_flow_format_segment (const GstSegment * segment,
    gchar ** logged_fields, gchar ** ignored_fields)
{
  Uint64Formatter uint64_format;
  gchar *segment_str;
  gchar *parts[12];
  GString *format;
  gchar start_str[32], offset_str[32], stop_str[32], time_str[32], base_str[32],
      position_str[32], duration_str[32];
  int parts_index = 0;

  uint64_format =
      segment->format == GST_FORMAT_TIME ? format_time : format_number;
  uint64_format (start_str, segment->start);
  uint64_format (offset_str, segment->offset);
  uint64_format (stop_str, segment->stop);
  uint64_format (time_str, segment->time);
  uint64_format (base_str, segment->base);
  uint64_format (position_str, segment->position);
  uint64_format (duration_str, segment->duration);

  format = g_string_new (gst_format_get_name (segment->format));
  format = g_string_ascii_up (format);

  if (use_field ("format", logged_fields, ignored_fields))
    parts[parts_index++] = g_strdup_printf ("format=%s", format->str);

  if (use_field ("start", logged_fields, ignored_fields))
    parts[parts_index++] = g_strdup_printf ("start=%s", start_str);

  if (use_field ("offset", logged_fields, ignored_fields))
    parts[parts_index++] = g_strdup_printf ("offset=%s", offset_str);

  if (use_field ("stop", logged_fields, ignored_fields))
    parts[parts_index++] = g_strdup_printf ("stop=%s", stop_str);

  if (segment->rate != 1.0)
    parts[parts_index++] = g_strdup_printf ("rate=%f", segment->rate);
  if (segment->applied_rate != 1.0)
    parts[parts_index++] =
        g_strdup_printf ("applied_rate=%f", segment->applied_rate);

  if (segment->flags && use_field ("flags", logged_fields, ignored_fields))
    parts[parts_index++] = g_strdup_printf ("flags=0x%02x", segment->flags);

  if (use_field ("time", logged_fields, ignored_fields))
    parts[parts_index++] = g_strdup_printf ("time=%s", time_str);
  if (use_field ("base", logged_fields, ignored_fields))
    parts[parts_index++] = g_strdup_printf ("base=%s", base_str);
  if (use_field ("position", logged_fields, ignored_fields))
    parts[parts_index++] = g_strdup_printf ("position=%s", position_str);
  if (GST_CLOCK_TIME_IS_VALID (segment->duration)
      && use_field ("duration", logged_fields, ignored_fields))
    parts[parts_index++] = g_strdup_printf ("duration=%s", duration_str);
  parts[parts_index] = NULL;

  segment_str = g_strjoinv (", ", parts);

  while (parts_index > 0)
    g_free (parts[--parts_index]);
  g_string_free (format, TRUE);

  return segment_str;
}

typedef struct
{
  GList *fields;

  gchar **wanted_fields;
  gchar **ignored_fields;
} StructureValues;

static gboolean
structure_set_fields (GQuark field_id, GValue * value, StructureValues * data)
{
  const gchar *field = g_quark_to_string (field_id);

  if (data->ignored_fields
      && g_strv_contains ((const gchar **) data->ignored_fields, field))
    return TRUE;

  if (data->wanted_fields
      && !g_strv_contains ((const gchar **) data->wanted_fields, field))
    return TRUE;

  data->fields = g_list_prepend (data->fields, (gchar *) field);

  return TRUE;
}

static GstStructure *
validate_flow_structure_cleanup (const GstStructure * structure,
    gchar ** wanted_fields, gchar ** ignored_fields)
{
  GstStructure *nstructure;
  StructureValues d = {
    .fields = NULL,
    .wanted_fields = wanted_fields,
    .ignored_fields = ignored_fields,
  };

  gst_structure_foreach (structure,
      (GstStructureForeachFunc) structure_set_fields, &d);
  d.fields = g_list_sort (d.fields, (GCompareFunc) g_ascii_strcasecmp);
  nstructure = gst_structure_new_empty (gst_structure_get_name (structure));
  for (GList * tmp = d.fields; tmp; tmp = tmp->next) {
    gchar *field = tmp->data;

    gst_structure_set_value (nstructure, field,
        gst_structure_get_value (structure, field));
  }

  g_list_free (d.fields);

  return nstructure;
}

gchar *
validate_flow_format_caps (const GstCaps * caps, gchar ** wanted_fields,
    gchar ** ignored_fields)
{
  guint i;
  GstCaps *new_caps = gst_caps_new_empty ();
  gchar *caps_str;

  /* A single GstCaps can contain several caps structures (although only one is
   * used in most cases). We will print them separated with spaces. */
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure =
        validate_flow_structure_cleanup (gst_caps_get_structure (caps, i),
        wanted_fields, ignored_fields);

    gst_caps_append_structure_full (new_caps, structure,
        gst_caps_features_copy (gst_caps_get_features (caps, i)));
  }

  caps_str = gst_caps_to_string (new_caps);
  gst_caps_unref (new_caps);

  return caps_str;
}


static gchar *
buffer_get_flags_string (GstBuffer * buffer)
{
  GFlagsClass *flags_class =
      G_FLAGS_CLASS (g_type_class_ref (gst_buffer_flags_get_type ()));
  GstBufferFlags flags = GST_BUFFER_FLAGS (buffer);
  GString *string = NULL;

  while (1) {
    GFlagsValue *value = g_flags_get_first_value (flags_class, flags);
    if (!value)
      break;

    if (string == NULL)
      string = g_string_new (NULL);
    else
      g_string_append (string, " ");

    g_string_append (string, value->value_nick);
    flags &= ~value->value;
  }

  return (string != NULL) ? g_string_free (string, FALSE) : NULL;
}

/* Returns a newly-allocated string describing the metas on this buffer, or NULL */
static gchar *
buffer_get_meta_string (GstBuffer * buffer)
{
  gpointer state = NULL;
  GstMeta *meta;
  GString *s = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    const gchar *desc = g_type_name (meta->info->type);

    if (meta->info->api == GST_PARENT_BUFFER_META_API_TYPE) {
      /* The parent buffer meta is added automatically every time a buffer gets
       * copied, it is not useful to track them. */
      continue;
    }

    if (s == NULL)
      s = g_string_new (NULL);
    else
      g_string_append (s, ", ");

    if (meta->info->api == GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE) {
      GstVideoRegionOfInterestMeta *roi = (GstVideoRegionOfInterestMeta *) meta;
      g_string_append_printf (s,
          "GstVideoRegionOfInterestMeta[x=%" G_GUINT32_FORMAT ", y=%"
          G_GUINT32_FORMAT ", width=%" G_GUINT32_FORMAT ", height=%"
          G_GUINT32_FORMAT "]", roi->x, roi->y, roi->w, roi->h);
    } else {
      g_string_append (s, desc);
    }
  }

  return (s != NULL) ? g_string_free (s, FALSE) : NULL;
}

gchar *
validate_flow_format_buffer (GstBuffer * buffer, gint checksum_type,
    GstStructure * logged_fields_struct, GstStructure * ignored_fields_struct)
{
  gchar *flags_str, *meta_str, *buffer_str;
  gchar *buffer_parts[7];
  int buffer_parts_index = 0;
  GstMapInfo map;
  gchar **logged_fields =
      logged_fields_struct ? gst_validate_utils_get_strv (logged_fields_struct,
      "buffer") : NULL;
  gchar **ignored_fields =
      ignored_fields_struct ?
      gst_validate_utils_get_strv (ignored_fields_struct, "buffer") : NULL;

  if (checksum_type != CHECKSUM_TYPE_NONE || (logged_fields
          && g_strv_contains (CONSTIFY (logged_fields), "checksum"))) {
    if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
      GST_ERROR ("Buffer could not be mapped.");
    } else if (checksum_type == CHECKSUM_TYPE_CONTENT_HEX) {
      gint i;
      GString *content = g_string_new ("content=");

      for (i = 0; i < map.size; i++) {
        if (i)
          g_string_append_c (content, ' ');
        g_string_append_printf (content, "0x%02x", map.data[i]);
      }

      buffer_parts[buffer_parts_index++] = g_string_free (content, FALSE);
    } else {
      gchar *sum =
          g_compute_checksum_for_data (checksum_type ==
          CHECKSUM_TYPE_AS_ID ? G_CHECKSUM_SHA1 : checksum_type, map.data,
          map.size);
      gst_buffer_unmap (buffer, &map);

      if (checksum_type == CHECKSUM_TYPE_AS_ID) {
        gint id;

        G_LOCK (checksums_as_id_lock);
        if (!checksums_as_id)
          checksums_as_id = gst_structure_new_empty ("checksums-id");
        if (!gst_structure_get_int (checksums_as_id, sum, &id)) {
          id = gst_structure_n_fields (checksums_as_id);
          gst_structure_set (checksums_as_id, sum, G_TYPE_INT, id, NULL);
        }
        G_UNLOCK (checksums_as_id_lock);

        buffer_parts[buffer_parts_index++] =
            g_strdup_printf ("content-id=%d", id);
      } else {
        buffer_parts[buffer_parts_index++] =
            g_strdup_printf ("checksum=%s", sum);
      }
      g_free (sum);
    }
  }

  if (GST_CLOCK_TIME_IS_VALID (buffer->dts)
      && use_field ("dts", logged_fields, ignored_fields)) {
    gchar time_str[32];
    format_time (time_str, buffer->dts);
    buffer_parts[buffer_parts_index++] = g_strdup_printf ("dts=%s", time_str);
  }

  if (GST_CLOCK_TIME_IS_VALID (buffer->pts)
      && use_field ("pts", logged_fields, ignored_fields)) {
    gchar time_str[32];
    format_time (time_str, buffer->pts);
    buffer_parts[buffer_parts_index++] = g_strdup_printf ("pts=%s", time_str);
  }

  if (GST_CLOCK_TIME_IS_VALID (buffer->duration)
      && use_field ("dur", logged_fields, ignored_fields)) {
    gchar time_str[32];
    format_time (time_str, buffer->duration);
    buffer_parts[buffer_parts_index++] = g_strdup_printf ("dur=%s", time_str);
  }

  flags_str = buffer_get_flags_string (buffer);
  if (flags_str && use_field ("flags", logged_fields, ignored_fields)) {
    buffer_parts[buffer_parts_index++] =
        g_strdup_printf ("flags=%s", flags_str);
  }

  meta_str = buffer_get_meta_string (buffer);
  if (meta_str && use_field ("meta", logged_fields, ignored_fields))
    buffer_parts[buffer_parts_index++] = g_strdup_printf ("meta=%s", meta_str);

  buffer_parts[buffer_parts_index] = NULL;
  buffer_str =
      buffer_parts_index > 0 ? g_strjoinv (", ",
      buffer_parts) : g_strdup ("(empty)");

  g_free (meta_str);
  g_free (flags_str);
  while (buffer_parts_index > 0)
    g_free (buffer_parts[--buffer_parts_index]);

  return buffer_str;
}

gchar *
validate_flow_format_event (GstEvent * event,
    const gchar * const *caps_properties,
    GstStructure * logged_fields_struct,
    GstStructure * ignored_fields_struct,
    const gchar * const *ignored_event_types,
    const gchar * const *logged_event_types)
{
  const gchar *event_type;
  gchar *structure_string;
  gchar *event_string;
  gchar **ignored_fields;
  gchar **logged_fields;

  event_type = gst_event_type_get_name (GST_EVENT_TYPE (event));

  if (logged_event_types && !g_strv_contains (logged_event_types, event_type))
    return NULL;

  if (ignored_event_types && g_strv_contains (ignored_event_types, event_type))
    return NULL;

  logged_fields =
      logged_fields_struct ? gst_validate_utils_get_strv (logged_fields_struct,
      event_type) : NULL;
  ignored_fields =
      ignored_fields_struct ?
      gst_validate_utils_get_strv (ignored_fields_struct, event_type) : NULL;
  if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
    const GstSegment *segment;
    gst_event_parse_segment (event, &segment);
    structure_string =
        validate_flow_format_segment (segment, logged_fields, ignored_fields);
  } else if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
    GstCaps *caps;
    gst_event_parse_caps (event, &caps);

    structure_string =
        validate_flow_format_caps (caps,
        logged_fields ? logged_fields : (gchar **) caps_properties,
        ignored_fields);
    /* FIXME: Remove spurious `;` and regenerate all the expectation files */
    event_string = g_strdup_printf ("%s: %s;", event_type, structure_string);
    goto done;
  } else if (!gst_event_get_structure (event)) {
    structure_string = g_strdup ("(no structure)");
  } else {
    GstStructure *structure =
        validate_flow_structure_cleanup (gst_event_get_structure (event),
        logged_fields, ignored_fields);
    structure_string = gst_structure_to_string (structure);
    gst_structure_free (structure);
  }

  event_string = g_strdup_printf ("%s: %s", event_type, structure_string);
done:
  g_strfreev (logged_fields);
  g_strfreev (ignored_fields);
  g_free (structure_string);
  return event_string;
}
