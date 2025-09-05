/* GStreamer Wayland Library
 *
 * Copyright (C) 2014 Collabora Ltd.
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwldisplay.h"
#include "gstwloutput-private.h"

#include "color-management-v1-client-protocol.h"
#include "color-representation-v1-client-protocol.h"
#include "fullscreen-shell-unstable-v1-client-protocol.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "single-pixel-buffer-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <errno.h>
#include <drm_fourcc.h>

#define GST_CAT_DEFAULT gst_wl_display_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

typedef struct _GstWlDisplayPrivate
{
  /* public objects */
  struct wl_display *display;
  struct wl_display *display_wrapper;
  struct wl_event_queue *queue;

  /* globals */
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_subcompositor *subcompositor;
  struct xdg_wm_base *xdg_wm_base;
  struct zwp_fullscreen_shell_v1 *fullscreen_shell;
  struct wp_single_pixel_buffer_manager_v1 *single_pixel_buffer;
  struct wl_shm *shm;
  struct wp_viewporter *viewporter;
  struct zwp_linux_dmabuf_v1 *dmabuf;
  struct wp_color_manager_v1 *color;
  struct wp_color_representation_manager_v1 *color_representation;

  GArray *shm_formats;
  GArray *dmabuf_formats;
  GArray *dmabuf_modifiers;

  gboolean color_parametric_creator_supported;
  gboolean color_mastering_display_supported;
  GArray *color_transfer_functions;
  GArray *color_primaries;
  GArray *color_alpha_modes;
  GArray *color_coefficients;
  GArray *color_coefficients_range;

  GMutex outputs_mutex;
  GHashTable *outputs;

  /* private */
  gboolean own_display;
  GThread *thread;
  GstPoll *wl_fd_poll;

  GRecMutex sync_mutex;

  GMutex buffers_mutex;
  GHashTable *buffers;
  gboolean shutting_down;
} GstWlDisplayPrivate;

G_DEFINE_TYPE_WITH_CODE (GstWlDisplay, gst_wl_display, G_TYPE_OBJECT,
    G_ADD_PRIVATE (GstWlDisplay)
    GST_DEBUG_CATEGORY_INIT (gst_wl_display_debug,
        "wldisplay", 0, "wldisplay library");
    );

static void gst_wl_display_finalize (GObject * gobject);

static void
gst_wl_display_class_init (GstWlDisplayClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = gst_wl_display_finalize;
}

static void
gst_wl_display_init (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  priv->shm_formats = g_array_new (FALSE, FALSE, sizeof (uint32_t));
  priv->dmabuf_formats = g_array_new (FALSE, FALSE, sizeof (uint32_t));
  priv->dmabuf_modifiers = g_array_new (FALSE, FALSE, sizeof (guint64));
  priv->color_transfer_functions =
      g_array_new (FALSE, FALSE, sizeof (uint32_t));
  priv->color_primaries = g_array_new (FALSE, FALSE, sizeof (uint32_t));
  priv->color_coefficients = g_array_new (FALSE, FALSE, sizeof (uint32_t));
  priv->color_coefficients_range =
      g_array_new (FALSE, FALSE, sizeof (uint32_t));
  priv->color_alpha_modes = g_array_new (FALSE, FALSE, sizeof (uint32_t));
  priv->wl_fd_poll = gst_poll_new (TRUE);
  priv->buffers = g_hash_table_new (g_direct_hash, g_direct_equal);
  g_mutex_init (&priv->buffers_mutex);
  g_rec_mutex_init (&priv->sync_mutex);

  g_mutex_init (&priv->outputs_mutex);
  priv->outputs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) g_object_unref);

  gst_wl_linux_dmabuf_init_once ();
  gst_wl_shm_init_once ();
  gst_shm_allocator_init_once ();
  gst_wl_videoformat_init_once ();
}

static void
gst_wl_ref_wl_buffer (gpointer key, gpointer value, gpointer user_data)
{
  g_object_ref (value);
}

