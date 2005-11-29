/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
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

#ifndef __GST_BASE_TRANSFORM_H__
#define __GST_BASE_TRANSFORM_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_TRANSFORM		(gst_base_transform_get_type())
#define GST_BASE_TRANSFORM(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_TRANSFORM,GstBaseTransform))
#define GST_BASE_TRANSFORM_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_TRANSFORM,GstBaseTransformClass))
#define GST_BASE_TRANSFORM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_BASE_TRANSFORM,GstBaseTransformClass))
#define GST_IS_BASE_TRANSFORM(obj)	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_TRANSFORM))
#define GST_IS_BASE_TRANSFORM_CLASS(obj)(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_TRANSFORM))

/**
 * GST_BASE_TRANSFORM_SINK_NAME:
 *
 * the name of the templates for the sink pad
 */
#define GST_BASE_TRANSFORM_SINK_NAME	"sink"
/**
 * GST_BASE_TRANSFORM_SRC_NAME:
 *
 * the name of the templates for the source pad
 */
#define GST_BASE_TRANSFORM_SRC_NAME	"src"

typedef struct _GstBaseTransform GstBaseTransform;
typedef struct _GstBaseTransformClass GstBaseTransformClass;

/**
 * GstBaseTransform:
 *
 * The opaque #GstBaseTransform data structure.
 */
struct _GstBaseTransform {
  GstElement	 element;

  /*< protected >*/
  /* source and sink pads */
  GstPad	*sinkpad;
  GstPad	*srcpad;

  /* Set by sub-class */
  gboolean	 passthrough;
  gboolean	 always_in_place;

  GstCaps	*cache_caps1;
  guint		 cache_caps1_size;
  GstCaps	*cache_caps2;
  guint		 cache_caps2_size;
  gboolean	 have_same_caps;

  gboolean	 delay_configure;
  gboolean	 pending_configure;
  gboolean	 negotiated;

  gboolean       have_newsegment;

  /* MT-protected (with STREAM_LOCK) */
  GstSegment     segment;

  GMutex	*transform_lock;

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

/**
 * GstBaseTransformClass::transform_caps:
 * @direction: the pad direction
 * @caps: the caps
 *
 * This method should answer the question "given this pad, and given these
 * caps, what caps would you allow on the other pad inside your element ?"
 */
struct _GstBaseTransformClass {
  GstElementClass parent_class;

  /*< public >*/
  /* virtual methods for subclasses */

  /* given the (non-)fixed simple caps on the pad in the given direction,
   * what can I do on the other pad ? */
  GstCaps*	(*transform_caps) (GstBaseTransform *trans,
                                   GstPadDirection direction,
                                   GstCaps *caps);

  /* given caps on one pad, how would you fixate caps on the other pad ? */
  void		(*fixate_caps)	  (GstBaseTransform *trans,
                                   GstPadDirection direction, GstCaps *caps,
                                   GstCaps *othercaps);

  /* given the size of a buffer in the given direction with the given caps,
   * calculate the byte size of an buffer on the other side with the given
   * other caps; the default
   * implementation uses get_size and keeps the number of units the same */
  gboolean      (*transform_size) (GstBaseTransform *trans,
                                   GstPadDirection direction,
                                   GstCaps *caps, guint size,
                                   GstCaps *othercaps, guint *othersize);

  /* get the byte size of one unit for a given caps.
   * Always needs to be implemented if the transform is not in-place. */
  gboolean      (*get_unit_size)  (GstBaseTransform *trans, GstCaps *caps,
                                   guint *size);

  /* notify the subclass of new caps */
  gboolean      (*set_caps)     (GstBaseTransform *trans, GstCaps *incaps,
                                 GstCaps *outcaps);

  /* start and stop processing, ideal for opening/closing the resource */
  gboolean      (*start)        (GstBaseTransform *trans);
  gboolean      (*stop)         (GstBaseTransform *trans);

  gboolean      (*event)        (GstBaseTransform *trans, GstEvent *event);

  /* transform one incoming buffer to one outgoing buffer.
   * Always needs to be implemented unless always operating in-place.
   * transform function is allowed to change size/timestamp/duration of
   * the outgoing buffer. */
  GstFlowReturn (*transform)    (GstBaseTransform *trans, GstBuffer *inbuf,
                                 GstBuffer *outbuf);

  /* transform a buffer inplace */
  GstFlowReturn (*transform_ip) (GstBaseTransform *trans, GstBuffer *buf);

  /* FIXME: When adjusting the padding, more these to nicer places in the class */
  /* Set by child classes to automatically do passthrough mode */
  gboolean       passthrough_on_same_caps;

  /* Subclasses can override this to do their own allocation of output buffers.
   * Elements that only do analysis can return a subbuffer or even just
   * increment the reference to the input buffer (if in passthrough mode)
   */
  GstFlowReturn (*prepare_output_buffer) (GstBaseTransform * trans,
     GstBuffer *input, gint size, GstCaps *caps, GstBuffer **buf);

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

GType           gst_base_transform_get_type         (void);

void		gst_base_transform_set_passthrough  (GstBaseTransform *trans,
	                                             gboolean passthrough);
gboolean	gst_base_transform_is_passthrough   (GstBaseTransform *trans);

void		gst_base_transform_set_in_place     (GstBaseTransform *trans,
	                                             gboolean in_place);
gboolean	gst_base_transform_is_in_place      (GstBaseTransform *trans);


G_END_DECLS

#endif /* __GST_BASE_TRANSFORM_H__ */
