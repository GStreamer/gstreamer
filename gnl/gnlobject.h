/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@gmail.com>
 *               2004-2008 Edward Hervey <bilboed@bilboed.com>
 *
 * gnlobject.h: Header for base GnlObject
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


#ifndef __GNL_OBJECT_H__
#define __GNL_OBJECT_H__

#include <gst/gst.h>

#include "gnltypes.h"

G_BEGIN_DECLS
#define GNL_TYPE_OBJECT \
  (gnl_object_get_type())
#define GNL_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GNL_TYPE_OBJECT,GnlObject))
#define GNL_OBJECT_CAST(obj) ((GnlObject*) (obj))
#define GNL_OBJECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GNL_TYPE_OBJECT,GnlObjectClass))
#define GNL_OBJECT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GNL_TYPE_OBJECT, GnlObjectClass))
#define GNL_IS_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GNL_TYPE_OBJECT))
#define GNL_IS_OBJECT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GNL_TYPE_OBJECT))

#define GNL_OBJECT_SRC(obj) (((GnlObject *) obj)->srcpad)

/**
 * GnlObjectFlags:
 * @GNL_OBJECT_IS_SOURCE:
 * @GNL_OBJECT_IS_OPERATION:
 * @GNL_OBJECT_IS_EXPANDABLE: The #GnlObject start/stop will extend accross the full composition.
 * @GNL_OBJECT_LAST_FLAG:
*/

typedef enum
{
  GNL_OBJECT_SOURCE = (GST_BIN_FLAG_LAST << 0),
  GNL_OBJECT_OPERATION = (GST_BIN_FLAG_LAST << 1),
  GNL_OBJECT_EXPANDABLE = (GST_BIN_FLAG_LAST << 2),
  GNL_OBJECT_COMPOSITION = (GST_BIN_FLAG_LAST << 3),
  /* padding */
  GNL_OBJECT_LAST_FLAG = (GST_BIN_FLAG_LAST << 5)
} GnlObjectFlags;


#define GNL_OBJECT_IS_SOURCE(obj) \
  (GST_OBJECT_FLAG_IS_SET(obj, GNL_OBJECT_SOURCE))
#define GNL_OBJECT_IS_OPERATION(obj) \
  (GST_OBJECT_FLAG_IS_SET(obj, GNL_OBJECT_OPERATION))
#define GNL_OBJECT_IS_EXPANDABLE(obj) \
  (GST_OBJECT_FLAG_IS_SET(obj, GNL_OBJECT_EXPANDABLE))
#define GNL_OBJECT_IS_COMPOSITION(obj) \
  (GST_OBJECT_FLAG_IS_SET(obj, GNL_OBJECT_COMPOSITION))

/* For internal usage only */
#define GNL_OBJECT_START(obj) (GNL_OBJECT_CAST (obj)->start)
#define GNL_OBJECT_STOP(obj) (GNL_OBJECT_CAST (obj)->stop)
#define GNL_OBJECT_DURATION(obj) (GNL_OBJECT_CAST (obj)->duration)
#define GNL_OBJECT_INPOINT(obj) (GNL_OBJECT_CAST (obj)->inpoint)
#define GNL_OBJECT_PRIORITY(obj) (GNL_OBJECT_CAST (obj)->priority)

#define GNL_OBJECT_IS_COMMITING(obj) (GNL_OBJECT_CAST (obj)->commiting)

struct _GnlObject
{
  GstBin parent;

  GstPad *srcpad;

  /* Time positionning */
  GstClockTime start;
  GstClockTime inpoint;
  GstClockTimeDiff duration;

  /* Pending time positionning
   * Should be == GST_CLOCK_TIME_NONE when nothing to do
   */
  GstClockTime pending_start;
  GstClockTime pending_inpoint;
  GstClockTimeDiff pending_duration;
  guint32 pending_priority;
  gboolean pending_active;

  gboolean commit_needed;
  gboolean commiting; /* Set to TRUE during the commiting time only */

  gboolean expandable;

  /* read-only */
  GstClockTime stop;

  /* priority in parent */
  guint32 priority;

  /* active in parent */
  gboolean active;

  /* Filtering caps */
  GstCaps *caps;

  /* current segment seek <RO> */
  gdouble segment_rate;
  GstSeekFlags segment_flags;
  gint64 segment_start;
  gint64 segment_stop;
};

struct _GnlObjectClass
{
  GstBinClass parent_class;

  /* Signal method handler */
  gboolean (*commit_signal_handler) (GnlObject * object, gboolean recurse);

  /* virtual methods for subclasses */
    gboolean (*prepare) (GnlObject * object);
    gboolean (*cleanup) (GnlObject * object);
    gboolean (*commit) (GnlObject * object, gboolean recurse);
};

GType gnl_object_get_type (void);

gboolean
gnl_object_to_media_time (GnlObject * object, GstClockTime otime,
			  GstClockTime * mtime);

gboolean
gnl_media_to_object_time (GnlObject * object, GstClockTime mtime,
			  GstClockTime * otime);

void
gnl_object_set_caps (GnlObject * object, const GstCaps * caps);

void
gnl_object_set_commit_needed (GnlObject *object);

gboolean
gnl_object_commit (GnlObject *object, gboolean recurse);

void
gnl_object_reset (GnlObject *object);
G_END_DECLS
#endif /* __GNL_OBJECT_H__ */
