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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtpbin.h"
#include "gstrtpjitterbuffer.h"
#include "gstrtpptdemux.h"
#include "gstrtpsession.h"
#include "gstrtprtxqueue.h"
#include "gstrtprtxreceive.h"
#include "gstrtprtxsend.h"
#include "gstrtpssrcdemux.h"
#include "gstrtpdtmfmux.h"
#include "gstrtpmux.h"
#include "gstrtpfunnel.h"
#include "gstrtpst2022-1-fecdec.h"
#include "gstrtpst2022-1-fecenc.h"
#include "gstrtphdrext-twcc.h"
#include "gstrtphdrext-clientaudiolevel.h"
#include "gstrtphdrext-mid.h"
#include "gstrtphdrext-ntp.h"
#include "gstrtphdrext-repairedstreamid.h"
#include "gstrtphdrext-streamid.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;

  ret |= GST_ELEMENT_REGISTER (rtpbin, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpjitterbuffer, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpptdemux, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpsession, plugin);
  ret |= GST_ELEMENT_REGISTER (rtprtxqueue, plugin);
  ret |= GST_ELEMENT_REGISTER (rtprtxreceive, plugin);
  ret |= GST_ELEMENT_REGISTER (rtprtxsend, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpssrcdemux, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpmux, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpdtmfmux, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpfunnel, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpst2022_1_fecdec, plugin);
  ret |= GST_ELEMENT_REGISTER (rtpst2022_1_fecenc, plugin);
  ret |= GST_ELEMENT_REGISTER (rtphdrexttwcc, plugin);
  ret |= GST_ELEMENT_REGISTER (rtphdrextclientaudiolevel, plugin);
  ret |= GST_ELEMENT_REGISTER (rtphdrextmid, plugin);
  ret |= GST_ELEMENT_REGISTER (rtphdrextntp64, plugin);
  ret |= GST_ELEMENT_REGISTER (rtphdrextstreamid, plugin);
  ret |= GST_ELEMENT_REGISTER (rtphdrextrepairedstreamid, plugin);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, rtpmanager,
    "RTP session management plugin library", plugin_init, VERSION, "LGPL",
    GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
