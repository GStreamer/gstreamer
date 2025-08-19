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
#include <gst/audio/audio.h>
#include "gstwasapi2util.h"

G_BEGIN_DECLS

#define GST_TYPE_WASAPI2_RBUF (gst_wasapi2_rbuf_get_type())
G_DECLARE_FINAL_TYPE (GstWasapi2Rbuf, gst_wasapi2_rbuf,
    GST, WASAPI2_RBUF, GstAudioRingBuffer);

typedef void (*GstWasapi2RbufCallback) (gpointer elem);

GstWasapi2Rbuf * gst_wasapi2_rbuf_new (gpointer parent,
                                       GstWasapi2RbufCallback callback);

void             gst_wasapi2_rbuf_set_device (GstWasapi2Rbuf * rbuf,
                                              const gchar * device_id,
                                              GstWasapi2EndpointClass endpoint_class,
                                              guint pid,
                                              gboolean low_latency,
                                              gboolean exclusive);

GstCaps *        gst_wasapi2_rbuf_get_caps (GstWasapi2Rbuf * rbuf);

void             gst_wasapi2_rbuf_set_mute  (GstWasapi2Rbuf * rbuf,
                                             gboolean mute);

gboolean         gst_wasapi2_rbuf_get_mute  (GstWasapi2Rbuf * rbuf);

void             gst_wasapi2_rbuf_set_volume (GstWasapi2Rbuf * rbuf,
                                              gdouble volume);

gdouble          gst_wasapi2_rbuf_get_volume (GstWasapi2Rbuf * rbuf);

void             gst_wasapi2_rbuf_set_device_mute_monitoring (GstWasapi2Rbuf * rbuf,
                                                              gboolean value);

void             gst_wasapi2_rbuf_set_continue_on_error (GstWasapi2Rbuf * rbuf,
                                                         gboolean value);

G_END_DECLS

