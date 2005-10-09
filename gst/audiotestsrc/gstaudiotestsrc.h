/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstsinesrc.h: 
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


#ifndef __GST_AUDIOTESTSRC_H__
#define __GST_AUDIOTESTSRC_H__


#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

G_BEGIN_DECLS


#define GST_TYPE_AUDIOTESTSRC \
  (gst_audiotestsrc_get_type())
#define GST_AUDIOTESTSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIOTESTSRC,GstAudioTestSrc))
#define GST_AUDIOTESTSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIOTESTSRC,GstAudioTestSrcClass))
#define GST_IS_AUDIOTESTSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIOTESTSRC))
#define GST_IS_AUDIOTESTSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIOTESTSRC))

typedef enum {
  GST_AUDIOTESTSRC_WAVE_SINE,
  GST_AUDIOTESTSRC_WAVE_SQUARE,
  GST_AUDIOTESTSRC_WAVE_SAW,
  GST_AUDIOTESTSRC_WAVE_TRIANGLE,
  GST_AUDIOTESTSRC_WAVE_SILENCE,
  GST_AUDIOTESTSRC_WAVE_WHITE_NOISE,
  GST_AUDIOTESTSRC_WAVE_PINK_NOISE,
} GstAudioTestSrcWaves; 

#define PINK_MAX_RANDOM_ROWS   (30)
#define PINK_RANDOM_BITS       (16)
#define PINK_RANDOM_SHIFT      ((sizeof(long)*8)-PINK_RANDOM_BITS)

typedef struct {
  glong      rows[PINK_MAX_RANDOM_ROWS];
  glong      running_sum;   /* Used to optimize summing of generators. */
  gint       index;         /* Incremented each sample. */
  gint       index_mask;    /* Index wrapped by ANDing with this mask. */
  gfloat     scalar;        /* Used to scale within range of -1.0 to +1.0 */
} GstPinkNoise;

typedef struct _GstAudioTestSrc GstAudioTestSrc;
typedef struct _GstAudioTestSrcClass GstAudioTestSrcClass;

struct _GstAudioTestSrc {
  GstBaseSrc parent;

  void (*process)(GstAudioTestSrc*, gint16 *);

  /* parameters */
  GstAudioTestSrcWaves wave;
  gdouble volume;
  gdouble freq;
    
  /* audio parameters */
  gint samplerate;

  gint samples_per_buffer;
  
  guint64 timestamp;
  guint64 offset;

  gdouble accumulator;

  gboolean tags_pushed;

  GstClockID clock_id;
  GstClockTimeDiff timestamp_offset;

  /* < private > */
  
  /* waveform specific context data */
  GstPinkNoise pink;
};

struct _GstAudioTestSrcClass {
  GstBaseSrcClass parent_class;
};

GType gst_audiotestsrc_get_type(void);
gboolean gst_audiotestsrc_factory_init (GstElementFactory *factory);

G_END_DECLS

#endif /* __GST_AUDIOTESTSRC_H__ */
