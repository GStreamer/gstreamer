/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstversion.h: Version information for GStreamer
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


#ifndef __GST_VERSION_H__
#define __GST_VERSION_H__

/*
 * Use these only when you want to know what GStreamer version your stuff was
 * compiled against.
 * Use the #gst_version function if you want to know which version of 
 * GStreamer you are currently linked against.
 */
#define GST_VERSION_MAJOR 0
#define GST_VERSION_MINOR 8
#define GST_VERSION_MICRO 1

void    gst_version     (guint *major, guint *minor, guint *micro);

#endif /* __GST_VERSION_H__ */
