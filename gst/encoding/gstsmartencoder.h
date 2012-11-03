/* GStreamer video re-encoder element
 * Copyright (C) <2010> Edward Hervey <bilboed@bilboed.com>
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
#ifndef __SMART_ENCODER_H__
#define __SMART_ENCODER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_SMART_ENCODER \
  (gst_smart_encoder_get_type())
#define GST_SMART_ENCODER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SMART_ENCODER,GstSmartEncoder))
#define GST_SMART_ENCODER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SMART_ENCODER,GstSmartEncoderClass))
#define GST_IS_SMART_ENCODER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SMART_ENCODER))
#define GST_IS_SMART_ENCODER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SMART_ENCODER))

typedef struct _GstSmartEncoder GstSmartEncoder;
typedef struct _GstSmartEncoderClass GstSmartEncoderClass;

struct _GstSmartEncoder {
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GstSegment *segment;
  GstEvent *newsegment;

  /* Pending GOP to be checked */
  GList *pending_gop;
  guint64 gop_start;		/* GOP start in running time */
  guint64 gop_stop;		/* GOP end in running time */

  /* Internal recoding elements */
  GstPad *internal_sinkpad;
  GstPad *internal_srcpad;
  GstElement *decoder;
  GstElement *encoder;

  /* Available caps at runtime */
  GstCaps *available_caps;
};

struct _GstSmartEncoderClass {
  GstElementClass parent_class;
};

GType gst_smart_encoder_get_type(void);

G_END_DECLS

#endif /* __SMART_ENCODER_H__ */
