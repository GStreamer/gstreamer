/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#include "gsthip.h"
#include "gsthip-private.h"
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>
#include <string.h>

/**
 * GstHipKernelModule:
 *
 * A compiled, device-specific module containing one or more kernel entry points.
 * The caller must keep the module alive until all submitted work has completed.
 *
 * Since: 1.30
 */

/* *INDENT-OFF* */
struct _GstHipKernelModule : public GstMiniObject
{
  ~_GstHipKernelModule ()
  {
    if (handle && gst_hip_device_set_current (device))
      gst_hip_result (HipModuleUnload (vendor, handle), vendor);
    gst_clear_object (&device);
  }

  GstHipDevice *device = nullptr;
  GstHipVendor vendor;
  hipModule_t handle = nullptr;
  std::mutex lock;
  std::unordered_map<std::string, hipFunction_t> functions;
};

enum class GstHipKernelArgType
{
  MEMORY,
  SCALAR,
  VALUE,
};

struct GstHipKernelArg
{
  GstHipKernelArgType type;
  GstMemory *memory = nullptr;
  gsize offset = 0;
  union
  {
    guint8 uint8;
    gint8 int8;
    guint16 uint16;
    gint16 int16;
    guint32 uint32;
    gint32 int32;
    guint64 uint64;
    gint64 int64;
    gfloat float32;
    gdouble float64;
    gpointer pointer;
  } scalar = { };
  std::shared_ptr<void> value;
};
/* *INDENT-ON* */

/**
 * GstHipKernelArgs:
 *
 * Reusable kernel argument list. Memory arguments are borrowed and their
 * lifetime must be managed by the caller.
 *
 * Since: 1.30
 */
/* *INDENT-OFF* */
struct _GstHipKernelArgs
{
  std::vector<GstHipKernelArg> values;
};
/* *INDENT-ON* */

static void
gst_hip_kernel_module_free (GstHipKernelModule * module)
{
  delete module;
}

GST_DEFINE_MINI_OBJECT_TYPE (GstHipKernelModule, gst_hip_kernel_module);
G_DEFINE_BOXED_TYPE (GstHipKernelArgs, gst_hip_kernel_args,
    gst_hip_kernel_args_copy, gst_hip_kernel_args_free);

/**
 * gst_hip_kernel_module_new_from_source:
 * @device: a #GstHipDevice
 * @source: NUL-terminated kernel source
 * @options: (array length=num_options) (element-type utf8) (nullable): compiler options
 * @num_options: number of compiler options
 * @error: return location for an error, including compiler diagnostics
 *
 * Compiles @source and loads a module for @device. Entry points should use C
 * linkage. Source and options must be supported by the device's runtime compiler.
 *
 * Returns: (transfer full): a new module, or %NULL on failure
 *
 * Since: 1.30
 */
GstHipKernelModule *
gst_hip_kernel_module_new_from_source (GstHipDevice * device,
    const gchar * source, const gchar ** options, guint num_options,
    GError ** error)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), nullptr);
  g_return_val_if_fail (source, nullptr);
  g_return_val_if_fail (options || num_options == 0, nullptr);
  g_return_val_if_fail (!error || !*error, nullptr);

  auto code = gst_hip_rtc_compile_full (device, source, options,
      num_options, error);

  if (!code)
    return nullptr;

  auto module = gst_hip_kernel_module_new_from_blob (device, code, error);
  g_free (code);

  return module;
}

/**
 * gst_hip_kernel_module_new_from_blob:
 * @device: a #GstHipDevice
 * @blob: (transfer none): a complete precompiled module image supported by @device
 * @error: return location for an error
 *
 * Loads @blob as a module for @device without runtime compilation. The caller
 * must provide a valid module image and keep it alive for the duration of this
 * call. The module does not retain @blob after this call returns.
 *
 * Returns: (transfer full): a new module, or %NULL on failure
 *
 * Since: 1.30
 */
GstHipKernelModule *
gst_hip_kernel_module_new_from_blob (GstHipDevice * device,
    gconstpointer blob, GError ** error)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), nullptr);
  g_return_val_if_fail (blob, nullptr);
  g_return_val_if_fail (!error || !*error, nullptr);

  if (!gst_hip_device_set_current (device)) {
    g_set_error_literal (error, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT,
        "Couldn't set HIP device");
    return nullptr;
  }

  auto vendor = gst_hip_device_get_vendor (device);
  hipModule_t handle = nullptr;
  auto ret = HipModuleLoadData (vendor, &handle, blob);

  if (!gst_hip_result (ret, vendor)) {
    g_set_error (error, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT,
        "Couldn't load HIP module: %d", ret);
    return nullptr;
  }

  auto module = new GstHipKernelModule ();
  module->device = (GstHipDevice *) gst_object_ref (device);
  module->vendor = vendor;
  module->handle = handle;

  gst_mini_object_init (module, 0, GST_TYPE_HIP_KERNEL_MODULE,
      nullptr, nullptr, (GstMiniObjectFreeFunction) gst_hip_kernel_module_free);

  return module;
}

