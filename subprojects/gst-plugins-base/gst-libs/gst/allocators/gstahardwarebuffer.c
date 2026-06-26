/* GStreamer AHardwareBuffer Library
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
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

#include "gstahardwarebuffer.h"

#if defined(__ANDROID__)
#include <android/api-level.h>

#if __ANDROID_API__ >= 26
#include <android/hardware_buffer.h>

#define AHB_FORMAT_ASSERT(name) \
  G_STATIC_ASSERT ((guint32) GST_AHARDWARE_BUFFER_FORMAT_ ## name == \
      (guint32) AHARDWAREBUFFER_FORMAT_ ## name)

AHB_FORMAT_ASSERT (R8G8B8A8_UNORM);
AHB_FORMAT_ASSERT (R8G8B8X8_UNORM);
AHB_FORMAT_ASSERT (R8G8B8_UNORM);
AHB_FORMAT_ASSERT (R5G6B5_UNORM);
AHB_FORMAT_ASSERT (R16G16B16A16_FLOAT);
AHB_FORMAT_ASSERT (BLOB);
AHB_FORMAT_ASSERT (R10G10B10A2_UNORM);

/* Guards follow the AOSP native hardware_buffer.h release tags, rounded up
 * where useful, since Java HardwareBuffer constants can lag the NDK surface. */
#if __ANDROID_API__ >= 29
AHB_FORMAT_ASSERT (D16_UNORM);
AHB_FORMAT_ASSERT (D24_UNORM);
AHB_FORMAT_ASSERT (D24_UNORM_S8_UINT);
AHB_FORMAT_ASSERT (D32_FLOAT);
AHB_FORMAT_ASSERT (D32_FLOAT_S8_UINT);
AHB_FORMAT_ASSERT (S8_UINT);
AHB_FORMAT_ASSERT (Y8Cb8Cr8_420);
#endif

#if __ANDROID_API__ >= 34
AHB_FORMAT_ASSERT (YCbCr_P010);
AHB_FORMAT_ASSERT (R8_UNORM);
AHB_FORMAT_ASSERT (R16_UINT);
AHB_FORMAT_ASSERT (R16G16_UINT);
AHB_FORMAT_ASSERT (R10G10B10A10_UNORM);
#endif

#if __ANDROID_API__ >= 37
AHB_FORMAT_ASSERT (YCbCr_P210);
AHB_FORMAT_ASSERT (R12_UINT);
AHB_FORMAT_ASSERT (R14_UINT);
AHB_FORMAT_ASSERT (R12G12_UINT);
AHB_FORMAT_ASSERT (R14G14_UINT);
AHB_FORMAT_ASSERT (R12G12B12A12_UINT);
AHB_FORMAT_ASSERT (R14G14B14A14_UINT);
AHB_FORMAT_ASSERT (B10G10R10A2_UNORM);
AHB_FORMAT_ASSERT (B10G10R10X2_UNORM);
#endif

#undef AHB_FORMAT_ASSERT
#endif
#endif

/**
 * SECTION:gstahardwarebuffer
 * @title: AHardwareBuffer
 * @short_description: AHardwareBuffer-backed memory helpers
 *
 * The AHardwareBuffer helpers provide a common interface for detecting and
 * querying AHardwareBuffer-backed #GstMemory.
 *
 * The #GST_CAPS_FEATURE_MEMORY_AHARDWAREBUFFER caps feature can be used on
 * `video/x-raw` caps to negotiate AHardwareBuffer-backed buffers.
 *
 * Since: 1.30
 */

typedef struct
{
  GType allocator_type;
  GstAHardwareBufferMemoryQueryFunction query;
} GstAHardwareBufferQueryEntry;

G_LOCK_DEFINE_STATIC (ahardware_buffer_query_functions);
static GArray *ahardware_buffer_query_functions;

typedef struct
{
  guint32 value;
  const gchar *name;
} GstAHardwareBufferFormatName;

/* AHardwareBuffer format names are part of the caps contract and must remain
 * available when inspecting caps on non-Android platforms. */
#define AHB_FORMAT(name) { GST_AHARDWARE_BUFFER_FORMAT_ ## name, #name }

