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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_AMRWBDEC_H__
#define __GST_AMRWBDEC_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>

#ifdef HAVE_OPENCORE_AMRWB_0_1_3_OR_LATER
#include <opencore-amrwb/dec_if.h>
#include <opencore-amrwb/if_rom.h>
#else
#include <dec_if.h>
#include <if_rom.h>
#endif

G_BEGIN_DECLS

#define GST_TYPE_AMRWBDEC			\
  (gst_amrwbdec_get_type())
#define GST_AMRWBDEC(obj)						\
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMRWBDEC, GstAmrwbDec))
#define GST_AMRWBDEC_CLASS(klass)					\
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AMRWBDEC, GstAmrwbDecClass))
#define GST_IS_AMRWBDEC(obj)					\
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AMRWBDEC))
#define GST_IS_AMRWBDEC_CLASS(klass)			\
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AMRWBDEC))

typedef struct _GstAmrwbDec GstAmrwbDec;
typedef struct _GstAmrwbDecClass GstAmrwbDecClass;

/**
 * GstAmrwbDec:
 *
 * Opaque data structure.
 */
struct _GstAmrwbDec {
  GstAudioDecoder element;

  /* library handle */
  void *handle;

  /* output settings */
  gint channels, rate;
};

struct _GstAmrwbDecClass {
  GstAudioDecoderClass parent_class;
};

GType gst_amrwbdec_get_type (void);

G_END_DECLS

#endif /* __GST_AMRWBDEC_H__ */
