/*
 * GStreamer
 * Copyright (C) 2014 Matthew Waters <ystreet00@gmail.com>
 * Copyright (C) 2015 Freescale Semiconductor <b55597@freescale.com>
 * Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
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

#include "gstgldisplay_viv_fb.h"

GST_DEBUG_CATEGORY_STATIC (gst_gl_display_debug);
#define GST_CAT_DEFAULT gst_gl_display_debug

/**
 * gst_gl_display_viv_fb_new:
 * @disp_idx: a display index
 *
 * Create a new #GstGLDisplay from the FB display index.
 *
 * Returns: (transfer full): a new #GstGLDisplayVivFB or %NULL
 */
GstGLDisplayEGL *
gst_gl_display_viv_fb_new (gint disp_idx)
{
  EGLDisplay display;

  GST_DEBUG_CATEGORY_GET (gst_gl_display_debug, "gldisplay");

  GST_DEBUG ("creating Vivante FB EGL display %d", disp_idx);

  display = fbGetDisplayByIndex (disp_idx);
  GST_DEBUG ("Created Vivante FB EGL display %p", (gpointer) display->display);
  return
      gst_gl_display_egl_new_with_egl_display (eglGetDisplay (
          (EGLNativeDisplayType) display));
}
