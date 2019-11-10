/*
 * GStreamer
 * Copyright (C) 2019 Matthew Waters <matthew@centricular.com>
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

#ifndef __GST_VULKAN_HANDLE_H__
#define __GST_VULKAN_HANDLE_H__

#include <gst/gst.h>

#include <gst/vulkan/vulkan_fwd.h>
#include <gst/vulkan/gstvkapi.h>

G_BEGIN_DECLS

GST_VULKAN_API
GType gst_vulkan_handle_get_type (void);
#define GST_TYPE_VULKAN_HANDLE (gst_vulkan_handle_get_type ())

VK_DEFINE_NON_DISPATCHABLE_HANDLE(GstVulkanHandleTypedef)

#if GLIB_SIZEOF_VOID_P == 8
# define GST_VULKAN_NON_DISPATCHABLE_HANDLE_FORMAT "p"
#else
# define GST_VULKAN_NON_DISPATCHABLE_HANDLE_FORMAT G_GUINT64_FORMAT
#endif

typedef void (*GstVulkanHandleDestroyNotify) (GstVulkanHandle * handle, gpointer user_data);

typedef enum
{
  GST_VULKAN_HANDLE_TYPE_DESCRIPTOR_SET_LAYOUT = 1,
} GstVulkanHandleType;

struct _GstVulkanHandle
{
  GstMiniObject             parent;

  GstVulkanDevice          *device;

  GstVulkanHandleType       type;
  GstVulkanHandleTypedef    handle;

  /* <protected> */
  GstVulkanHandleDestroyNotify notify;
  gpointer                  user_data;
};

/**
 * gst_vulkan_handle_ref: (skip)
 * @cmd: a #GstVulkanHandle.
 *
 * Increases the refcount of the given handle by one.
 *
 * Returns: (transfer full): @buf
 */
static inline GstVulkanHandle* gst_vulkan_handle_ref(GstVulkanHandle* handle);
static inline GstVulkanHandle *
gst_vulkan_handle_ref (GstVulkanHandle * handle)
{
  return (GstVulkanHandle *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (handle));
}

/**
 * gst_vulkan_handle_unref: (skip)
 * @cmd: (transfer full): a #GstVulkanHandle.
 *
 * Decreases the refcount of the buffer. If the refcount reaches 0, the buffer
 * will be freed.
 */
static inline void gst_vulkan_handle_unref(GstVulkanHandle* handle);
static inline void
gst_vulkan_handle_unref (GstVulkanHandle * handle)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (handle));
}

/**
 * gst_clear_vulkan_handle: (skip)
 * @handle_ptr: a pointer to a #GstVulkanHandle reference
 *
 * Clears a reference to a #GstVulkanHandle.
 *
 * @handle_ptr must not be %NULL.
 *
 * If the reference is %NULL then this function does nothing. Otherwise, the
 * reference count of the handle is decreased and the pointer is set to %NULL.
 *
 * Since: 1.16
 */
static inline void
gst_clear_vulkan_handle (GstVulkanHandle ** handle_ptr)
{
  gst_clear_mini_object ((GstMiniObject **) handle_ptr);
}

GST_VULKAN_API
GstVulkanHandle *       gst_vulkan_handle_new_wrapped       (GstVulkanDevice *device,
                                                             GstVulkanHandleType type,
                                                             GstVulkanHandleTypedef handle,
                                                             GstVulkanHandleDestroyNotify notify,
                                                             gpointer user_data);

GST_VULKAN_API
void                    gst_vulkan_handle_free_descriptor_set_layout (GstVulkanHandle * handle,
                                                                      gpointer user_data);

G_END_DECLS

#endif /* _GST_VULKAN_HANDLE_H_ */