static void
gst_wl_display_finalize (GObject * gobject)
{
  GstWlDisplay *self = GST_WL_DISPLAY (gobject);
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  gst_poll_set_flushing (priv->wl_fd_poll, TRUE);
  if (priv->thread)
    g_thread_join (priv->thread);

  /* to avoid buffers being unregistered from another thread
   * at the same time, take their ownership */
  g_mutex_lock (&priv->buffers_mutex);
  priv->shutting_down = TRUE;
  g_hash_table_foreach (priv->buffers, gst_wl_ref_wl_buffer, NULL);
  g_mutex_unlock (&priv->buffers_mutex);

  g_hash_table_foreach (priv->buffers,
      (GHFunc) gst_wl_buffer_force_release_and_unref, NULL);
  g_hash_table_remove_all (priv->buffers);

  g_array_unref (priv->shm_formats);
  g_array_unref (priv->dmabuf_formats);
  g_array_unref (priv->dmabuf_modifiers);

  g_array_unref (priv->color_transfer_functions);
  g_array_unref (priv->color_primaries);
  g_array_unref (priv->color_alpha_modes);
  g_array_unref (priv->color_coefficients);
  g_array_unref (priv->color_coefficients_range);

  gst_poll_free (priv->wl_fd_poll);
  g_hash_table_unref (priv->buffers);
  g_mutex_clear (&priv->buffers_mutex);
  g_rec_mutex_clear (&priv->sync_mutex);

  g_mutex_clear (&priv->outputs_mutex);
  g_hash_table_unref (priv->outputs);

  if (priv->color)
    wp_color_manager_v1_destroy (priv->color);

  if (priv->color_representation)
    wp_color_representation_manager_v1_destroy (priv->color_representation);

  if (priv->viewporter)
    wp_viewporter_destroy (priv->viewporter);

  if (priv->shm)
    wl_shm_destroy (priv->shm);

  if (priv->dmabuf)
    zwp_linux_dmabuf_v1_destroy (priv->dmabuf);

  if (priv->xdg_wm_base)
    xdg_wm_base_destroy (priv->xdg_wm_base);

  if (priv->fullscreen_shell)
    zwp_fullscreen_shell_v1_release (priv->fullscreen_shell);

  if (priv->single_pixel_buffer)
    wp_single_pixel_buffer_manager_v1_destroy (priv->single_pixel_buffer);

  if (priv->compositor)
    wl_compositor_destroy (priv->compositor);

  if (priv->subcompositor)
    wl_subcompositor_destroy (priv->subcompositor);

  if (priv->registry)
    wl_registry_destroy (priv->registry);

  if (priv->display_wrapper)
    wl_proxy_wrapper_destroy (priv->display_wrapper);

  if (priv->queue)
    wl_event_queue_destroy (priv->queue);

  if (priv->own_display) {
    wl_display_flush (priv->display);
    wl_display_disconnect (priv->display);
  }

  G_OBJECT_CLASS (gst_wl_display_parent_class)->finalize (gobject);
}

static void
shm_format (void *data, struct wl_shm *wl_shm, uint32_t format)
{
  GstWlDisplay *self = data;
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  g_array_append_val (priv->shm_formats, format);
}

static const struct wl_shm_listener shm_listener = {
  shm_format
};

static void
dmabuf_format (void *data, struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
    uint32_t format)
{
}

static void
dmabuf_modifier (void *data, struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
    uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo)
{
  GstWlDisplay *self = data;
  guint64 modifier = (guint64) modifier_hi << 32 | modifier_lo;
  GstVideoFormat gst_format = gst_wl_dmabuf_format_to_video_format (format);
  static uint32_t last_format = 0;

  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  /*
   * Ignore unsupported formats along with implicit modifiers. Implicit
   * modifiers have been source of garbled output for many many years and it
   * was decided that we prefer disabling zero-copy over risking a bad output.
   */
  if (format == DRM_FORMAT_INVALID || modifier == DRM_FORMAT_MOD_INVALID)
    return;

  if (last_format == 0) {
    GST_INFO ("===== All DMA Formats With Modifiers =====");
    GST_INFO ("| Gst Format   | DRM Format              |");
  }

  if (last_format != format) {
    GST_INFO ("|-----------------------------------------");
    last_format = format;
  }

  GST_INFO ("| %-12s | %-23s |",
      (modifier == 0) ? gst_video_format_to_string (gst_format) : "",
      gst_video_dma_drm_fourcc_to_string (format, modifier));

  g_array_append_val (priv->dmabuf_formats, format);
  g_array_append_val (priv->dmabuf_modifiers, modifier);
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
  dmabuf_format,
  dmabuf_modifier,
};

static void
color_supported_intent (void *data,
    struct wp_color_manager_v1 *wp_color_manager_v1, uint32_t render_intent)
{
}

static void
color_supported_feature (void *data,
    struct wp_color_manager_v1 *wp_color_manager_v1, uint32_t feature)
{
  GstWlDisplay *self = data;
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  switch (feature) {
    case WP_COLOR_MANAGER_V1_FEATURE_PARAMETRIC:
      GST_INFO_OBJECT (self, "New_parametric_creator supported");
      priv->color_parametric_creator_supported = TRUE;
      break;
    case WP_COLOR_MANAGER_V1_FEATURE_SET_MASTERING_DISPLAY_PRIMARIES:
      GST_INFO_OBJECT (self, "Mastering Display supported");
      priv->color_mastering_display_supported = TRUE;
      break;
    default:
      break;
  }
}