static gboolean
gst_hip_kernel_module_get_function (GstHipKernelModule * self,
    const gchar * entry_point, hipFunction_t * function)
{
  std::lock_guard < std::mutex > lk (self->lock);

  auto ep = std::string (entry_point);

  auto it = self->functions.find (ep);
  if (it != self->functions.end ()) {
    if (function)
      *function = it->second;

    return TRUE;
  }

  hipFunction_t func = nullptr;
  auto ret = HipModuleGetFunction (self->vendor,
      &func, self->handle, entry_point);
  if (!gst_hip_result (ret, self->vendor))
    return FALSE;

  self->functions.emplace (ep, func);
  if (function)
    *function = func;

  return TRUE;
}

/**
 * gst_hip_kernel_module_ensure_entry_point:
 * @module: a #GstHipKernelModule
 * @entry_point: kernel entry-point name
 *
 * Ensures that @entry_point is available in @module.
 *
 * Returns: %TRUE if @entry_point is available
 *
 * Since: 1.30
 */
gboolean
gst_hip_kernel_module_ensure_entry_point (GstHipKernelModule * module,
    const gchar * entry_point)
{
  g_return_val_if_fail (module, FALSE);
  g_return_val_if_fail (entry_point && *entry_point, FALSE);

  return gst_hip_kernel_module_get_function (module, entry_point, nullptr);
}

/**
 * gst_hip_kernel_module_ref:
 * @module: a #GstHipKernelModule
 *
 * Increments the reference count on @module.
 *
 * Returns: (transfer full): @module
 *
 * Since: 1.30
 */
GstHipKernelModule *
gst_hip_kernel_module_ref (GstHipKernelModule * module)
{
  return (GstHipKernelModule *) gst_mini_object_ref (module);
}

/**
 * gst_hip_kernel_module_unref:
 * @module: (transfer full): a #GstHipKernelModule
 *
 * Decrements the reference count on @module.
 *
 * Since: 1.30
 */
void
gst_hip_kernel_module_unref (GstHipKernelModule * module)
{
  gst_mini_object_unref (module);
}

/**
 * gst_hip_kernel_module_launch:
 * @module: a #GstHipKernelModule
 * @entry_point: kernel entry-point name
 * @stream: (nullable): the stream to submit work to, or %NULL for the default HIP stream
 * @grid_x: number of blocks in X
 * @grid_y: number of blocks in Y
 * @grid_z: number of blocks in Z
 * @block_x: number of threads per block in X
 * @block_y: number of threads per block in Y
 * @block_z: number of threads per block in Z
 * @args: kernel arguments in parameter order
 *
 * Launches @entry_point with @args
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.30
 */
gboolean
gst_hip_kernel_module_launch (GstHipKernelModule * module,
    const gchar * entry_point, GstHipStream * stream,
    guint grid_x, guint grid_y, guint grid_z,
    guint block_x, guint block_y, guint block_z, const GstHipKernelArgs * args)
{
  g_return_val_if_fail (module, FALSE);
  g_return_val_if_fail (entry_point && *entry_point, FALSE);
  g_return_val_if_fail (args, FALSE);
  g_return_val_if_fail (grid_x && grid_y && grid_z, FALSE);
  g_return_val_if_fail (block_x && block_y && block_z, FALSE);

  auto device_id = gst_hip_device_get_device_id (module->device);
  if (stream && (gst_hip_stream_get_vendor (stream) != module->vendor ||
          gst_hip_stream_get_device_id (stream) != device_id)) {
    GST_ERROR_OBJECT (module->device,
        "Kernel stream belongs to another device");
    return FALSE;
  }

  std::vector < gpointer > values (args->values.size ());
  std::vector < gpointer > params (args->values.size ());

  for (size_t i = 0; i < args->values.size (); i++) {
    const auto & arg = args->values[i];
    if (arg.type == GstHipKernelArgType::MEMORY) {
      auto mem = arg.memory;
      if (!gst_is_hip_memory (mem) || arg.offset >= mem->size) {
        GST_ERROR_OBJECT (module->device, "Invalid kernel memory argument");
        return FALSE;
      }

      auto hmem = GST_HIP_MEMORY_CAST (mem);
      if (gst_hip_device_get_vendor (hmem->device) != module->vendor ||
          gst_hip_device_get_device_id (hmem->device) != device_id) {
        GST_ERROR_OBJECT (module->device,
            "Kernel memory belongs to another device");
        return FALSE;
      }

      auto device_ptr = (guint8 *) gst_hip_memory_get_pointer (hmem);
      values[i] = device_ptr + arg.offset;
      params[i] = &values[i];
    } else if (arg.type == GstHipKernelArgType::SCALAR) {
      params[i] = (gpointer) & arg.scalar;
    } else {
      params[i] = arg.value.get ();
    }
  }

  if (!gst_hip_device_set_current (module->device))
    return FALSE;

  hipFunction_t function = nullptr;
  if (!gst_hip_kernel_module_get_function (module, entry_point, &function))
    return FALSE;

  auto hip_ret = HipModuleLaunchKernel (module->vendor, function,
      grid_x, grid_y, grid_z, block_x, block_y, block_z, 0,
      gst_hip_stream_get_handle (stream),
      params.empty ()? nullptr : params.data (), nullptr);

  return gst_hip_result (hip_ret, module->vendor);
}

