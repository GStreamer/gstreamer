/* GStreamer
 * Copyright (C) 2011 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>.
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
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

#ifndef __GST_AUDIO_ENCODER_H__
#define __GST_AUDIO_ENCODER_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

#define GST_TYPE_AUDIO_ENCODER		   (gst_audio_encoder_get_type())
#define GST_AUDIO_ENCODER(obj)		   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_ENCODER,GstAudioEncoder))
#define GST_AUDIO_ENCODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_ENCODER,GstAudioEncoderClass))
#define GST_AUDIO_ENCODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_AUDIO_ENCODER,GstAudioEncoderClass))
#define GST_IS_AUDIO_ENCODER(obj)	   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_ENCODER))
#define GST_IS_AUDIO_ENCODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_ENCODER))
#define GST_AUDIO_ENCODER_CAST(obj)	((GstAudioEncoder *)(obj))

/**
 * GST_AUDIO_ENCODER_SINK_NAME:
 *
 * the name of the templates for the sink pad
 *
 * Since: 0.10.36
 */
#define GST_AUDIO_ENCODER_SINK_NAME	"sink"
/**
 * GST_AUDIO_ENCODER_SRC_NAME:
 *
 * the name of the templates for the source pad
 *
 * Since: 0.10.36
 */
#define GST_AUDIO_ENCODER_SRC_NAME	        "src"

/**
 * GST_AUDIO_ENCODER_SRC_PAD:
 * @obj: base parse instance
 *
 * Gives the pointer to the source #GstPad object of the element.
 *
 * Since: 0.10.36
 */
#define GST_AUDIO_ENCODER_SRC_PAD(obj)	(GST_AUDIO_ENCODER_CAST (obj)->srcpad)

/**
 * GST_AUDIO_ENCODER_SINK_PAD:
 * @obj: base parse instance
 *
 * Gives the pointer to the sink #GstPad object of the element.
 *
 * Since: 0.10.36
 */
#define GST_AUDIO_ENCODER_SINK_PAD(obj)	(GST_AUDIO_ENCODER_CAST (obj)->sinkpad)

/**
 * GST_AUDIO_ENCODER_SEGMENT:
 * @obj: base parse instance
 *
 * Gives the segment of the element.
 *
 * Since: 0.10.36
 */
#define GST_AUDIO_ENCODER_SEGMENT(obj)     (GST_AUDIO_ENCODER_CAST (obj)->segment)

#define GST_AUDIO_ENCODER_STREAM_LOCK(enc) g_static_rec_mutex_lock (&GST_AUDIO_ENCODER (enc)->stream_lock)
#define GST_AUDIO_ENCODER_STREAM_UNLOCK(enc) g_static_rec_mutex_unlock (&GST_AUDIO_ENCODER (enc)->stream_lock)

typedef struct _GstAudioEncoder GstAudioEncoder;
typedef struct _GstAudioEncoderClass GstAudioEncoderClass;

typedef struct _GstAudioEncoderPrivate GstAudioEncoderPrivate;

/**
 * GstAudioEncoder:
 *
 * The opaque #GstAudioEncoder data structure.
 *
 * Since: 0.10.36
 */
struct _GstAudioEncoder {
  GstElement     element;

  /*< protected >*/
  /* source and sink pads */
  GstPad         *sinkpad;
  GstPad         *srcpad;

  /* protects all data processing, i.e. is locked
   * in the chain function, finish_frame and when
   * processing serialized events */
  GStaticRecMutex stream_lock;

  /* MT-protected (with STREAM_LOCK) */
  GstSegment      segment;

  /*< private >*/
  GstAudioEncoderPrivate *priv;
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

/**
 * GstAudioEncoderClass:
 * @element_class:  The parent class structure
 * @start:          Optional.
 *                  Called when the element starts processing.
 *                  Allows opening external resources.
 * @stop:           Optional.
 *                  Called when the element stops processing.
 *                  Allows closing external resources.
 * @set_format:     Notifies subclass of incoming data format.
 *                  GstAudioInfo contains the format according to provided caps.
 * @handle_frame:   Provides input samples (or NULL to clear any remaining data)
 *                  according to directions as configured by the subclass
 *                  using the API.  Input data ref management is performed
 *                  by base class, subclass should not care or intervene,
 *                  and input data is only valid until next call to base class,
 *                  most notably a call to gst_audio_encoder_finish_frame().
 * @flush:          Optional.
 *                  Instructs subclass to clear any codec caches and discard
 *                  any pending samples and not yet returned encoded data.
 * @event:          Optional.
 *                  Event handler on the sink pad. This function should return
 *                  TRUE if the event was handled and should be discarded
 *                  (i.e. not unref'ed).
 * @pre_push:       Optional.
 *                  Called just prior to pushing (encoded data) buffer downstream.
 *                  Subclass has full discretionary access to buffer,
 *                  and a not OK flow return will abort downstream pushing.
 * @getcaps:        Optional.
 *                  Allows for a custom sink getcaps implementation (e.g.
 *                  for multichannel input specification).  If not implemented,
 *                  default returns gst_audio_encoder_proxy_getcaps
 *                  applied to sink template caps.
 *
 * Subclasses can override any of the available virtual methods or not, as
 * needed. At minimum @set_format and @handle_frame needs to be overridden.
 *
 * Since: 0.10.36
 */
struct _GstAudioEncoderClass {
  GstElementClass element_class;

