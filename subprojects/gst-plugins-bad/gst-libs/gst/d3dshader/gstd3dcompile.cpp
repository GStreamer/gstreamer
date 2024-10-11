/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#include "gstd3dcompile.h"
#include <gmodule.h>
#include <mutex>

/**
 * SECTION:gstd3dcompile
 * @title: GstD3DCompile
 * @short_description: HLSL compiler and utility
 *
 * A set of HLSL compile helper methods
 *
 * Since: 1.26
 */

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;
  static std::once_flag cat_once;

  std::call_once (cat_once, [&]() {
    cat = _gst_debug_category_new ("d3dcompile", 0, "d3dcompile");
  });

  return cat;
}
#endif /* GST_DISABLE_GST_DEBUG */

static GModule *d3d_compiler_module = nullptr;
static pD3DCompile GstD3DCompileFunc = nullptr;

/**
 * gst_d3d_compile_init:
 *
 * Loads HLSL compiler library
 *
 * Returns: %TRUE if HLSL compiler library is available
 *
 * Since: 1.26
 */
gboolean
gst_d3d_compile_init (void)
{
  static std::once_flag init_once;
  std::call_once (init_once, [&]() {
    static const gchar *d3d_compiler_names[] = {
      "d3dcompiler_47.dll",
      "d3dcompiler_46.dll",
      "d3dcompiler_45.dll",
      "d3dcompiler_44.dll",
      "d3dcompiler_43.dll",
    };

    for (guint i = 0; i < G_N_ELEMENTS (d3d_compiler_names); i++) {
      d3d_compiler_module =
          g_module_open (d3d_compiler_names[i], G_MODULE_BIND_LAZY);

      if (d3d_compiler_module) {
        GST_INFO ("D3D compiler %s is available", d3d_compiler_names[i]);
        if (!g_module_symbol (d3d_compiler_module, "D3DCompile",
                (gpointer *) & GstD3DCompileFunc)) {
          GST_ERROR ("Cannot load D3DCompile symbol from %s",
              d3d_compiler_names[i]);
          g_module_close (d3d_compiler_module);
          d3d_compiler_module = nullptr;
          GstD3DCompileFunc = nullptr;
        } else {
          break;
        }
      }
    }

    if (!GstD3DCompileFunc)
      GST_WARNING ("D3D compiler library is unavailable");
  });

  if (!GstD3DCompileFunc)
    return FALSE;

  return TRUE;
}

/**
 * gst_d3d_compile:
 * @src_data: source data to compile
 * @src_data_size: length of src_data
 * @source_name: (nullable): used for strings that specify error messages
 * @defines: (nullable): null-terminated array of D3D_SHADER_MACRO struct that defines shader macros
 * @include: (nullable): a ID3DInclude
 * @entry_point: (nullable): the name of entry point function
 * @target: a string specifies the shader target
 * @flags1: flags defined by D3DCOMPILE constants
 * @flags2: flags defined by D3DCOMPILE_EFFECT constants
 * @code: (out) (optional): a compiled code
 * @error_msgs: (out) (optional) (nullable): compiler error messages
 *
 * Compiles HLSL code or an effect file into bytecode for a given target
 *
 * Returns: HRESULT return code
 *
 * Since: 1.26
 */
HRESULT
gst_d3d_compile (LPCVOID src_data, SIZE_T src_data_size, LPCSTR source_name,
    CONST D3D_SHADER_MACRO * defines, ID3DInclude * include, LPCSTR entry_point,
    LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code,
    ID3DBlob ** error_msgs)
{
  if (!gst_d3d_compile_init ())
    return E_FAIL;

  return GstD3DCompileFunc (src_data, src_data_size, source_name, defines,
      include, entry_point, target, flags1, flags2, code, error_msgs);
}