/**
 * gst_hip_kernel_args_new:
 *
 * Creates an empty argument list.
 *
 * Returns: (transfer full): a new #GstHipKernelArgs
 *
 * Since: 1.30
 */
GstHipKernelArgs *
gst_hip_kernel_args_new (void)
{
  return new GstHipKernelArgs ();
}

/**
 * gst_hip_kernel_args_copy:
 * @args: a #GstHipKernelArgs
 *
 * Copies the argument list without referencing its borrowed memory arguments.
 *
 * Returns: (transfer full): a copy of @args
 *
 * Since: 1.30
 */
GstHipKernelArgs *
gst_hip_kernel_args_copy (const GstHipKernelArgs * args)
{
  g_return_val_if_fail (args, nullptr);

  return new GstHipKernelArgs (*args);
}

/**
 * gst_hip_kernel_args_free:
 * @args: (transfer full): a #GstHipKernelArgs
 *
 * Frees the host-side argument list.
 *
 * Since: 1.30
 */
void
gst_hip_kernel_args_free (GstHipKernelArgs * args)
{
  delete args;
}

/**
 * gst_hip_kernel_args_clear:
 * @args: a #GstHipKernelArgs
 *
 * Clears the arguments while retaining the argument list capacity.
 *
 * Since: 1.30
 */
void
gst_hip_kernel_args_clear (GstHipKernelArgs * args)
{
  g_return_if_fail (args);

  args->values.clear ();
}

/**
 * gst_hip_kernel_args_add_memory:
 * @args: a #GstHipKernelArgs
 * @memory: (transfer none): HIP memory, borrowed without taking a reference
 * @offset: byte offset from the start of the visible memory region
 *
 * Appends a device pointer argument. @memory must remain valid through launch;
 * its allocation must remain alive until GPU completion. No mapping, transfer
 * or synchronization is performed. The caller must ensure the kernel's accesses
 * stay within the memory region.
 *
 * Returns: %TRUE if the argument was appended
 *
 * Since: 1.30
 */
gboolean
gst_hip_kernel_args_add_memory (GstHipKernelArgs * args,
    GstMemory * memory, gsize offset)
{
  g_return_val_if_fail (args, FALSE);
  g_return_val_if_fail (gst_is_hip_memory (memory), FALSE);
  g_return_val_if_fail (offset < memory->size, FALSE);

  GstHipKernelArg arg;
  arg.type = GstHipKernelArgType::MEMORY;
  arg.memory = memory;
  arg.offset = offset;

  args->values.push_back (arg);

  return TRUE;
}

/**
 * gst_hip_kernel_args_add_value:
 * @args: a #GstHipKernelArgs
 * @value: (array length=size) (element-type guint8): argument data
 * @size: size of @value in bytes
 * @alignment: required alignment of the argument type, in bytes. Must be a
 *   non-zero power of two
 *
 * Appends a by-value kernel argument by copying @value into storage aligned
 * to @alignment.
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.30
 */
gboolean
gst_hip_kernel_args_add_value (GstHipKernelArgs * args,
    gconstpointer value, gsize size, gsize alignment)
{
  g_return_val_if_fail (args, FALSE);
  g_return_val_if_fail (value, FALSE);
  g_return_val_if_fail (size > 0, FALSE);
  g_return_val_if_fail (alignment > 0, FALSE);
  g_return_val_if_fail ((alignment & (alignment - 1)) == 0, FALSE);
  g_return_val_if_fail (size <= G_MAXSIZE - (alignment - 1), FALSE);

  GstHipKernelArg arg;
  arg.type = GstHipKernelArgType::VALUE;
  gsize space = size + alignment - 1;
  auto storage = std::shared_ptr < void >(g_malloc (space), g_free);
  auto aligned = storage.get ();
  std::align (alignment, size, aligned, space);
  arg.value = std::shared_ptr < void >(storage, aligned);
  memcpy (arg.value.get (), value, size);
  args->values.push_back (arg);

  return TRUE;
}

