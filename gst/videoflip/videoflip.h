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


#ifndef __VIDEOFLIP_H__
#define __VIDEOFLIP_H__

#include "gstvideoflip.h"

struct videoflip_format_struct {
	char *fourcc;
	int depth;
	void (*scale)(GstVideoflip *,unsigned char *dest, unsigned char *src);
	int bpp;
	unsigned int endianness;
	unsigned int red_mask;
	unsigned int green_mask;
	unsigned int blue_mask;
};

extern struct videoflip_format_struct videoflip_formats[];
extern int videoflip_n_formats;

GstStructure *videoflip_get_cap(struct videoflip_format_struct *format);

struct videoflip_format_struct *videoflip_find_by_caps(const GstCaps *caps);


#endif

