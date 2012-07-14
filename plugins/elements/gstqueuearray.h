/* GStreamer
 * Copyright (C) 2009-2010 Edward Hervey <bilboed@bilboed.com>
 *
 * gstqueuearray.h:
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

#include <glib.h>

#ifndef __GST_QUEUE_ARRAY_H__
#define __GST_QUEUE_ARRAY_H__

typedef struct _GstQueueArray GstQueueArray;

struct _GstQueueArray
{
  gpointer *array;
  guint size;
  guint head;
  guint tail;
  guint length;
};

G_GNUC_INTERNAL void            gst_queue_array_init  (GstQueueArray * array,
                                                       guint           initial_size);

G_GNUC_INTERNAL void            gst_queue_array_clear (GstQueueArray * array);

G_GNUC_INTERNAL GstQueueArray * gst_queue_array_new       (guint initial_size);

G_GNUC_INTERNAL gpointer        gst_queue_array_pop_head  (GstQueueArray * array);

G_GNUC_INTERNAL void            gst_queue_array_push_tail (GstQueueArray * array,
                                                           gpointer        data);

G_GNUC_INTERNAL gboolean        gst_queue_array_is_empty  (GstQueueArray * array);

G_GNUC_INTERNAL void            gst_queue_array_free      (GstQueueArray * array);

G_GNUC_INTERNAL void            gst_queue_array_drop_element (GstQueueArray * array,
                                                              guint           idx);

G_GNUC_INTERNAL guint           gst_queue_array_find (GstQueueArray * array,
                                                      GCompareFunc    func,
                                                      gpointer        data);

#endif
