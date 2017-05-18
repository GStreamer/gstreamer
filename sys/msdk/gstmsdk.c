/* GStreamer Intel MSDK plugin
 * Copyright (c) 2016, Oblong Industries, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstmsdkh264dec.h"
#include "gstmsdkh264enc.h"
#include "gstmsdkh265dec.h"
#include "gstmsdkh265enc.h"
#include "gstmsdkmjpegdec.h"
#include "gstmsdkmjpegenc.h"
#include "gstmsdkmpeg2enc.h"
#include "gstmsdkvp8enc.h"

GST_DEBUG_CATEGORY (gst_msdkdec_debug);
GST_DEBUG_CATEGORY (gst_msdkenc_debug);
GST_DEBUG_CATEGORY (gst_msdkh264dec_debug);
GST_DEBUG_CATEGORY (gst_msdkh264enc_debug);
GST_DEBUG_CATEGORY (gst_msdkh265dec_debug);
GST_DEBUG_CATEGORY (gst_msdkh265enc_debug);
GST_DEBUG_CATEGORY (gst_msdkmjpegdec_debug);
GST_DEBUG_CATEGORY (gst_msdkmjpegenc_debug);
GST_DEBUG_CATEGORY (gst_msdkmpeg2enc_debug);
GST_DEBUG_CATEGORY (gst_msdkvp8enc_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret;

  GST_DEBUG_CATEGORY_INIT (gst_msdkdec_debug, "msdkdec", 0, "msdkdec");
  GST_DEBUG_CATEGORY_INIT (gst_msdkenc_debug, "msdkenc", 0, "msdkenc");
  GST_DEBUG_CATEGORY_INIT (gst_msdkh264dec_debug, "msdkh264dec", 0,
      "msdkh264dec");
  GST_DEBUG_CATEGORY_INIT (gst_msdkh264enc_debug, "msdkh264enc", 0,
      "msdkh264enc");
  GST_DEBUG_CATEGORY_INIT (gst_msdkh265dec_debug, "msdkh265dec", 0,
      "msdkh265dec");
  GST_DEBUG_CATEGORY_INIT (gst_msdkh265enc_debug, "msdkh265enc", 0,
      "msdkh265enc");
  GST_DEBUG_CATEGORY_INIT (gst_msdkmjpegdec_debug, "msdkmjpegdec", 0,
      "msdkmjpegdec");
  GST_DEBUG_CATEGORY_INIT (gst_msdkmjpegenc_debug, "msdkmjpegenc", 0,
      "msdkmjpegenc");
  GST_DEBUG_CATEGORY_INIT (gst_msdkmpeg2enc_debug, "msdkmpeg2enc", 0,
      "msdkmpeg2enc");
  GST_DEBUG_CATEGORY_INIT (gst_msdkvp8enc_debug, "msdkvp8enc", 0, "msdkvp8enc");


  if (!msdk_is_available ())
    return FALSE;

  ret = gst_element_register (plugin, "msdkh264dec", GST_RANK_NONE,
      GST_TYPE_MSDKH264DEC);

  ret = gst_element_register (plugin, "msdkh264enc", GST_RANK_NONE,
      GST_TYPE_MSDKH264ENC);

  ret = gst_element_register (plugin, "msdkh265dec", GST_RANK_NONE,
      GST_TYPE_MSDKH265DEC);

  ret = gst_element_register (plugin, "msdkh265enc", GST_RANK_NONE,
      GST_TYPE_MSDKH265ENC);

  ret = gst_element_register (plugin, "msdkmjpegdec", GST_RANK_NONE,
      GST_TYPE_MSDKMJPEGDEC);

  ret = gst_element_register (plugin, "msdkmjpegenc", GST_RANK_NONE,
      GST_TYPE_MSDKMJPEGENC);

  ret = gst_element_register (plugin, "msdkmpeg2enc", GST_RANK_NONE,
      GST_TYPE_MSDKMPEG2ENC);

  ret = gst_element_register (plugin, "msdkvp8enc", GST_RANK_NONE,
      GST_TYPE_MSDKVP8ENC);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    msdk,
    "Intel Media SDK encoders",
    plugin_init, VERSION, "BSD", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
