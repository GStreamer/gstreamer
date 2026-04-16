/* GStreamer
 * Copyright (C) 2019 Josh Matthews <josh@joshmatthews.net>
 *
 * avfdeviceprovider.c: AVF device probing and monitoring
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
#include "config.h"
#endif

#include <TargetConditionals.h>
#import <AVFoundation/AVFoundation.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#define GST_AVF_NO_DISPLAY_ID kCGNullDirectDisplay
#else
#define GST_AVF_NO_DISPLAY_ID 0
#endif
#include <dispatch/dispatch.h>
#include "avfvideosrc.h"
#include "avfdeviceprovider.h"
#include "helpers.h"

#include <string.h>

#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC (gst_avf_device_provider_debug);
#define GST_CAT_DEFAULT gst_avf_device_provider_debug

typedef struct
{
  GstDevice *device;
  GstDevice *changed_device;
} GstAvfDeviceChange;

typedef struct _GstAVFDeviceProviderPrivate
{
  GMutex           lock;
  dispatch_queue_t refresh_queue;
  id               camera_connected_observer;
  id               camera_disconnected_observer;
  gboolean         watchers_active;
#if TARGET_OS_OSX
  gboolean         screen_callback_registered;
#endif
} GstAVFDeviceProviderPrivate;

#ifndef GST_DISABLE_GST_DEBUG
#define GST_AVF_DEVICE_PROVIDER_DEBUG_DEVICE(self, action, device) \
    G_STMT_START { \
      if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= \
          GST_LEVEL_DEBUG) { \
        char *name = gst_device_get_display_name (device); \
        GST_DEBUG_OBJECT (self, "Device %s: \"%s\" (%s)", action, name, \
            GST_AVF_DEVICE (device)->unique_id); \
        g_free (name); \
      } \
    } G_STMT_END
#else
#define GST_AVF_DEVICE_PROVIDER_DEBUG_DEVICE(self, action, device) \
    G_STMT_START { } G_STMT_END
#endif

static GstDevice *gst_avf_device_new (const gchar * device_name,
                                      const gchar * unique_id,
                                      GstCaps * caps,
                                      GstAvfDeviceType type,
                                      GstStructure *props);
G_DEFINE_TYPE_WITH_PRIVATE (GstAVFDeviceProvider, gst_avf_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

GST_DEVICE_PROVIDER_REGISTER_DEFINE (avfdeviceprovider, "avfdeviceprovider",
    GST_RANK_PRIMARY, GST_TYPE_AVF_DEVICE_PROVIDER);

static gboolean gst_avf_device_provider_start (GstDeviceProvider * provider);
static void gst_avf_device_provider_stop (GstDeviceProvider * provider);
static GList *gst_avf_device_provider_probe (GstDeviceProvider * provider);
static gboolean gst_avf_device_provider_probe_internal
    (GstAVFDeviceProvider * self, GList ** out_devices);
static void gst_avf_device_provider_schedule_refresh
    (GstAVFDeviceProvider * self, guint display_id,
    guint display_reconfiguration_flags);
static void gst_avf_device_provider_refresh (GstAVFDeviceProvider * self,
    guint display_id, guint display_reconfiguration_flags);
static void gst_avf_device_provider_dispose (GObject * object);
static void gst_avf_device_provider_finalize (GObject * object);

#if TARGET_OS_OSX
static void gst_avf_device_provider_display_reconfigured
    (CGDirectDisplayID display_id, CGDisplayChangeSummaryFlags flags,
    void *user_info);
#endif

static void
gst_avf_device_provider_class_init (GstAVFDeviceProviderClass * klass)
{
  GstDeviceProviderClass *dm_class = GST_DEVICE_PROVIDER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  dm_class->start = gst_avf_device_provider_start;
  dm_class->stop = gst_avf_device_provider_stop;
  dm_class->probe = gst_avf_device_provider_probe;
  object_class->dispose = gst_avf_device_provider_dispose;
  object_class->finalize = gst_avf_device_provider_finalize;

  gst_avf_video_src_debug_init ();
  GST_DEBUG_CATEGORY_INIT (gst_avf_device_provider_debug, "avfdeviceprovider",
      0, "AVF device provider");

  gst_device_provider_class_set_static_metadata (dm_class,
                                                 "AVF Device Provider", "Source/Video/Monitor",
                                                 "List and provide AVF source devices",
                                                 "Josh Matthews <josh@joshmatthews.net>");
}

static void
gst_avf_device_provider_init (GstAVFDeviceProvider * self)
{
  GstAVFDeviceProviderPrivate *priv =
      gst_avf_device_provider_get_instance_private (self);

  g_mutex_init (&priv->lock);
  priv->refresh_queue =
      dispatch_queue_create ("org.freedesktop.gstreamer.avfdeviceprovider",
      DISPATCH_QUEUE_SERIAL);
}

static void
gst_avf_device_provider_dispose (GObject * object)
{
  gst_avf_device_provider_stop (GST_DEVICE_PROVIDER (object));

  G_OBJECT_CLASS (gst_avf_device_provider_parent_class)->dispose (object);
}

static void
gst_avf_device_provider_finalize (GObject * object)
{
  GstAVFDeviceProvider *self = GST_AVF_DEVICE_PROVIDER (object);
  GstAVFDeviceProviderPrivate *priv =
      gst_avf_device_provider_get_instance_private (self);

  priv->refresh_queue = NULL;
  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (gst_avf_device_provider_parent_class)->finalize (object);
}

static GstStructure *
gst_av_capture_device_get_props (AVCaptureDevice *device)
{
  char *unique_id, *model_id;
  GstStructure *props = gst_structure_new_empty ("avf-proplist");

  unique_id = g_strdup ([[device uniqueID] UTF8String]);
  model_id = g_strdup ([[device modelID] UTF8String]);

  gst_structure_set (props,
    "device.api", G_TYPE_STRING, "avf",
    "avf.unique_id", G_TYPE_STRING, unique_id,
    "avf.model_id", G_TYPE_STRING, model_id,
    "avf.has_flash", G_TYPE_BOOLEAN, [device hasFlash],
    "avf.has_torch", G_TYPE_BOOLEAN, [device hasTorch],
  NULL);

  g_free (unique_id);
  g_free (model_id);

#if !TARGET_OS_WATCH
#if MAC_OS_X_VERSION_MAX_ALLOWED < 140000
  if (__builtin_available (macOS 10.9, iOS 14.0, tvOS 17.0, *)) {
#else
  if (__builtin_available (macOS 10.9, iOS 14.0, tvOS 17.0, visionOS 2.1, *)) {
#endif
    char *manufacturer = g_strdup ([[device manufacturer] UTF8String]);
    gst_structure_set (props,
      "avf.manufacturer", G_TYPE_STRING, manufacturer,
    NULL);
    g_free (manufacturer);
  }
#endif

  return props;
}

#if TARGET_OS_OSX
static GstStructure *
gst_avf_screen_get_props (CGDirectDisplayID display_id, const gchar * unique_id)
{
  GstStructure *props = gst_structure_new_empty ("avf-proplist");

  gst_structure_set (props,
      "device.api", G_TYPE_STRING, "avf",
      "avf.unique_id", G_TYPE_STRING, unique_id,
      "avf.capture_screen", G_TYPE_BOOLEAN, TRUE,
      "avf.display_id", G_TYPE_UINT64, (guint64) display_id,
      NULL);

  return props;
}

static GstCaps *
gst_avf_screen_get_caps (NSScreen * screen, AVCaptureVideoDataOutput * output)
{
  CGDirectDisplayID display_id = gst_avf_screen_get_display_id (screen);
  gdouble scale = [screen backingScaleFactor];

  return gst_av_capture_screen_get_caps (display_id, scale, output);
}
#endif

static gboolean
gst_avf_device_provider_probe_internal (GstAVFDeviceProvider * self,
    GList ** out_devices)
{
  GList *result = NULL;
  AVCaptureVideoDataOutput *output = [[AVCaptureVideoDataOutput alloc] init];

  if (output == nil) {
    GST_WARNING_OBJECT (self,
        "Could not create AVCaptureVideoDataOutput for AVF device probing");
    return FALSE;
  }

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  NSArray *devices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
G_GNUC_END_IGNORE_DEPRECATIONS
  for (AVCaptureDevice *device in devices) {
    g_assert (device != nil);

    GstCaps *caps = gst_av_capture_device_get_caps (device, output,
        GST_AVF_VIDEO_SOURCE_ORIENTATION_DEFAULT);
    GstStructure *props = gst_av_capture_device_get_props (device);
    const gchar *unique_id = gst_structure_get_string (props, "avf.unique_id");
    const gchar *deviceName = [[device localizedName] UTF8String];
    GstDevice *gst_device = gst_avf_device_new (deviceName, unique_id, caps,
        GST_AVF_DEVICE_TYPE_VIDEO_SOURCE, props);

    result = g_list_prepend (result, gst_object_ref_sink (gst_device));

    gst_structure_free (props);
    gst_caps_unref (caps);
  }

#if TARGET_OS_OSX
  for (NSScreen *screen in [NSScreen screens]) {
    CGDirectDisplayID display_id = gst_avf_screen_get_display_id (screen);
    gchar *unique_id = gst_avf_screen_dup_unique_id (display_id);
    gchar *display_name;
    GstCaps *caps;
    GstStructure *props;
    GstDevice *gst_device;

    if (unique_id == NULL)
      continue;

    display_name = gst_avf_screen_dup_name (screen, display_id);
    caps = gst_avf_screen_get_caps (screen, output);
    if (caps == NULL) {
      GST_WARNING_OBJECT (self,
          "Could not create caps for display \"%s\" (%u), skipping",
          display_name, display_id);
      g_free (display_name);
      g_free (unique_id);
      continue;
    }

    props = gst_avf_screen_get_props (display_id, unique_id);
    gst_device = gst_avf_device_new (display_name, unique_id, caps,
        GST_AVF_DEVICE_TYPE_SCREEN_SOURCE, props);

    result = g_list_prepend (result, gst_object_ref_sink (gst_device));

    g_free (display_name);
    g_free (unique_id);
    gst_structure_free (props);
    gst_caps_unref (caps);
  }
#endif

  result = g_list_reverse (result);

  *out_devices = result;

  return TRUE;
}

static GList *
gst_avf_device_provider_probe (GstDeviceProvider * provider)
{
  GList *result = NULL;

  if (!gst_avf_device_provider_probe_internal (GST_AVF_DEVICE_PROVIDER
          (provider), &result))
    return NULL;

  return result;
}

static gboolean
gst_avf_device_has_same_identity (GstDevice * current, GstDevice * fresh)
{
  GstAvfDevice *current_avf = GST_AVF_DEVICE (current);
  GstAvfDevice *fresh_avf = GST_AVF_DEVICE (fresh);

  return current_avf->type == fresh_avf->type
      && g_strcmp0 (current_avf->unique_id, fresh_avf->unique_id) == 0;
}

static GstDevice *
gst_avf_device_find_identity_match (GList * devices, GstDevice * target)
{
  GList *iter;

  for (iter = devices; iter != NULL; iter = g_list_next (iter)) {
    GstDevice *candidate = GST_DEVICE (iter->data);

    if (gst_avf_device_has_same_identity (candidate, target))
      return candidate;
  }

  return NULL;
}

#if TARGET_OS_OSX
static gboolean
gst_avf_device_has_screen_reconfig
    (guint reconfigured_display_id,
    guint display_reconfiguration_flags, GstDevice * device)
{
  GstStructure *props;
  guint64 display_id = 0;

  if (GST_AVF_DEVICE (device)->type != GST_AVF_DEVICE_TYPE_SCREEN_SOURCE)
    return FALSE;

  if (display_reconfiguration_flags == 0)
    return FALSE;

  /* Some display reconfiguration notifications may not identify one display. */
  if (reconfigured_display_id == kCGNullDirectDisplay)
    return TRUE;

  props = gst_device_get_properties (device);
  g_assert (props != NULL);
  if (!gst_structure_get_uint64 (props, "avf.display_id", &display_id)
      || display_id > G_MAXUINT) {
    g_clear_pointer (&props, gst_structure_free);
    return FALSE;
  }
  g_clear_pointer (&props, gst_structure_free);

  return (guint) display_id == reconfigured_display_id;
}
#endif

