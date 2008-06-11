/*
 *
 * GStreamer
 * Copyright (C) 2004 Billy Biggs <vektor@dumbterm.net>
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

/*
 * Relicensed for GStreamer from GPL to LGPL with permit from Billy Biggs.
 * See: http://bugzilla.gnome.org/show_bug.cgi?id=163578
 */

#ifndef GREEDYH_H_INCLUDED
#define GREEDYH_H_INCLUDED

#include "gstdeinterlace2.h"

#ifdef __cplusplus
extern "C" {
#endif

void greedyh_init( void );
void greedyh_filter_mmx( GstDeinterlace2 *object );
void greedyh_filter_3dnow( GstDeinterlace2 *object );
void greedyh_filter_sse( GstDeinterlace2 *object );

#ifdef __cplusplus
};
#endif

#endif /* GREEDYH_H_INCLUDED */
