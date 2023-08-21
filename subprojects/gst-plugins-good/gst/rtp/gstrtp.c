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

#include <gst/tag/tag.h>

#include "gstrtpelements.h"


static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;

  ret |= GST_ELEMENT_REGISTER (rtpac3depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpac3pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpbvdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpbvpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpceltdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpceltpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpdvdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpdvpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpgstdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpgstpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpilbcpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpilbcdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpg722depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpg722pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpg723depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpg723pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpg726depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpg726pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpg729depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpg729pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpgsmdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpgsmpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpamrdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpamrpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtppcmadepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtppcmudepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtppcmupay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtppcmapay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmpadepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmpapay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmparobustdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmpvdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmpvpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpopusdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpopuspay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtppassthroughpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtph261pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtph261depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtph263ppay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtph263pdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtph263depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtph263pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtph264depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtph264pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtph265depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtph265pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpj2kdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpj2kpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpjpegdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpjpegpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpklvdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpklvpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpL8pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpL8depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpL16pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpL16depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpL24pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpL24depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpldacpay, plugin);
  ret |= GST_ELEMENT_REGISTER (asteriskh263, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmp1sdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmp2tdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmp2tpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmp4vpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmp4vdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmp4apay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmp4adepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmp4gdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmp4gpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpqcelpdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpqdm2depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpsbcdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpsbcpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpsirenpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpsirendepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpspeexpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpspeexdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpsv3vdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtptheoradepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtptheorapay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpvorbisdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpvorbispay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpvp8depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpvp8pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpvp9depay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpvp9pay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpvrawdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpvrawpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpstreampay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpstreamdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpisacpay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpisacdepay, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpredenc, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpreddec, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpulpfecdec, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpulpfecenc, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpstorage, plugin);
  ret |= GST_ELEMENT_REGISTER (rtphdrextcolorspace, plugin);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rtp,
    "Real-time protocol plugins",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