void
gst_hip_kernel_args_add_scalar (GstHipKernelArgs * args,
    gconstpointer value, gsize size)
{
  g_return_if_fail (args);
  g_return_if_fail (value);
  g_return_if_fail (size > 0);

  GstHipKernelArg arg;
  g_assert (size <= sizeof (arg.scalar));

  arg.type = GstHipKernelArgType::SCALAR;
  memcpy (&arg.scalar, value, size);
  args->values.push_back (arg);
}

/**
 * gst_hip_kernel_args_add_uint8:
 * @args: a #GstHipKernelArgs
 * @value: the argument value
 *
 * Appends a guint8 argument by value.
 *
 * Since: 1.30
 */
void
gst_hip_kernel_args_add_uint8 (GstHipKernelArgs * args, guint8 value)
{
  gst_hip_kernel_args_add_scalar (args, &value, sizeof (value));
}

/**
 * gst_hip_kernel_args_add_int8:
 * @args: a #GstHipKernelArgs
 * @value: the argument value
 *
 * Appends a gint8 argument by value.
 *
 * Since: 1.30
 */
void
gst_hip_kernel_args_add_int8 (GstHipKernelArgs * args, gint8 value)
{
  gst_hip_kernel_args_add_scalar (args, &value, sizeof (value));
}

/**
 * gst_hip_kernel_args_add_uint16:
 * @args: a #GstHipKernelArgs
 * @value: the argument value
 *
 * Appends a guint16 argument by value.
 *
 * Since: 1.30
 */
void
gst_hip_kernel_args_add_uint16 (GstHipKernelArgs * args, guint16 value)
{
  gst_hip_kernel_args_add_scalar (args, &value, sizeof (value));
}

/**
 * gst_hip_kernel_args_add_int16:
 * @args: a #GstHipKernelArgs
 * @value: the argument value
 *
 * Appends a gint16 argument by value.
 *
 * Since: 1.30
 */
void
gst_hip_kernel_args_add_int16 (GstHipKernelArgs * args, gint16 value)
{
  gst_hip_kernel_args_add_scalar (args, &value, sizeof (value));
}

/**
 * gst_hip_kernel_args_add_uint32:
 * @args: a #GstHipKernelArgs
 * @value: the argument value
 *
 * Appends a guint32 argument by value.
 *
 * Since: 1.30
 */
void
gst_hip_kernel_args_add_uint32 (GstHipKernelArgs * args, guint32 value)
{
  gst_hip_kernel_args_add_scalar (args, &value, sizeof (value));
}

/**
 * gst_hip_kernel_args_add_int32:
 * @args: a #GstHipKernelArgs
 * @value: the argument value
 *
 * Appends a gint32 argument by value.
 *
 * Since: 1.30
 */
void
gst_hip_kernel_args_add_int32 (GstHipKernelArgs * args, gint32 value)
{
  gst_hip_kernel_args_add_scalar (args, &value, sizeof (value));
}

/**
 * gst_hip_kernel_args_add_uint64:
 * @args: a #GstHipKernelArgs
 * @value: the argument value
 *
 * Appends a guint64 argument by value.
 *
 * Since: 1.30
 */
void
gst_hip_kernel_args_add_uint64 (GstHipKernelArgs * args, guint64 value)
{
  gst_hip_kernel_args_add_scalar (args, &value, sizeof (value));
}

/**
 * gst_hip_kernel_args_add_int64:
 * @args: a #GstHipKernelArgs
 * @value: the argument value
 *
 * Appends a gint64 argument by value.
 *
 * Since: 1.30
 */
void
gst_hip_kernel_args_add_int64 (GstHipKernelArgs * args, gint64 value)
{
  gst_hip_kernel_args_add_scalar (args, &value, sizeof (value));
}

/**
 * gst_hip_kernel_args_add_float:
 * @args: a #GstHipKernelArgs
 * @value: the argument value
 *
 * Appends a gfloat argument by value.
 *
 * Since: 1.30
 */
void
gst_hip_kernel_args_add_float (GstHipKernelArgs * args, gfloat value)
{
  gst_hip_kernel_args_add_scalar (args, &value, sizeof (value));
}

/**
 * gst_hip_kernel_args_add_double:
 * @args: a #GstHipKernelArgs
 * @value: the argument value
 *
 * Appends a gdouble argument by value.
 *
 * Since: 1.30
 */
void
gst_hip_kernel_args_add_double (GstHipKernelArgs * args, gdouble value)
{
  gst_hip_kernel_args_add_scalar (args, &value, sizeof (value));
}