static gboolean
gst_avf_device_representation_changed
    (guint reconfigured_display_id,
    guint display_reconfiguration_flags, GstDevice * current, GstDevice * fresh)
{
  gchar *current_name, *fresh_name;
  GstCaps *current_caps, *fresh_caps;
  GstStructure *current_props, *fresh_props;
  gboolean changed;

  if (g_strcmp0 (gst_device_get_device_class (current),
          gst_device_get_device_class (fresh)) != 0)
    return TRUE;

  current_name = gst_device_get_display_name (current);
  fresh_name = gst_device_get_display_name (fresh);
  changed = g_strcmp0 (current_name, fresh_name) != 0;
  g_free (current_name);
  g_free (fresh_name);
  if (changed)
    return TRUE;

  current_caps = gst_device_get_caps (current);
  fresh_caps = gst_device_get_caps (fresh);
  changed = !gst_caps_is_equal (current_caps, fresh_caps);
  g_clear_pointer (&current_caps, gst_caps_unref);
  g_clear_pointer (&fresh_caps, gst_caps_unref);
  if (changed)
    return TRUE;

  current_props = gst_device_get_properties (current);
  fresh_props = gst_device_get_properties (fresh);
  changed = !gst_structure_is_equal (current_props, fresh_props);
  g_clear_pointer (&current_props, gst_structure_free);
  g_clear_pointer (&fresh_props, gst_structure_free);
  if (changed)
    return TRUE;

#if TARGET_OS_OSX
  return gst_avf_device_has_screen_reconfig
      (reconfigured_display_id, display_reconfiguration_flags, fresh);
#else
  (void) reconfigured_display_id;
  (void) display_reconfiguration_flags;

  return FALSE;
#endif
}

