/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstdata_private.h: private gstdata stuff
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

#include "gstatomic_impl.h"

#define _GST_DATA_INIT(data, ptype, pflags, pfree, pcopy) 	\
G_STMT_START {							\
  gst_atomic_int_init (&(data)->refcount, 1);			\
  (data)->type = ptype;						\
  (data)->flags = pflags;					\
  (data)->free = pfree;						\
  (data)->copy = pcopy;						\
} G_STMT_END;

#define _GST_DATA_DISPOSE(data)				 	\
G_STMT_START {							\
  gst_atomic_int_destroy (&(data)->refcount);			\
} G_STMT_END;