static void
color_supported_tf_named (void *data,
    struct wp_color_manager_v1 *wp_color_manager_v1, uint32_t tf)
{
  GstWlDisplay *self = data;
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  GST_INFO_OBJECT (self, "Supported transfer function 0x%x", tf);
  g_array_append_val (priv->color_transfer_functions, tf);
}

static void
color_supported_primaries_named (void *data,
    struct wp_color_manager_v1 *wp_color_manager_v1, uint32_t primaries)
{
  GstWlDisplay *self = data;
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  GST_INFO_OBJECT (self, "Supported primaries: 0x%x", primaries);
  g_array_append_val (priv->color_primaries, primaries);
}

static void
color_done (void *data, struct wp_color_manager_v1 *wp_color_manager_v1)
{
}

static const struct wp_color_manager_v1_listener color_listener = {
  .supported_intent = color_supported_intent,
  .supported_feature = color_supported_feature,
  .supported_tf_named = color_supported_tf_named,
  .supported_primaries_named = color_supported_primaries_named,
  .done = color_done,
};

static void
color_representation_supported_alpha_mode (void *data,
    struct wp_color_representation_manager_v1
    *wp_color_representation_manager_v1, uint32_t alpha_mode)
{
  GstWlDisplay *self = data;
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  GST_INFO_OBJECT (self, "Supported alpha mode: 0x%x", alpha_mode);
  g_array_append_val (priv->color_alpha_modes, alpha_mode);
}

static void
color_representation_supported_coefficients_and_ranges (void *data,
    struct wp_color_representation_manager_v1
    *wp_color_representation_manager_v1, uint32_t coefficients, uint32_t range)
{
  GstWlDisplay *self = data;
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  GST_INFO_OBJECT (self, "Supported coefficients and range: 0x%x/0x%x",
      coefficients, range);
  g_array_append_val (priv->color_coefficients, coefficients);
  g_array_append_val (priv->color_coefficients_range, range);
}

static void
color_representation_done (void *data, struct wp_color_representation_manager_v1
    *wp_color_representation_manager_v1)
{
}

static const struct wp_color_representation_manager_v1_listener
    color_representation_listener = {
  .supported_alpha_mode = color_representation_supported_alpha_mode,
  .supported_coefficients_and_ranges =
      color_representation_supported_coefficients_and_ranges,
  .done = color_representation_done,
};

static void
output_geometry (void *data, struct wl_output *wl_output,
    int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
    int32_t subpixel, const char *make, const char *model, int32_t transform)
{
  GstWlOutput *output = GST_WL_OUTPUT (data);
  gst_wl_output_set_geometry (output, x, y, physical_width, physical_height,
      subpixel, make, model, transform);
}

static void
output_mode (void *data, struct wl_output *wl_output,
    uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
  GstWlOutput *output = GST_WL_OUTPUT (data);
  gst_wl_output_set_mode (output, flags, width, height, refresh);
}

static void
output_scale (void *data, struct wl_output *wl_output, int32_t factor)
{
  GstWlOutput *output = GST_WL_OUTPUT (data);
  gst_wl_output_set_scale (output, factor);
}

static void
output_name (void *data, struct wl_output *wl_output, const char *name)
{
  GstWlOutput *output = GST_WL_OUTPUT (data);
  gst_wl_output_set_name (output, name);
}

static void
output_description (void *data, struct wl_output *wl_output,
    const char *description)
{
  GstWlOutput *output = GST_WL_OUTPUT (data);
  gst_wl_output_set_description (output, description);
}

static void
output_done (void *data, struct wl_output *wl_output)
{
  GstWlOutput *output = GST_WL_OUTPUT (data);
  GstWlDisplay *self = g_object_steal_data (G_OBJECT (output), "display");
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);
  const gchar *name = gst_wl_output_get_name (output);

  GST_INFO ("Adding output %s (%p):", name, wl_output);
  GST_INFO ("  Make:       %s", gst_wl_output_get_make (output));
  GST_INFO ("  Model:      %s", gst_wl_output_get_model (output));

#define ARGS(r) (r) /1000 , (r) % 1000
  GST_INFO ("  Mode:       %ix%i px %i.%ifps flags %x",
      gst_wl_output_get_width (output), gst_wl_output_get_height (output),
      ARGS (gst_wl_output_get_refresh (output)),
      gst_wl_output_get_mode_flags (output));
