/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifndef __GST_ATOMIC_H__
#define __GST_ATOMIC_H__

#include <glib.h>

G_BEGIN_DECLS

typedef volatile gint gst_vgint;	/* gtk-doc volatile workaround */

typedef struct _GstAtomicInt GstAtomicInt;

struct _GstAtomicInt {
  gst_vgint     counter;
  GMutex	*lock;		/* for C fallback */
};


void      	gst_atomic_int_init     	(GstAtomicInt *aint, gint val);
void      	gst_atomic_int_destroy  	(GstAtomicInt *aint);
void      	gst_atomic_int_set      	(GstAtomicInt *aint, gint val);
gint      	gst_atomic_int_read     	(GstAtomicInt *aint);
void 		gst_atomic_int_add 		(GstAtomicInt *aint, gint val);
void 		gst_atomic_int_inc 		(GstAtomicInt *aint);
gboolean 	gst_atomic_int_dec_and_test 	(GstAtomicInt *aint);


G_END_DECLS

#endif /*  __GST_ATOMIC_H__ */
