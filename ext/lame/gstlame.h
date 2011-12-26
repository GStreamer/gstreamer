/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_LAME_H__
#define __GST_LAME_H__


#include <gst/gst.h>

G_BEGIN_DECLS

#include <lame/lame.h>
#include <gst/audio/gstaudioencoder.h>
#include <gst/base/gstadapter.h>

#define GST_TYPE_LAME \
  (gst_lame_get_type())
#define GST_LAME(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LAME,GstLame))
#define GST_LAME_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_LAME,GstLameClass))
#define GST_IS_LAME(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LAME))
#define GST_IS_LAME_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LAME))

typedef struct _GstLame GstLame;
typedef struct _GstLameClass GstLameClass;

/**
 * GstLame:
 *
 * Opaque data structure.
 */
struct _GstLame {
  GstAudioEncoder element;

  /*< private >*/

  gint samplerate;
  gint out_samplerate;
  gint num_channels;
  gboolean setup;

  gint bitrate;
  gfloat compression_ratio;
  gint quality;
  gint mode; /* actual mode in use now */
  gint requested_mode; /* requested mode by user/app */
  gboolean force_ms;
  gboolean free_format;
  gboolean copyright;
  gboolean original;
  gboolean error_protection;
  gboolean extension;
  gboolean strict_iso;
  gboolean disable_reservoir;
  gint vbr;
  gint vbr_quality;
  gint vbr_mean_bitrate;
  gint vbr_min_bitrate;
  gint vbr_max_bitrate;
  gint vbr_hard_min;
  gint lowpass_freq;
  gint lowpass_width;
  gint highpass_freq;
  gint highpass_width;
  gboolean ath_only;
  gboolean ath_short;
  gboolean no_ath;
  gint ath_type;
  gint ath_lower;
  gboolean allow_diff_short;
  gboolean no_short_blocks;
  gboolean emphasis;
  gint preset;

  lame_global_flags *lgf;

  GstAdapter *adapter;
};

struct _GstLameClass {
  GstAudioEncoderClass parent_class;
};

GType gst_lame_get_type(void);
gboolean gst_lame_register (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_LAME_H__ */
