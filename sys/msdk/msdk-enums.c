/* GStreamer Intel MSDK plugin
 * Copyright (c) 2018, Intel corporation
 * All rights reserved.
 *
 * Author:Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#include "msdk-enums.h"

GType
gst_msdkenc_rate_control_get_type (void)
{
  static GType type = 0;

  static const GEnumValue values[] = {
    {MFX_RATECONTROL_CBR, "Constant Bitrate", "cbr"},
    {MFX_RATECONTROL_VBR, "Variable Bitrate", "vbr"},
    {MFX_RATECONTROL_CQP, "Constant Quantizer", "cqp"},
    {MFX_RATECONTROL_AVBR, "Average Bitrate", "avbr"},
    {MFX_RATECONTROL_LA, "VBR with look ahead (Non HRD compliant)", "la_vbr"},
    {MFX_RATECONTROL_ICQ, "Intelligent CQP", "icq"},
    {MFX_RATECONTROL_VCM, "Video Conferencing Mode (Non HRD compliant)", "vcm"},
    {MFX_RATECONTROL_LA_ICQ, "Intelligent CQP with LA (Non HRD compliant)",
        "la_icq"},
#if 0
    /* intended for one to N transcode scenario */
    {MFX_RATECONTROL_LA_EXT, "Extended LA", "la_ext"},
#endif
    {MFX_RATECONTROL_LA_HRD, "HRD compliant LA", "la_hrd"},
    {MFX_RATECONTROL_QVBR, "VBR with CQP", "qvbr"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("GstMsdkEncRateControl", values);
  }
  return type;
}

GType
gst_msdkenc_trellis_quantization_get_type (void)
{
  static GType type = 0;

  static const GFlagsValue values[] = {
    {_MFX_TRELLIS_NONE, "Disable for all frames", "None"},
    {MFX_TRELLIS_I, "Enable for I frames", "i"},
    {MFX_TRELLIS_P, "Enable for P frames", "p"},
    {MFX_TRELLIS_B, "Enable for B frames", "b"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_flags_register_static ("GstMsdkEncTrellisQuantization", values);
  }
  return type;
}

GType
gst_msdkenc_rc_lookahead_ds_get_type (void)
{
  static GType type = 0;

  static const GEnumValue values[] = {
    {MFX_LOOKAHEAD_DS_UNKNOWN, "SDK desides what to do", "default"},
    {MFX_LOOKAHEAD_DS_OFF, "No downsampling", "off"},
    {MFX_LOOKAHEAD_DS_2x, "Down sample 2-times before estimation", "2x"},
    {MFX_LOOKAHEAD_DS_4x, "Down sample 4-times before estimation", "4x"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("GstMsdkEncRCLookAheadDownsampling", values);
  }
  return type;
}

GType
gst_msdkenc_mbbrc_get_type (void)
{
  static GType type = 0;

  static const GEnumValue values[] = {
    {MFX_CODINGOPTION_UNKNOWN, "SDK desides what to do", "auto"},
    {MFX_CODINGOPTION_OFF, "Disable Macroblock level bit rate control", "off"},
    {MFX_CODINGOPTION_ON, "Enable Macroblock level bit rate control ", "on"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("GstMsdkEncMbBitrateControl", values);
  }
  return type;
}

GType
gst_msdkenc_adaptive_i_get_type (void)
{
  static GType type = 0;

  static const GEnumValue values[] = {
    {MFX_CODINGOPTION_UNKNOWN, "SDK desides what to do", "auto"},
    {MFX_CODINGOPTION_OFF, "Disable Adaptive I frame insertion ", "off"},
    {MFX_CODINGOPTION_ON, "Enable Aaptive I frame insertion ", "on"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("GstMsdkEncAdaptiveI", values);
  }
  return type;
}

GType
gst_msdkenc_adaptive_b_get_type (void)
{
  static GType type = 0;

  static const GEnumValue values[] = {
    {MFX_CODINGOPTION_UNKNOWN, "SDK desides what to do", "auto"},
    {MFX_CODINGOPTION_OFF, "Disable Adaptive B-Frame insertion ", "off"},
    {MFX_CODINGOPTION_ON, "Enable Aaptive B-Frame insertion ", "on"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("GstMsdkEncAdaptiveB", values);
  }
  return type;
}
