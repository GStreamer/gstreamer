/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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
#ifndef _VK_UTILS_PRIVATE_H_
#define _VK_UTILS_PRIVATE_H_

#include <gst/gst.h>
#include "vk.h"

G_BEGIN_DECLS

gboolean _check_for_all_layers (uint32_t check_count, const char ** check_names,
    uint32_t layer_count, VkLayerProperties * layers, guint32 * enabled_layer_count,
    gchar *** enabled_layers);

G_END_DECLS

#endif /*_VK_UTILS_H_ */
