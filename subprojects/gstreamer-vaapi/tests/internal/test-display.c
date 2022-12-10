/*
 *  test-display.c - Test GstVaapiDisplayX11
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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

#define _GNU_SOURCE
#include "gst/vaapi/sysdeps.h"
#include <gst/video/video.h>
#if GST_VAAPI_USE_DRM
# include <gst/vaapi/gstvaapidisplay_drm.h>
# include <va/va_drm.h>
# include <fcntl.h>
# include <unistd.h>
# ifndef DRM_DEVICE_PATH
# define DRM_DEVICE_PATH "/dev/dri/card0"
# endif
#endif
#if GST_VAAPI_USE_X11
# include <gst/vaapi/gstvaapidisplay_x11.h>
#endif
#if GST_VAAPI_USE_GLX
# include <gst/vaapi/gstvaapidisplay_glx.h>
#endif
#if GST_VAAPI_USE_WAYLAND
# include <gst/vaapi/gstvaapidisplay_wayland.h>
#endif
#if GST_VAAPI_USE_EGL
# include <gst/vaapi/gstvaapidisplay_egl.h>
#endif

#ifdef HAVE_VA_VA_GLX_H
# include <va/va_glx.h>
#endif

static void
print_value (const GValue * value, const gchar * name)
{
  gchar *value_string;

  value_string = g_strdup_value_contents (value);
  if (!value_string)
    return;
  g_print ("  %s: %s\n", name, value_string);
  g_free (value_string);
}

static void
print_profiles (GArray * profiles, const gchar * name)
{
  GstVaapiCodec codec;
  const gchar *codec_name, *profile_name;
  guint i;

  g_print ("%u %s caps\n", profiles->len, name);

  for (i = 0; i < profiles->len; i++) {
    const GstVaapiProfile profile =
        g_array_index (profiles, GstVaapiProfile, i);

    codec = gst_vaapi_profile_get_codec (profile);
    if (!codec)
      continue;

    codec_name = gst_vaapi_codec_get_name (codec);
    if (!codec_name)
      continue;

    profile_name = gst_vaapi_profile_get_va_name (profile);
    if (!profile_name)
      continue;

    g_print ("  %s: %s profile\n", codec_name, profile_name);
  }
}

static void
print_format_yuv (const VAImageFormat * va_format)
{
  const guint32 fourcc = va_format->fourcc;

  g_print (" fourcc '%c%c%c%c'",
      fourcc & 0xff,
      (fourcc >> 8) & 0xff, (fourcc >> 16) & 0xff, (fourcc >> 24) & 0xff);
}

static void
print_format_rgb (const VAImageFormat * va_format)
{
  g_print (" %d bits per pixel, %s endian,",
      va_format->bits_per_pixel,
      va_format->byte_order == VA_MSB_FIRST ? "big" : "little");
  g_print (" %s masks", va_format->alpha_mask ? "rgba" : "rgb");
  g_print (" 0x%08x 0x%08x 0x%08x",
      va_format->red_mask, va_format->green_mask, va_format->blue_mask);
  if (va_format->alpha_mask)
    g_print (" 0x%08x", va_format->alpha_mask);
}

static void
print_formats (GArray * formats, const gchar * name)
{
  guint i;

  g_print ("%u %s caps\n", formats->len, name);

  for (i = 0; i < formats->len; i++) {
    const GstVideoFormat format = g_array_index (formats, GstVideoFormat, i);
    const VAImageFormat *va_format;

    g_print ("  %s:", gst_vaapi_video_format_to_string (format));

    va_format = gst_vaapi_video_format_to_va_format (format);
    if (!va_format)
      g_error ("could not determine VA format");

    if (gst_vaapi_video_format_is_yuv (format))
      print_format_yuv (va_format);
    else
      print_format_rgb (va_format);
    g_print ("\n");
  }
}

static void
dump_properties (GstVaapiDisplay * display)
{
  GParamSpec **properties;
  guint i, n_properties;

  properties =
      g_object_class_list_properties (G_OBJECT_GET_CLASS (display),
      &n_properties);

  for (i = 0; i < n_properties; i++) {
    const gchar *const name = g_param_spec_get_nick (properties[i]);
    GValue value = G_VALUE_INIT;

    if (!gst_vaapi_display_has_property (display, name))
      continue;

    g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (properties[i]));
    g_object_get_property (G_OBJECT (display), name, &value);
    print_value (&value, name);
  }

  g_free (properties);
}

static void
dump_info (GstVaapiDisplay * display)
{
  GArray *profiles, *formats;

  profiles = gst_vaapi_display_get_decode_profiles (display);
  if (!profiles)
    g_error ("could not get VA decode profiles");

  print_profiles (profiles, "decoders");
  g_array_unref (profiles);

  profiles = gst_vaapi_display_get_encode_profiles (display);
  if (!profiles)
    g_error ("could not get VA encode profiles");

  print_profiles (profiles, "encoders");
  g_array_unref (profiles);

  formats = gst_vaapi_display_get_image_formats (display);
  if (!formats)
    g_error ("could not get VA image formats");

  print_formats (formats, "image");
  g_array_unref (formats);

  formats = gst_vaapi_display_get_subpicture_formats (display);
  if (!formats)
    g_error ("could not get VA subpicture formats");

  print_formats (formats, "subpicture");
  g_array_unref (formats);

  dump_properties (display);
}

int
main (int argc, char *argv[])
{
  GstVaapiDisplay *display;
#if GST_VAAPI_USE_GLX || GST_VAAPI_USE_WAYLAND
  guint width, height;
  guint par_n, par_d;
#endif

  gst_init (&argc, &argv);

#if GST_VAAPI_USE_DRM
  g_print ("#\n");
  g_print ("# Create display with gst_vaapi_display_drm_new()\n");
  g_print ("#\n");
  {
    display = gst_vaapi_display_drm_new (NULL);
    if (!display)
      g_error ("could not create Gst/VA display");

    dump_info (display);
    gst_object_unref (display);
  }
  g_print ("\n");

  g_print ("#\n");
  g_print ("# Create display with gst_vaapi_display_drm_new_with_device()\n");
  g_print ("#\n");
  {
    int drm_device;

    drm_device = open (DRM_DEVICE_PATH, O_RDWR | O_CLOEXEC);
    if (drm_device < 0)
      g_error ("could not open DRM device");

    display = gst_vaapi_display_drm_new_with_device (drm_device);
    if (!display)
      g_error ("could not create Gst/VA display");

    dump_info (display);
    gst_object_unref (display);
    close (drm_device);
  }
  g_print ("\n");

  g_print ("#\n");
  g_print
      ("# Create display with gst_vaapi_display_new_with_display() [vaGetDisplayDRM()]\n");
  g_print ("#\n");
  {
    int drm_device;
    VADisplay va_display;

    drm_device = open (DRM_DEVICE_PATH, O_RDWR | O_CLOEXEC);
    if (drm_device < 0)
      g_error ("could not open DRM device");

    va_display = vaGetDisplayDRM (drm_device);
    if (!va_display)
      g_error ("could not create VA display");

    display = gst_vaapi_display_new_with_display (va_display);
    if (!display)
      g_error ("could not create Gst/VA display");

    dump_info (display);
    gst_object_unref (display);
    close (drm_device);
  }
  g_print ("\n");
#endif

#if GST_VAAPI_USE_X11
  g_print ("#\n");
  g_print ("# Create display with gst_vaapi_display_x11_new()\n");
  g_print ("#\n");
  {
    display = gst_vaapi_display_x11_new (NULL);
    if (!display)
      g_error ("could not create Gst/VA display");
  }
  g_print ("\n");

  g_print ("#\n");
  g_print ("# Create display with gst_vaapi_display_x11_new_with_display()\n");
  g_print ("#\n");
  {
    Display *x11_display;

    x11_display = XOpenDisplay (NULL);
    if (!x11_display)
      g_error ("could not create X11 display");

    display = gst_vaapi_display_x11_new_with_display (x11_display);
    if (!display)
      g_error ("could not create Gst/VA display");

    dump_info (display);
    gst_object_unref (display);
    XCloseDisplay (x11_display);
  }
  g_print ("\n");

  g_print ("#\n");
  g_print
      ("# Create display with gst_vaapi_display_new_with_display() [vaGetDisplay()]\n");
  g_print ("#\n");
  {
    Display *x11_display;
    VADisplay va_display;

    x11_display = XOpenDisplay (NULL);
    if (!x11_display)
      g_error ("could not create X11 display");

    va_display = vaGetDisplay (x11_display);
    if (!va_display)
      g_error ("could not create VA display");

    display = gst_vaapi_display_new_with_display (va_display);
    if (!display)
      g_error ("could not create Gst/VA display");

    dump_info (display);
    gst_object_unref (display);
    XCloseDisplay (x11_display);
  }
  g_print ("\n");
#endif

#if GST_VAAPI_USE_GLX
  g_print ("#\n");
  g_print ("# Create display with gst_vaapi_display_glx_new()\n");
  g_print ("#\n");
  {
    display = gst_vaapi_display_glx_new (NULL);
    if (!display)
      g_error ("could not create Gst/VA display");

    gst_vaapi_display_get_size (display, &width, &height);
    g_print ("Display size: %ux%u\n", width, height);

    gst_vaapi_display_get_pixel_aspect_ratio (display, &par_n, &par_d);
    g_print ("Pixel aspect ratio: %u/%u\n", par_n, par_d);

    dump_info (display);
    gst_object_unref (display);
  }
  g_print ("\n");

  g_print ("#\n");
  g_print ("# Create display with gst_vaapi_display_glx_new_with_display()\n");
  g_print ("#\n");
  {
    Display *x11_display;

    x11_display = XOpenDisplay (NULL);
    if (!x11_display)
      g_error ("could not create X11 display");

    display = gst_vaapi_display_glx_new_with_display (x11_display);
    if (!display)
      g_error ("could not create Gst/VA display");

    dump_info (display);
    gst_object_unref (display);
    XCloseDisplay (x11_display);
  }
  g_print ("\n");

#ifdef HAVE_VA_VA_GLX_H
  g_print ("#\n");
  g_print
      ("# Create display with gst_vaapi_display_new_with_display() [vaGetDisplayGLX()]\n");
  g_print ("#\n");
  {
    Display *x11_display;
    VADisplay va_display;

    x11_display = XOpenDisplay (NULL);
    if (!x11_display)
      g_error ("could not create X11 display");

    va_display = vaGetDisplayGLX (x11_display);
    if (!va_display)
      g_error ("could not create VA display");

    display = gst_vaapi_display_new_with_display (va_display);
    if (!display)
      g_error ("could not create Gst/VA display");

    dump_info (display);
    gst_object_unref (display);
    XCloseDisplay (x11_display);
  }
  g_print ("\n");
#endif
#endif

#if GST_VAAPI_USE_WAYLAND
  g_print ("#\n");
  g_print ("# Create display with gst_vaapi_display_wayland_new()\n");
  g_print ("#\n");
  {
    display = gst_vaapi_display_wayland_new (NULL);
    if (!display)
      g_error ("could not create Gst/VA display");

    gst_vaapi_display_get_size (display, &width, &height);
    g_print ("Display size: %ux%u\n", width, height);

    gst_vaapi_display_get_pixel_aspect_ratio (display, &par_n, &par_d);
    g_print ("Pixel aspect ratio: %u/%u\n", par_n, par_d);

    dump_info (display);
    gst_object_unref (display);
  }
  g_print ("\n");
#endif

  gst_deinit ();
  return 0;
}
