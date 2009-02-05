/*
 * GStreamer
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
 *
 * Photography interface implementation for camerabin.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstcamerabinphotography.h"
#include "gstcamerabin.h"

GST_DEBUG_CATEGORY_STATIC (camerabinphoto_debug);
#define GST_CAT_DEFAULT camerabinphoto_debug

#define PHOTOGRAPHY_IS_OK(photo_elem) (GST_IS_ELEMENT (photo_elem) && \
                                       gst_element_implements_interface (photo_elem, GST_TYPE_PHOTOGRAPHY))

#define GST_PHOTOGRAPHY_IMPL_TEMPLATE(function_name, param_type) \
static gboolean \
gst_camerabin_set_ ## function_name (GstPhotography *photo, param_type param) \
{ \
  GstCameraBin *camera; \
  gboolean ret = FALSE; \
  g_return_val_if_fail (photo != NULL, FALSE); \
  camera = GST_CAMERABIN (photo); \
  if (PHOTOGRAPHY_IS_OK (camera->src_vid_src)) { \
    ret = gst_photography_set_ ## function_name (GST_PHOTOGRAPHY (camera->src_vid_src), param); \
  } \
  return ret; \
} \
static gboolean \
gst_camerabin_get_ ## function_name (GstPhotography *photo, param_type * param) \
{ \
  GstCameraBin *camera; \
  gboolean ret = FALSE; \
  g_return_val_if_fail (photo != NULL, FALSE); \
  camera = GST_CAMERABIN (photo); \
  if (PHOTOGRAPHY_IS_OK (camera->src_vid_src)) { \
    ret = gst_photography_get_ ## function_name (GST_PHOTOGRAPHY (camera->src_vid_src), param); \
  } \
  return ret; \
}

GST_PHOTOGRAPHY_IMPL_TEMPLATE (ev_compensation, gfloat);
GST_PHOTOGRAPHY_IMPL_TEMPLATE (iso_speed, guint);
GST_PHOTOGRAPHY_IMPL_TEMPLATE (white_balance_mode, GstWhiteBalanceMode);
GST_PHOTOGRAPHY_IMPL_TEMPLATE (colour_tone_mode, GstColourToneMode);
GST_PHOTOGRAPHY_IMPL_TEMPLATE (flash_mode, GstFlashMode);

static gboolean
gst_camerabin_set_zoom (GstPhotography * photo, gfloat zoom)
{
  GstCameraBin *camera;

  g_return_val_if_fail (photo != NULL, FALSE);

  camera = GST_CAMERABIN (photo);

  /* camerabin can zoom by itself */
  g_object_set (camera, "zoom", (gint) (CLAMP (zoom, 1.0, 10.0) * 100), NULL);

  return TRUE;
}

static gboolean
gst_camerabin_get_zoom (GstPhotography * photo, gfloat * zoom)
{
  GstCameraBin *camera;
  gint cb_zoom = 0;

  g_return_val_if_fail (photo != NULL, FALSE);

  camera = GST_CAMERABIN (photo);

  g_object_get (camera, "zoom", &cb_zoom, NULL);
  *zoom = (gfloat) (cb_zoom / 100.0);

  return TRUE;
}

static gboolean
gst_camerabin_set_scene_mode (GstPhotography * photo, GstSceneMode scene_mode)
{
  GstCameraBin *camera;
  gboolean ret = FALSE;

  g_return_val_if_fail (photo != NULL, FALSE);

  camera = GST_CAMERABIN (photo);

  if (scene_mode == GST_PHOTOGRAPHY_SCENE_MODE_NIGHT) {
    GST_DEBUG ("enabling night mode, lowering fps");
    /* Make camerabin select the lowest allowed frame rate */
    camera->night_mode = TRUE;
    /* Remember frame rate before setting night mode */
    camera->pre_night_fps_n = camera->fps_n;
    camera->pre_night_fps_d = camera->fps_d;
    g_signal_emit_by_name (camera, "user-res-fps", camera->width,
        camera->height, 0, 0, 0);
  } else {
    if (camera->night_mode) {
      GST_DEBUG ("disabling night mode, restoring fps to %d/%d",
          camera->pre_night_fps_n, camera->pre_night_fps_d);
      camera->night_mode = FALSE;
      g_signal_emit_by_name (camera, "user-res-fps", camera->width,
          camera->height, camera->pre_night_fps_n, camera->pre_night_fps_d, 0);
    }
  }

  if (PHOTOGRAPHY_IS_OK (camera->src_vid_src)) {
    ret = gst_photography_set_scene_mode (GST_PHOTOGRAPHY (camera->src_vid_src),
        scene_mode);
  }
  return ret;
}

