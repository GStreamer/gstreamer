/* GStreamer xvid encoder plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_XVIDENC_H__
#define __GST_XVIDENC_H__

#include <gst/gst.h>
#include "gstxvid.h"

G_BEGIN_DECLS

#define GST_TYPE_XVIDENC \
  (gst_xvidenc_get_type())
#define GST_XVIDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_XVIDENC, GstXvidEnc))
#define GST_XVIDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_XVIDENC, GstXvidEncClass))
#define GST_IS_XVIDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_XVIDENC))
#define GST_IS_XVIDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_XVIDENC))

typedef struct _GstXvidEnc GstXvidEnc;
typedef struct _GstXvidEncClass GstXvidEncClass;

struct _GstXvidEnc {
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  /* xvid handle */
  void *handle;

  /* cache in place */
  xvid_enc_frame_t *xframe_cache;

  /* caps information */
  gint csp;
  gint width, height;
  gint fbase;
  gint fincr;
  gint par_width;
  gint par_height;

  /* delayed buffers if bframe usage */
  GQueue *delay;

  /* encoding profile */
  gint profile;
  gint used_profile;

  /* quantizer type; h263, MPEG */
  gint quant_type;

  /* encoding type; cbr, vbr, quant */
  gint pass;

  /* quality of encoded image */
  gint bitrate;
  gint quant;

  /* gop */
  gint max_key_interval;
  gboolean closed_gop;

  /* motion estimation */
  gint motion;
  gboolean me_chroma;
  gint me_vhq;
  gboolean me_quarterpel;

  /* lumimasking */
  gboolean lumimasking;

  /* b-frames */
  gint max_bframes;
  gint bquant_ratio;
  gint bquant_offset;
  gint bframe_threshold;

  /* misc */
  gboolean gmc;
  gboolean trellis;
  gboolean interlaced;
  gboolean cartoon;
  gboolean greyscale;
  gboolean hqacpred;

  /* quantizer ranges */
  gint max_iquant, min_iquant;
  gint max_pquant, min_pquant;
  gint max_bquant, min_bquant;

  /* cbr (single pass) encoding */
  gint reaction_delay_factor;
  gint averaging_period;
  gint buffer;

  /* vbr (2pass) encoding */
  gchar *filename;
  gint keyframe_boost;
  gint curve_compression_high;
  gint curve_compression_low;
  gint overflow_control_strength;
  gint max_overflow_improvement;
  gint max_overflow_degradation;
  gint kfreduction;
  gint kfthreshold;
  gint container_frame_overhead;
};

struct _GstXvidEncClass {
  GstElementClass parent_class;
};

GType gst_xvidenc_get_type(void);

G_END_DECLS

#endif /* __GST_XVIDENC_H__ */