static const GstAHardwareBufferFormatName ahardware_buffer_formats[] = {
  AHB_FORMAT (R8G8B8A8_UNORM),
  AHB_FORMAT (R8G8B8X8_UNORM),
  AHB_FORMAT (R8G8B8_UNORM),
  AHB_FORMAT (R5G6B5_UNORM),
  AHB_FORMAT (R16G16B16A16_FLOAT),
  AHB_FORMAT (BLOB),
  AHB_FORMAT (Y8Cb8Cr8_420),
  AHB_FORMAT (R10G10B10A2_UNORM),
  AHB_FORMAT (D16_UNORM),
  AHB_FORMAT (D24_UNORM),
  AHB_FORMAT (D24_UNORM_S8_UINT),
  AHB_FORMAT (D32_FLOAT),
  AHB_FORMAT (D32_FLOAT_S8_UINT),
  AHB_FORMAT (S8_UINT),
  AHB_FORMAT (YCbCr_P010),
  AHB_FORMAT (R8_UNORM),
  AHB_FORMAT (R16_UINT),
  AHB_FORMAT (R16G16_UINT),
  AHB_FORMAT (R10G10B10A10_UNORM),
  AHB_FORMAT (YCbCr_P210),
  AHB_FORMAT (R12_UINT),
  AHB_FORMAT (R14_UINT),
  AHB_FORMAT (R12G12_UINT),
  AHB_FORMAT (R14G14_UINT),
  AHB_FORMAT (R12G12B12A12_UINT),
  AHB_FORMAT (R14G14B14A14_UINT),
  AHB_FORMAT (B10G10R10A2_UNORM),
  AHB_FORMAT (B10G10R10X2_UNORM),
};

#undef AHB_FORMAT

/**
 * gst_ahardware_buffer_format_to_caps_string:
 * @format: an Android AHardwareBuffer format value
 *
 * Converts an Android AHardwareBuffer format value to the canonical string
 * representation used by the `ahb-format` caps field. Known formats use the
 * Android constant suffix and unknown formats use fixed-width hexadecimal.
 *
 * Returns: (transfer full): the canonical format string.
 *
 * Since: 1.30
 */
gchar *
gst_ahardware_buffer_format_to_caps_string (guint32 format)
{
  for (guint i = 0; i < G_N_ELEMENTS (ahardware_buffer_formats); i++) {
    if (ahardware_buffer_formats[i].value == format)
      return g_strdup (ahardware_buffer_formats[i].name);
  }

  return g_strdup_printf ("0x%08x", format);
}

/**
 * gst_ahardware_buffer_format_from_caps_string:
 * @value: an AHardwareBuffer format string
 * @format: (out): location for the Android AHardwareBuffer format value
 *
 * Parses the canonical string representation used by the `ahb-format` caps
 * field.
 *
 * Returns: %TRUE if @value is a known name or canonical hexadecimal value.
 *
 * Since: 1.30
 */
gboolean
gst_ahardware_buffer_format_from_caps_string (const gchar * value,
    guint32 * format)
{
  guint64 parsed;

  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (format != NULL, FALSE);

  for (guint i = 0; i < G_N_ELEMENTS (ahardware_buffer_formats); i++) {
    if (g_str_equal (ahardware_buffer_formats[i].name, value)) {
      *format = ahardware_buffer_formats[i].value;
      return TRUE;
    }
  }

  if (strlen (value) != 10 || value[0] != '0' || value[1] != 'x')
    return FALSE;

  for (guint i = 2; i < 10; i++) {
    if (!g_ascii_isxdigit (value[i]) ||
        (g_ascii_isalpha (value[i]) && !g_ascii_islower (value[i])))
      return FALSE;
  }

  if (!g_ascii_string_to_unsigned (value + 2, 16, 0, G_MAXUINT32, &parsed,
          NULL))
    return FALSE;

  *format = (guint32) parsed;
  return TRUE;
}

/* Returns a process-lifetime query function. There is intentionally no
 * unregister operation, so callers can safely invoke the returned function
 * after releasing the query-functions lock.
 */
static GstAHardwareBufferMemoryQueryFunction
gst_ahardware_buffer_memory_find_query_function (GstMemory * mem)
{
  GType allocator_type;
  GstAHardwareBufferMemoryQueryFunction query = NULL;

  g_return_val_if_fail (mem != NULL, NULL);
  g_return_val_if_fail (mem->allocator != NULL, NULL);

  allocator_type = G_OBJECT_TYPE (mem->allocator);

  G_LOCK (ahardware_buffer_query_functions);
  if (ahardware_buffer_query_functions) {
    for (guint i = 0; i < ahardware_buffer_query_functions->len; i++) {
      GstAHardwareBufferQueryEntry *entry =
          &g_array_index (ahardware_buffer_query_functions,
          GstAHardwareBufferQueryEntry, i);

      if (g_type_is_a (allocator_type, entry->allocator_type)) {
        query = entry->query;
        break;
      }
    }
  }
  G_UNLOCK (ahardware_buffer_query_functions);

  return query;
}