static void
gst_avf_device_change_free (gpointer data)
{
  GstAvfDeviceChange *change = data;

  if (change == NULL)
    return;

  g_clear_object (&change->device);
  g_clear_object (&change->changed_device);
  g_free (change);
}

static gboolean
gst_avf_device_provider_register_watchers (GstAVFDeviceProvider * self)
{
  GstAVFDeviceProviderPrivate *priv =
      gst_avf_device_provider_get_instance_private (self);
  NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
  gboolean installed = FALSE;

  if (priv->camera_connected_observer == nil) {
    priv->camera_connected_observer =
        [center addObserverForName:AVCaptureDeviceWasConnectedNotification
                            object:nil
                             queue:nil
                        usingBlock:^(NSNotification *notification G_GNUC_UNUSED)
        {
          gst_avf_device_provider_schedule_refresh (self, GST_AVF_NO_DISPLAY_ID,
              0);
        }];
    if (priv->camera_connected_observer != nil)
      installed = TRUE;
    else
      GST_WARNING_OBJECT (self,
          "Could not register AVFoundation device-connected observer");
  }

  if (priv->camera_disconnected_observer == nil) {
    priv->camera_disconnected_observer =
        [center addObserverForName:AVCaptureDeviceWasDisconnectedNotification
                            object:nil
                             queue:nil
                        usingBlock:^(NSNotification *notification G_GNUC_UNUSED)
        {
          gst_avf_device_provider_schedule_refresh (self, GST_AVF_NO_DISPLAY_ID,
              0);
        }];
    if (priv->camera_disconnected_observer != nil)
      installed = TRUE;
    else
      GST_WARNING_OBJECT (self,
          "Could not register AVFoundation device-disconnected observer");
  }

#if TARGET_OS_OSX
  if (!priv->screen_callback_registered) {
    CGError result =
        CGDisplayRegisterReconfigurationCallback
        (gst_avf_device_provider_display_reconfigured, self);

    if (result == kCGErrorSuccess) {
      priv->screen_callback_registered = TRUE;
      installed = TRUE;
    } else {
      GST_WARNING_OBJECT (self,
          "Could not register display reconfiguration callback: %d", result);
    }
  }
#endif

  return installed;
}

