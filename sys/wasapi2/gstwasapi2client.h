/*
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#ifndef __GST_WASAPI2_CLIENT_H__
#define __GST_WASAPI2_CLIENT_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

typedef enum
{
  GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE = 0,
  GST_WASAPI2_CLIENT_DEVICE_CLASS_RENDER,
} GstWasapi2ClientDeviceClass;

#define GST_TYPE_WASAPI2_CLIENT_DEVICE_CLASS (gst_wasapi2_client_device_class_get_type())
GType gst_wasapi2_client_device_class_get_type (void);

#define GST_TYPE_WASAPI2_CLIENT (gst_wasapi2_client_get_type())
G_DECLARE_FINAL_TYPE (GstWasapi2Client,
    gst_wasapi2_client, GST, WASAPI2_CLIENT, GstObject);

GstCaps * gst_wasapi2_client_get_caps (GstWasapi2Client * client);

gboolean  gst_wasapi2_client_open     (GstWasapi2Client * client,
                                       GstAudioRingBufferSpec * spec,
                                       GstAudioRingBuffer * buf);

gboolean  gst_wasapi2_client_start    (GstWasapi2Client * client);

gboolean  gst_wasapi2_client_stop     (GstWasapi2Client * client);

gint      gst_wasapi2_client_read     (GstWasapi2Client * client,
                                       gpointer data,
                                       guint length);

gint      gst_wasapi2_client_write    (GstWasapi2Client * client,
                                       gpointer data,
                                       guint length);

guint     gst_wasapi2_client_delay    (GstWasapi2Client * client);

gboolean  gst_wasapi2_client_set_mute  (GstWasapi2Client * client,
                                        gboolean mute);

gboolean  gst_wasapi2_client_get_mute  (GstWasapi2Client * client,
                                        gboolean * mute);

gboolean  gst_wasapi2_client_set_volume (GstWasapi2Client * client,
                                         gfloat volume);

gboolean  gst_wasapi2_client_get_volume (GstWasapi2Client * client,
                                         gfloat * volume);

gboolean gst_wasapi2_client_ensure_activation (GstWasapi2Client * client);

GstWasapi2Client * gst_wasapi2_client_new (GstWasapi2ClientDeviceClass device_class,
                                           gboolean low_latency,
                                           gint device_index,
                                           const gchar * device_id,
                                           gpointer dispatcher);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstWasapi2Client, gst_object_unref)

G_END_DECLS

#endif /* __GST_WASAPI2_CLIENT_H__ */
