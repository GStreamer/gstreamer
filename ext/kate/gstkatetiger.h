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

#ifndef __GST_KATE_TIGER_H__
#define __GST_KATE_TIGER_H__

#include <kate/kate.h>
#include <tiger/tiger.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/video-overlay-composition.h>
#include "gstkateutil.h"

G_BEGIN_DECLS
/* #defines don't like whitespacey bits */
#define GST_TYPE_KATE_TIGER \
  (gst_kate_tiger_get_type())
#define GST_KATE_TIGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_KATE_TIGER,GstKateTiger))
#define GST_KATE_TIGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_KATE,GstKateTigerClass))
#define GST_IS_KATE_TIGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_KATE_TIGER))
#define GST_IS_KATE_TIGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_KATE_TIGER))
typedef struct _GstKateTiger GstKateTiger;
typedef struct _GstKateTigerClass GstKateTigerClass;

struct _GstKateTiger
{
  GstKateDecoderBase decoder;

  GstPad *katesinkpad;
  GstPad *videosinkpad;
  GstPad *srcpad;

  tiger_renderer *tr;

  gdouble quality;
  gchar *default_font_desc;
  gboolean default_font_effect;
  gdouble default_font_effect_strength;
  guchar default_font_r;
  guchar default_font_g;
  guchar default_font_b;
  guchar default_font_a;
  guchar default_background_r;
  guchar default_background_g;
  guchar default_background_b;
  guchar default_background_a;
  gboolean silent;

  GstVideoFormat video_format;
  gint video_width;
  gint video_height;
  gboolean swap_rgb;
  GstBuffer *render_buffer;
  GstVideoOverlayComposition *composition;

  GMutex *mutex;
  GCond *cond;

  GstSegment video_segment;
  gboolean video_flushing;
  gboolean seen_header;
};

struct _GstKateTigerClass
{
  GstElementClass parent_class;
};

GType gst_kate_tiger_get_type (void);

G_END_DECLS
#endif /* __GST_KATE_TIGER_H__ */