static void
gst_avf_device_provider_unregister_watchers (GstAVFDeviceProvider * self)
{
  GstAVFDeviceProviderPrivate *priv =
      gst_avf_device_provider_get_instance_private (self);
  NSNotificationCenter *center = [NSNotificationCenter defaultCenter];

  if (priv->camera_connected_observer != nil) {
    [center removeObserver:priv->camera_connected_observer];
    priv->camera_connected_observer = nil;
  }

  if (priv->camera_disconnected_observer != nil) {
    [center removeObserver:priv->camera_disconnected_observer];
    priv->camera_disconnected_observer = nil;
  }

#if TARGET_OS_OSX
  if (priv->screen_callback_registered) {
    CGDisplayRemoveReconfigurationCallback
        (gst_avf_device_provider_display_reconfigured, self);
    priv->screen_callback_registered = FALSE;
  }
#endif
}

static void
gst_avf_device_provider_schedule_refresh (GstAVFDeviceProvider * self,
    guint display_id, guint display_reconfiguration_flags)
{
  GstAVFDeviceProviderPrivate *priv =
      gst_avf_device_provider_get_instance_private (self);
  gboolean should_dispatch = FALSE;

  g_mutex_lock (&priv->lock);
  should_dispatch = priv->watchers_active;
  g_mutex_unlock (&priv->lock);

  if (should_dispatch) {
    dispatch_async (priv->refresh_queue, ^{
      gst_avf_device_provider_refresh (self, display_id,
          display_reconfiguration_flags);
    });
  }
}

