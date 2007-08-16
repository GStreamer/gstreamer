/* 
 * GStreamer
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
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

#ifndef __GST_AUDIO_CHEBYSHEV_FREQ_BAND_H__
#define __GST_AUDIO_CHEBYSHEV_FREQ_BAND_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>

G_BEGIN_DECLS
#define GST_TYPE_AUDIO_CHEBYSHEV_FREQ_BAND            (gst_audio_chebyshev_freq_band_get_type())
#define GST_AUDIO_CHEBYSHEV_FREQ_BAND(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_CHEBYSHEV_FREQ_BAND,GstAudioChebyshevFreqBand))
#define GST_IS_AUDIO_CHEBYSHEV_FREQ_BAND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_CHEBYSHEV_FREQ_BAND))
#define GST_AUDIO_CHEBYSHEV_FREQ_BAND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_AUDIO_CHEBYSHEV_FREQ_BAND,GstAudioChebyshevFreqBandClass))
#define GST_IS_AUDIO_CHEBYSHEV_FREQ_BAND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_AUDIO_CHEBYSHEV_FREQ_BAND))
#define GST_AUDIO_CHEBYSHEV_FREQ_BAND_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_AUDIO_CHEBYSHEV_FREQ_BAND,GstAudioChebyshevFreqBandClass))
typedef struct _GstAudioChebyshevFreqBand GstAudioChebyshevFreqBand;
typedef struct _GstAudioChebyshevFreqBandClass GstAudioChebyshevFreqBandClass;

typedef void (*GstAudioChebyshevFreqBandProcessFunc) (GstAudioChebyshevFreqBand *, guint8 *, guint);

typedef struct
{
  gdouble *x;
  gint x_pos;
  gdouble *y;
  gint y_pos;
} GstAudioChebyshevFreqBandChannelCtx;

struct _GstAudioChebyshevFreqBand
{
  GstAudioFilter audiofilter;

  gint mode;
  gint type;
  gint poles;
  gfloat lower_frequency;
  gfloat upper_frequency;
  gfloat ripple;

  /* < private > */
  GstAudioChebyshevFreqBandProcessFunc process;

  gboolean have_coeffs;
  gdouble *a;
  gint num_a;
  gdouble *b;
  gint num_b;
  GstAudioChebyshevFreqBandChannelCtx *channels;
};

struct _GstAudioChebyshevFreqBandClass
{
  GstAudioFilterClass parent;
};

GType gst_audio_chebyshev_freq_band_get_type (void);

G_END_DECLS
#endif /* __GST_AUDIO_CHEBYSHEV_FREQ_BAND_H__ */
