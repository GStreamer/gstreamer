/* GStreamer
 * Copyright (C) <2005> Arwed v. Merkatz <v.merkatz@gmx.net>
 *
 * esdsink.h: an EsounD audio sink
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


#ifndef __GST_ESDSINK_H__
#define __GST_ESDSINK_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiosink.h>

G_BEGIN_DECLS

#define GST_TYPE_ESDSINK \
  (gst_esdsink_get_type())
#define GST_ESDSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ESDSINK,GstEsdSink))
#define GST_ESDSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ESDSINK,GstEsdSinkClass))
#define GST_IS_ESDSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ESDSINK))
#define GST_IS_ESDSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ESDSINK))

typedef struct _GstEsdSink GstEsdSink;
typedef struct _GstEsdSinkClass GstEsdSinkClass;

struct _GstEsdSink {
  GstAudioSink   sink;

  int       fd;
  int       ctrl_fd;
  gchar    *host;

  guint     rate;
  GstCaps  *cur_caps;
};

struct _GstEsdSinkClass {
  GstAudioSinkClass parent_class;
};

GType gst_esdsink_get_type (void);

G_END_DECLS

#endif /* __GST_ESDSINK_H__ */
