/*
 * GStreamer
 * Copyright 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright 2008 Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

#ifndef __GST_KATE_ENC_H__
#define __GST_KATE_ENC_H__

#include <gst/gst.h>
#include <kate/kate.h>

#include "gstkateutil.h"

G_BEGIN_DECLS
/* #defines don't like whitespacey bits */
#define GST_TYPE_KATE_ENC \
  (gst_kate_enc_get_type())
#define GST_KATE_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_KATE_ENC,GstKateEnc))
#define GST_KATE_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_KATE,GstKateEncClass))
#define GST_IS_KATE_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_KATE_ENC))
#define GST_IS_KATE_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_KATE_ENC))
typedef struct _GstKateEnc GstKateEnc;
typedef struct _GstKateEncClass GstKateEncClass;

struct _GstKateEnc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  kate_info ki;
  kate_comment kc;
  kate_state k;

  GstTagList *tags;

  GstClockTime last_timestamp;
  GstClockTime latest_end_time;

  GstEvent *pending_segment;

  gboolean headers_sent;
  gboolean initialized;
  gboolean delayed_spu;
  GstClockTime delayed_start;
  kate_bitmap *delayed_bitmap;
  kate_palette *delayed_palette;
  kate_region *delayed_region;
  gchar *language;
  gchar *category;

  GstKateFormat format;

  int granule_rate_numerator;
  int granule_rate_denominator;
  int granule_shift;

  float keepalive_min_time;
  float default_spu_duration;

  size_t original_canvas_width;
  size_t original_canvas_height;

  /* SPU decoding */
  guint8 spu_colormap[4];
  guint32 spu_clut[16];
  guint8 spu_alpha[4];
  guint16 spu_top;
  guint16 spu_left;
  guint16 spu_right;
  guint16 spu_bottom;
  guint16 spu_pix_data[2];
  guint16 show_time;
  guint16 hide_time;
};

struct _GstKateEncClass
{
  GstElementClass parent_class;
};

GType gst_kate_enc_get_type (void);

G_END_DECLS
#endif /* __GST_KATE_ENC_H__ */
