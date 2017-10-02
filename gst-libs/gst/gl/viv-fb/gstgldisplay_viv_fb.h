/*
 * GStreamer
 * Copyright (C) 2014 Matthew Waters <ystreet00@gmail.com>
 * Copyright (C) 2015 Freescale Semiconductor <b55597@freescale.com>
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

#ifndef __GST_GL_DISPLAY_VIV_FB_H__
#define __GST_GL_DISPLAY_VIV_FB_H__

#include <gst/gst.h>
#include <gst/gl/egl/gstgldisplay_egl.h>
#include <gst/gl/egl/gstegl.h>

G_BEGIN_DECLS

GstGLDisplayEGL *gst_gl_display_viv_fb_new (gint disp_idx);

G_END_DECLS

#endif /* __GST_GL_DISPLAY_VIV_FB_H__ */
