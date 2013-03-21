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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <glib.h>

#ifndef __EGL_GST_QUEUE_ARRAY_H__
#define __EGL_GST_QUEUE_ARRAY_H__

typedef struct _EGLGstQueueArray EGLGstQueueArray;

G_GNUC_INTERNAL
EGLGstQueueArray * egl_gst_queue_array_new       (guint initial_size);
G_GNUC_INTERNAL
void            egl_gst_queue_array_free      (EGLGstQueueArray * array);
G_GNUC_INTERNAL
gpointer        egl_gst_queue_array_pop_head  (EGLGstQueueArray * array);
G_GNUC_INTERNAL
gpointer        egl_gst_queue_array_peek_head (EGLGstQueueArray * array);
G_GNUC_INTERNAL
void            egl_gst_queue_array_push_tail (EGLGstQueueArray * array,
                                           gpointer        data);
G_GNUC_INTERNAL
gboolean        egl_gst_queue_array_is_empty  (EGLGstQueueArray * array);
G_GNUC_INTERNAL
gpointer        egl_gst_queue_array_drop_element (EGLGstQueueArray * array,
                                              guint           idx);
G_GNUC_INTERNAL
guint           egl_gst_queue_array_find (EGLGstQueueArray * array,
                                      GCompareFunc    func,
                                      gpointer        data);
G_GNUC_INTERNAL
guint           egl_gst_queue_array_get_length (EGLGstQueueArray * array);

#endif
