/*
 * GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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
#include "gstdecklink2utils.h"
#include "gstdecklink2device.h"

G_BEGIN_DECLS

#define GST_TYPE_DECKLINK2_PROVIDER_DEVICE (gst_decklink2_provider_device_get_type())
G_DECLARE_FINAL_TYPE (GstDeckLink2ProviderDevice, gst_decklink2_provider_device,
    GST, DECKLINK2_PROVIDER_DEVICE, GstDevice);

#define GST_TYPE_DECKLINK2_DEVICE_PROVIDER (gst_decklink2_device_provider_get_type())
G_DECLARE_FINAL_TYPE (GstDeckLink2DeviceProvider, gst_decklink2_device_provider,
    GST, DECKLINK2_DEVICE_PROVIDER, GstDeviceProvider);

GstDevice * gst_decklink2_provider_device_new (gboolean is_src,
                                               const gchar * model_name,
                                               const gchar * display_name,
                                               const gchar * serial_number,
                                               GstCaps * caps,
                                               gint64 persistent_id,
                                               guint device_number,
                                               guint max_audio_channels,
                                               const gchar * driver_ver,
                                               const gchar * api_ver,
                                               const GstDeckLink2ColorCapabilities * color_caps);

GST_DEVICE_PROVIDER_REGISTER_DECLARE (decklink2deviceprovider);

G_END_DECLS
