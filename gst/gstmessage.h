/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * gstmessage.h: Header for GstMessage subsystem
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

#ifndef __GST_MESSAGE_H__
#define __GST_MESSAGE_H__

#include <gst/gsttypes.h>
#include <gst/gstdata.h>
#include <gst/gstobject.h>
#include <gst/gsttag.h>
#include <gst/gststructure.h>

G_BEGIN_DECLS GST_EXPORT GType _gst_message_type;

typedef enum
{
  GST_MESSAGE_UNKNOWN = 0,
  GST_MESSAGE_EOS = 1,
  GST_MESSAGE_ERROR = 2,
  GST_MESSAGE_WARNING = 3,
  GST_MESSAGE_INFO = 4,
  GST_MESSAGE_TAG = 5,
  GST_MESSAGE_BUFFERING = 6,
  GST_MESSAGE_STATE_CHANGED = 7,
  GST_MESSAGE_STEP_DONE = 8,
} GstMessageType;

#define GST_MESSAGE_TRACE_NAME	"GstMessage"

#define GST_TYPE_MESSAGE	(_gst_message_type)
#define GST_MESSAGE(message)	((GstMessage*)(message))
#define GST_IS_MESSAGE(message)	(GST_DATA_TYPE(message) == GST_TYPE_MESSAGE)

#define GST_MESSAGE_TYPE(message)	(GST_MESSAGE(message)->type)
#define GST_MESSAGE_TIMESTAMP(message)	(GST_MESSAGE(message)->timestamp)
#define GST_MESSAGE_SRC(message)	(GST_MESSAGE(message)->src)

#define GST_MESSAGE_TAG_LIST(message)	(GST_MESSAGE(message)->message_data.tag.list)

#define GST_MESSAGE_ERROR_GERROR(message)	(GST_MESSAGE(message)->message_data.error.gerror)
#define GST_MESSAGE_ERROR_DEBUG(message)	(GST_MESSAGE(message)->message_data.error.debug)
#define GST_MESSAGE_WARNING_GERROR(message)	(GST_MESSAGE(message)->message_data.error.gerror)
#define GST_MESSAGE_WARNING_DEBUG(message)	(GST_MESSAGE(message)->message_data.error.debug)

struct _GstMessage
{
  GstData data;

  /*< public > *//* with MESSAGE_LOCK */
  GMutex *lock;                 /* lock and cond for async delivery */
  GCond *cond;

  /*< public > *//* with COW */
  GstMessageType type;
  guint64 timestamp;
  GstObject *src;

  union
  {
    struct
    {
      GError *gerror;
      gchar *debug;
    } error;
    struct
    {
      GstStructure *structure;
    } structure;
    struct
    {
      GstTagList *list;
    } tag;
  } message_data;

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

void _gst_message_initialize (void);

GType gst_message_get_type (void);
GstMessage *gst_message_new (GstMessageType type, GstObject * src);

/* refcounting */
#define         gst_message_ref(ev)		GST_MESSAGE (gst_data_ref (GST_DATA (ev)))
#define         gst_message_ref_by_count(ev,c)	GST_MESSAGE (gst_data_ref_by_count (GST_DATA (ev), c))
#define         gst_message_unref(ev)		gst_data_unref (GST_DATA (ev))
/* copy message */
#define         gst_message_copy(ev)		GST_MESSAGE (gst_data_copy (GST_DATA (ev)))

GstMessage *gst_message_new_eos (GstObject * src);
GstMessage *gst_message_new_error (GstObject * src, GError * error,
    gchar * debug);
GstMessage *gst_message_new_warning (GstObject * src, GError * error,
    gchar * debug);
GstMessage *gst_message_new_tag (GstObject * src, GstTagList * tag_list);

G_END_DECLS
#endif /* __GST_MESSAGE_H__ */
