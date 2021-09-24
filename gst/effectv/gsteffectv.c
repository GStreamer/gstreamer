/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * EffecTV is free software. This library is free software;
 * you can redistribute it and/or
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

#include "gsteffectv.h"
#include "gstaging.h"
#include "gstdice.h"
#include "gstedge.h"
#include "gstquark.h"
#include "gstrev.h"
#include "gstshagadelic.h"
#include "gstvertigo.h"
#include "gstwarp.h"
#include "gstop.h"
#include "gstradioac.h"
#include "gststreak.h"
#include "gstripple.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;

  ret |= GST_ELEMENT_REGISTER (edgetv, plugin);
  ret |= GST_ELEMENT_REGISTER (agingtv, plugin);
  ret |= GST_ELEMENT_REGISTER (dicetv, plugin);
  ret |= GST_ELEMENT_REGISTER (warptv, plugin);
  ret |= GST_ELEMENT_REGISTER (shagadelictv, plugin);
  ret |= GST_ELEMENT_REGISTER (vertigotv, plugin);
  ret |= GST_ELEMENT_REGISTER (revtv, plugin);
  ret |= GST_ELEMENT_REGISTER (quarktv, plugin);
  ret |= GST_ELEMENT_REGISTER (optv, plugin);
  ret |= GST_ELEMENT_REGISTER (radioactv, plugin);
  ret |= GST_ELEMENT_REGISTER (streaktv, plugin);
  ret |= GST_ELEMENT_REGISTER (rippletv, plugin);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    effectv,
    "effect plugins from the effectv project",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
