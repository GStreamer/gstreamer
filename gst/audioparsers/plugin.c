/* GStreamer audio parsers
 * Copyright (C) 2009 Tim-Philipp Müller <tim centricular net>
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
#include "config.h"
#endif

#include "gstaudioparserselements.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;

  ret |= GST_ELEMENT_REGISTER (aacparse, plugin);
  ret |= GST_ELEMENT_REGISTER (amrparse, plugin);
  ret |= GST_ELEMENT_REGISTER (ac3parse, plugin);
  ret |= GST_ELEMENT_REGISTER (dcaparse, plugin);
  ret |= GST_ELEMENT_REGISTER (flacparse, plugin);
  ret |= GST_ELEMENT_REGISTER (mpegaudioparse, plugin);
  ret |= GST_ELEMENT_REGISTER (sbcparse, plugin);
  ret |= GST_ELEMENT_REGISTER (wavpackparse, plugin);

  return ret;
}


GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    audioparsers,
    "Parsers for various audio formats",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
