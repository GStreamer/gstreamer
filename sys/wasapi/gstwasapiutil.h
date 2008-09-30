/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_WASAPI_UTIL_H__
#define __GST_WASAPI_UTIL_H__

#include <gst/gst.h>

#include <audioclient.h>

const gchar *
gst_wasapi_util_hresult_to_string (HRESULT hr);

gboolean
gst_wasapi_util_get_default_device_client (GstElement * element,
                                           gboolean capture,
                                           guint rate,
                                           GstClockTime buffer_time,
                                           GstClockTime period_time,
                                           DWORD flags,
                                           IAudioClient ** ret_client,
                                           GstClockTime * ret_latency);

#endif /* __GST_WASAPI_UTIL_H__ */

