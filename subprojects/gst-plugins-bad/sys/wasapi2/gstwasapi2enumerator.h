/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <gst/gst.h>
#include "gstwasapi2util.h"
#include <string>

G_BEGIN_DECLS

#define GST_TYPE_WASAPI2_ENUMERATOR (gst_wasapi2_enumerator_get_type ())
G_DECLARE_FINAL_TYPE (GstWasapi2Enumerator, gst_wasapi2_enumerator,
    GST, WASAPI2_ENUMERATOR, GstObject);

G_END_DECLS

struct GstWasapi2DeviceProps
{
  EndpointFormFactor form_factor;
  std::string enumerator_name;
};

struct GstWasapi2EnumeratorEntry
{
  ~GstWasapi2EnumeratorEntry()
  {
    gst_clear_caps (&caps);
    gst_clear_caps (&exclusive_caps);
  }

  std::string device_id;
  std::string device_name;
  std::string actual_device_id;
  std::string actual_device_name;
  gboolean is_default = FALSE;
  GstCaps *caps = nullptr;
  GstCaps *exclusive_caps = nullptr;
  EDataFlow flow;
  GstWasapi2DeviceProps device_props = { };

  gint64 shared_mode_engine_default_period_us = 0;
  gint64 shared_mode_engine_fundamental_period_us = 0;
  gint64 shared_mode_engine_min_period_us = 0;
  gint64 shared_mode_engine_max_period_us = 0;

  gint64 default_device_period_us = 0;
  gint64 min_device_period_us = 0;
};

GstWasapi2Enumerator * gst_wasapi2_enumerator_new (void);

void gst_wasapi2_enumerator_activate_notification (GstWasapi2Enumerator * object,
                                                   gboolean active);

void gst_wasapi2_enumerator_entry_free (GstWasapi2EnumeratorEntry * entry);

void gst_wasapi2_enumerator_enumerate_devices (GstWasapi2Enumerator * object,
                                               GPtrArray * entry);

const gchar * gst_wasapi2_form_factor_to_string (EndpointFormFactor form_factor);

