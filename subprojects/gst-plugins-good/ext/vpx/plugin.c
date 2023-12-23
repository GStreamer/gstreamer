/* VP8
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Entropy Wave Inc
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstvpxelements.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;

#ifdef HAVE_VP8_DECODER
  ret |= GST_ELEMENT_REGISTER (vp8dec, plugin);
#endif

#ifdef HAVE_VP8_ENCODER
  ret |= GST_ELEMENT_REGISTER (vp8enc, plugin);
#endif

#ifdef HAVE_VP9_DECODER
  ret |= GST_ELEMENT_REGISTER (vp9dec, plugin);
#endif

#ifdef HAVE_VP9_ENCODER
  ret |= GST_ELEMENT_REGISTER (vp9enc, plugin);
#endif

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vpx,
    "VP8/VP9 video encoding and decoding based on libvpx",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
