/* GStreamer Android AAsset Source
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
/**
 * SECTION:element-aassetsrc
 * @title: aassetsrc
 *
 * The aassetsrc element reads bytes from the current Android application's
 * AAssetManager namespace. The location property is relative to that namespace;
 * for example, an APK member stored as assets/video.webm is opened as
 * location=video.webm.
 *
 * aassetsrc does not demux or decode media. It uses Android's AAsset API to
 * read both compressed and uncompressed assets. Compressed assets may be less
 * efficient for seeking and reading because Android has to decompress them.
 * It implements the `android-asset:` URI scheme for automatic source selection
 * by elements such as uridecodebin.
 *
 * ## Example pipeline
 * |[
 * uridecodebin uri=android-asset://video.webm ! videoconvert ! autovideosink
 * ]|
 * This pipeline can be used from an Android application with gst_parse_launch()
 * after GStreamer has been initialized with an Android Context.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstaassetsrc.h"

#include "gstjniutils.h"

#include <android/asset_manager_jni.h>
#include <gst/gstbuffer.h>
#include <gst/gstelement.h>
#include <gst/gsterror.h>
#include <gst/gstinfo.h>
#include <gst/gstpadtemplate.h>
#include <gst/gsturi.h>
#include <gst/gstutils.h>
#include <gmodule.h>
#include <stdio.h>

GST_DEBUG_CATEGORY_STATIC (gst_aasset_src_debug);
#define GST_CAT_DEFAULT gst_aasset_src_debug

#define parent_class gst_aasset_src_parent_class

static jobject (*gst_android_get_application_context) (void) = NULL;

#define GST_AASSET_SRC_URI_SCHEME "android-asset"
#define GST_AASSET_SRC_URI_PREFIX GST_AASSET_SRC_URI_SCHEME "://"

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static GstStaticPadTemplate gst_aasset_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_aasset_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_aasset_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_aasset_src_finalize (GObject * object);

static gboolean gst_aasset_src_start (GstBaseSrc * src);
static gboolean gst_aasset_src_stop (GstBaseSrc * src);
static gboolean gst_aasset_src_get_size (GstBaseSrc * src, guint64 * size);
static gboolean gst_aasset_src_is_seekable (GstBaseSrc * src);
static GstFlowReturn gst_aasset_src_fill (GstBaseSrc * src, guint64 offset,
    guint size, GstBuffer * buffer);
static gboolean gst_aasset_src_unlock (GstBaseSrc * src);
static gboolean gst_aasset_src_unlock_stop (GstBaseSrc * src);
static void gst_aasset_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GstAAssetSrc, gst_aasset_src, GST_TYPE_BASE_SRC,
    GST_DEBUG_CATEGORY_INIT (gst_aasset_src_debug, "aassetsrc", 0,
        "Android AAsset source");
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_aasset_src_uri_handler_init));

GST_ELEMENT_REGISTER_DEFINE (aassetsrc, "aassetsrc", GST_RANK_NONE,
    GST_TYPE_AASSET_SRC);

static gboolean
gst_aasset_src_init_android_context_provider (void)
{
  GModule *module;
  gboolean ret;

  if (gst_android_get_application_context)
    return TRUE;

  module = g_module_open (NULL, G_MODULE_BIND_LOCAL);
  if (!module)
    return FALSE;

  ret = g_module_symbol (module, "gst_android_get_application_context",
      (gpointer *) & gst_android_get_application_context);
  g_module_close (module);

  return ret && gst_android_get_application_context;
}

static void
gst_aasset_src_clear_asset (GstAAssetSrc * self)
{
  g_clear_pointer (&self->asset, AAsset_close);
  if (self->asset_manager_ref) {
    JNIEnv *env = gst_amc_jni_get_env ();

    (*env)->DeleteGlobalRef (env, (jobject) self->asset_manager_ref);
    self->asset_manager_ref = NULL;
  }
  self->asset_manager = NULL;
  self->size = 0;
}

static gchar *
gst_aasset_src_uri_from_location (const gchar * location)
{
  gchar *escaped, *uri;

  escaped = g_uri_escape_string (location, "/", FALSE);
  uri = g_strdup_printf (GST_AASSET_SRC_URI_PREFIX "%s", escaped);
  g_free (escaped);

  return uri;
}

static void
gst_aasset_src_class_init (GstAAssetSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_aasset_src_set_property;
  gobject_class->get_property = gst_aasset_src_get_property;
  gobject_class->finalize = gst_aasset_src_finalize;

  properties[PROP_LOCATION] = g_param_spec_string ("location", "Location",
      "Android asset path relative to the application AssetManager namespace",
      NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class, PROP_LOCATION,
      properties[PROP_LOCATION]);

  gst_element_class_add_static_pad_template (element_class,
      &gst_aasset_src_template);
  gst_element_class_set_static_metadata (element_class,
      "Android AAsset source", "Source/File",
      "Read bytes from Android application assets using AAssetManager",
      "Dominique Leroux <dominique.p.leroux@gmail.com>");

  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_aasset_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_aasset_src_stop);
  basesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_aasset_src_get_size);
  basesrc_class->is_seekable = GST_DEBUG_FUNCPTR (gst_aasset_src_is_seekable);
  basesrc_class->fill = GST_DEBUG_FUNCPTR (gst_aasset_src_fill);
  basesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_aasset_src_unlock);
  basesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_aasset_src_unlock_stop);
}

static void
gst_aasset_src_init (GstAAssetSrc * self)
{
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_BYTES);
}

static void
gst_aasset_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAAssetSrc *self = GST_AASSET_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      GST_OBJECT_LOCK (self);
      g_free (self->location);
      self->location = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_aasset_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAAssetSrc *self = GST_AASSET_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      GST_OBJECT_LOCK (self);
      g_value_set_string (value, self->location);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_aasset_src_finalize (GObject * object)
{
  GstAAssetSrc *self = GST_AASSET_SRC (object);

  gst_aasset_src_clear_asset (self);
  g_free (self->location);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_aasset_src_start (GstBaseSrc * src)
{
  GstAAssetSrc *self = GST_AASSET_SRC (src);
  JNIEnv *env = NULL;
  jobject context;
  jobject asset_manager_object = NULL;
  jclass context_class = NULL;
  jmethodID get_assets_id;
  gchar *location;
  gboolean ret = FALSE;

  GST_OBJECT_LOCK (self);
  location = g_strdup (self->location);
  self->flushing = FALSE;
  GST_OBJECT_UNLOCK (self);

  if (location == NULL || *location == '\0') {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("No Android asset location was specified"), (NULL));
    goto done;
  }

  if (!gst_aasset_src_init_android_context_provider ()) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ,
        ("Could not retrieve the Android application context provider"),
        ("GStreamer must be initialized with an Android Context"));
    goto done;
  }

  context = gst_android_get_application_context ();
  if (!context) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ,
        ("Could not retrieve the Android application context"),
        ("GStreamer must be initialized with an Android Context"));
    goto done;
  }

  env = gst_amc_jni_get_env ();
  context_class = (*env)->GetObjectClass (env, context);
  if (!context_class)
    goto jni_failed;

  get_assets_id = (*env)->GetMethodID (env, context_class, "getAssets",
      "()Landroid/content/res/AssetManager;");
  if ((*env)->ExceptionCheck (env) || !get_assets_id)
    goto jni_failed;

  asset_manager_object = (*env)->CallObjectMethod (env, context, get_assets_id);
  if ((*env)->ExceptionCheck (env) || !asset_manager_object)
    goto jni_failed;

  self->asset_manager = AAssetManager_fromJava (env, asset_manager_object);
  if (!self->asset_manager) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ,
        ("Could not retrieve the Android AAssetManager"), (NULL));
    goto done;
  }

  self->asset_manager_ref = (*env)->NewGlobalRef (env, asset_manager_object);
  self->asset = AAssetManager_open (self->asset_manager, location,
      AASSET_MODE_RANDOM);
  if (!self->asset) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Could not open Android asset '%s'", location),
        ("Asset paths are relative to the application AssetManager namespace"));
    goto done;
  }

  self->size = AAsset_getLength64 (self->asset);
  GST_INFO_OBJECT (self, "Opened Android asset '%s' with size %"
      G_GUINT64_FORMAT, location, self->size);
  ret = TRUE;
  goto done;

jni_failed:
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
  }
  GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ,
      ("Could not retrieve the Android AssetManager from the application context"),
      (NULL));

done:
  if (env && context_class)
    (*env)->DeleteLocalRef (env, context_class);
  if (env && asset_manager_object)
    (*env)->DeleteLocalRef (env, asset_manager_object);
  if (!ret)
    gst_aasset_src_clear_asset (self);
  g_free (location);

  return ret;
}

static gboolean
gst_aasset_src_stop (GstBaseSrc * src)
{
  GstAAssetSrc *self = GST_AASSET_SRC (src);

  gst_aasset_src_clear_asset (self);

  GST_OBJECT_LOCK (self);
  self->flushing = FALSE;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static gboolean
gst_aasset_src_get_size (GstBaseSrc * src, guint64 * size)
{
  GstAAssetSrc *self = GST_AASSET_SRC (src);

  if (!self->asset)
    return FALSE;

  *size = self->size;
  return TRUE;
}

static gboolean
gst_aasset_src_is_seekable (GstBaseSrc * src)
{
  return TRUE;
}

static GstFlowReturn
gst_aasset_src_fill (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer * buffer)
{
  GstAAssetSrc *self = GST_AASSET_SRC (src);
  GstMapInfo info;
  guint64 remaining;
  guint to_read;
  guint done = 0;

  GST_OBJECT_LOCK (self);
  if (self->flushing) {
    GST_OBJECT_UNLOCK (self);
    return GST_FLOW_FLUSHING;
  }
  GST_OBJECT_UNLOCK (self);

  if (!self->asset)
    return GST_FLOW_ERROR;

  if (offset >= self->size) {
    gst_buffer_resize (buffer, 0, 0);
    return GST_FLOW_EOS;
  }

  remaining = self->size - offset;
  to_read = (guint) MIN ((guint64) size, remaining);
  if (to_read == 0) {
    gst_buffer_resize (buffer, 0, 0);
    return GST_FLOW_EOS;
  }

  if (AAsset_seek64 (self->asset, offset, SEEK_SET) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, SEEK,
        ("Could not seek Android asset to offset %" G_GUINT64_FORMAT, offset),
        (NULL));
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (buffer, &info, GST_MAP_WRITE)) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE, (NULL),
        ("Can't write to buffer"));
    return GST_FLOW_ERROR;
  }
  if (info.size < to_read) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE, (NULL),
        ("Mapped buffer size %" G_GSIZE_FORMAT
            " is smaller than requested read size %u", info.size, to_read));
    gst_buffer_unmap (buffer, &info);
    return GST_FLOW_ERROR;
  }

  while (done < to_read) {
    int ret;

    GST_OBJECT_LOCK (self);
    if (self->flushing) {
      GST_OBJECT_UNLOCK (self);
      gst_buffer_unmap (buffer, &info);
      return GST_FLOW_FLUSHING;
    }
    GST_OBJECT_UNLOCK (self);

    ret = AAsset_read (self->asset, info.data + done, to_read - done);
    if (ret < 0) {
      GST_ELEMENT_ERROR (self, RESOURCE, READ,
          ("Could not read Android asset '%s'", GST_STR_NULL (self->location)),
          (NULL));
      gst_buffer_unmap (buffer, &info);
      gst_buffer_resize (buffer, 0, 0);
      return GST_FLOW_ERROR;
    }
    if (ret == 0)
      break;

    done += ret;
  }

  gst_buffer_unmap (buffer, &info);

  if (done == 0) {
    gst_buffer_resize (buffer, 0, 0);
    return GST_FLOW_EOS;
  }

  if (done < to_read)
    gst_buffer_resize (buffer, 0, done);

  GST_BUFFER_OFFSET (buffer) = offset;
  GST_BUFFER_OFFSET_END (buffer) = offset + done;

  return GST_FLOW_OK;
}

static gboolean
gst_aasset_src_unlock (GstBaseSrc * src)
{
  GstAAssetSrc *self = GST_AASSET_SRC (src);

  GST_OBJECT_LOCK (self);
  self->flushing = TRUE;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static gboolean
gst_aasset_src_unlock_stop (GstBaseSrc * src)
{
  GstAAssetSrc *self = GST_AASSET_SRC (src);

  GST_OBJECT_LOCK (self);
  self->flushing = FALSE;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_aasset_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_aasset_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { GST_AASSET_SRC_URI_SCHEME, NULL };

  return protocols;
}

static gchar *
gst_aasset_src_uri_get_uri (GstURIHandler * handler)
{
  GstAAssetSrc *self = GST_AASSET_SRC (handler);
  gchar *uri;

  GST_OBJECT_LOCK (self);
  if (self->location)
    uri = gst_aasset_src_uri_from_location (self->location);
  else
    uri = NULL;
  GST_OBJECT_UNLOCK (self);

  return uri;
}

static gboolean
gst_aasset_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstAAssetSrc *self = GST_AASSET_SRC (handler);
  gchar *scheme = NULL;
  gchar *location = NULL;
  gchar *asset_path;
  guint leading_slashes = 0;
  gboolean ret = FALSE;

  if (uri == NULL || g_strcmp0 (uri, GST_AASSET_SRC_URI_PREFIX) == 0) {
    GST_OBJECT_LOCK (self);
    g_clear_pointer (&self->location, g_free);
    GST_OBJECT_UNLOCK (self);
    return TRUE;
  }

  scheme = gst_uri_get_protocol (uri);
  if (scheme == NULL) {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Invalid " GST_AASSET_SRC_URI_SCHEME " URI '%s'", uri);
    goto done;
  }

  if (g_ascii_strcasecmp (scheme, GST_AASSET_SRC_URI_SCHEME) != 0) {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Invalid " GST_AASSET_SRC_URI_SCHEME " URI scheme '%s'",
        GST_STR_NULL (scheme));
    goto done;
  }

  location = gst_uri_get_location (uri);
  if (location == NULL || *location == '\0') {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Android asset URI has no asset path");
    goto done;
  }

  while (location[leading_slashes] == '/')
    leading_slashes++;

  /* Canonicalize to android-asset://video.webm, accept
   * android-asset:///video.webm, but reject android-asset:////video.webm
   * instead of normalizing /video.webm.
   */
  if (leading_slashes > 1) {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Android asset URI path must not start with '/'");
    goto done;
  }

  asset_path = location + leading_slashes;
  if (*asset_path == '\0') {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Android asset URI has no asset path");
    goto done;
  }

  GST_OBJECT_LOCK (self);
  g_free (self->location);
  self->location = g_strdup (asset_path);
  GST_OBJECT_UNLOCK (self);

  ret = TRUE;

done:
  g_free (scheme);
  g_free (location);

  return ret;
}

static void
gst_aasset_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_aasset_src_uri_get_type;
  iface->get_protocols = gst_aasset_src_uri_get_protocols;
  iface->get_uri = gst_aasset_src_uri_get_uri;
  iface->set_uri = gst_aasset_src_uri_set_uri;
}
