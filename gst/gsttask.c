/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gsttask.c: Streaming tasks
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

#include "gst_private.h"

#include "gstinfo.h"
#include "gsttask.h"

static void gst_task_class_init (GstTaskClass * klass);
static void gst_task_init (GstTask * sched);
static void gst_task_dispose (GObject * object);

static GstObjectClass *parent_class = NULL;

GType
gst_task_get_type (void)
{
  static GType _gst_task_type = 0;

  if (!_gst_task_type) {
    static const GTypeInfo task_info = {
      sizeof (GstTaskClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_task_class_init,
      NULL,
      NULL,
      sizeof (GstTask),
      0,
      (GInstanceInitFunc) gst_task_init,
      NULL
    };

    _gst_task_type =
        g_type_register_static (GST_TYPE_OBJECT, "GstTask",
        &task_info, G_TYPE_FLAG_ABSTRACT);
  }
  return _gst_task_type;
}

static void
gst_task_class_init (GstTaskClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_task_dispose);
}

static void
gst_task_init (GstTask * sched)
{
}

static void
gst_task_dispose (GObject * object)
{
  GstTask *task = GST_TASK (object);

  /* thse lists should all be NULL */
  GST_DEBUG ("task %p dispose", task);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

gboolean
gst_task_start (GstTask * task)
{
  GstTaskClass *tclass;
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_TASK (task), result);

  tclass = GST_TASK_GET_CLASS (task);

  if (tclass->start)
    result = tclass->start (task);

  return result;
}

gboolean
gst_task_stop (GstTask * task)
{
  GstTaskClass *tclass;
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_TASK (task), result);

  tclass = GST_TASK_GET_CLASS (task);

  if (tclass->stop)
    result = tclass->stop (task);

  return result;
}
