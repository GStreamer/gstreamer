/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstformat.h: Header for GstFormat types of offset
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


#ifndef __GST_FORMAT_H__
#define __GST_FORMAT_H__

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  GST_FORMAT_NONE   	= 0,
  GST_FORMAT_DEFAULT   	= 1,
  GST_FORMAT_BYTES   	= 2,
  GST_FORMAT_TIME 	= 3,
  GST_FORMAT_BUFFERS	= 4,
  GST_FORMAT_PERCENT	= 5,
  /* samples for audio, frames/fields for video */
  GST_FORMAT_UNIT 	= 6,
} GstFormat;

G_END_DECLS

#endif /* __GST_FORMAT_H__ */
