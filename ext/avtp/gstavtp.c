/*
 * GStreamer AVTP Plugin
 * Copyright (C) 2019 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later
 * version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

/**
 * plugin-avtp:
 *
 * ## Audio Video Transport Protocol (AVTP) Plugin
 *
 * The AVTP plugin implements typical Talker and Listener functionalities that
 * can be leveraged by GStreamer-based applications in order to implement TSN
 * audio/video applications.
 *
 * ### Dependencies
 *
 * The plugin uses libavtp to handle AVTP packetization. Libavtp source code can
 * be found in https://github.com/AVnu/libavtp as well as instructions to build
 * and install it.
 *
 * If libavtp isn't detected by configure, the plugin isn't built.
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>

static gboolean
plugin_init (GstPlugin * plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    avtp, "Audio/Video Transport Protocol (AVTP) plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
