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
#ifndef _VK_MACROS_H_
#define _VK_MACROS_H_

#include <gst/gst.h>
#include <vk.h>

G_BEGIN_DECLS

#define GST_VK_COMPONENT_MAPPING(component, r_, g_, b_, a_) \
  G_STMT_START { \
    component.r = r_; \
    component.g = g_; \
    component.b = b_; \
    component.a = a_; \
  } G_STMT_END

#define GST_VK_EXTENT3D(extent,w,h,d) \
  G_STMT_START { \
    extent.width = w; \
    extent.height = h; \
    extent.depth = d; \
  } G_STMT_END

#define GST_VK_IMAGE_SUBRESOURCE(sub,a,m,l) \
    G_STMT_START { \
      sub.aspectMask = a; \
      sub.mipLevel = m; \
      sub.arrayLayer = l; \
    } G_STMT_END

#define GST_VK_IMAGE_SUBRESOURCE_LAYERS(res,aspect_,mip,base_layer,layer_count) \
  G_STMT_START { \
    res.aspectMask = aspect_; \
    res.mipLevel = mip; \
    res.baseArrayLayer = base_layer; \
    res.layerCount = layer_count; \
  } G_STMT_END

#define GST_VK_IMAGE_SUBRESOURCE_RANGE(range, aspect, mip_lvl, mip_lvl_count, array, layer_count) \
  G_STMT_START { \
    range.aspectMask = aspect; \
    range.baseMipLevel = mip_lvl; \
    range.levelCount = mip_lvl_count; \
    range.baseArrayLayer = array; \
    range.layerCount = layer_count; \
  } G_STMT_END

#define GST_VK_OFFSET3D(offset,x_,y_,z_) \
  G_STMT_START { \
    offset.x = x_; \
    offset.y = y_; \
    offset.z = z_; \
  } G_STMT_END

G_END_DECLS

#endif /*_VK_UTILS_H_ */
