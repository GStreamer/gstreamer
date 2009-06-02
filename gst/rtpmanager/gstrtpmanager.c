/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtpbin.h"
#include "gstrtpjitterbuffer.h"
#include "gstrtpptdemux.h"
#include "gstrtpsession.h"
#include "gstrtpssrcdemux.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "gstrtpbin", GST_RANK_NONE,
          GST_TYPE_RTP_BIN))
    return FALSE;

  if (!gst_element_register (plugin, "gstrtpjitterbuffer", GST_RANK_NONE,
          GST_TYPE_RTP_JITTER_BUFFER))
    return FALSE;

  if (!gst_element_register (plugin, "gstrtpptdemux", GST_RANK_NONE,
          GST_TYPE_RTP_PT_DEMUX))
    return FALSE;

  if (!gst_element_register (plugin, "gstrtpsession", GST_RANK_NONE,
          GST_TYPE_RTP_SESSION))
    return FALSE;

  if (!gst_element_register (plugin, "gstrtpssrcdemux", GST_RANK_NONE,
          GST_TYPE_RTP_SSRC_DEMUX))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gstrtpmanager",
    "RTP session management plugin library",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
