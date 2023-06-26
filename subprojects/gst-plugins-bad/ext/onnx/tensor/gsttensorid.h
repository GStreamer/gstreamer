/*
 * GStreamer gstreamer-tensorid
 * Copyright (C) 2023 Collabora Ltd
 *
 * gsttensorid.h
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
#ifndef __GST_TENSOR_ID_H__
#define __GST_TENSOR_ID_H__

G_BEGIN_DECLS
/**
 * gst_tensorid_get_quark get tensor id
 *
 * @param tensor_id unique string id for tensor node
 */
    GQuark gst_tensorid_get_quark (const char *tensor_id);

G_END_DECLS
#endif
