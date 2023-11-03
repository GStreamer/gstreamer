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
#include "gstdecklink2input.h"
#include "gstdecklink2output.h"

G_BEGIN_DECLS

#define GST_TYPE_DECKLINK2_DEVICE (gst_decklink2_device_get_type())
G_DECLARE_FINAL_TYPE (GstDeckLink2Device, gst_decklink2_device,
    GST, DECKLINK2_DEVICE, GstObject);

GstDeckLink2Input *  gst_decklink2_acquire_input  (guint device_number,
                                                   gint64 persistent_id);

GstDeckLink2Output * gst_decklink2_acquire_output (guint device_number,
                                                   gint64 persistent_id);

void                 gst_decklink2_release_input  (GstDeckLink2Input * input);

void                 gst_decklink2_release_output (GstDeckLink2Output * output);

void                 gst_decklink2_device_deinit  (void);

GList *              gst_decklink2_get_devices    (void);

HRESULT              gst_decklink2_device_set_profile_id (GstDeckLink2Device * device,
                                                          BMDProfileID profile_id);

const gchar *        gst_decklink2_device_get_serial_number (GstDeckLink2Device * device);

G_END_DECLS
