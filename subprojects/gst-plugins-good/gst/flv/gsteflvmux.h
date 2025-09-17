/* GStreamer
 *
 * Copyright (c) 2025 Centricular Ltd
 *  @author: Taruntej Kanakamalla <tarun@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_EFLV_MUX_H__
#define __GST_EFLV_MUX_H__

#include "gstflvmux.h"

typedef struct _GstEFlvMuxClass
{
  GstFlvMuxClass parent_class;
} GstEFlvMuxClass;

typedef struct _GstEFlvMux
{
  GstFlvMux parent;
} GstEFlvMux;

GType gst_eflv_mux_get_type    (void);

#define GST_TYPE_EFLV_MUX     (gst_eflv_mux_get_type ())

#endif /* __GST_EFLV_MUX_H_ */