/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_D3D11_UTILS_H__
#define __GST_D3D11_UTILS_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstd3d11_fwd.h"

G_BEGIN_DECLS

gboolean        gst_d3d11_handle_set_context        (GstElement * element,
                                                     GstContext * context,
                                                     gint adapter,
                                                     GstD3D11Device ** device);

gboolean        gst_d3d11_handle_context_query      (GstElement * element,
                                                     GstQuery * query,
                                                     GstD3D11Device * device);

gboolean        gst_d3d11_ensure_element_data       (GstElement * element,
                                                     gint adapter,
                                                     GstD3D11Device ** device);

gboolean        gst_d3d11_is_windows_8_or_greater   (void);

GstQuery *      gst_query_new_d3d11_usage           (D3D11_USAGE usage);

void            gst_query_parse_d3d11_usage         (GstQuery * query,
                                                     D3D11_USAGE *usage);

void            gst_query_set_d3d11_usage_result    (GstQuery * query,
                                                     gboolean result);

void            gst_query_parse_d3d11_usage_result  (GstQuery * query,
                                                     gboolean * result);

gboolean        gst_query_is_d3d11_usage            (GstQuery * query);

GstCaps *       gst_d3d11_caps_fixate_format        (GstCaps * caps,
                                                     GstCaps * othercaps);

static void
gst_d3d11_format_error (gint error_code, gchar ** str)
{
  g_return_if_fail(str);

  FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
    NULL, error_code,
    MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
    (LPTSTR)str, 0, NULL);
};

#ifndef GST_DISABLE_GST_DEBUG
static inline gboolean
_gst_d3d11_debug(HRESULT result, GstDebugCategory * category,
    const gchar * file, const gchar * function, gint line)
{
  if (FAILED(result)) {
    gchar *error_text = NULL;

    gst_d3d11_format_error(result,&error_text);
    gst_debug_log (category, GST_LEVEL_WARNING, file, function, line,
        NULL, "D3D11 call failed: 0x%x, %s", (guint)result, error_text);
    LocalFree(error_text);

    return FALSE;
  }

  return TRUE;
}

/**
 * gst_d3d11_result:
 * @result: D3D11 API return code #HRESULT
 *
 * Returns: %TRUE if D3D11 API call result is SUCCESS
 */
#define gst_d3d11_result(result) \
    _gst_d3d11_debug(result, GST_CAT_DEFAULT, __FILE__, GST_FUNCTION, __LINE__)
#else

static inline gboolean
_gst_d3d11_debug(HRESULT result, GstDebugCategory * category,
    const gchar * file, const gchar * function, gint line)
{
  return SUCCESS(result);
}

/**
 * gst_d3d11_result:
 * @result: D3D11 API return code #HRESULT
 *
 * Returns: %TRUE if D3D11 API call result is SUCCESS
 */
#define gst_d3d11_result(result) \
  _gst_d3d11_debug(result, NULL, __FILE__, GST_FUNCTION, __LINE__)
#endif



G_END_DECLS

#endif /* __GST_D3D11_UTILS_H__ */
