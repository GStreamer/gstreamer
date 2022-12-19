/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstelementsplugin.c:
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
/**
 * plugin-coreelements:
 *
 * GStreamer core elements
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>

#include "gstcoreelementselements.h"


static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;

  ret |= GST_ELEMENT_REGISTER (capsfilter, plugin);
  ret |= GST_ELEMENT_REGISTER (clocksync, plugin);
  ret |= GST_ELEMENT_REGISTER (concat, plugin);
  ret |= GST_ELEMENT_REGISTER (dataurisrc, plugin);
  ret |= GST_ELEMENT_REGISTER (downloadbuffer, plugin);
  ret |= GST_ELEMENT_REGISTER (fakesrc, plugin);
  ret |= GST_ELEMENT_REGISTER (fakesink, plugin);
#if defined(HAVE_SYS_SOCKET_H) || defined(G_OS_WIN32)
  ret |= GST_ELEMENT_REGISTER (fdsrc, plugin);
  ret |= GST_ELEMENT_REGISTER (fdsink, plugin);
#endif
  ret |= GST_ELEMENT_REGISTER (filesrc, plugin);
  ret |= GST_ELEMENT_REGISTER (funnel, plugin);
  ret |= GST_ELEMENT_REGISTER (identity, plugin);
  ret |= GST_ELEMENT_REGISTER (input_selector, plugin);
  ret |= GST_ELEMENT_REGISTER (output_selector, plugin);
  ret |= GST_ELEMENT_REGISTER (queue, plugin);
  ret |= GST_ELEMENT_REGISTER (queue2, plugin);
  ret |= GST_ELEMENT_REGISTER (filesink, plugin);
  ret |= GST_ELEMENT_REGISTER (tee, plugin);
  ret |= GST_ELEMENT_REGISTER (typefind, plugin);
  ret |= GST_ELEMENT_REGISTER (multiqueue, plugin);
  ret |= GST_ELEMENT_REGISTER (valve, plugin);
  ret |= GST_ELEMENT_REGISTER (streamiddemux, plugin);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, coreelements,
    "GStreamer core elements", plugin_init, VERSION, GST_LICENSE,
    GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
