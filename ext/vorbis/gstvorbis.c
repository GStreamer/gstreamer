/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include "gst/tag/tag.h"

#include "gstvorbisenc.h"
#include "gstvorbisdec.h"
#include "gstvorbisparse.h"
#include "gstvorbistag.h"

GST_DEBUG_CATEGORY (vorbisenc_debug);
GST_DEBUG_CATEGORY (vorbisdec_debug);
GST_DEBUG_CATEGORY (vorbisparse_debug);
GST_DEBUG_CATEGORY (vorbistag_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "vorbisenc", GST_RANK_PRIMARY,
          GST_TYPE_VORBISENC))
    return FALSE;

  if (!gst_element_register (plugin, "vorbisdec", GST_RANK_PRIMARY,
          gst_vorbis_dec_get_type ()))
    return FALSE;

  if (!gst_element_register (plugin, "vorbisparse", GST_RANK_NONE,
          gst_vorbis_parse_get_type ()))
    return FALSE;

  if (!gst_element_register (plugin, "vorbistag", GST_RANK_NONE,
          gst_vorbis_tag_get_type ()))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (vorbisenc_debug, "vorbisenc", 0,
      "vorbis encoding element");
  GST_DEBUG_CATEGORY_INIT (vorbisdec_debug, "vorbisdec", 0,
      "vorbis decoding element");
  GST_DEBUG_CATEGORY_INIT (vorbisparse_debug, "vorbisparse", 0,
      "vorbis parsing element");
  GST_DEBUG_CATEGORY_INIT (vorbistag_debug, "vorbistag", 0,
      "vorbis tagging element");

  gst_tag_register_musicbrainz_tags ();

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vorbis,
    "Vorbis plugin library",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
