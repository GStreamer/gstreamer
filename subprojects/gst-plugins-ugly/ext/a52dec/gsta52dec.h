/* GStreamer
 * Copyright (C) <2001> David I. Lehn <dlehn@users.sourceforge.net>
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


#ifndef __GST_A52DEC_H__
#define __GST_A52DEC_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiodecoder.h>

G_BEGIN_DECLS

#define GST_TYPE_A52DEC \
  (gst_a52dec_get_type())
#define GST_A52DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_A52DEC,GstA52Dec))
#define GST_A52DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_A52DEC,GstA52DecClass))
#define GST_IS_A52DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_A52DEC))
#define GST_IS_A52DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_A52DEC))

typedef struct _GstA52Dec GstA52Dec;
typedef struct _GstA52DecClass GstA52DecClass;

struct _GstA52Dec {
  GstAudioDecoder element;

  GstPadChainFunction base_chain;

  gboolean       dvdmode;
  gboolean       flag_update;
  int            prev_flags;

  /* stream properties */
  int            bit_rate;
  int            sample_rate;
  int            stream_channels;
  int            request_channels;
  int            using_channels;

  gint           channel_reorder_map[6];

  sample_t       level;
  sample_t       bias;
  gboolean       dynamic_range_compression;
  sample_t      *samples;
  a52_state_t   *state;
};

struct _GstA52DecClass {
  GstAudioDecoderClass parent_class;

  guint32 a52_cpuflags;
};

GType gst_a52dec_get_type (void);
GST_ELEMENT_REGISTER_DECLARE (a52dec);

#ifndef A52_MONO
#define A52_MONO 1
#endif
#ifndef A52_STEREO
#define A52_STEREO 2
#endif
#ifndef A52_3F
#define A52_3F 3
#endif
#ifndef A52_2F1R
#define A52_2F1R 4
#endif
#ifndef A52_3F1R
#define A52_3F1R 5
#endif
#ifndef A52_2F2R
#define A52_2F2R 6
#endif
#ifndef A52_3F2R
#define A52_3F2R 7
#endif
#ifndef A52_DOLBY
#define A52_DOLBY 10
#endif

G_END_DECLS

#endif /* __GST_A52DEC_H__ */