static void
gst_avf_device_provider_refresh (GstAVFDeviceProvider * self,
    guint display_id, guint display_reconfiguration_flags)
{
  GstAVFDeviceProviderPrivate *priv =
      gst_avf_device_provider_get_instance_private (self);
  GstDeviceProvider *provider = GST_DEVICE_PROVIDER_CAST (self);
  GList *prev_devices = NULL;
  GList *new_devices = NULL;
  GList *to_add = NULL;
  GList *to_remove = NULL;
  GList *to_change = NULL;

  g_mutex_lock (&priv->lock);
  if (!priv->watchers_active) {
    g_mutex_unlock (&priv->lock);
    return;
  }
  g_mutex_unlock (&priv->lock);

  GST_OBJECT_LOCK (provider);
  prev_devices = g_list_copy_deep (provider->devices,
      (GCopyFunc) gst_object_ref, NULL);
  GST_OBJECT_UNLOCK (provider);

  if (!gst_avf_device_provider_probe_internal (self, &new_devices)) {
    GST_WARNING_OBJECT (self,
        "Keeping AVF device provider state unchanged after failed refresh probe");
    goto done;
  }

  for (GList *iter = prev_devices; iter != NULL; iter = g_list_next (iter)) {
    GstDevice *current = GST_DEVICE (iter->data);
    GstDevice *fresh = gst_avf_device_find_identity_match (new_devices,
        current);

    if (fresh == NULL) {
      GST_AVF_DEVICE_PROVIDER_DEBUG_DEVICE (self, "removed", current);
      to_remove = g_list_prepend (to_remove, gst_object_ref (current));
    } else if (gst_avf_device_representation_changed
        (display_id, display_reconfiguration_flags, current, fresh)) {
      GstAvfDeviceChange *change = g_new0 (GstAvfDeviceChange, 1);

      GST_AVF_DEVICE_PROVIDER_DEBUG_DEVICE (self, "changed", fresh);
      change->device = gst_object_ref (fresh);
      change->changed_device = gst_object_ref (current);
      to_change = g_list_prepend (to_change, change);
    } else {
      GST_AVF_DEVICE_PROVIDER_DEBUG_DEVICE (self, "unchanged", current);
    }
  }

  for (GList *iter = new_devices; iter != NULL; iter = g_list_next (iter)) {
    GstDevice *fresh = GST_DEVICE (iter->data);

    if (gst_avf_device_find_identity_match (prev_devices, fresh) == NULL) {
      GST_AVF_DEVICE_PROVIDER_DEBUG_DEVICE (self, "added", fresh);
      to_add = g_list_prepend (to_add, gst_object_ref (fresh));
    }
  }

  for (GList *iter = to_remove; iter != NULL; iter = g_list_next (iter))
    gst_device_provider_device_remove (provider, GST_DEVICE (iter->data));

  for (GList *iter = to_change; iter != NULL; iter = g_list_next (iter)) {
    GstAvfDeviceChange *change = iter->data;

    gst_device_provider_device_changed (provider, change->device,
        change->changed_device);
  }

  for (GList *iter = to_add; iter != NULL; iter = g_list_next (iter))
    gst_device_provider_device_add (provider, GST_DEVICE (iter->data));

done:
  g_clear_list (&prev_devices, (GDestroyNotify) gst_object_unref);
  g_clear_list (&new_devices, (GDestroyNotify) gst_object_unref);
  g_clear_list (&to_add, (GDestroyNotify) gst_object_unref);
  g_clear_list (&to_remove, (GDestroyNotify) gst_object_unref);
  g_clear_list (&to_change, gst_avf_device_change_free);
}

