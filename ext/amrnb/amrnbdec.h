/* GStreamer Adaptive Multi-Rate Narrow-Band (AMR-NB) plugin
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_AMRNBDEC_H__
#define __GST_AMRNBDEC_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>

#include <opencore-amrnb/interf_dec.h>

G_BEGIN_DECLS

#define GST_TYPE_AMRNBDEC \
  (gst_amrnbdec_get_type())
#define GST_AMRNBDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMRNBDEC, GstAmrnbDec))
#define GST_AMRNBDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AMRNBDEC, GstAmrnbDecClass))
#define GST_IS_AMRNBDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AMRNBDEC))
#define GST_IS_AMRNBDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AMRNBDEC))

typedef struct _GstAmrnbDec GstAmrnbDec;
typedef struct _GstAmrnbDecClass GstAmrnbDecClass;

typedef enum
{
  GST_AMRNB_VARIANT_IF1,
  GST_AMRNB_VARIANT_IF2
} GstAmrnbVariant;

struct _GstAmrnbDec {
  GstAudioDecoder element;

  GstAmrnbVariant variant;

  /* library handle */
  void *handle;

  /* output settings */
  gint channels, rate;
};

struct _GstAmrnbDecClass {
  GstAudioDecoderClass parent_class;
};

GType gst_amrnbdec_get_type (void);

G_END_DECLS

#endif /* __GST_AMRNBDEC_H__ */