static gboolean
gst_camerabin_get_scene_mode (GstPhotography * photo, GstSceneMode * scene_mode)
{
  GstCameraBin *camera;
  gboolean ret = FALSE;

  g_return_val_if_fail (photo != NULL, FALSE);

  camera = GST_CAMERABIN (photo);

  if (PHOTOGRAPHY_IS_OK (camera->src_vid_src)) {
    ret = gst_photography_get_scene_mode (GST_PHOTOGRAPHY (camera->src_vid_src),
        scene_mode);
  }
  return ret;
}

static GstPhotoCaps
gst_camerabin_get_capabilities (GstPhotography * photo)
{
  GstCameraBin *camera;
  /* camerabin can zoom by itself */
  GstPhotoCaps pcaps = GST_PHOTOGRAPHY_CAPS_ZOOM;

  g_return_val_if_fail (photo != NULL, FALSE);

  camera = GST_CAMERABIN (photo);

  if (GST_IS_ELEMENT (camera->src_vid_src) &&
      gst_element_implements_interface (camera->src_vid_src,
          GST_TYPE_PHOTOGRAPHY)) {
    GstPhotography *p2 = GST_PHOTOGRAPHY (camera->src_vid_src);
    pcaps |= gst_photography_get_capabilities (p2);
  }

  return pcaps;
}

static void
gst_camerabin_set_autofocus (GstPhotography * photo, gboolean on)
{
  GstCameraBin *camera;

  g_return_if_fail (photo != NULL);

  camera = GST_CAMERABIN (photo);

  GST_DEBUG_OBJECT (camera, "setting autofocus %s", on ? "ON" : "OFF");

  if (PHOTOGRAPHY_IS_OK (camera->src_vid_src)) {
    gst_photography_set_autofocus (GST_PHOTOGRAPHY (camera->src_vid_src), on);
  }
}


void
gst_camerabin_photography_init (GstPhotographyInterface * iface)
{
  GST_DEBUG_CATEGORY_INIT (camerabinphoto_debug, "camerabinphoto", 0,
      "Camerabin photography interface debugging");

  GST_INFO ("initing");

  iface->set_ev_compensation = gst_camerabin_set_ev_compensation;
  iface->get_ev_compensation = gst_camerabin_get_ev_compensation;

  iface->set_iso_speed = gst_camerabin_set_iso_speed;
  iface->get_iso_speed = gst_camerabin_get_iso_speed;

  iface->set_white_balance_mode = gst_camerabin_set_white_balance_mode;
  iface->get_white_balance_mode = gst_camerabin_get_white_balance_mode;

  iface->set_colour_tone_mode = gst_camerabin_set_colour_tone_mode;
  iface->get_colour_tone_mode = gst_camerabin_get_colour_tone_mode;

  iface->set_scene_mode = gst_camerabin_set_scene_mode;
  iface->get_scene_mode = gst_camerabin_get_scene_mode;

  iface->set_flash_mode = gst_camerabin_set_flash_mode;
  iface->get_flash_mode = gst_camerabin_get_flash_mode;

  iface->set_zoom = gst_camerabin_set_zoom;
  iface->get_zoom = gst_camerabin_get_zoom;

  iface->get_capabilities = gst_camerabin_get_capabilities;

  iface->set_autofocus = gst_camerabin_set_autofocus;
}
