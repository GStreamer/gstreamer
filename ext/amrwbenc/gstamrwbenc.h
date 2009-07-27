/* GStreamer Adaptive Multi-Rate Wide-Band (AMR-WB) plugin
 * Copyright (C) 2006 Edgard Lima <edgard.lima@indt.org.br>
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

#ifndef __GST_AMRWBENC_H__
#define __GST_AMRWBENC_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <amrwb/enc_if.h>
#include <amrwb/typedef.h>

G_BEGIN_DECLS

#define GST_TYPE_AMRWBENC			\
  (gst_amrwbenc_get_type())
#define GST_AMRWBENC(obj)						\
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMRWBENC, GstAmrwbEnc))
#define GST_AMRWBENC_CLASS(klass)					\
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AMRWBENC, GstAmrwbEncClass))
#define GST_IS_AMRWBENC(obj)					\
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AMRWBENC))
#define GST_IS_AMRWBENC_CLASS(klass)			\
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AMRWBENC))

typedef struct _GstAmrwbEnc GstAmrwbEnc;
typedef struct _GstAmrwbEncClass GstAmrwbEncClass;

struct _GstAmrwbEnc {
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;
  guint64 ts;
  gboolean discont;

  GstAdapter *adapter;

  /* library handle */
  void *handle;

  /* input settings */
  gint bandmode;
  gint channels, rate;
};

struct _GstAmrwbEncClass {
  GstElementClass parent_class;
};

GType gst_amrwbenc_get_type (void);

G_END_DECLS

#endif /* __GST_AMRWBENC_H__ */
