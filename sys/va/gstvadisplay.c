/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#include "gstvadisplay.h"
#include "gstvaprofile.h"
#include "gstvavideoformat.h"

GST_DEBUG_CATEGORY (gst_va_display_debug);
#define GST_CAT_DEFAULT gst_va_display_debug

typedef struct _GstVaDisplayPrivate GstVaDisplayPrivate;
struct _GstVaDisplayPrivate
{
  GRecMutex lock;
  VADisplay display;

  gboolean foreign;
  gboolean init;
};

#define gst_va_display_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVaDisplay, gst_va_display, GST_TYPE_OBJECT,
    G_ADD_PRIVATE (GstVaDisplay);
    GST_DEBUG_CATEGORY_INIT (gst_va_display_debug, "vadisplay", 0,
        "VA Display"));
enum
{
  PROP_VA_DISPLAY = 1,
  N_PROPERTIES
};

static GParamSpec *g_properties[N_PROPERTIES];

#define GET_PRIV(obj) gst_va_display_get_instance_private (GST_VA_DISPLAY (obj))

static gboolean
_gst_va_display_driver_filter (VADisplay display)
{
  const char *vendor = vaQueryVendorString (display);

  GST_INFO ("VA-API driver vendor: %s", vendor);

  /* XXX(victor): driver quirks & driver whitelist */

  return TRUE;
}

static void
gst_va_display_set_display (GstVaDisplay * self, gpointer display)
{
  GstVaDisplayPrivate *priv = GET_PRIV (self);

  if (!display)
    return;

  if (vaDisplayIsValid (display) == 0) {
    GST_WARNING_OBJECT (self,
        "User's VA display is invalid. An internal one will be tried.");
    return;
  }

  if (!_gst_va_display_driver_filter (display))
    return;

  priv->display = display;
  priv->foreign = TRUE;

  /* assume driver is already initialized */
  priv->init = TRUE;
}

