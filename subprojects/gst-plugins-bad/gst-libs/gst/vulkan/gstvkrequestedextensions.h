/*
 * GStreamer
 * Copyright (C) 2026 Azat Nurgaliev <azat.nurg@gmail.com>
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

#ifndef __GST_VULKAN_REQUESTED_EXTENSIONS_H__
#define __GST_VULKAN_REQUESTED_EXTENSIONS_H__

#include <gst/gst.h>
#include <gst/vulkan/vulkan-prelude.h>

G_BEGIN_DECLS

/**
 * SECTION:gstvkrequestedextensions
 * @title: Vulkan requested extensions
 * @short_description: Negotiating Vulkan instance and device extensions between elements
 * @see_also: #GstVulkanInstance, #GstVulkanDevice,
 * gst_vulkan_requested_extensions_global_context_query(),
 * gst_vulkan_requested_extensions_local_context_query()
 */

typedef struct _GstVulkanInstance GstVulkanInstance;

/**
 * GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR:
 *
 * Context type for negotiating Vulkan **instance** extensions at runtime.
 * The #GstStructure holds a `G_TYPE_STRV` field `extensions`. Merge helpers
 * ignore any %GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR field for this type so
 * negotiation stays decoupled from a specific #GstVulkanInstance.
 *
 * Since: 1.30
 */
#define GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR "gst.vulkan.requested-instance-extensions"

/**
 * GST_VULKAN_REQUESTED_DEVICE_EXTENSIONS_CONTEXT_TYPE_STR:
 *
 * Context type for negotiating Vulkan **device** extensions at runtime.
 * The #GstStructure contains a `G_TYPE_STRV` field `extensions` and must be
 * anchored on a #GstVulkanInstance (same field name as
 * gst_context_set_vulkan_instance(), %GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR)
 * so answerers can inspect physical devices, filter requested device extension
 * names, and so merge logic can ignore unrelated fragments when instances
 * disagree.
 *
 * Since: 1.30
 */
#define GST_VULKAN_REQUESTED_DEVICE_EXTENSIONS_CONTEXT_TYPE_STR "gst.vulkan.requested-device-extensions"

GST_VULKAN_API
void                    gst_vulkan_requested_extensions_context_add (GstContext * context,
                                                                     const gchar * extension_name);

GST_VULKAN_API
gchar **                gst_vulkan_requested_extensions_context_dup_extensions (GstContext * context)
                                                                                G_GNUC_WARN_UNUSED_RESULT;

GST_VULKAN_API
void                    gst_vulkan_requested_extensions_context_set_vulkan_instance (GstContext * context,
                                                                                     GstVulkanInstance * instance);

GST_VULKAN_API
gboolean                gst_vulkan_requested_extensions_context_get_vulkan_instance (GstContext * context,
                                                                                     GstVulkanInstance ** instance);

GST_VULKAN_API
GstContext *            gst_vulkan_requested_instance_extensions_context_new (void) G_GNUC_WARN_UNUSED_RESULT;

GST_VULKAN_API
GstContext *            gst_vulkan_requested_device_extensions_context_new (void) G_GNUC_WARN_UNUSED_RESULT;

GST_VULKAN_API
void                    gst_vulkan_requested_extensions_merge_from_element (GstElement * element,
                                                                            GstContext * dst);

GST_VULKAN_API
GstContext *            gst_vulkan_element_get_merged_requested_instance_extensions_context (GstElement * element)
                                                                                             G_GNUC_WARN_UNUSED_RESULT;

GST_VULKAN_API
GstContext *            gst_vulkan_element_get_merged_requested_device_extensions_context (GstElement * element)
                                                                                           G_GNUC_WARN_UNUSED_RESULT;

GST_VULKAN_API
GstQuery *              gst_vulkan_requested_extensions_local_context_query (GstElement * element,
                                                                             const gchar * context_type,
                                                                             GstVulkanInstance * instance)
                                                                             G_GNUC_WARN_UNUSED_RESULT;

GST_VULKAN_API
void                    gst_vulkan_requested_extensions_global_context_query (GstElement * element,
                                                                              const gchar * context_type);

GST_VULKAN_API
gboolean                gst_vulkan_requested_extensions_handle_context_query (GstElement * element,
                                                                              GstQuery * query,
                                                                              GstPadDirection continue_direction,
                                                                              GstVulkanInstance * instance);

G_END_DECLS

#endif /* __GST_VULKAN_REQUESTED_EXTENSIONS_H__ */
