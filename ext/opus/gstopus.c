/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#include <config.h>
#endif

#include "gstopusdec.h"
#include "gstopusenc.h"
#include "gstopusparse.h"

#include "gstrtpopuspay.h"
#include "gstrtpopusdepay.h"

#include <gst/tag/tag.h>

static gboolean
plugin_init (GstPlugin * plugin)
{

  if (!gst_element_register (plugin, "opusenc", GST_RANK_PRIMARY,
          GST_TYPE_OPUS_ENC))
    return FALSE;

  if (!gst_element_register (plugin, "opusdec", GST_RANK_PRIMARY,
          GST_TYPE_OPUS_DEC))
    return FALSE;

  if (!gst_element_register (plugin, "opusparse", GST_RANK_NONE,
          GST_TYPE_OPUS_PARSE))
    return FALSE;

  if (!gst_element_register (plugin, "rtpopusdepay", GST_RANK_SECONDARY,
          GST_TYPE_RTP_OPUS_DEPAY))
    return FALSE;

  if (!gst_element_register (plugin, "rtpopuspay", GST_RANK_SECONDARY,
          GST_TYPE_RTP_OPUS_PAY))
    return FALSE;

  gst_tag_register_musicbrainz_tags ();

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    opus,
    "OPUS plugin library",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