/**
 * gst_is_ahardware_buffer_memory:
 * @mem: a #GstMemory
 *
 * Returns: %TRUE if @mem exposes an AHardwareBuffer.
 *
 * Since: 1.30
 */
gboolean
gst_is_ahardware_buffer_memory (GstMemory * mem)
{
  return gst_ahardware_buffer_memory_peek_buffer (mem, NULL);
}

/**
 * gst_is_ahardware_buffer_buffer:
 * @buffer: a #GstBuffer
 *
 * Returns: %TRUE if @buffer is non-empty and every memory block in @buffer
 * exposes an AHardwareBuffer.
 *
 * Since: 1.30
 */
gboolean
gst_is_ahardware_buffer_buffer (GstBuffer * buffer)
{
  guint n_mem;

  g_return_val_if_fail (buffer != NULL, FALSE);

  n_mem = gst_buffer_n_memory (buffer);
  if (n_mem == 0)
    return FALSE;

  for (guint i = 0; i < n_mem; i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    if (!gst_is_ahardware_buffer_memory (mem))
      return FALSE;
  }

  return TRUE;
}

/**
 * gst_ahardware_buffer_memory_peek_buffer:
 * @mem: a #GstMemory
 * @buffer: (out) (transfer none) (optional): the `AHardwareBuffer`
 *
 * Queries whether @mem is backed by an AHardwareBuffer and, if so, returns the
 * AHardwareBuffer represented by this memory.
 * @buffer is only modified if this function returns %TRUE.
 *
 * The returned AHardwareBuffer is owned by @mem and is valid for as long as
 * @mem is alive. Callers must call `AHardwareBuffer_acquire()` if they want to
 * keep the AHardwareBuffer beyond @mem's lifetime.
 *
 * Returns: %TRUE if @mem exposes an AHardwareBuffer.
 *
 * Since: 1.30
 */
gboolean
gst_ahardware_buffer_memory_peek_buffer (GstMemory * mem,
    AHardwareBuffer ** buffer)
{
  GstAHardwareBufferMemoryQueryFunction query =
      gst_ahardware_buffer_memory_find_query_function (mem);
  AHardwareBuffer *queried_buffer = NULL;

  if (!query)
    return FALSE;

  if (!query (mem, &queried_buffer))
    return FALSE;

  if (!queried_buffer)
    return FALSE;

  if (buffer)
    *buffer = queried_buffer;

  return TRUE;
}

/**
 * gst_ahardware_buffer_memory_register_query_function:
 * @allocator_type: a #GstAllocator type
 * @query: function used to query AHardwareBuffer backing for memory allocated
 *     by @allocator_type
 *
 * Registers @query as the AHardwareBuffer query function for #GstMemory
 * objects allocated by @allocator_type.
 *
 * This function is intended for memory implementers rather than applications.
 * @query must remain valid for the lifetime of the process and must only
 * return borrowed AHardwareBuffer references owned by the queried memory.
 *
 * Since: 1.30
 */
void
gst_ahardware_buffer_memory_register_query_function (GType allocator_type,
    GstAHardwareBufferMemoryQueryFunction query)
{
  GstAHardwareBufferQueryEntry entry;

  g_return_if_fail (allocator_type != G_TYPE_INVALID);
  g_return_if_fail (query != NULL);

  G_LOCK (ahardware_buffer_query_functions);
  if (!ahardware_buffer_query_functions)
    ahardware_buffer_query_functions =
        g_array_new (FALSE, FALSE, sizeof (GstAHardwareBufferQueryEntry));

  for (guint i = 0; i < ahardware_buffer_query_functions->len; i++) {
    GstAHardwareBufferQueryEntry *existing =
        &g_array_index (ahardware_buffer_query_functions,
        GstAHardwareBufferQueryEntry, i);

    if (existing->allocator_type == allocator_type) {
      existing->query = query;
      G_UNLOCK (ahardware_buffer_query_functions);
      return;
    }
  }

  entry.allocator_type = allocator_type;
  entry.query = query;
  g_array_append_val (ahardware_buffer_query_functions, entry);
  G_UNLOCK (ahardware_buffer_query_functions);
}
