/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdtlsdec.h"
#include "gstdtlsenc.h"
#include "gstdtlssrtpenc.h"
#include "gstdtlssrtpdec.h"
#include "gstdtlssrtpdemux.h"

#include <gst/gst.h>

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "dtlsenc", GST_RANK_NONE,
      GST_TYPE_DTLS_ENC)
      && gst_element_register (plugin, "dtlsdec", GST_RANK_NONE,
      GST_TYPE_DTLS_DEC)
      && gst_element_register (plugin, "dtlssrtpdec", GST_RANK_NONE,
      GST_TYPE_DTLS_SRTP_DEC)
      && gst_element_register (plugin, "dtlssrtpenc", GST_RANK_NONE,
      GST_TYPE_DTLS_SRTP_ENC)
      && gst_element_register (plugin, "dtlssrtpdemux", GST_RANK_NONE,
      GST_TYPE_DTLS_SRTP_DEMUX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dtls,
    "DTLS decoder and encoder plugins",
    plugin_init, PACKAGE_VERSION, "BSD", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
