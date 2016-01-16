/* GStreamer
 * Copyright (C) 2014 Collabora
 *   Author: Olivier Crete <olivier.crete@collabora.com>
 *
 * gstaudioaggregator.h:
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

#ifndef __GST_AUDIO_AGGREGATOR_H__
#define __GST_AUDIO_AGGREGATOR_H__

#ifndef GST_USE_UNSTABLE_API
#warning "The Base library from gst-plugins-bad is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>
#include <gst/base/gstaggregator.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

/*******************************
 * GstAudioAggregator Structs  *
 *******************************/

typedef struct _GstAudioAggregator GstAudioAggregator;
typedef struct _GstAudioAggregatorPrivate GstAudioAggregatorPrivate;
typedef struct _GstAudioAggregatorClass GstAudioAggregatorClass;


/************************
 * GstAudioAggregatorPad API *
 ***********************/

#define GST_TYPE_AUDIO_AGGREGATOR_PAD            (gst_audio_aggregator_pad_get_type())
#define GST_AUDIO_AGGREGATOR_PAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_AGGREGATOR_PAD, GstAudioAggregatorPad))
#define GST_AUDIO_AGGREGATOR_PAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_AGGREGATOR_PAD, GstAudioAggregatorPadClass))
#define GST_AUDIO_AGGREGATOR_PAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_AUDIO_AGGREGATOR_PAD, GstAudioAggregatorPadClass))
#define GST_IS_AUDIO_AGGREGATOR_PAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_AGGREGATOR_PAD))
#define GST_IS_AUDIO_AGGREGATOR_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_AGGREGATOR_PAD))

/****************************
 * GstAudioAggregatorPad Structs *
 ***************************/

typedef struct _GstAudioAggregatorPad GstAudioAggregatorPad;
typedef struct _GstAudioAggregatorPadClass GstAudioAggregatorPadClass;
typedef struct _GstAudioAggregatorPadPrivate GstAudioAggregatorPadPrivate;

/**
 * GstAudioAggregatorPad:
 * @parent: The parent #GstAggregatorPad
 * @info: The audio info for this pad set from the incoming caps
 *
 * The implementation the GstPad to use with #GstAudioAggregator
 */
struct _GstAudioAggregatorPad
{
  GstAggregatorPad                  parent;

  GstAudioInfo                      info;

  /*< private >*/
  GstAudioAggregatorPadPrivate   *  priv;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstAudioAggregatorPadClass:
 *
 */
struct _GstAudioAggregatorPadClass
{
  GstAggregatorPadClass   parent_class;

  /*< private >*/
  gpointer      _gst_reserved[GST_PADDING];
};

GType gst_audio_aggregator_pad_get_type           (void);

/**************************
 * GstAudioAggregator API *
 **************************/

#define GST_TYPE_AUDIO_AGGREGATOR            (gst_audio_aggregator_get_type())
#define GST_AUDIO_AGGREGATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_AGGREGATOR,GstAudioAggregator))
#define GST_AUDIO_AGGREGATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_AGGREGATOR,GstAudioAggregatorClass))
#define GST_AUDIO_AGGREGATOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_AUDIO_AGGREGATOR,GstAudioAggregatorClass))
#define GST_IS_AUDIO_AGGREGATOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_AGGREGATOR))
#define GST_IS_AUDIO_AGGREGATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_AGGREGATOR))

#define GST_FLOW_CUSTOM_SUCCESS        GST_FLOW_NOT_HANDLED

/**
 * GstAudioAggregator:
 * @parent: The parent #GstAggregator
 * @info: The information parsed from the current caps
 * @current_caps: The caps set by the subclass
 *
 * GstAudioAggregator object
 */
struct _GstAudioAggregator
{
  GstAggregator            parent;

  /* All member are read only for subclasses, must hold OBJECT lock  */
  GstAudioInfo    info;

  GstCaps *current_caps;

  /*< private >*/
  GstAudioAggregatorPrivate *priv;

  gpointer                 _gst_reserved[GST_PADDING];
};

/**
 * GstAudioAggregatorClass:
 * @create_output_buffer: Create a new output buffer contains num_frames frames.
 * @aggregate_one_buffer: Aggregates one input buffer to the output
 *  buffer.  The in_offset and out_offset are in "frames", which is
 *  the size of a sample times the number of channels. Returns TRUE if
 *  any non-silence was added to the buffer
 */
struct _GstAudioAggregatorClass {
  GstAggregatorClass   parent_class;

  GstBuffer * (* create_output_buffer) (GstAudioAggregator * aagg,
      guint num_frames);
  gboolean (* aggregate_one_buffer) (GstAudioAggregator * aagg,
      GstAudioAggregatorPad * pad, GstBuffer * inbuf, guint in_offset,
      GstBuffer * outbuf, guint out_offset, guint num_frames);

  /*< private >*/
  gpointer          _gst_reserved[GST_PADDING];
};

/*************************
 * GstAggregator methods *
 ************************/

GType gst_audio_aggregator_get_type(void);

void
gst_audio_aggregator_set_sink_caps (GstAudioAggregator * aagg,
    GstAudioAggregatorPad * pad, GstCaps * caps);

gboolean
gst_audio_aggregator_set_src_caps (GstAudioAggregator * aagg, GstCaps * caps);


G_END_DECLS

#endif /* __GST_AUDIO_AGGREGATOR_H__ */
