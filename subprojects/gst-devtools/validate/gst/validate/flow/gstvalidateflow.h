/* GStreamer
 *
 * Copyright (C) 2018-2019 Igalia S.L.
 * Copyright (C) 2018 Metrological Group B.V.
 *  Author: Alicia Boya García <aboya@igalia.com>
 *
 * gstvalidateflow.c: A plugin to record streams and match them to
 * expectation files.
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

#pragma once

#include <stdio.h>
#include <gst/gst.h>
#include "../../gst/validate/validate.h"

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE (ValidateFlowOverride, validate_flow_override,
    VALIDATE, FLOW_OVERRIDE, GstValidateOverride);

typedef enum _ValidateFlowMode
{
  VALIDATE_FLOW_MODE_WRITING_EXPECTATIONS,
  VALIDATE_FLOW_MODE_WRITING_ACTUAL_RESULTS
} ValidateFlowMode;

struct _ValidateFlowOverride
{
  GstValidateOverride parent;

  const gchar *pad_name;
  gboolean record_buffers;
  gint checksum_type;
  gchar *expectations_dir;
  gchar *actual_results_dir;
  gboolean error_writing_file;
  gchar **caps_properties;
  GstStructure *ignored_fields;
  GstStructure *logged_fields;

  gchar **logged_event_types;
  gchar **logged_upstream_event_types;
  gchar **ignored_event_types;
  gchar **logged_unregistered_sei_uuids;

  gchar *expectations_file_path;
  gchar *actual_results_file_path;
  gchar **extra_serialized_metas;
  gboolean extra_serialized_metas_all;
  ValidateFlowMode mode;
  gboolean was_attached;
  GstStructure *config;

  /* output_file will refer to the expectations file if it did not exist,
   * or to the actual results file otherwise. */
  gchar *output_file_path;
  FILE *output_file;
  GMutex flow_mutex;

  /* Live comparison state, protected by flow_mutex */
  gchar **expected_lines;
  gsize expected_line_index;
  gboolean live_mismatch_found;

  /* Tracks async mismatch reports queued via gst_call_async() */
  GMutex async_report_lock;
  GCond async_report_cond;
  gint pending_async_reports;
};

G_END_DECLS