#undef ARGS

  GST_INFO ("  Geometry:   %i,%i %ix%i mm scale %i",
      gst_wl_output_get_x (output), gst_wl_output_get_y (output),
      gst_wl_output_get_physical_width (output),
      gst_wl_output_get_physical_height (output),
      gst_wl_output_get_scale (output));
  GST_INFO ("  Subpixel    %i", gst_wl_output_get_subpixel (output));
  GST_INFO ("  Transform:  %i", gst_wl_output_get_transform (output));
  GST_INFO ("---");

  g_mutex_lock (&priv->outputs_mutex);
  g_hash_table_replace (priv->outputs, g_strdup (name), output);
  g_mutex_unlock (&priv->outputs_mutex);
}

static const struct wl_output_listener output_listener = {
  output_geometry,
  output_mode,
  output_done,
  output_scale,
  output_name,
  output_description,
};

gboolean
gst_wl_display_check_format_for_shm (GstWlDisplay * self,
    const GstVideoInfo * video_info)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);
  GstVideoFormat format = GST_VIDEO_INFO_FORMAT (video_info);
  enum wl_shm_format shm_fmt;
  GArray *formats;
  guint i;

  shm_fmt = gst_video_format_to_wl_shm_format (format);
  if (shm_fmt == (enum wl_shm_format) -1)
    return FALSE;

  formats = priv->shm_formats;
  for (i = 0; i < formats->len; i++) {
    if (g_array_index (formats, uint32_t, i) == shm_fmt)
      return TRUE;
  }

  return FALSE;
}

gboolean
gst_wl_display_check_format_for_dmabuf (GstWlDisplay * self,
    const GstVideoInfoDmaDrm * drm_info)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);
  guint64 modifier = drm_info->drm_modifier;
  guint fourcc = drm_info->drm_fourcc;
  GArray *formats, *modifiers;
  guint i;

  if (!priv->dmabuf)
    return FALSE;

  formats = priv->dmabuf_formats;
  modifiers = priv->dmabuf_modifiers;
  for (i = 0; i < formats->len; i++) {
    if (g_array_index (formats, uint32_t, i) == fourcc) {
      if (g_array_index (modifiers, guint64, i) == modifier) {
        return TRUE;
      }
    }
  }

  return FALSE;
}

static void
handle_xdg_wm_base_ping (void *user_data, struct xdg_wm_base *xdg_wm_base,
    uint32_t serial)
{
  xdg_wm_base_pong (xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
  handle_xdg_wm_base_ping
};

static void
registry_handle_global (void *data, struct wl_registry *registry,
    uint32_t id, const char *interface, uint32_t version)
{
  GstWlDisplay *self = data;
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  if (g_strcmp0 (interface, "wl_compositor") == 0) {
    priv->compositor = wl_registry_bind (registry, id, &wl_compositor_interface,
        MIN (version, 4));
  } else if (g_strcmp0 (interface, "wl_subcompositor") == 0) {
    priv->subcompositor =
        wl_registry_bind (registry, id, &wl_subcompositor_interface, 1);
  } else if (g_strcmp0 (interface, "xdg_wm_base") == 0) {
    priv->xdg_wm_base =
        wl_registry_bind (registry, id, &xdg_wm_base_interface, 1);
    xdg_wm_base_add_listener (priv->xdg_wm_base, &xdg_wm_base_listener, self);
  } else if (g_strcmp0 (interface, "zwp_fullscreen_shell_v1") == 0) {
    priv->fullscreen_shell = wl_registry_bind (registry, id,
        &zwp_fullscreen_shell_v1_interface, 1);
  } else if (g_strcmp0 (interface, "wl_shm") == 0) {
    priv->shm = wl_registry_bind (registry, id, &wl_shm_interface, 1);
    wl_shm_add_listener (priv->shm, &shm_listener, self);
  } else if (g_strcmp0 (interface, "wp_viewporter") == 0) {
    priv->viewporter =
        wl_registry_bind (registry, id, &wp_viewporter_interface, 1);
  } else if (g_strcmp0 (interface, "zwp_linux_dmabuf_v1") == 0) {
    priv->dmabuf =
        wl_registry_bind (registry, id, &zwp_linux_dmabuf_v1_interface, 3);
    zwp_linux_dmabuf_v1_add_listener (priv->dmabuf, &dmabuf_listener, self);
  } else if (g_strcmp0 (interface, "wp_single_pixel_buffer_manager_v1") == 0) {
    priv->single_pixel_buffer =
        wl_registry_bind (registry, id,
        &wp_single_pixel_buffer_manager_v1_interface, 1);
  } else if (g_strcmp0 (interface, wp_color_manager_v1_interface.name) == 0) {
    priv->color = wl_registry_bind (registry, id,
        &wp_color_manager_v1_interface, 1);
    wp_color_manager_v1_add_listener (priv->color, &color_listener, self);
  } else if (g_strcmp0 (interface,
          wp_color_representation_manager_v1_interface.name) == 0) {
    priv->color_representation =
        wl_registry_bind (registry, id,
        &wp_color_representation_manager_v1_interface, 1);
    wp_color_representation_manager_v1_add_listener (priv->color_representation,
        &color_representation_listener, self);
  } else if (g_strcmp0 (interface, "wl_output") == 0) {
    struct wl_output *wl_output =
        wl_registry_bind (registry, id, &wl_output_interface, MIN (version, 4));
    GstWlOutput *output = gst_wl_output_new (wl_output, id);
    g_object_set_data (G_OBJECT (output), "display", self);
    wl_output_add_listener (wl_output, &output_listener, output);
  }
}

static void
registry_handle_global_remove (void *data, struct wl_registry *registry,
    uint32_t name)
{
  GstWlDisplay *self = data;
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  g_mutex_lock (&priv->outputs_mutex);

  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init (&iter, priv->outputs);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GstWlOutput *output = value;

    if (gst_wl_output_get_id (output) == name) {
      g_hash_table_iter_remove (&iter);
      break;
    }
  }

  g_mutex_unlock (&priv->outputs_mutex);
}

