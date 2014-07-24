/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-override.c - Validate Override that allows customizing Validate behavior
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

#include <string.h>

#include "gst-validate-internal.h"
#include "gst-validate-override.h"

GstValidateOverride *
gst_validate_override_new (void)
{
  GstValidateOverride *override = g_slice_new0 (GstValidateOverride);

  override->level_override = g_hash_table_new (g_direct_hash, g_direct_equal);

  return override;
}

void
gst_validate_override_free (GstValidateOverride * override)
{
  g_hash_table_unref (override->level_override);
  g_slice_free (GstValidateOverride, override);
}

void
gst_validate_override_change_severity (GstValidateOverride * override,
    GstValidateIssueId issue_id, GstValidateReportLevel new_level)
{
  g_hash_table_insert (override->level_override, (gpointer) issue_id,
      (gpointer) new_level);
}

/*
 * Also receives @default_level to preserve a custom level that might have
 * been set by a previous GstValidateOverride and should not go back to the
 * GstValidateIssue default
 */
GstValidateReportLevel
gst_validate_override_get_severity (GstValidateOverride * override,
    GstValidateIssueId issue_id, GstValidateReportLevel default_level)
{
  GstValidateReportLevel *level = NULL;

  if (g_hash_table_lookup_extended (override->level_override,
          (gpointer) issue_id, NULL, (gpointer) & level)) {

    return GPOINTER_TO_INT (level);
  }
  return default_level;
}

void
gst_validate_override_set_event_handler (GstValidateOverride * override,
    GstValidateOverrideEventHandler handler)
{
  override->event_handler = handler;
}

void
gst_validate_override_set_buffer_handler (GstValidateOverride * override,
    GstValidateOverrideBufferHandler handler)
{
  override->buffer_handler = handler;
}

void
gst_validate_override_set_query_handler (GstValidateOverride * override,
    GstValidateOverrideQueryHandler handler)
{
  override->query_handler = handler;
}

void
gst_validate_override_set_buffer_probe_handler (GstValidateOverride * override,
    GstValidateOverrideBufferHandler handler)
{
  override->buffer_probe_handler = handler;
}

void
gst_validate_override_set_getcaps_handler (GstValidateOverride * override,
    GstValidateOverrideGetCapsHandler handler)
{
  override->getcaps_handler = handler;
}

void
gst_validate_override_set_setcaps_handler (GstValidateOverride * override,
    GstValidateOverrideSetCapsHandler handler)
{
  override->setcaps_handler = handler;
}

void
gst_validate_override_event_handler (GstValidateOverride * override,
    GstValidateMonitor * monitor, GstEvent * event)
{
  if (override->event_handler)
    override->event_handler (override, monitor, event);
}

void
gst_validate_override_buffer_handler (GstValidateOverride * override,
    GstValidateMonitor * monitor, GstBuffer * buffer)
{
  if (override->buffer_handler)
    override->buffer_handler (override, monitor, buffer);
}

void
gst_validate_override_query_handler (GstValidateOverride * override,
    GstValidateMonitor * monitor, GstQuery * query)
{
  if (override->query_handler)
    override->query_handler (override, monitor, query);
}

void
gst_validate_override_buffer_probe_handler (GstValidateOverride * override,
    GstValidateMonitor * monitor, GstBuffer * buffer)
{
  if (override->buffer_probe_handler)
    override->buffer_probe_handler (override, monitor, buffer);
}

void
gst_validate_override_getcaps_handler (GstValidateOverride * override,
    GstValidateMonitor * monitor, GstCaps * caps)
{
  if (override->getcaps_handler)
    override->getcaps_handler (override, monitor, caps);
}

void
gst_validate_override_setcaps_handler (GstValidateOverride * override,
    GstValidateMonitor * monitor, GstCaps * caps)
{
  if (override->setcaps_handler)
    override->setcaps_handler (override, monitor, caps);
}
