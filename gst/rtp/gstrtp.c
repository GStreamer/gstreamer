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

#include "gstrtpdepay.h"
#include "gstrtpg711pay.h"
#include "gstrtpg711depay.h"
#include "gstrtpgsmpay.h"
#include "gstrtpgsmparse.h"
#include "gstrtpamrpay.h"
#include "gstrtpamrdepay.h"
#include "gstrtpmpapay.h"
#include "gstrtpmpadepay.h"
#include "gstrtph263pdepay.h"
#include "gstrtph263ppay.h"
#include "gstrtph263pay.h"
#include "gstasteriskh263.h"
#include "gstrtpmp4vpay.h"
#include "gstrtpmp4vdepay.h"
#include "gstrtpspeexpay.h"
#include "gstrtpspeexdepay.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_rtp_depay_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_gsm_parse_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_gsm_pay_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_amr_depay_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_amr_pay_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_g711_depay_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_g711_pay_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_mpa_depay_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_mpa_pay_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_h263p_pay_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_h263p_depay_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_h263_pay_plugin_init (plugin))
    return FALSE;

  if (!gst_asteriskh263_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_mp4v_pay_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_mp4v_depay_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_speex_pay_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_speex_depay_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "rtp",
    "Real-time protocol plugins",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