static const struct wl_registry_listener registry_listener = {
  registry_handle_global,
  registry_handle_global_remove
};

static gpointer
gst_wl_display_thread_run (gpointer data)
{
  GstWlDisplay *self = data;
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);
  GstPollFD pollfd = GST_POLL_FD_INIT;

  pollfd.fd = wl_display_get_fd (priv->display);
  gst_poll_add_fd (priv->wl_fd_poll, &pollfd);
  gst_poll_fd_ctl_read (priv->wl_fd_poll, &pollfd, TRUE);

  /* main loop */
  while (1) {
    g_rec_mutex_lock (&priv->sync_mutex);
    while (wl_display_prepare_read_queue (priv->display, priv->queue) != 0) {
      if (wl_display_dispatch_queue_pending (priv->display, priv->queue) == -1) {
        g_rec_mutex_unlock (&priv->sync_mutex);
        goto error;
      }
    }
    g_rec_mutex_unlock (&priv->sync_mutex);
    wl_display_flush (priv->display);

    if (gst_poll_wait (priv->wl_fd_poll, GST_CLOCK_TIME_NONE) < 0) {
      gboolean normal = (errno == EBUSY);
      wl_display_cancel_read (priv->display);
      if (normal)
        break;
      else
        goto error;
    }
    if (wl_display_read_events (priv->display) == -1)
      goto error;

    g_rec_mutex_lock (&priv->sync_mutex);
    if (wl_display_dispatch_queue_pending (priv->display, priv->queue) == -1) {
      g_rec_mutex_unlock (&priv->sync_mutex);
      goto error;
    }
    g_rec_mutex_unlock (&priv->sync_mutex);
  }

  return NULL;

error:
  GST_ERROR ("Error communicating with the wayland server");
  return NULL;
}

GstWlDisplay *
gst_wl_display_new (const gchar * name, GError ** error)
{
  struct wl_display *display;

  display = wl_display_connect (name);

  if (!display) {
    *error = g_error_new (g_quark_from_static_string ("GstWlDisplay"), 0,
        "Failed to connect to the wayland display '%s'",
        name ? name : "(default)");
    return NULL;
  } else {
    return gst_wl_display_new_existing (display, TRUE, error);
  }
}