  /*< public >*/
  /* virtual methods for subclasses */

  gboolean      (*start)              (GstAudioEncoder *enc);

  gboolean      (*stop)               (GstAudioEncoder *enc);

  gboolean      (*set_format)         (GstAudioEncoder *enc,
                                       GstAudioInfo        *info);

  GstFlowReturn (*handle_frame)       (GstAudioEncoder *enc,
                                       GstBuffer *buffer);

  void          (*flush)              (GstAudioEncoder *enc);

  GstFlowReturn (*pre_push)           (GstAudioEncoder *enc,
                                       GstBuffer **buffer);

  gboolean      (*event)              (GstAudioEncoder *enc,
                                       GstEvent *event);

  GstCaps *     (*getcaps)            (GstAudioEncoder *enc);

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

GType           gst_audio_encoder_get_type         (void);

GstFlowReturn   gst_audio_encoder_finish_frame (GstAudioEncoder * enc,
                                                GstBuffer       * buffer,
                                                gint              samples);

GstCaps *       gst_audio_encoder_proxy_getcaps (GstAudioEncoder * enc,
                                                 GstCaps         * caps);


/* context parameters */
GstAudioInfo  * gst_audio_encoder_get_audio_info (GstAudioEncoder * enc);

gint            gst_audio_encoder_get_frame_samples_min (GstAudioEncoder * enc);

void            gst_audio_encoder_set_frame_samples_min (GstAudioEncoder * enc, gint num);

gint            gst_audio_encoder_get_frame_samples_max (GstAudioEncoder * enc);

void            gst_audio_encoder_set_frame_samples_max (GstAudioEncoder * enc, gint num);

gint            gst_audio_encoder_get_frame_max (GstAudioEncoder * enc);

void            gst_audio_encoder_set_frame_max (GstAudioEncoder * enc, gint num);

gint            gst_audio_encoder_get_lookahead (GstAudioEncoder * enc);

void            gst_audio_encoder_set_lookahead (GstAudioEncoder * enc, gint num);

void            gst_audio_encoder_get_latency (GstAudioEncoder * enc,
                                               GstClockTime    * min,
                                               GstClockTime    * max);

void            gst_audio_encoder_set_latency (GstAudioEncoder * enc,
                                               GstClockTime      min,
                                               GstClockTime      max);

/* object properties */

void            gst_audio_encoder_set_mark_granule (GstAudioEncoder * enc,
                                                    gboolean enabled);

gboolean        gst_audio_encoder_get_mark_granule (GstAudioEncoder * enc);

void            gst_audio_encoder_set_perfect_timestamp (GstAudioEncoder * enc,
                                                         gboolean          enabled);

gboolean        gst_audio_encoder_get_perfect_timestamp (GstAudioEncoder * enc);

void            gst_audio_encoder_set_hard_resync (GstAudioEncoder * enc,
                                                   gboolean          enabled);

gboolean        gst_audio_encoder_get_hard_resync (GstAudioEncoder * enc);

void            gst_audio_encoder_set_tolerance (GstAudioEncoder * enc,
                                                 gint64            tolerance);

gint64          gst_audio_encoder_get_tolerance (GstAudioEncoder * enc);

void            gst_audio_encoder_set_hard_min (GstAudioEncoder * enc,
                                                gboolean enabled);

gboolean        gst_audio_encoder_get_hard_min (GstAudioEncoder * enc);

void            gst_audio_encoder_set_drainable (GstAudioEncoder * enc,
                                                 gboolean enabled);

gboolean        gst_audio_encoder_get_drainable (GstAudioEncoder * enc);

void            gst_audio_encoder_merge_tags (GstAudioEncoder * enc,
                                              const GstTagList * tags, GstTagMergeMode mode);

G_END_DECLS

#endif /* __GST_AUDIO_ENCODER_H__ */
