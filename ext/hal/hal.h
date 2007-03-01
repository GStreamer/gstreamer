/* GStreamer
 * Copyright (C) <2002> Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) <2006> Jürg Billeter <j@bitron.ch>
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

#ifndef GST_HAL_H
#define GST_HAL_H

/*
 * this library handles interaction with Hal
 */

#include <gst/gst.h>
#include <dbus/dbus.h>
#include <libhal.h>

G_BEGIN_DECLS

typedef enum
{
  GST_HAL_AUDIOSINK,
  GST_HAL_AUDIOSRC
} GstHalDeviceType;

GstElement *gst_hal_render_bin_from_udi (const gchar * udi,
    GstHalDeviceType type);

GstElement *gst_hal_get_audio_sink (const gchar * udi);
GstElement *gst_hal_get_audio_src (const gchar * udi);

G_END_DECLS

#endif /* GST_HAL_H */
