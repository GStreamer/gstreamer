/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Wim Taymans <wim@fluendo.com>
 *
 * gstfdset.h: fdset datastructure
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

#ifndef __GST_FDSET_H__
#define __GST_FDSET_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstFDSet GstFDSet;

typedef struct {
  int fd;
  gint idx;
} GstFD;

typedef enum {
  GST_FDSET_MODE_SELECT,
  GST_FDSET_MODE_POLL,
  GST_FDSET_MODE_EPOLL
} GstFDSetMode;

#define GST_TYPE_FDSET_MODE (gst_fdset_mode_get_type())
GType gst_fdset_mode_get_type (void);


GstFDSet*       gst_fdset_new (GstFDSetMode mode);
void            gst_fdset_free (GstFDSet *set);

void            gst_fdset_set_mode (GstFDSet *set, GstFDSetMode mode);
GstFDSetMode    gst_fdset_get_mode (GstFDSet *set);

gboolean        gst_fdset_add_fd (GstFDSet *set, GstFD *fd); 
gboolean        gst_fdset_remove_fd (GstFDSet *set, GstFD *fd); 

void            gst_fdset_fd_ctl_write (GstFDSet *set, GstFD *fd, gboolean active); 
void            gst_fdset_fd_ctl_read (GstFDSet *set, GstFD *fd, gboolean active); 

gboolean        gst_fdset_fd_has_closed (GstFDSet *set, GstFD *fd); 
gboolean        gst_fdset_fd_has_error (GstFDSet *set, GstFD *fd); 
gboolean        gst_fdset_fd_can_read (GstFDSet *set, GstFD *fd); 
gboolean        gst_fdset_fd_can_write (GstFDSet *set, GstFD *fd); 

gint            gst_fdset_wait (GstFDSet *set, int timeout);

G_END_DECLS

#endif /* __GST_FDSET_H__ */
