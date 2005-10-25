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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtpdec.h"
#include "gstrtpg711enc.h"
#include "gstrtpg711dec.h"
#include "gstrtpgsmenc.h"
#include "gstrtpgsmparse.h"
#include "gstrtpamrenc.h"
#include "gstrtpamrdec.h"
#include "gstrtpmpaenc.h"
#include "gstrtpmpadec.h"
#include "gstrtph263pdec.h"
#include "gstrtph263penc.h"
#include "gstrtph263enc.h"
#include "gstasteriskh263.h"
#include "gstrtpmp4venc.h"
#include "gstrtpmp4vdec.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_rtpdec_plugin_init (plugin))
    return FALSE;

  if (!gst_rtpgsmparse_plugin_init (plugin))
    return FALSE;

  if (!gst_rtpgsmenc_plugin_init (plugin))
    return FALSE;

  if (!gst_rtpamrdec_plugin_init (plugin))
    return FALSE;

  if (!gst_rtpamrenc_plugin_init (plugin))
    return FALSE;

  if (!gst_rtpg711dec_plugin_init (plugin))
    return FALSE;

  if (!gst_rtpg711enc_plugin_init (plugin))
    return FALSE;

  if (!gst_rtpmpadec_plugin_init (plugin))
    return FALSE;

  if (!gst_rtpmpaenc_plugin_init (plugin))
    return FALSE;

  if (!gst_rtph263penc_plugin_init (plugin))
    return FALSE;

  if (!gst_rtph263pdec_plugin_init (plugin))
    return FALSE;

  if (!gst_rtph263enc_plugin_init (plugin))
    return FALSE;

  if (!gst_asteriskh263_plugin_init (plugin))
    return FALSE;

  if (!gst_rtpmp4venc_plugin_init (plugin))
    return FALSE;

  if (!gst_rtpmp4vdec_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "rtp",
    "Real-time protocol plugins",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
