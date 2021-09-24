/*
 *  test-textures.c - Test GstVaapiTexture
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/vaapi/gstvaapidisplay_glx.h>
#include <gst/vaapi/gstvaapiwindow_glx.h>
#include <gst/vaapi/gstvaapitexture_glx.h>
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapiimage.h>
#include "image.h"

static inline void
pause (void)
{
  g_print ("Press any key to continue...\n");
  getchar ();
}

static inline guint
gl_get_current_texture_2d (void)
{
  GLint texture;
  glGetIntegerv (GL_TEXTURE_BINDING_2D, &texture);
  return (guint) texture;
}

int
main (int argc, char *argv[])
{
  GstVaapiDisplay *display;
  GstVaapiWindow *window;
  GstVaapiWindowGLX *glx_window;
  GstVaapiSurface *surface;
  GstVaapiImage *image;
  GstVaapiTexture *textures[2];
  GstVaapiTexture *texture;
  GLuint texture_id;
  GstVaapiRectangle src_rect;
  GstVaapiRectangle dst_rect;
  guint flags = GST_VAAPI_PICTURE_STRUCTURE_FRAME;

  static const GstVaapiChromaType chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
  static const guint width = 320;
  static const guint height = 240;
  static const guint win_width = 640;
  static const guint win_height = 480;

  gst_init (&argc, &argv);

  display = gst_vaapi_display_glx_new (NULL);
  if (!display)
    g_error ("could not create VA display");

  surface = gst_vaapi_surface_new (display, chroma_type, width, height);
  if (!surface)
    g_error ("could not create VA surface");

  image = image_generate (display, GST_VIDEO_FORMAT_NV12, width, height);
  if (!image)
    g_error ("could not create VA image");
  if (!image_upload (image, surface))
    g_error ("could not upload VA image to surface");

  window = gst_vaapi_window_glx_new (display, win_width, win_height);
  if (!window)
    g_error ("could not create window");
  glx_window = GST_VAAPI_WINDOW_GLX (window);

  gst_vaapi_window_show (window);

  if (!gst_vaapi_window_glx_make_current (glx_window))
    g_error ("coult not bind GL context");

  g_print ("#\n");
  g_print ("# Create texture with gst_vaapi_texture_glx_new()\n");
  g_print ("#\n");
  {
    texture = gst_vaapi_texture_glx_new (display,
        GL_TEXTURE_2D, GL_RGBA, width, height);
    if (!texture)
      g_error ("could not create VA texture");

    textures[0] = texture;
    texture_id = gst_vaapi_texture_get_id (texture);

    if (!gst_vaapi_texture_put_surface (texture, surface, NULL, flags))
      g_error ("could not transfer VA surface to texture");

    if (!gst_vaapi_window_glx_put_texture (glx_window, texture, NULL, NULL))
      g_error ("could not render texture into the window");
  }

  g_print ("#\n");
  g_print ("# Create texture with gst_vaapi_texture_glx_new_wrapped()\n");
  g_print ("#\n");
  {
    const GLenum target = GL_TEXTURE_2D;
    const GLenum format = GL_BGRA;

    glEnable (target);
    glGenTextures (1, &texture_id);
    glBindTexture (target, texture_id);
    glTexParameteri (target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D (target,
        0, GL_RGBA8, width, height, 0, format, GL_UNSIGNED_BYTE, NULL);
    glDisable (target);

    texture = gst_vaapi_texture_glx_new_wrapped (display,
        texture_id, target, format);
    if (!texture)
      g_error ("could not create VA texture");

    if (texture_id != gst_vaapi_texture_get_id (texture))
      g_error ("invalid texture id");

    if (gl_get_current_texture_2d () != texture_id)
      g_error ("gst_vaapi_texture_glx_new_wrapped() altered texture bindings");

    textures[1] = texture;

    if (!gst_vaapi_texture_put_surface (texture, surface, NULL, flags))
      g_error ("could not transfer VA surface to texture");

    if (gl_get_current_texture_2d () != texture_id)
      g_error ("gst_vaapi_texture_put_surface() altered texture bindings");

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = width;
    src_rect.height = height;

    dst_rect.x = win_width / 2;
    dst_rect.y = win_height / 2;
    dst_rect.width = win_width / 2;
    dst_rect.height = win_height / 2;

    if (!gst_vaapi_window_glx_put_texture (glx_window, texture,
            &src_rect, &dst_rect))
      g_error ("could not render texture into the window");

    if (gl_get_current_texture_2d () != texture_id)
      g_error ("gst_vaapi_window_glx_put_texture() altered texture bindings");
  }

  gst_vaapi_window_glx_swap_buffers (glx_window);
  pause ();

  gst_mini_object_unref (GST_MINI_OBJECT_CAST (textures[0]));
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (textures[1]));
  glDeleteTextures (1, &texture_id);

  gst_object_unref (window);
  gst_object_unref (display);
  gst_deinit ();
  return 0;
}
