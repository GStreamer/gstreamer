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

#ifndef __GST_MSDKH264ENC_H__
#define __GST_MSDKH264ENC_H__

#include "gstmsdkenc.h"

G_BEGIN_DECLS

#define GST_TYPE_MSDKH264ENC \
  (gst_msdkh264enc_get_type())
#define GST_MSDKH264ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MSDKH264ENC,GstMsdkH264Enc))
#define GST_MSDKH264ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MSDKH264ENC,GstMsdkH264EncClass))
#define GST_IS_MSDKH264ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MSDKH264ENC))
#define GST_IS_MSDKH264ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MSDKH264ENC))

typedef struct _GstMsdkH264Enc GstMsdkH264Enc;
typedef struct _GstMsdkH264EncClass GstMsdkH264EncClass;

struct _GstMsdkH264Enc
{
  GstMsdkEnc base;

  mfxExtCodingOption option;

  gint profile;
  gint level;

  gboolean cabac;
  gboolean lowpower;
  gint frame_packing;
  guint lookahead_ds;
  guint trellis;
  guint max_slice_size;
  guint b_pyramid;
};

struct _GstMsdkH264EncClass
{
  GstMsdkEncClass parent_class;
};

GType gst_msdkh264enc_get_type (void);

G_END_DECLS

#endif /* __GST_MSDKH264ENC_H__ */
