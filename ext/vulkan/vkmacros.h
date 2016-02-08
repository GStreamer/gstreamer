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

#define GST_VK_STRUCT_1(a) \
  { a }
#define GST_VK_STRUCT_2(a, b) \
  { a, b }
#define GST_VK_STRUCT_3(a, b, c) \
  { a, b, c }
#define GST_VK_STRUCT_4(a, b, c, d) \
  { a, b, c, d }
#define GST_VK_STRUCT_5(a, b, c, d, e) \
  { a, b, c, d, e }
#define GST_VK_STRUCT_6(a, b, c, d, e, f) \
  { a, b, c, d, e, f }
#define GST_VK_STRUCT_7(a, b, c, d, e, f, g) \
  { a, b, c, d, e, f, g }
#define GST_VK_STRUCT_8(a, b, c, d, e, f, g, h) \
  { a, b, c, d, e, f, g, h }

#define GST_VK_BUFFER_IMAGE_COPY_INIT GST_VK_STRUCT_6
#define GST_VK_BUFFER_IMAGE_COPY(info,bufferOffset_,bufferRowLength_,bufferImageHeight_,imageSubresourceLayers_,imageOffset_,imageExtent_) \
  G_STMT_START { \
    VkImageSubresourceLayers sub = imageSubresourceLayers_; \
    VkOffset3D offset = imageOffset_; \
    VkExtent3D extent = imageExtent_; \
    VkBufferImageCopy tmp = GST_VK_BUFFER_IMAGE_COPY_INIT(bufferOffset_,bufferRowLength_,bufferImageHeight_,sub,offset,extent); \
    (region) = tmp; \
  } G_STMT_END

#define GST_VK_COMPONENT_MAPPING_INIT GST_VK_STRUCT_4
#define GST_VK_COMPONENT_MAPPING(component, r_, g_, b_, a_) \
  G_STMT_START { \
    VkComponentMapping tmp = GST_VK_COMPONENT_MAPPING_INIT(r_, g_, b_, a_); \
    (component) = tmp; \
  } G_STMT_END

#define GST_VK_EXTENT3D_INIT GST_VK_STRUCT_3
#define GST_VK_EXTENT3D(extent,w,h,d) \
  G_STMT_START { \
    VkExtent3D tmp = GST_VK_EXTENT3D_INIT(w,h,d); \
    (extent) = tmp; \
  } G_STMT_END

#define GST_VK_IMAGE_COPY_INIT GST_VK_STRUCT_5
#define GST_VK_IMAGE_COPY(copy,srcSubresource_,srcOffset_,dstSubresource_,dstOffset_,extent_) \
  G_STMT_START { \
    VkImageSubresourceLayers src_res = srcSubresource_; \
    VkOffset3D src_offset = srcOffset_; \
    VkImageSubresourceLayers dst_res = dstSubresource_; \
    VkOffset3D dst_offset = dstOffset_; \
    VkExtent3D ext = extent_; \
    VkImageCopy tmp = GST_VK_IMAGE_COPY_INIT(src_res,src_offset,dst_res,dst_offset,ext); \
    (copy) = tmp; \
  } G_STMT_END

#define GST_VK_IMAGE_BLIT_INIT GST_VK_STRUCT_6
#define GST_VK_IMAGE_BLIT(blit,srcSubresource_,srcOffset_,srcExtent_,dstSubresource_,dstOffset_,dstExtent_) \
  G_STMT_START { \
    VkImageSubresourceLayers src_res = srcSubresource_; \
    VkOffset3D src_offset = srcOffset; \
    VkExtent3D src_ext = srcExtent_; \
    VkImageSubresourceLayers dst_res = dstSubresource_; \
    VkOffset3D dst_offset = dstSubresource_; \
    VkExtent3D dst_ext = dstExtent_; \
    VkImageBlit tmp = GST_VK_IMAGE_BLIT_INIT(src_res, src_offset, src_ext, dst_res, dst_offset, dst_ext); \
    (blit) = tmp; \
  } G_STMT_END

#define GST_VK_IMAGE_SUBRESOURCE_INIT GST_VK_STRUCT_3
#define GST_VK_IMAGE_SUBRESOURCE(subresource,aspectMast,mipLevel,arrayLayer) \
  G_STMT_START { \
    VkImageSubresource tmp = GST_VK_IMAGE_SUBRESOURCE_INIT(aspectMast,mipLevel,arrayLayer); \
    (subresource) = tmp; \
  } G_STMT_END

#define GST_VK_IMAGE_SUBRESOURCE_LAYERS_INIT GST_VK_STRUCT_4
#define GST_VK_IMAGE_SUBRESOURCE_LAYERS(res,aspect_,mip,base_layer,layer_count) \
  G_STMT_START { \
    VkImageSubresourceLayers tmp = GST_VK_IMAGE_SUBRESOURCE_LAYERS_INIT(aspect_,mip,base_layer,layer_count); \
    (res) = tmp; \
  } G_STMT_END

#define GST_VK_IMAGE_SUBRESOURCE_RANGE_INIT GST_VK_STRUCT_5
#define GST_VK_IMAGE_SUBRESOURCE_RANGE(range, aspect, mip_lvl, mip_lvl_count, array, layer_count) \
  G_STMT_START { \
    VkImageSubresourceRange tmp = GST_VK_IMAGE_SUBRESOURCE_RANGE_INIT(aspect,mip_lvl,mip_lvl_count,array,layer_count); \
    (range) = tmp; \
  } G_STMT_END

#define GST_VK_OFFSET3D_INIT GST_VK_STRUCT_3
#define GST_VK_OFFSET3D(offset,x_,y_,z_) \
  G_STMT_START { \
    VkOffset3D tmp = GST_VK_OFFSET3D_INIT (x_,y_,z_); \
    (offset) = tmp; \
  } G_STMT_END

G_END_DECLS

#endif /*_VK_MACROS_H_ */