GstWlDisplay *
gst_wl_display_new_existing (struct wl_display *display,
    gboolean take_ownership, GError ** error)
{
  GstWlDisplay *self;
  GstWlDisplayPrivate *priv;
  GError *err = NULL;
  gint i;

  g_return_val_if_fail (display != NULL, NULL);

  self = g_object_new (GST_TYPE_WL_DISPLAY, NULL);
  priv = gst_wl_display_get_instance_private (self);
  priv->display = display;
  priv->display_wrapper = wl_proxy_create_wrapper (display);
  priv->own_display = take_ownership;

#ifdef HAVE_WL_EVENT_QUEUE_NAME
  priv->queue = wl_display_create_queue_with_name (priv->display,
      "GStreamer display queue");
#else
  priv->queue = wl_display_create_queue (priv->display);
#endif
  wl_proxy_set_queue ((struct wl_proxy *) priv->display_wrapper, priv->queue);
  priv->registry = wl_display_get_registry (priv->display_wrapper);
  wl_registry_add_listener (priv->registry, &registry_listener, self);

  /* we need exactly 2 roundtrips to discover global objects and their state */
  for (i = 0; i < 2; i++) {
    if (wl_display_roundtrip_queue (priv->display, priv->queue) < 0) {
      *error = g_error_new (g_quark_from_static_string ("GstWlDisplay"), 0,
          "Error communicating with the wayland display");
      g_object_unref (self);
      return NULL;
    }
  }

  /* verify we got all the required interfaces */
#define VERIFY_INTERFACE_EXISTS(var, interface) \
  if (!priv->var) { \
    g_set_error (error, g_quark_from_static_string ("GstWlDisplay"), 0, \
        "Could not bind to " interface ". Either it is not implemented in " \
        "the compositor, or the implemented version doesn't match"); \
    g_object_unref (self); \
    return NULL; \
  }

  VERIFY_INTERFACE_EXISTS (compositor, "wl_compositor");
  VERIFY_INTERFACE_EXISTS (subcompositor, "wl_subcompositor");
  VERIFY_INTERFACE_EXISTS (shm, "wl_shm");

#undef VERIFY_INTERFACE_EXISTS

  /* We make the viewporter optional even though it may cause bad display.
   * This is so one can test wayland display on older compositor or on
   * compositor that don't implement this extension. */
  if (!priv->viewporter) {
    g_warning ("Wayland compositor is missing the ability to scale, video "
        "display may not work properly.");
  }

  if (!priv->dmabuf) {
    g_warning ("Could not bind to zwp_linux_dmabuf_v1");
  }

  if (!priv->xdg_wm_base && !priv->fullscreen_shell) {
    /* If wl_surface and wl_display are passed via GstContext
     * xdg_shell and zwp_fullscreen_shell are not used.
     * In this case is correct to continue.
     */
    g_warning ("Could not bind to either xdg_wm_base or zwp_fullscreen_shell, "
        "video display may not work properly.");
  }

  priv->thread = g_thread_try_new ("GstWlDisplay", gst_wl_display_thread_run,
      self, &err);
  if (err) {
    g_propagate_prefixed_error (error, err,
        "Failed to start thread for the display's events");
    g_object_unref (self);
    return NULL;
  }

  return self;
}

void
gst_wl_display_register_buffer (GstWlDisplay * self, gpointer gstmem,
    gpointer wlbuffer)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  g_assert (!priv->shutting_down);

  GST_TRACE_OBJECT (self, "registering GstWlBuffer %p to GstMem %p",
      wlbuffer, gstmem);

  g_mutex_lock (&priv->buffers_mutex);
  g_hash_table_replace (priv->buffers, gstmem, wlbuffer);
  g_mutex_unlock (&priv->buffers_mutex);
}

gpointer
gst_wl_display_lookup_buffer (GstWlDisplay * self, gpointer gstmem)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);
  gpointer wlbuffer;

  g_mutex_lock (&priv->buffers_mutex);
  wlbuffer = g_hash_table_lookup (priv->buffers, gstmem);
  g_mutex_unlock (&priv->buffers_mutex);
  return wlbuffer;
}

void
gst_wl_display_unregister_buffer (GstWlDisplay * self, gpointer gstmem)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  GST_TRACE_OBJECT (self, "unregistering GstWlBuffer owned by %p", gstmem);

  g_mutex_lock (&priv->buffers_mutex);
  if (G_LIKELY (!priv->shutting_down))
    g_hash_table_remove (priv->buffers, gstmem);
  g_mutex_unlock (&priv->buffers_mutex);
}

/* gst_wl_display_sync
 *
 * A syncronized version of `wl_display_sink` that ensures that the
 * callback will not be dispatched before the listener has been attached.
 */
struct wl_callback *
gst_wl_display_sync (GstWlDisplay * self,
    const struct wl_callback_listener *listener, gpointer data)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);
  struct wl_callback *callback;

  g_rec_mutex_lock (&priv->sync_mutex);

  callback = wl_display_sync (priv->display_wrapper);
  if (callback && listener)
    wl_callback_add_listener (callback, listener, data);

  g_rec_mutex_unlock (&priv->sync_mutex);

  return callback;
}

/* gst_wl_display_object_destroy
 *
 * A syncronized version of `xxx_destroy` that ensures that the
 * once this function returns, the destroy_func will either have already completed,
 * or will never be called.
 */
