/* GStreamer
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
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


#ifndef __VIDEOSCALE_H__
#define __VIDEOSCALE_H__

#include "gstvideoscale.h"

struct videoscale_format_struct {
	unsigned int fourcc;
	int bpp;
	void (*scale)(GstVideoscale *,unsigned char *dest, unsigned char *src);
	int depth;
	unsigned int endianness;
	unsigned int red_mask;
	unsigned int green_mask;
	unsigned int blue_mask;
};

extern struct videoscale_format_struct videoscale_formats[];
extern int videoscale_n_formats;

GstStructure *videoscale_get_structure(struct videoscale_format_struct *format);

struct videoscale_format_struct *videoscale_find_by_structure (GstStructure *structure);


#endif

