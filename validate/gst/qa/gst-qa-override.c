/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-override.c - QA Override that allows customizing QA behavior
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

#include <string.h>

#include "gst-qa-override.h"

GstQaOverride *
gst_qa_override_new (void)
{
  GstQaOverride *override = g_slice_new0 (GstQaOverride);

  override->level_override = g_hash_table_new (g_direct_hash, g_direct_equal);

  return override;
}

void
gst_qa_override_free (GstQaOverride * override)
{
  g_hash_table_unref (override->level_override);
  g_slice_free (GstQaOverride, override);
}

void
gst_qa_override_change_severity (GstQaOverride * override,
    GstQaIssueId issue_id, GstQaReportLevel new_level)
{
  g_hash_table_insert (override->level_override, (gpointer) issue_id,
      (gpointer) new_level);
}

/*
 * Also receives @default_level to preserve a custom level that might have
 * been set by a previous GstQaOverride and should not go back to the
 * GstQaIssue default
 */
GstQaReportLevel
gst_qa_override_get_severity (GstQaOverride * override, GstQaIssueId issue_id,
    GstQaReportLevel default_level)
{
  GstQaReportLevel level;
  if (g_hash_table_lookup_extended (override->level_override,
          (gpointer) issue_id, NULL, (gpointer *) & level)) {
    return level;
  }
  return default_level;
}

void
gst_qa_override_set_event_handler (GstQaOverride * override,
    GstQaOverrideEventHandler handler)
{
  override->event_handler = handler;
}

void
gst_qa_override_set_buffer_handler (GstQaOverride * override,
    GstQaOverrideBufferHandler handler)
{
  override->buffer_handler = handler;
}

void
gst_qa_override_set_query_handler (GstQaOverride * override,
    GstQaOverrideQueryHandler handler)
{
  override->query_handler = handler;
}