void
gst_wl_display_object_destroy (GstWlDisplay * self,
    gpointer * object, GDestroyNotify destroy_func)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  g_rec_mutex_lock (&priv->sync_mutex);

  if (*object) {
    destroy_func (*object);
    *object = NULL;
  }

  g_rec_mutex_unlock (&priv->sync_mutex);
}

/* gst_wl_display_callback_destroy
 *
 * A syncronized version of `wl_callback_destroy` that ensures that the
 * once this function returns, the callback will either have already completed,
 * or will never be called.
 */
void
gst_wl_display_callback_destroy (GstWlDisplay * self,
    struct wl_callback **callback)
{
  gst_wl_display_object_destroy (self, (gpointer *) callback,
      (GDestroyNotify) wl_callback_destroy);
}

struct wl_display *
gst_wl_display_get_display (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->display;
}

struct wl_event_queue *
gst_wl_display_get_event_queue (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->queue;
}

struct wl_compositor *
gst_wl_display_get_compositor (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->compositor;
}

struct wl_subcompositor *
gst_wl_display_get_subcompositor (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->subcompositor;
}

struct xdg_wm_base *
gst_wl_display_get_xdg_wm_base (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->xdg_wm_base;
}

struct zwp_fullscreen_shell_v1 *
gst_wl_display_get_fullscreen_shell_v1 (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->fullscreen_shell;
}

struct wp_viewporter *
gst_wl_display_get_viewporter (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->viewporter;
}

struct wl_shm *
gst_wl_display_get_shm (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->shm;
}

GArray *
gst_wl_display_get_shm_formats (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->shm_formats;
}

struct zwp_linux_dmabuf_v1 *
gst_wl_display_get_dmabuf_v1 (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->dmabuf;
}

GArray *
gst_wl_display_get_dmabuf_modifiers (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->dmabuf_modifiers;
}

GArray *
gst_wl_display_get_dmabuf_formats (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->dmabuf_formats;
}

/**
 * gst_wl_display_fill_shm_format_list:
 * @self: A #GstWlDisplay
 * @format_list: A #GValue of type #GST_TYPE_LIST
 *
 * Append supported SHM formats to a given list, suitable for use with the "format" caps value.
 *
 * Since: 1.26
 */
void
gst_wl_display_fill_shm_format_list (GstWlDisplay * self, GValue * format_list)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);
  GValue value = G_VALUE_INIT;
  guint fmt;
  GstVideoFormat gfmt;

  for (gint i = 0; i < priv->shm_formats->len; i++) {
    fmt = g_array_index (priv->shm_formats, uint32_t, i);
    gfmt = gst_wl_shm_format_to_video_format (fmt);
    if (gfmt != GST_VIDEO_FORMAT_UNKNOWN) {
      g_value_init (&value, G_TYPE_STRING);
      g_value_set_static_string (&value, gst_video_format_to_string (gfmt));
      gst_value_list_append_and_take_value (format_list, &value);
    }
  }
}

/**
 * gst_wl_display_fill_drm_format_list:
 * @self: A #GstWlDisplay
 * @format_list: A #GValue of type #GST_TYPE_LIST
 *
 * Append supported DRM formats to a given list, suitable for use with the "drm-format" caps value.
 *
 * Since: 1.26
 */
void
gst_wl_display_fill_dmabuf_format_list (GstWlDisplay * self,
    GValue * format_list)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);
  GValue value = G_VALUE_INIT;
  guint fmt;
  guint64 mod;

  for (gint i = 0; i < priv->dmabuf_formats->len; i++) {
    fmt = g_array_index (priv->dmabuf_formats, uint32_t, i);
    mod = g_array_index (priv->dmabuf_modifiers, guint64, i);
    g_value_init (&value, G_TYPE_STRING);
    g_value_take_string (&value, gst_video_dma_drm_fourcc_to_string (fmt, mod));
    gst_value_list_append_and_take_value (format_list, &value);
  }
}

struct wp_single_pixel_buffer_manager_v1 *
gst_wl_display_get_single_pixel_buffer_manager_v1 (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->single_pixel_buffer;
}

gboolean
gst_wl_display_has_own_display (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->own_display;
}

/**
 * gst_wl_display_get_color_manager_v1:
 * @self: A #GstWlDisplay
 *
 * Returns: (transfer none): The color manager global or %NULL
 *
 * Since: 1.28
 */
struct wp_color_manager_v1 *
gst_wl_display_get_color_manager_v1 (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->color;
}

/**
 * gst_wl_display_get_color_representation_manager_v1:
 * @self: A #GstWlDisplay
 *
 * Returns: (transfer none): The color representation global or %NULL
 *
 * Since: 1.28
 */
