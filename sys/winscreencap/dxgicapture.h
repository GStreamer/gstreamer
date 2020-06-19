/* GStreamer
 * Copyright (C) 2019 OKADA Jun-ichi <okada@abt.jp>
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

#ifndef __DXGICAP_H__
#define __DXGICAP_H__

#define COBJMACROS
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#undef COBJMACROS

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>

#define RECT_WIDTH(r) (r.right - r.left)
#define RECT_HEIGHT(r) (r.bottom - r.top)

#define HR_FAILED_AND(hr,func,and) \
  G_STMT_START { \
    if (FAILED (hr)) { \
      gchar *msg = get_hresult_to_string (hr); \
      GST_ERROR_OBJECT (src, #func " failed (%x): %s", (guint) hr, msg); \
      g_free (msg); \
      and; \
    } \
  } G_STMT_END

#define HR_FAILED_RET(hr,func,ret) HR_FAILED_AND(hr,func,return ret)

#define HR_FAILED_GOTO(hr,func,where) HR_FAILED_AND(hr,func,goto where)

#define HR_FAILED_INFO(hr, func) \
  G_STMT_START { \
    if (FAILED (hr)) { \
      gchar *msg = get_hresult_to_string (hr); \
      GST_INFO_OBJECT (src, #func " failed (%x): %s", (guint) hr, msg); \
      g_free (msg); \
    } \
  } G_STMT_END


typedef struct _GstDXGIScreenCapSrc GstDXGIScreenCapSrc;

typedef struct _DxgiCapture DxgiCapture;

gboolean gst_dxgicap_shader_init (void);

DxgiCapture *dxgicap_new (HMONITOR monitor, GstDXGIScreenCapSrc * src);
void dxgicap_destory (DxgiCapture * _this);

gboolean dxgicap_start (DxgiCapture * _this);
void dxgicap_stop (DxgiCapture * _this);

gint dxgicap_acquire_next_frame (DxgiCapture * _this, gboolean show_cursor,
    guint timeout);
gboolean dxgicap_copy_buffer (DxgiCapture * _this, gboolean show_cursor,
    LPRECT src_rect, GstVideoInfo * video_info, GstBuffer * buf);

HMONITOR get_hmonitor_by_device_name (const gchar * device_name);
HMONITOR get_hmonitor_primary (void);
HMONITOR get_hmonitor_by_index (int index);

gboolean get_monitor_physical_size (HMONITOR hmonitor, LPRECT rect);

gchar *get_hresult_to_string (HRESULT hr);

#endif /* __DXGICAP_H__ */
