/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstcpu.h: Header for CPU-specific routines
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


#ifndef __GST_CPU_H__
#define __GST_CPU_H__

G_BEGIN_DECLS

typedef enum {
  GST_CPU_FLAG_MMX      = (1<<0),
  GST_CPU_FLAG_SSE      = (1<<1),
  GST_CPU_FLAG_MMXEXT   = (1<<2),
  GST_CPU_FLAG_3DNOW    = (1<<3)
} GstCPUFlags;

void 		_gst_cpu_initialize	(gboolean useopt);

GstCPUFlags 	gst_cpu_get_flags	(void);

G_END_DECLS

#endif /* __GST_CPU_H__ */
