/* GStreamer
 *
 * Copyright (C) 2011 Intel
 * Copyright (C) 2011 Collabora Ltd.
 *   Author: Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
 *
 * video-context.h: Video Context interface and helpers
 *
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

#ifndef __GST_VIDEO_CONTEXT_H__
#define __GST_VIDEO_CONTEXT_H__

#ifndef GST_USE_UNSTABLE_API
#warning "The GstVideoContext interface is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_CONTEXT              (gst_video_context_iface_get_type ())
#define GST_VIDEO_CONTEXT(obj)              (GST_IMPLEMENTS_INTERFACE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VIDEO_CONTEXT, GstVideoContext))
#define GST_IS_VIDEO_CONTEXT(obj)           (GST_IMPLEMENTS_INTERFACE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VIDEO_CONTEXT))
#define GST_VIDEO_CONTEXT_GET_IFACE(inst)   (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GST_TYPE_VIDEO_CONTEXT, GstVideoContextInterface))

/**
 * GstVideoContext:
 *
 * Opaque #GstVideoContext data structure.
 */
typedef struct _GstVideoContext GstVideoContext;
typedef struct _GstVideoContextInterface GstVideoContextInterface;

/**
 * GstVideoContextInterface:
 * @parent: parent interface type.
 * @set_context: vmethod to set video context.
 *
 * #GstVideoContextInterface interface.
 */
struct _GstVideoContextInterface
{
  GTypeInterface parent;

  /* virtual functions */
  void (*set_context) (GstVideoContext * context,
                       const gchar * type,
                       const GValue * value);

};

GType    gst_video_context_iface_get_type (void);

/* virtual class method and associated helpers */
void     gst_video_context_set_context           (GstVideoContext * context,
                                                  const gchar * type,
                                                  const GValue * value);
void     gst_video_context_set_context_string    (GstVideoContext * context,
                                                  const gchar * type,
                                                  const gchar * value);
void     gst_video_context_set_context_pointer   (GstVideoContext * context,
                                                  const gchar * type,
                                                  gpointer value);
void     gst_video_context_set_context_object    (GstVideoContext * context,
                                                  const gchar * type,
                                                  GObject * value);


/* message helpers */
void      gst_video_context_prepare                (GstVideoContext *context,
                                                    const gchar ** types);

gboolean  gst_video_context_message_parse_prepare  (GstMessage * message,
                                                    const gchar *** types,
                                                    GstVideoContext ** ctx);

/* query helpers */
GstQuery     * gst_video_context_query_new                    (const gchar ** types);
gboolean       gst_video_context_run_query                    (GstElement *element,
                                                               GstQuery *query);
const gchar ** gst_video_context_query_get_supported_types    (GstQuery * query);
void           gst_video_context_query_parse_value            (GstQuery * query,
                                                               const gchar ** type,
                                                               const GValue ** value);
void           gst_video_context_query_set_value              (GstQuery * query,
                                                               const gchar * type,
                                                               GValue * value);
void           gst_video_context_query_set_string             (GstQuery * query,
                                                               const gchar * type,
                                                               const gchar * value);
void           gst_video_context_query_set_pointer            (GstQuery * query,
                                                               const gchar * type,
                                                               gpointer value);
void           gst_video_context_query_set_object             (GstQuery * query,
                                                               const gchar * type,
                                                               GObject * value);

G_END_DECLS

#endif /* __GST_VIDEO_CONTEXT_H__ */