static gboolean
gst_avf_device_provider_start (GstDeviceProvider * provider)
{
  GstAVFDeviceProvider *self = GST_AVF_DEVICE_PROVIDER (provider);
  GstAVFDeviceProviderPrivate *priv =
      gst_avf_device_provider_get_instance_private (self);
  GList *devices = NULL;
  GList *iter;

  if (!gst_avf_device_provider_probe_internal (self, &devices))
    return FALSE;

  for (iter = devices; iter != NULL; iter = g_list_next (iter))
    gst_device_provider_device_add (provider, GST_DEVICE (iter->data));

  g_list_free_full (devices, gst_object_unref);

  g_mutex_lock (&priv->lock);
  priv->watchers_active = TRUE;
  g_mutex_unlock (&priv->lock);

  if (!gst_avf_device_provider_register_watchers (self)) {
    GST_WARNING_OBJECT (self,
        "AVF live monitoring is unavailable; initial device population succeeded");
  }

  return TRUE;
}

static void
gst_avf_device_provider_stop (GstDeviceProvider * provider)
{
  GstAVFDeviceProvider *self = GST_AVF_DEVICE_PROVIDER (provider);
  GstAVFDeviceProviderPrivate *priv =
      gst_avf_device_provider_get_instance_private (self);

  g_mutex_lock (&priv->lock);
  priv->watchers_active = FALSE;
  g_mutex_unlock (&priv->lock);

  gst_avf_device_provider_unregister_watchers (self);

  if (priv->refresh_queue != NULL) {
    dispatch_sync (priv->refresh_queue, ^{
    });
  }
}

#if TARGET_OS_OSX
static void
gst_avf_device_provider_display_reconfigured (CGDirectDisplayID display_id,
    CGDisplayChangeSummaryFlags flags, void *user_info)
{
  if (flags == kCGDisplayBeginConfigurationFlag)
    return;

  gst_avf_device_provider_schedule_refresh (GST_AVF_DEVICE_PROVIDER (user_info),
      display_id, (guint) flags);
}
#endif

enum
{
  PROP_DEVICE_INDEX = 1,
  PROP_UNIQUE_ID,
};

G_DEFINE_TYPE (GstAvfDevice, gst_avf_device, GST_TYPE_DEVICE);

static GstElement *gst_avf_device_create_element (GstDevice * device,
                                                 const gchar * name);
static gboolean gst_avf_device_reconfigure_element (GstDevice * device,
                                                   GstElement * element);

static void gst_avf_device_get_property (GObject * object, guint prop_id,
                                         GValue * value, GParamSpec * pspec);
static void gst_avf_device_set_property (GObject * object, guint prop_id,
                                         const GValue * value, GParamSpec * pspec);
static void gst_avf_device_finalize (GObject * object);

