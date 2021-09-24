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
#ifndef __GST_DXGI_SCREEN_CAP_SRC_H__
#define __GST_DXGI_SCREEN_CAP_SRC_H__

#include <windows.h>
#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS
#define GST_TYPE_DXGI_SCREEN_CAP_SRC  (gst_dxgi_screen_cap_src_get_type())
G_DECLARE_FINAL_TYPE (GstDXGIScreenCapSrc, gst_dxgi_screen_cap_src, GST,
    DXGI_SCREEN_CAP_SRC, GstPushSrc);

void gst_dxgi_screen_cap_src_register (GstPlugin * plugin,
                                       GstRank rank);

G_END_DECLS
#endif /* __GST_DXGI_SCREEN_CAP_SRC_H__ */
