/* VP9
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Entropy Wave Inc
 * Copyright (C) 2010-2013 Sebastian Dröge <slomo@circular-chaos.org>
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
 *
 */
#ifndef __GST_VP9_ENC_H__
#define __GST_VP9_ENC_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_VP9_ENCODER

#include <gstvpxenc.h>

/* FIXME: Undef HAVE_CONFIG_H because vpx_codec.h uses it,
 * which causes compilation failures */
#ifdef HAVE_CONFIG_H
#undef HAVE_CONFIG_H
#endif

G_BEGIN_DECLS

#define GST_TYPE_VP9_ENC (gst_vp9_enc_get_type())
G_DECLARE_FINAL_TYPE (GstVP9Enc, gst_vp9_enc, GST, VP9_ENC, GstVPXEnc)

struct _GstVP9Enc
{
  GstVPXEnc base_vpx_encoder;

  guint tile_columns;
  guint tile_rows;
#ifdef VPX_CTRL_VP9E_SET_ROW_MT
  gboolean row_mt;
#endif
  GstVPXAQ aq_mode;
  gboolean frame_parallel_decoding;
};

G_END_DECLS

#endif

#endif /* __GST_VP9_ENC_H__ */
