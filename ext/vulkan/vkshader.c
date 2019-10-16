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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vkshader.h"

#define SPIRV_MAGIC_NUMBER_NE 0x07230203
#define SPIRV_MAGIC_NUMBER_OE 0x03022307

VkShaderModule
_vk_create_shader (GstVulkanDevice * device, gchar * code, gsize size,
    GError ** error)
{
  VkShaderModule ret;
  VkResult res;

  /* *INDENT-OFF* */
  VkShaderModuleCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .codeSize = size,
      .pCode = (guint32 *) code
  };
  /* *INDENT-ON* */
  guint32 first_word;
  guint32 *new_code = NULL;

  g_return_val_if_fail (size >= 4, VK_NULL_HANDLE);
  g_return_val_if_fail (size % 4 == 0, VK_NULL_HANDLE);

  first_word = ((guint32 *) code)[0];
  g_return_val_if_fail (first_word == SPIRV_MAGIC_NUMBER_NE
      || first_word == SPIRV_MAGIC_NUMBER_OE, VK_NULL_HANDLE);
  if (first_word == SPIRV_MAGIC_NUMBER_OE) {
    /* endianness swap... */
    guint32 *old_code = (guint32 *) code;
    gsize i;

    GST_DEBUG ("performaing endianness conversion on spirv shader of size %"
        G_GSIZE_FORMAT, size);
    new_code = g_new0 (guint32, size / 4);

    for (i = 0; i < size / 4; i++) {
      guint32 old = old_code[i];
      guint32 new = 0;

      new |= (old & 0xff) << 24;
      new |= (old & 0xff00) << 8;
      new |= (old & 0xff0000) >> 8;
      new |= (old & 0xff000000) >> 24;
      new_code[i] = new;
    }

    first_word = ((guint32 *) new_code)[0];
    g_assert (first_word == SPIRV_MAGIC_NUMBER_NE);

    info.pCode = new_code;
  }

  res = vkCreateShaderModule (device->device, &info, NULL, &ret);
  g_free (new_code);
  if (gst_vulkan_error_to_g_error (res, error, "VkCreateShaderModule") < 0)
    return VK_NULL_HANDLE;

  g_free (new_code);

  return ret;
}