static void
gst_va_display_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaDisplay *self = GST_VA_DISPLAY (object);

  switch (prop_id) {
    case PROP_VA_DISPLAY:{
      gpointer display = g_value_get_pointer (value);
      gst_va_display_set_display (self, display);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_va_display_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVaDisplayPrivate *priv = GET_PRIV (object);

  switch (prop_id) {
    case PROP_VA_DISPLAY:
      g_value_set_pointer (value, priv->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_va_display_constructed (GObject * object)
{
  GstVaDisplay *self = GST_VA_DISPLAY (object);
  GstVaDisplayPrivate *priv = GET_PRIV (object);
  GstVaDisplayClass *klass = GST_VA_DISPLAY_GET_CLASS (object);

  if (!priv->display && klass->create_va_display)
    priv->display = klass->create_va_display (self);

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
gst_va_display_dispose (GObject * object)
{
  GstVaDisplayPrivate *priv = GET_PRIV (object);

  if (priv->display && !priv->foreign)
    vaTerminate (priv->display);
  priv->display = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_va_display_finalize (GObject * object)
{
  GstVaDisplayPrivate *priv = GET_PRIV (object);

  g_rec_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_va_display_class_init (GstVaDisplayClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_va_display_set_property;
  gobject_class->get_property = gst_va_display_get_property;
  gobject_class->constructed = gst_va_display_constructed;
  gobject_class->dispose = gst_va_display_dispose;
  gobject_class->finalize = gst_va_display_finalize;

  g_properties[PROP_VA_DISPLAY] =
      g_param_spec_pointer ("va-display", "VADisplay", "VA Display handler",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPERTIES, g_properties);
}

static void
gst_va_display_init (GstVaDisplay * self)
{
  GstVaDisplayPrivate *priv = GET_PRIV (self);

  g_rec_mutex_init (&priv->lock);
}

void
gst_va_display_lock (GstVaDisplay * self)
{
  GstVaDisplayPrivate *priv;

  g_return_if_fail (GST_IS_VA_DISPLAY (self));

  priv = GET_PRIV (self);

  g_rec_mutex_lock (&priv->lock);
}

void
gst_va_display_unlock (GstVaDisplay * self)
{
  GstVaDisplayPrivate *priv;

  g_return_if_fail (GST_IS_VA_DISPLAY (self));

  priv = GET_PRIV (self);

  g_rec_mutex_unlock (&priv->lock);
}

#ifndef GST_DISABLE_GST_DEBUG
static gchar *
_strip_msg (const char *message)
{
  gchar *msg = g_strdup (message);
  if (!msg)
    return NULL;
  return g_strstrip (msg);
}

static void
_va_warning (gpointer object, const char *message)
{
  GstVaDisplay *self = GST_VA_DISPLAY (object);
  gchar *msg;

  if ((msg = _strip_msg (message))) {
    GST_WARNING_OBJECT (self, "VA error: %s", msg);
    g_free (msg);
  }
}

static void
_va_info (gpointer object, const char *message)
{
  GstVaDisplay *self = GST_VA_DISPLAY (object);
  gchar *msg;

  if ((msg = _strip_msg (message))) {
    GST_INFO_OBJECT (self, "VA info: %s", msg);
    g_free (msg);
  }
}
#endif

/**
 * gst_va_display_initialize:
 * @self: a #GstVaDisplay
 *
 * If the display is set by the user (foreign) it is assumed that the
 * driver is already initialized, thus this function is noop.
 *
 * If the display is opened internally, this function will initialize
 * the driver and it will set driver's message callbacks.
 *
 * NOTE: this function is supposed to be private, only used by
 * GstVaDisplay descendants.
 *
 * Returns: %TRUE if the VA driver can be initialized; %FALSE
 *     otherwise
 **/
gboolean
gst_va_display_initialize (GstVaDisplay * self)
{
  GstVaDisplayPrivate *priv;
  VAStatus status;
  int major_version = -1, minor_version = -1;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), FALSE);

  priv = GET_PRIV (self);

  if (priv->init)
    return TRUE;

  if (!priv->display)
    return FALSE;

#ifndef GST_DISABLE_GST_DEBUG
  vaSetErrorCallback (priv->display, _va_warning, self);
  vaSetInfoCallback (priv->display, _va_info, self);
#endif

  status = vaInitialize (priv->display, &major_version, &minor_version);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaInitialize: %s", vaErrorStr (status));
    return FALSE;
  }

  GST_INFO_OBJECT (self, "VA-API version %d.%d", major_version, minor_version);

  priv->init = TRUE;

  if (!_gst_va_display_driver_filter (priv->display))
    return FALSE;

  return TRUE;
}

VADisplay
gst_va_display_get_va_dpy (GstVaDisplay * self)
{
  VADisplay dpy = 0;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), 0);

  g_object_get (self, "va-display", &dpy, NULL);
  return dpy;
}

GArray *
gst_va_display_get_profiles (GstVaDisplay * self, guint32 codec,
    VAEntrypoint entrypoint)
{
  GArray *ret = NULL;
  VADisplay dpy;
  VAEntrypoint *entrypoints;
  VAProfile *profiles;
  VAStatus status;
  gint i, j, num_entrypoints = 0, num_profiles = 0;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), NULL);

  dpy = gst_va_display_get_va_dpy (self);

  gst_va_display_lock (self);
  num_profiles = vaMaxNumProfiles (dpy);
  num_entrypoints = vaMaxNumEntrypoints (dpy);
  gst_va_display_unlock (self);

  profiles = g_new (VAProfile, num_profiles);
  entrypoints = g_new (VAEntrypoint, num_entrypoints);

  gst_va_display_lock (self);
  status = vaQueryConfigProfiles (dpy, profiles, &num_profiles);
  gst_va_display_unlock (self);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaQueryConfigProfile: %s", vaErrorStr (status));
    goto bail;
  }

  for (i = 0; i < num_profiles; i++) {
    if (codec != gst_va_profile_codec (profiles[i]))
      continue;

    gst_va_display_lock (self);
    status = vaQueryConfigEntrypoints (dpy, profiles[i], entrypoints,
        &num_entrypoints);
    gst_va_display_unlock (self);
    if (status != VA_STATUS_SUCCESS) {
      GST_ERROR ("vaQueryConfigEntrypoints: %s", vaErrorStr (status));
      goto bail;
    }

    for (j = 0; j < num_entrypoints; j++) {
      if (entrypoints[j] == entrypoint) {
        if (!ret)
          ret = g_array_new (FALSE, FALSE, sizeof (VAProfile));
        g_array_append_val (ret, profiles[i]);
        break;
      }
    }
  }

bail:
  g_free (entrypoints);
  g_free (profiles);
  return ret;
}

GArray *
gst_va_display_get_image_formats (GstVaDisplay * self)
{
  GArray *ret = NULL;
  GstVideoFormat format;
  VADisplay dpy = gst_va_display_get_va_dpy (self);
  VAImageFormat *va_formats;
  VAStatus status;
  int i, max, num = 0;

  gst_va_display_lock (self);
  max = vaMaxNumImageFormats (dpy);
  gst_va_display_unlock (self);
  if (max == 0)
    return NULL;

  va_formats = g_new (VAImageFormat, max);

  gst_va_display_lock (self);
  status = vaQueryImageFormats (dpy, va_formats, &num);
  gst_va_display_unlock (self);

  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaQueryImageFormats: %s", vaErrorStr (status));
    goto bail;
  }

  ret = g_array_sized_new (FALSE, FALSE, sizeof (GstVideoFormat), num);
  for (i = 0; i < num; i++) {
    format = gst_va_video_format_from_va_image_format (&va_formats[i]);
    if (format != GST_VIDEO_FORMAT_UNKNOWN)
      g_array_append_val (ret, format);
  }

  if (ret->len == 0) {
    g_array_unref (ret);
    ret = NULL;
  }

bail:
  g_free (va_formats);
  return ret;
}
