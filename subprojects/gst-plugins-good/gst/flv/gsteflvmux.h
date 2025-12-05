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

#define GST_TYPE_EFLV_MUX_PAD (gst_eflv_mux_pad_get_type())
#define GST_EFLV_MUX_PAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_EFLV_MUX_PAD, GstEFlvMuxPad))
#define GST_EFLV_MUX_PAD_CAST(obj) ((GstEFlvMuxPad *)(obj))
#define GST_EFLV_MUX_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_EFLV_MUX_PAD, GstEFlvMuxPad))
#define GST_IS_EFLV_MUX_PAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_EFLV_MUX_PAD))
#define GST_IS_EFLV_MUX_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_EFLV_MUX_PAD))

typedef struct _GstEFlvMuxClass
{
  GstFlvMuxClass parent_class;
} GstEFlvMuxClass;

typedef struct _GstEFlvMux
{
  GstFlvMux parent;
} GstEFlvMux;

/** GstEFlvMuxPad
 *
 * Pad to stream the default track in a enhanced (multitrack) FLV or legacy FLV
 * format
 *
 * Since: 1.28
 */
typedef struct _GstEFlvMuxPad
{
  GstFlvMuxPad parent_pad;
} GstEFlvMuxPad;

typedef struct _GstEFlvMuxPadClass {
  GstFlvMuxPadClass parent;
} GstEFlvMuxPadClass;

GType gst_eflv_mux_get_type    (void);
GType gst_eflv_mux_pad_get_type (void);

#define GST_TYPE_EFLV_MUX     (gst_eflv_mux_get_type ())

#endif /* __GST_EFLV_MUX_H_ */