struct wp_color_representation_manager_v1 *
gst_wl_display_get_color_representation_manager_v1 (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->color_representation;
}

/**
 * gst_wl_display_is_color_parametric_creator_supported:
 * @self: A #GstWlDisplay
 *
 * Returns: %TRUE if the compositor supports parametric image descriptions
 *
 * Since: 1.28
 */
gboolean
gst_wl_display_is_color_parametric_creator_supported (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->color_parametric_creator_supported;
}

/**
 * gst_wl_display_is_color_mastering_display_supported:
 * @self: A #GstWlDisplay
 *
 * Returns: %TRUE if the compositor supports mastering display primaries
 *          image descriptions
 *
 * Since: 1.28
 */
gboolean
gst_wl_display_is_color_mastering_display_supported (GstWlDisplay * self)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);

  return priv->color_mastering_display_supported;
}

/**
 * gst_wl_display_is_color_transfer_function_supported:
 * @self: A #GstWlDisplay
 *
 * Returns: %TRUE if the compositor supports @transfer_function
 *
 * Since: 1.28
 */
gboolean
gst_wl_display_is_color_transfer_function_supported (GstWlDisplay * self,
    uint32_t transfer_function)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);
  guint i;

  /* A value of 0 is invalid and will never be present in the list of enums. */
  if (transfer_function == 0)
    return FALSE;

  for (i = 0; i < priv->color_transfer_functions->len; i++) {
    uint32_t candidate =
        g_array_index (priv->color_transfer_functions, uint32_t, i);

    if (candidate == transfer_function)
      return TRUE;
  }

  return FALSE;
}

/**
 * gst_wl_display_are_color_primaries_supported:
 * @self: A #GstWlDisplay
 *
 * Returns: %TRUE if the compositor supports @primaries
 *
 * Since: 1.28
 */
gboolean
gst_wl_display_are_color_primaries_supported (GstWlDisplay * self,
    uint32_t primaries)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);
  guint i;

  /* A value of 0 is invalid and will never be present in the list of enums. */
  if (primaries == 0)
    return FALSE;

  for (i = 0; i < priv->color_primaries->len; i++) {
    uint32_t candidate = g_array_index (priv->color_primaries, uint32_t, i);

    if (candidate == primaries)
      return TRUE;
  }

  return FALSE;
}

/**
 * gst_wl_display_is_color_alpha_mode_supported:
 * @self: A #GstWlDisplay
 *
 * Returns: %TRUE if the compositor supports @alpha_mode
 *
 * Since: 1.28
 */
gboolean
gst_wl_display_is_color_alpha_mode_supported (GstWlDisplay * self,
    uint32_t alpha_mode)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);
  guint i;

  for (i = 0; i < priv->color_alpha_modes->len; i++) {
    uint32_t candidate = g_array_index (priv->color_alpha_modes, uint32_t, i);

    if (candidate == alpha_mode)
      return TRUE;
  }

  return FALSE;
}

/**
 * gst_wl_display_are_color_coefficients_supported:
 * @self: A #GstWlDisplay
 *
 * Returns: %TRUE if the compositor supports the combination of @coefficients and @range
 *
 * Since: 1.28
 */
gboolean
gst_wl_display_are_color_coefficients_supported (GstWlDisplay * self,
    uint32_t coefficients, uint32_t range)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);
  guint i;

  /* A value of 0 is invalid and will never be present in the list of enums. */
  if (coefficients == 0 || range == 0)
    return FALSE;

  for (i = 0; i < priv->color_coefficients->len; i++) {
    uint32_t candidate = g_array_index (priv->color_coefficients, uint32_t, i);
    uint32_t candidate_range =
        g_array_index (priv->color_coefficients_range, uint32_t, i);

    if (candidate == coefficients && candidate_range == range)
      return TRUE;
  }

  return FALSE;
}

/**
* gst_wl_display_get_output_by_name:
* @self: A #GstWlDisplay
* @output_name: Name of the output
*
* Lookup for a wl_output with the specified name.
*
* Returns: (transfer full): A #GstWlOutput or %NULL if not found.
*
* Since: 1.28
*/
GstWlOutput *
gst_wl_display_get_output_by_name (GstWlDisplay * self,
    const gchar * output_name)
{
  GstWlDisplayPrivate *priv = gst_wl_display_get_instance_private (self);
  GstWlOutput *output;

  g_mutex_lock (&priv->outputs_mutex);
  output = GST_WL_OUTPUT (g_hash_table_lookup (priv->outputs, output_name));
  if (output)
    g_object_ref (output);
  g_mutex_unlock (&priv->outputs_mutex);

  return output;
}