static void
gst_avf_device_class_init (GstAvfDeviceClass * klass)
{
  GstDeviceClass *dev_class = GST_DEVICE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  dev_class->create_element = gst_avf_device_create_element;
  dev_class->reconfigure_element = gst_avf_device_reconfigure_element;

  object_class->finalize = gst_avf_device_finalize;
  object_class->get_property = gst_avf_device_get_property;
  object_class->set_property = gst_avf_device_set_property;

  g_object_class_install_property (object_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("device-index", "Device Index",
          "The zero-based device index (deprecated, non-functional)",
          -1, G_MAXINT, -1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_DEPRECATED));
  g_object_class_install_property (object_class, PROP_UNIQUE_ID,
      g_param_spec_string ("unique-id", "Unique ID",
          "Stable unique identifier of the AVF capture source", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_avf_device_init (GstAvfDevice * device)
{
}

static void
gst_avf_device_finalize (GObject * object)
{
  GstAvfDevice *device = GST_AVF_DEVICE_CAST (object);

  g_clear_pointer (&device->unique_id, g_free);

  G_OBJECT_CLASS (gst_avf_device_parent_class)->finalize (object);
}

static GstElement *
gst_avf_device_create_element (GstDevice * device, const gchar * name)
{
  GstAvfDevice *avf_dev = GST_AVF_DEVICE (device);
  GstElement *elem;
  elem = gst_element_factory_make (avf_dev->element, name);
  if (avf_dev->type == GST_AVF_DEVICE_TYPE_SCREEN_SOURCE) {
    g_object_set (elem,
        "capture-screen", TRUE,
        "unique-id", avf_dev->unique_id,
        NULL);
  } else {
    g_object_set (elem, "unique-id", avf_dev->unique_id, NULL);
  }

  return elem;
}

static gboolean
gst_avf_device_reconfigure_element (GstDevice * device, GstElement * element)
{
  GstAvfDevice *avf_dev = GST_AVF_DEVICE (device);
  if (!strcmp (avf_dev->element, "avfvideosrc") && GST_IS_AVF_VIDEO_SRC (element)) {
    if (avf_dev->type == GST_AVF_DEVICE_TYPE_SCREEN_SOURCE) {
      g_object_set (element,
          "capture-screen", TRUE,
          "unique-id", avf_dev->unique_id,
          NULL);
    } else {
      g_object_set (element, "unique-id", avf_dev->unique_id, NULL);
    }
    return TRUE;
  }

  return FALSE;
}

static void
gst_avf_device_get_property (GObject * object, guint prop_id,
                             GValue * value, GParamSpec * pspec)
{
  GstAvfDevice *device;

  device = GST_AVF_DEVICE_CAST (object);

  switch (prop_id) {
    case PROP_DEVICE_INDEX:
      g_value_set_int (value, -1);
      break;
    case PROP_UNIQUE_ID:
      g_value_set_string (value, device->unique_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_avf_device_set_property (GObject * object, guint prop_id,
                             const GValue * value, GParamSpec * pspec)
{
  GstAvfDevice *device;

  device = GST_AVF_DEVICE_CAST (object);

  switch (prop_id) {
    case PROP_DEVICE_INDEX:
      if (g_value_get_int (value) != -1) {
        g_warning ("The \"device-index\" property of GstAvfDevice is "
            "deprecated and non-functional. Use \"unique-id\" instead.");
      }
      break;
    case PROP_UNIQUE_ID:
      g_free (device->unique_id);
      device->unique_id = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstDevice *
gst_avf_device_new (const gchar * device_name, const gchar * unique_id,
    GstCaps * caps, GstAvfDeviceType type,
    GstStructure *props)
{
  GstAvfDevice *gstdev;
  const gchar *element = NULL;
  const gchar *klass = NULL;

  g_return_val_if_fail (device_name, NULL);

  switch (type) {
    case GST_AVF_DEVICE_TYPE_VIDEO_SOURCE:
      element = "avfvideosrc";
      klass = "Video/Source";
      break;
    case GST_AVF_DEVICE_TYPE_SCREEN_SOURCE:
      element = "avfvideosrc";
      klass = "Source/Monitor";
      break;
    default:
      g_assert_not_reached ();
      break;
  }


  gstdev = g_object_new (GST_TYPE_AVF_DEVICE,
                         "display-name", device_name, "caps", caps, "device-class", klass,
                         "unique-id", unique_id, "properties", props, NULL);

  gstdev->type = type;
  gstdev->element = element;

  return GST_DEVICE (gstdev);
}
