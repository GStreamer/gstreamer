/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@gmail.com>
 *               2004-2008 Edward Hervey <bilboed@bilboed.com>
 *
 * nleobject.h: Header for base NleObject
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


#ifndef __NLE_OBJECT_H__
#define __NLE_OBJECT_H__

#include <gst/gst.h>

#include "nletypes.h"

G_BEGIN_DECLS
#define NLE_TYPE_OBJECT \
  (nle_object_get_type())
#define NLE_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),NLE_TYPE_OBJECT,NleObject))
#define NLE_OBJECT_CAST(obj) ((NleObject*) (obj))
#define NLE_OBJECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),NLE_TYPE_OBJECT,NleObjectClass))
#define NLE_OBJECT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), NLE_TYPE_OBJECT, NleObjectClass))
#define NLE_IS_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),NLE_TYPE_OBJECT))
#define NLE_IS_OBJECT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),NLE_TYPE_OBJECT))

#define NLE_OBJECT_SRC(obj) (((NleObject *) obj)->srcpad)

/**
 * NleObjectFlags:
 * @NLE_OBJECT_IS_SOURCE:
 * @NLE_OBJECT_IS_OPERATION:
 * @NLE_OBJECT_IS_EXPANDABLE: The #NleObject start/stop will extend accross the full composition.
 * @NLE_OBJECT_LAST_FLAG:
*/

typedef enum
{
  NLE_OBJECT_SOURCE = (GST_BIN_FLAG_LAST << 0),
  NLE_OBJECT_OPERATION = (GST_BIN_FLAG_LAST << 1),
  NLE_OBJECT_EXPANDABLE = (GST_BIN_FLAG_LAST << 2),
  NLE_OBJECT_COMPOSITION = (GST_BIN_FLAG_LAST << 3),
  /* padding */
  NLE_OBJECT_LAST_FLAG = (GST_BIN_FLAG_LAST << 5)
} NleObjectFlags;


#define NLE_OBJECT_IS_SOURCE(obj) \
  (GST_OBJECT_FLAG_IS_SET(obj, NLE_OBJECT_SOURCE))
#define NLE_OBJECT_IS_OPERATION(obj) \
  (GST_OBJECT_FLAG_IS_SET(obj, NLE_OBJECT_OPERATION))
#define NLE_OBJECT_IS_EXPANDABLE(obj) \
  (GST_OBJECT_FLAG_IS_SET(obj, NLE_OBJECT_EXPANDABLE))
#define NLE_OBJECT_IS_COMPOSITION(obj) \
  (GST_OBJECT_FLAG_IS_SET(obj, NLE_OBJECT_COMPOSITION))

/* For internal usage only */
#define NLE_OBJECT_START(obj) (NLE_OBJECT_CAST (obj)->start)
#define NLE_OBJECT_STOP(obj) (NLE_OBJECT_CAST (obj)->stop)
#define NLE_OBJECT_DURATION(obj) (NLE_OBJECT_CAST (obj)->duration)
#define NLE_OBJECT_INPOINT(obj) (NLE_OBJECT_CAST (obj)->inpoint)
#define NLE_OBJECT_PRIORITY(obj) (NLE_OBJECT_CAST (obj)->priority)

#define NLE_OBJECT_IS_COMMITING(obj) (NLE_OBJECT_CAST (obj)->commiting)

struct _NleObject
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

  gboolean in_composition;
};

struct _NleObjectClass
{
  GstBinClass parent_class;

  /* Signal method handler */
  gboolean (*commit_signal_handler) (NleObject * object, gboolean recurse);

  /* virtual methods for subclasses */
    gboolean (*prepare) (NleObject * object);
    gboolean (*cleanup) (NleObject * object);
    gboolean (*commit) (NleObject * object, gboolean recurse);
};

GType nle_object_get_type (void);

gboolean
nle_object_to_media_time (NleObject * object, GstClockTime otime,
			  GstClockTime * mtime);

gboolean
nle_media_to_object_time (NleObject * object, GstClockTime mtime,
			  GstClockTime * otime);

void
nle_object_set_caps (NleObject * object, const GstCaps * caps);

void
nle_object_set_commit_needed (NleObject *object);

gboolean
nle_object_commit (NleObject *object, gboolean recurse);

void
nle_object_reset (NleObject *object);

GstStateChangeReturn
nle_object_cleanup (NleObject * object);

G_END_DECLS
#endif /* __NLE_OBJECT_H__ */
