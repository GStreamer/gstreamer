/*
 * GStreamer
 * Copyright (c) 2010, Texas Instruments Incorporated
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __GST_DUCATI_H__
#define __GST_DUCATI_H__

#include <stdint.h>
#include <string.h>

#include <tiler.h>
#include <tilermem.h>
#include <memmgr.h>
#include "pvr2d.h"


#include <gst/gst.h>

G_BEGIN_DECLS

/* align x to next highest multiple of 2^n */
#define ALIGN2(x,n)   (((x) + ((1 << (n)) - 1)) & ~((1 << (n)) - 1))

void * gst_ducati_alloc_1d (gint sz);
void * gst_ducati_alloc_2d (gint width, gint height, guint * sz);

const gchar * gst_pvr2d_error_get_string (PVR2DERROR code);

G_END_DECLS

#endif /* __GST_DUCATI_H__ */
