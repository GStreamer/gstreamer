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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_AMRNBENC_H__
#define __GST_AMRNBENC_H__

#include <gst/gst.h>
#include <gst/audio/gstaudioencoder.h>

#ifdef HAVE_OPENCORE_AMRNB_0_1_3_OR_LATER
#include <opencore-amrnb/interf_enc.h>
#else
#include <interf_enc.h>
#endif

G_BEGIN_DECLS

#define GST_TYPE_AMRNBENC \
  (gst_amrnbenc_get_type())
#define GST_AMRNBENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMRNBENC, GstAmrnbEnc))
#define GST_AMRNBENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AMRNBENC, GstAmrnbEncClass))
#define GST_IS_AMRNBENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AMRNBENC))
#define GST_IS_AMRNBENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AMRNBENC))

typedef struct _GstAmrnbEnc GstAmrnbEnc;
typedef struct _GstAmrnbEncClass GstAmrnbEncClass;

struct _GstAmrnbEnc {
  GstAudioEncoder element;

  /* library handle */
  void *handle;

  /* input settings */
  gint channels, rate;
  gint duration;

  /* property */
  enum Mode bandmode;
};

struct _GstAmrnbEncClass {
  GstAudioEncoderClass parent_class;
};

GType gst_amrnbenc_get_type (void);

G_END_DECLS

#endif /* __GST_AMRNBENC_H__ */
