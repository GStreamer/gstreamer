/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
 * Copyright (C) 2020 Huawei Technologies Co., Ltd.
 *   @Author: Stéphane Cerveau <stephane.cerveau@collabora.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>

#include "gstdebugutilselements.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;

  ret |= GST_ELEMENT_REGISTER (breakmydata, plugin);
  ret |= GST_ELEMENT_REGISTER (capssetter, plugin);
  ret |= GST_ELEMENT_REGISTER (rndbuffersize, plugin);
  ret |= GST_ELEMENT_REGISTER (navseek, plugin);
  ret |= GST_ELEMENT_REGISTER (pushfilesrc, plugin);
  ret |= GST_ELEMENT_REGISTER (progressreport, plugin);
  ret |= GST_ELEMENT_REGISTER (taginject, plugin);
  ret |= GST_ELEMENT_REGISTER (testsink, plugin);
#if 0
  ret |= GST_ELEMENT_REGISTER (capsdebug, plugin);
#endif
  ret |= GST_ELEMENT_REGISTER (cpureport, plugin);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    debug,
    "elements for testing and debugging",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
