
/* GStreamer
 * Copyright (C) <2009> Collabora Ltd
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk
 * Copyright (C) <2009> Nokia Inc
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

#ifndef __GST_SHM_H__
#define __GST_SHM_H__

#include <gst/gst.h>
#include <semaphore.h>

G_BEGIN_DECLS
#define GST_SHM_MAX_CAPS_LENGTH (1024)
#define SHM_LOCK(shm)   sem_wait (&(shm)->mutex);
#define SHM_UNLOCK(shm) sem_post (&(shm)->mutex);
#define GST_SHM_CAPS_BUFFER(shm) ((shm)->data)
#define GST_SHM_BUFFER(shm) ((shm)->data+(shm)->caps_size)
    struct GstShmHeader
{
  sem_t notification;
  sem_t mutex;

  guint caps_gen;
  guint buffer_gen;

  gint caps_size;
  gint buffer_size;

  guint flags;

  GstClockTime timestamp;
  GstClockTime duration;

  guint64 offset;
  guint64 offset_end;

  gboolean eos;

  gchar data[0];
  /*
   * gchar caps_buffer[caps_size];
   * gchar buffer[buffer_size];
   */
};

G_END_DECLS
#endif /* __GST_SHM_H__ */
