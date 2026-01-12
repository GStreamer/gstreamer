/* GStreamer
 *  Copyright (C) 2021 Intel Corporation
 *     Author: He Junyan <junyan.he@intel.com>
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

#include "gstvaencoder.h"

#include <gst/va/gstvavideoformat.h>
#include <gst/va/vasurfaceimage.h>

#include "vacompat.h"
#include "gstvacaps.h"
#include "gstvaprofile.h"
#include "gstvadisplay_priv.h"

struct _GstVaEncoder
{
  GstObject parent;

  GArray *available_profiles;
  GstCaps *srcpad_caps;
  GstCaps *sinkpad_caps;
  GstVaDisplay *display;
  VAConfigID config;
  VAContextID context;
  VAProfile profile;
  VAEntrypoint entrypoint;
  guint rt_format;
  gint coded_width;
  gint coded_height;
  gint codedbuf_size;

  struct
  {
    GstBufferPool *pool;
    GstVideoFormat format;
    gint max_surfaces;
  } recon;
};

GST_DEBUG_CATEGORY_STATIC (gst_va_encoder_debug);
#define GST_CAT_DEFAULT gst_va_encoder_debug

#define gst_va_encoder_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVaEncoder, gst_va_encoder, GST_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (gst_va_encoder_debug, "vaencoder", 0,
        "VA Encoder"));

enum
{
  PROP_DISPLAY = 1,
  PROP_PROFILE,
  PROP_ENTRYPOINT,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_CHROMA,
  PROP_CODED_BUF_SIZE,
  N_PROPERTIES
};

static GParamSpec *g_properties[N_PROPERTIES];

static gboolean
_destroy_buffer (GstVaDisplay * display, VABufferID buffer)
{
  VAStatus status;
  gboolean ret = TRUE;
  VADisplay dpy = gst_va_display_get_va_dpy (display);

  status = vaDestroyBuffer (dpy, buffer);
  if (status != VA_STATUS_SUCCESS) {
    ret = FALSE;
    GST_WARNING ("Failed to destroy the buffer: %s", vaErrorStr (status));
  }

  return ret;
}

static VABufferID
_create_buffer (GstVaEncoder * self, VABufferType type, gpointer data,
    guint size)
{
  VAStatus status;
  VADisplay dpy;
  VABufferID buffer;
  VAContextID context;

  GST_OBJECT_LOCK (self);
  context = self->context;
  GST_OBJECT_UNLOCK (self);

  dpy = gst_va_display_get_va_dpy (self->display);
  status = vaCreateBuffer (dpy, context, type, size, 1, data, &buffer);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaCreateBuffer: %s", vaErrorStr (status));
    return VA_INVALID_ID;
  }

  return buffer;
}

static void
gst_va_encoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaEncoder *self = GST_VA_ENCODER (object);

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_DISPLAY:{
      /* G_PARAM_CONSTRUCT_ONLY */
      g_assert (!self->display);
      self->display = g_value_dup_object (value);
      break;
    }
    case PROP_ENTRYPOINT:{
      self->entrypoint = g_value_get_int (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_va_encoder_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVaEncoder *self = GST_VA_ENCODER (object);

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_object (value, self->display);
      break;
    case PROP_PROFILE:
      g_value_set_int (value, self->profile);
      break;
    case PROP_ENTRYPOINT:
      g_value_set_int (value, self->entrypoint);
      break;
    case PROP_CHROMA:
      g_value_set_uint (value, self->rt_format);
      break;
    case PROP_WIDTH:
      g_value_set_int (value, self->coded_width);
      break;
    case PROP_HEIGHT:
      g_value_set_int (value, self->coded_height);
      break;
    case PROP_CODED_BUF_SIZE:
      g_value_set_int (value, self->codedbuf_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_va_encoder_init (GstVaEncoder * self)
{
  self->profile = VAProfileNone;
  self->config = VA_INVALID_ID;
  self->context = VA_INVALID_ID;
  self->rt_format = 0;
  self->coded_width = -1;
  self->coded_height = -1;
  self->codedbuf_size = 0;

  self->recon.pool = NULL;
  self->recon.max_surfaces = 0;
  self->recon.format = GST_VIDEO_FORMAT_UNKNOWN;
}

static inline gboolean
_is_setup_unlocked (GstVaEncoder * self)
{
  return (self->config != VA_INVALID_ID && self->profile != VAProfileNone);
}

static inline gboolean
gst_va_encoder_is_setup (GstVaEncoder * self)
{
  gboolean ret;

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);

  GST_OBJECT_LOCK (self);
  ret = _is_setup_unlocked (self);
  GST_OBJECT_UNLOCK (self);
  return ret;
}

static inline gboolean
_is_open_unlocked (GstVaEncoder * self)
{
  return (_is_setup_unlocked (self) && self->context != VA_INVALID_ID);
}

gboolean
gst_va_encoder_is_open (GstVaEncoder * self)
{
  gboolean ret;

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);

  GST_OBJECT_LOCK (self);
  ret = _is_open_unlocked (self);
  GST_OBJECT_UNLOCK (self);
  return ret;
}

static inline void
_destroy_context (GstVaEncoder * self)
{
  VADisplay dpy;
  VAStatus status;
  VAContextID context;
  GstBufferPool *pool;

  GST_OBJECT_LOCK (self);
  context = self->context;
  self->context = VA_INVALID_ID;
  self->coded_width = -1;
  self->coded_height = -1;

  if ((pool = self->recon.pool)) {
    self->recon.pool = NULL;
    self->recon.format = GST_VIDEO_FORMAT_UNKNOWN;
    self->recon.max_surfaces = 0;
  }
  GST_OBJECT_UNLOCK (self);

  if (pool) {
    gst_buffer_pool_set_active (pool, FALSE);
    gst_object_unref (pool);
  }

  if (context == VA_INVALID_ID)
    return;

  dpy = gst_va_display_get_va_dpy (self->display);
  status = vaDestroyContext (dpy, context);
  if (status != VA_STATUS_SUCCESS)
    GST_ERROR_OBJECT (self, "vaDestroyContext: %s", vaErrorStr (status));
}

gboolean
gst_va_encoder_close (GstVaEncoder * self)
{
  VADisplay dpy;
  VAStatus status;
  VAConfigID config;

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);

  _destroy_context (self);

  gst_caps_replace (&self->srcpad_caps, NULL);
  gst_caps_replace (&self->sinkpad_caps, NULL);

  GST_OBJECT_LOCK (self);
  config = self->config;

  gst_va_encoder_init (self);
  GST_OBJECT_UNLOCK (self);

  if (config == VA_INVALID_ID)
    return TRUE;

  dpy = gst_va_display_get_va_dpy (self->display);
  status = vaDestroyConfig (dpy, config);
  if (status != VA_STATUS_SUCCESS)
    GST_ERROR_OBJECT (self, "vaDestroyConfig: %s", vaErrorStr (status));

  return TRUE;
}

/* for querying the customized surface alignment */
guint
gst_va_encoder_get_surface_alignment (GstVaEncoder * self)
{
  guint alignment = 0;
#if VA_CHECK_VERSION(1, 21, 0)
  VASurfaceAttrib *attr_list;
  guint i, count;
  VAConfigID config;

  GST_OBJECT_LOCK (self);
  config = self->config;
  GST_OBJECT_UNLOCK (self);

  if (config == VA_INVALID_ID) {
    GST_ERROR_OBJECT (self,
        "Encoder has to be setup before getting surface alignment");
    return 0;
  }

  attr_list = gst_va_get_surface_attribs (self->display, config, &count);
  if (!attr_list)
    goto bail;

  for (i = 0; i < count; i++) {
    if (attr_list[i].type != VASurfaceAttribAlignmentSize)
      continue;

    alignment = attr_list[i].value.value.i;
    GST_INFO_OBJECT (self, "Using customized surface alignment [%dx%d]",
        1 << (alignment & 0xf), 1 << ((alignment & 0xf0) >> 4));
    break;
  }
  g_free (attr_list);

bail:
#endif
  return alignment;
}

static GArray *
_get_surface_formats (GstVaDisplay * display, VAConfigID config)
{
  GArray *formats;
  GstVideoFormat format;
  VASurfaceAttrib *attribs;
  guint i, attrib_count;

  attribs = gst_va_get_surface_attribs (display, config, &attrib_count);
  if (!attribs)
    return NULL;

  formats = g_array_new (FALSE, FALSE, sizeof (GstVideoFormat));

  for (i = 0; i < attrib_count; i++) {
    if (attribs[i].value.type != VAGenericValueTypeInteger)
      continue;
    switch (attribs[i].type) {
      case VASurfaceAttribPixelFormat:
        format = gst_va_video_format_from_va_fourcc (attribs[i].value.value.i);
        if (format != GST_VIDEO_FORMAT_UNKNOWN)
          g_array_append_val (formats, format);
        break;
      default:
        break;
    }
  }

  g_free (attribs);

  if (formats->len == 0) {
    g_array_unref (formats);
    return NULL;
  }

  return formats;
}

static inline GstCaps *
_get_reconstructed_caps (GstVaEncoder * self)
{
  GstVideoInfo info;
  GstCaps *caps;
  GstVideoFormat format;
  gint width, height;

  GST_OBJECT_LOCK (self);
  format = self->recon.format;
  width = self->coded_width;
  height = self->coded_height;
  GST_OBJECT_UNLOCK (self);

  if (!gst_video_info_set_format (&info, format, width, height)) {
    GST_WARNING_OBJECT (self, "Invalid video info");
    return NULL;
  }
  caps = gst_video_info_to_caps (&info);
  if (!caps)
    return NULL;

  gst_caps_set_features_simple (caps,
      gst_caps_features_new_single_static_str (GST_CAPS_FEATURE_MEMORY_VA));
  return caps;
}

static inline GstAllocator *
_get_reconstructed_allocator (GstVaEncoder * self)
{
  GArray *surface_formats;
  VAConfigID config;

  GST_OBJECT_LOCK (self);
  config = self->config;
  GST_OBJECT_UNLOCK (self);

  g_assert (config != VA_INVALID_ID);

  surface_formats = _get_surface_formats (self->display, config);
  if (!surface_formats) {
    GST_ERROR_OBJECT (self, "Failed to get surface formats");
    return NULL;
  }

  return gst_va_allocator_new (self->display, surface_formats);
}

static GstBufferPool *
_get_reconstructed_buffer_pool (GstVaEncoder * self)
{
  GstAllocator *allocator = NULL;
  guint usage_hint;
  GstAllocationParams params;
  GstBufferPool *pool = NULL;
  GstCaps *caps;
  gint max_surfaces;

  GST_OBJECT_LOCK (self);
  pool = self->recon.pool ? gst_object_ref (self->recon.pool) : NULL;
  max_surfaces = self->recon.max_surfaces;
  GST_OBJECT_UNLOCK (self);

  if (pool)
    return pool;

  allocator = _get_reconstructed_allocator (self);
  if (!allocator) {
    GST_ERROR_OBJECT (self, "Failed to create reconstruct allocator");
    return NULL;
  }

  caps = _get_reconstructed_caps (self);
  if (!caps) {
    GST_ERROR_OBJECT (self, "Failed to configure reconstruct caps");
    goto bail;
  }

  usage_hint = va_get_surface_usage_hint (self->display, self->entrypoint,
      GST_PAD_SINK, FALSE);

  gst_allocation_params_init (&params);

  /* create one reconstruct surface at least */
  pool = gst_va_pool_new_with_config (caps, 1, max_surfaces, usage_hint,
      GST_VA_FEATURE_AUTO, allocator, &params);
  if (!pool) {
    GST_ERROR_OBJECT (self, "Failed to create reconstruct pool");
    goto bail;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Failed to activate reconstruct pool");
    gst_clear_object (&pool);
  }

bail:
  gst_clear_object (&allocator);
  gst_clear_caps (&caps);

  gst_object_replace ((GstObject **) & self->recon.pool,
      GST_OBJECT_CAST (pool));
  return pool;
}

static inline gboolean
_skip_setup (GstVaEncoder * self, VAProfile profile, guint rt_format,
    guint rc_ctrl, guint32 packed_headers)
{
  VADisplay dpy;
  VAStatus status;
  /* *INDENT-OFF* */
  VAConfigAttrib attribs[] = {
    { .type = VAConfigAttribRateControl, .value = 0, },
    { .type = VAConfigAttribEncPackedHeaders, .value = 0, },
  };
  /* *INDENT-ON* */
  gboolean same;

  /* encoder is closed */
  if (!gst_va_encoder_is_setup (self))
    return FALSE;

  GST_OBJECT_LOCK (self);
  same = (profile == self->profile) && (rt_format == self->rt_format);
  GST_OBJECT_UNLOCK (self);
  if (!same)
    goto close_and_bail;

  dpy = gst_va_display_get_va_dpy (self->display);
  status = vaGetConfigAttributes (dpy, profile, self->entrypoint, attribs,
      G_N_ELEMENTS (attribs));
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaGetConfigAttributes: %s", vaErrorStr (status));
    goto close_and_bail;
  }

  same = ((attribs[0].value == VA_ATTRIB_NOT_SUPPORTED)
      && (rc_ctrl == VA_RC_NONE))
      || ((attribs[0].value & rc_ctrl) == rc_ctrl);
  if (!same)
    goto close_and_bail;

  same = ((attribs[1].value == VA_ATTRIB_NOT_SUPPORTED)
      && (packed_headers == 0))
      || ((attribs[1].value & packed_headers) == packed_headers);
  if (!same)
    goto close_and_bail;

  /* the same setup can be reused */
  return TRUE;

close_and_bail:
  gst_va_encoder_close (self);
  return FALSE;
}

gboolean
gst_va_encoder_setup (GstVaEncoder * self, VAProfile profile, guint rt_format,
    guint rc_ctrl, guint32 packed_headers)
{
  /* *INDENT-OFF* */
  VAConfigAttrib attribs[3] = {
    { .type = VAConfigAttribRTFormat, .value = rt_format, },
  };
  /* *INDENT-ON* */
  VAConfigID config = VA_INVALID_ID;
  VADisplay dpy;
  VAStatus status;
  guint attrib_idx = 1;

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);
  g_return_val_if_fail (profile != VAProfileNone, FALSE);
  g_return_val_if_fail (rc_ctrl > 0, FALSE);
  g_return_val_if_fail (rt_format > 0, FALSE);

  if (_skip_setup (self, profile, rt_format, rc_ctrl, packed_headers))
    return TRUE;

  if (!gst_va_encoder_has_profile (self, profile)) {
    GST_ERROR_OBJECT (self, "Unsupported profile: %s, entrypoint: %d",
        gst_va_profile_name (profile), self->entrypoint);
    return FALSE;
  }

  if (rc_ctrl != VA_RC_NONE) {
    attribs[attrib_idx].type = VAConfigAttribRateControl;
    attribs[attrib_idx].value = rc_ctrl;
    attrib_idx++;
  }

  if (packed_headers > 0) {
    attribs[attrib_idx].type = VAConfigAttribEncPackedHeaders;
    attribs[attrib_idx].value = packed_headers;
    attrib_idx++;
  }

  dpy = gst_va_display_get_va_dpy (self->display);
  status = vaCreateConfig (dpy, profile, self->entrypoint, attribs, attrib_idx,
      &config);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaCreateConfig: %s", vaErrorStr (status));
    return FALSE;
  }

  GST_OBJECT_LOCK (self);
  self->config = config;
  self->profile = profile;
  self->rt_format = rt_format;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static inline gboolean
_skip_open (GstVaEncoder * self, gint coded_width, gint coded_height)
{
  gboolean same_size;

  if (!gst_va_encoder_is_open (self))
    return FALSE;

  GST_OBJECT_LOCK (self);
  same_size = (self->coded_width == coded_width)
      && (self->coded_height == coded_height);
  GST_OBJECT_UNLOCK (self);

  if (same_size)
    return TRUE;

  /* partial close: context & pool */
  _destroy_context (self);

  return FALSE;
}

gboolean
gst_va_encoder_open_2 (GstVaEncoder * self, gint coded_width, gint coded_height)
{
  VAConfigID config = VA_INVALID_ID;
  VAContextID context = VA_INVALID_ID;
  VADisplay dpy;
  VAStatus status;

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);

  if (!gst_va_encoder_is_setup (self)) {
    /* clean up any misleading previous state */
    _destroy_context (self);
    GST_ERROR_OBJECT (self, "call gst_va_encoder_setup() previous!");
    return FALSE;
  }

  if (_skip_open (self, coded_width, coded_height))
    return TRUE;

  GST_OBJECT_LOCK (self);
  config = self->config;
  GST_OBJECT_UNLOCK (self);

  dpy = gst_va_display_get_va_dpy (self->display);
  status = vaCreateContext (dpy, config, coded_width, coded_height,
      VA_PROGRESSIVE, NULL, 0, &context);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaCreateConfig: %s", vaErrorStr (status));
    return FALSE;
  }

  GST_OBJECT_LOCK (self);
  self->context = context;
  self->coded_width = coded_width;
  self->coded_height = coded_height;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

gboolean
gst_va_encoder_open (GstVaEncoder * self, VAProfile profile,
    GstVideoFormat video_format, guint rt_format, gint coded_width,
    gint coded_height, gint codedbuf_size, guint max_reconstruct_surfaces,
    guint rc_ctrl, guint32 packed_headers)
{
  GstBufferPool *recon_pool;

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);
  g_return_val_if_fail (codedbuf_size > 0, FALSE);

  if (!gst_va_encoder_setup (self, profile, rt_format, rc_ctrl, packed_headers))
    return FALSE;

  if (!gst_va_encoder_open_2 (self, coded_width, coded_height))
    return FALSE;

  if (!gst_va_encoder_set_reconstruct_pool_config (self, video_format,
          max_reconstruct_surfaces))
    return FALSE;
  recon_pool = _get_reconstructed_buffer_pool (self);
  if (!recon_pool)
    return FALSE;
  gst_object_unref (recon_pool);

  gst_va_encoder_set_coded_buffer_size (self, codedbuf_size);

  /* XXX: now we should return now only this profile's caps */
  gst_caps_replace (&self->srcpad_caps, NULL);

  return TRUE;
}

static void
gst_va_encoder_dispose (GObject * object)
{
  GstVaEncoder *self = GST_VA_ENCODER (object);

  gst_va_encoder_close (self);

  g_clear_pointer (&self->available_profiles, g_array_unref);
  gst_clear_object (&self->display);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_va_encoder_class_init (GstVaEncoderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_va_encoder_set_property;
  gobject_class->get_property = gst_va_encoder_get_property;
  gobject_class->dispose = gst_va_encoder_dispose;

  g_properties[PROP_DISPLAY] =
      g_param_spec_object ("display", "GstVaDisplay", "GstVaDisplay object",
      GST_TYPE_VA_DISPLAY,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_properties[PROP_PROFILE] =
      g_param_spec_int ("va-profile", "VAProfile", "VA Profile",
      VAProfileNone, 50, VAProfileNone,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_properties[PROP_ENTRYPOINT] =
      g_param_spec_int ("va-entrypoint", "VAEntrypoint", "VA Entrypoint",
      0, 14, 0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_properties[PROP_CHROMA] =
      g_param_spec_uint ("va-rt-format", "VARTFormat", "VA RT Format",
      VA_RT_FORMAT_YUV420, VA_RT_FORMAT_PROTECTED, VA_RT_FORMAT_YUV420,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_properties[PROP_WIDTH] =
      g_param_spec_int ("coded-width", "coded-picture-width",
      "coded picture width", 0, G_MAXINT, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_properties[PROP_HEIGHT] =
      g_param_spec_int ("coded-height", "coded-picture-height",
      "coded picture height", 0, G_MAXINT, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_properties[PROP_CODED_BUF_SIZE] =
      g_param_spec_int ("coded-buf-size", "coded-buffer-size",
      "coded buffer size", 0, G_MAXINT, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPERTIES, g_properties);
}

static gboolean
gst_va_encoder_initialize (GstVaEncoder * self, guint32 codec)
{
  if (self->available_profiles)
    return FALSE;

  self->available_profiles =
      gst_va_display_get_profiles (self->display, codec, self->entrypoint);

  if (!self->available_profiles)
    return FALSE;

  if (self->available_profiles->len == 0) {
    g_clear_pointer (&self->available_profiles, g_array_unref);
    return FALSE;
  }

  return TRUE;
}

GstVaEncoder *
gst_va_encoder_new (GstVaDisplay * display, guint32 codec,
    VAEntrypoint entrypoint)
{
  GstVaEncoder *self;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (display), NULL);

  self = g_object_new (GST_TYPE_VA_ENCODER, "display", display,
      "va-entrypoint", entrypoint, NULL);
  gst_object_ref_sink (self);

  if (!gst_va_encoder_initialize (self, codec))
    gst_clear_object (&self);

  return self;
}

void
gst_va_encoder_set_coded_buffer_size (GstVaEncoder * self,
    guint coded_buffer_size)
{
  g_return_if_fail (GST_IS_VA_ENCODER (self));
  g_return_if_fail (coded_buffer_size > 0);

  GST_OBJECT_LOCK (self);
  self->codedbuf_size = coded_buffer_size;
  GST_OBJECT_UNLOCK (self);
}

gboolean
gst_va_encoder_set_reconstruct_pool_config (GstVaEncoder * self,
    GstVideoFormat format, guint max_surfaces)
{
  GstBufferPool *old_pool = NULL;
  guint new_rt_format;

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);

  new_rt_format = gst_va_chroma_from_video_format (format);
  g_return_val_if_fail (new_rt_format > 0, FALSE);

  GST_OBJECT_LOCK (self);

  if (!_is_setup_unlocked (self))
    goto no_setup_error;

  if (new_rt_format != self->rt_format)
    goto bad_rt_format_error;

  /* if it's the same configuration, carry on */
  if (self->recon.format == format && self->recon.max_surfaces == max_surfaces)
    goto bail;

  /* if there's a previous reconstruct pool, destroy it */
  old_pool = self->recon.pool;
  self->recon.pool = NULL;

  self->recon.max_surfaces = max_surfaces;
  self->recon.format = format;

bail:
  GST_OBJECT_UNLOCK (self);

  if (old_pool) {
    GST_DEBUG_OBJECT (self, "De-allocating previous reconstruct pool");
    gst_object_unref (old_pool);
  }

  return TRUE;

  /* ERRORS */
no_setup_error:
  {
    GST_OBJECT_UNLOCK (self);
    GST_WARNING_OBJECT (self, "Can't configure reconstruct pool without setting"
        " up the encoder previously");
    return FALSE;
  }
bad_rt_format_error:
  {
    GST_OBJECT_UNLOCK (self);
    GST_WARNING_OBJECT (self, "Reconstruct pool format (%s) doesn't have same"
        " chroma as encoder setup", gst_video_format_to_string (format));
    return FALSE;
  }
}

gboolean
gst_va_encoder_get_reconstruct_pool_config (GstVaEncoder * self,
    GstCaps ** caps, guint * max_surfaces)
{
  GstBufferPool *pool;
  GstStructure *config;
  gboolean ret;

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);

  GST_OBJECT_LOCK (self);
  pool = self->recon.pool ? gst_object_ref (self->recon.pool) : NULL;
  GST_OBJECT_UNLOCK (self);

  if (!pool)
    return FALSE;

  config = gst_buffer_pool_get_config (pool);
  ret = gst_buffer_pool_config_get_params (config, caps, NULL, NULL,
      max_surfaces);
  gst_structure_free (config);

  gst_object_unref (pool);

  return ret;
}

gboolean
gst_va_encoder_has_profile (GstVaEncoder * self, VAProfile profile)
{
  VAProfile p;
  gint i;

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);

  for (i = 0; i < self->available_profiles->len; i++) {
    p = g_array_index (self->available_profiles, VAProfile, i);
    if (p == profile)
      return TRUE;
  }

  return FALSE;
}

/* Add packed header such as SPS, PPS, SEI, etc. If adding slice header,
   it is attached to the last slice parameter. */
gboolean
gst_va_encoder_add_packed_header (GstVaEncoder * self, GstVaEncodePicture * pic,
    gint type, gpointer data, gsize size_in_bits, gboolean has_emulation_bytes)
{
  VABufferID buffer;
  VAEncPackedHeaderParameterBuffer param = {
    .type = type,
    .bit_length = size_in_bits,
    .has_emulation_bytes = has_emulation_bytes,
  };

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);
  g_return_val_if_fail (self->context != VA_INVALID_ID, FALSE);
  g_return_val_if_fail (pic && data && size_in_bits > 0, FALSE);
  g_return_val_if_fail (type >= VAEncPackedHeaderSequence
      && type <= VAEncPackedHeaderRawData, FALSE);

  if (!gst_va_encoder_is_open (self)) {
    GST_ERROR_OBJECT (self, "encoder has not been opened yet");
    return FALSE;
  }

  buffer = _create_buffer (self, VAEncPackedHeaderParameterBufferType, &param,
      sizeof (param));
  if (buffer == VA_INVALID_ID)
    return FALSE;

  g_array_append_val (pic->params, buffer);

  buffer = _create_buffer (self, VAEncPackedHeaderDataBufferType, data,
      (size_in_bits + 7) / 8);
  if (buffer == VA_INVALID_ID)
    return FALSE;

  g_array_append_val (pic->params, buffer);

  return TRUE;
}

gboolean
gst_va_encoder_add_param (GstVaEncoder * self, GstVaEncodePicture * pic,
    VABufferType type, gpointer data, gsize size)
{
  VABufferID buffer;

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);
  g_return_val_if_fail (pic && data && size > 0, FALSE);

  if (!gst_va_encoder_is_open (self)) {
    GST_ERROR_OBJECT (self, "encoder has not been opened yet");
    return FALSE;
  }

  buffer = _create_buffer (self, type, data, size);
  if (buffer == VA_INVALID_ID)
    return FALSE;

  g_array_append_val (pic->params, buffer);

  return TRUE;
}

GArray *
gst_va_encoder_get_surface_formats (GstVaEncoder * self)
{
  g_return_val_if_fail (GST_IS_VA_ENCODER (self), NULL);

  if (!gst_va_encoder_is_setup (self))
    return NULL;

  return _get_surface_formats (self->display, self->config);
}

static gboolean
_get_codec_caps (GstVaEncoder * self)
{
  GstCaps *sinkpad_caps = NULL, *srcpad_caps = NULL;

  if (!gst_va_encoder_is_setup (self)
      && GST_IS_VA_DISPLAY_WRAPPED (self->display)) {
    if (gst_va_caps_from_profiles (self->display, self->available_profiles,
            self->entrypoint, &srcpad_caps, &sinkpad_caps)) {
      gst_caps_replace (&self->sinkpad_caps, sinkpad_caps);
      gst_caps_replace (&self->srcpad_caps, srcpad_caps);
      gst_caps_unref (srcpad_caps);
      gst_caps_unref (sinkpad_caps);

      return TRUE;
    }
  }

  return FALSE;
}

GstCaps *
gst_va_encoder_get_sinkpad_caps (GstVaEncoder * self)
{
  GstCaps *sinkpad_caps = NULL;

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);

  if (g_atomic_pointer_get (&self->sinkpad_caps))
    return gst_caps_ref (self->sinkpad_caps);

  if (_get_codec_caps (self))
    return gst_caps_ref (self->sinkpad_caps);

  if (gst_va_encoder_is_setup (self)) {
    VAConfigID config;

    GST_OBJECT_LOCK (self);
    config = self->config;
    GST_OBJECT_UNLOCK (self);

    sinkpad_caps = gst_va_create_raw_caps_from_config (self->display, config);
    if (!sinkpad_caps) {
      GST_WARNING_OBJECT (self, "Invalid configuration caps");
      return NULL;
    }
    gst_caps_replace (&self->sinkpad_caps, sinkpad_caps);
    gst_caps_unref (sinkpad_caps);

    return gst_caps_ref (self->sinkpad_caps);
  }

  return NULL;
}

GstCaps *
gst_va_encoder_get_srcpad_caps (GstVaEncoder * self)
{
  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);

  if (g_atomic_pointer_get (&self->srcpad_caps))
    return gst_caps_ref (self->srcpad_caps);

  if (_get_codec_caps (self))
    return gst_caps_ref (self->srcpad_caps);

  if (gst_va_encoder_is_setup (self)) {
    VAProfile profile;
    GstCaps *caps;

    GST_OBJECT_LOCK (self);
    profile = self->profile;
    GST_OBJECT_UNLOCK (self);

    caps = gst_va_create_coded_caps (self->display, profile, self->entrypoint,
        NULL);
    if (caps) {
      gst_caps_replace (&self->srcpad_caps, caps);
      return gst_caps_ref (self->srcpad_caps);
    }
  }

  return NULL;
}

static gboolean
_destroy_all_buffers (GstVaEncodePicture * pic)
{
  GstVaDisplay *display;
  VABufferID buffer;
  guint i;
  gboolean ret = TRUE;

  display = gst_va_buffer_peek_display (pic->raw_buffer);
  if (!display)
    return FALSE;

  for (i = 0; i < pic->params->len; i++) {
    buffer = g_array_index (pic->params, VABufferID, i);
    ret &= _destroy_buffer (display, buffer);
  }
  pic->params = g_array_set_size (pic->params, 0);

  return ret;
}

gboolean
gst_va_encoder_encode (GstVaEncoder * self, GstVaEncodePicture * pic)
{
  VADisplay dpy;
  VAStatus status;
  VASurfaceID surface;
  VAContextID context;
  gboolean ret = FALSE;

  g_return_val_if_fail (pic, FALSE);

  GST_OBJECT_LOCK (self);

  if (!_is_open_unlocked (self)) {
    GST_OBJECT_UNLOCK (self);
    GST_ERROR_OBJECT (self, "encoder has not been opened yet");
    return FALSE;
  }

  context = self->context;
  GST_OBJECT_UNLOCK (self);

  surface = gst_va_encode_picture_get_raw_surface (pic);
  if (surface == VA_INVALID_ID) {
    GST_ERROR_OBJECT (self, "Encode picture without valid raw surface");
    goto bail;
  }

  GST_TRACE_OBJECT (self, "Encode the surface %#x", surface);

  dpy = gst_va_display_get_va_dpy (self->display);

  status = vaBeginPicture (dpy, context, surface);
  if (status != VA_STATUS_SUCCESS) {
    GST_WARNING_OBJECT (self, "vaBeginPicture: %s", vaErrorStr (status));
    goto bail;
  }

  if (pic->params->len > 0) {
    status = vaRenderPicture (dpy, context, (VABufferID *) pic->params->data,
        pic->params->len);
    if (status != VA_STATUS_SUCCESS) {
      GST_WARNING_OBJECT (self, "vaRenderPicture: %s", vaErrorStr (status));
      goto fail_end_pic;
    }
  }

  status = vaEndPicture (dpy, context);
  ret = (status == VA_STATUS_SUCCESS);
  if (!ret)
    GST_WARNING_OBJECT (self, "vaEndPicture: %s", vaErrorStr (status));

bail:
  _destroy_all_buffers (pic);

  return ret;

fail_end_pic:
  {
    _destroy_all_buffers (pic);
    status = vaEndPicture (dpy, context);
    ret = FALSE;
    goto bail;
  }
}

VASurfaceID
gst_va_encode_picture_get_reconstruct_surface (GstVaEncodePicture * pic)
{
  g_return_val_if_fail (pic, VA_INVALID_ID);
  g_return_val_if_fail (pic->reconstruct_buffer, VA_INVALID_ID);

  return gst_va_buffer_get_surface (pic->reconstruct_buffer);
}

VASurfaceID
gst_va_encode_picture_get_raw_surface (GstVaEncodePicture * pic)
{
  g_return_val_if_fail (pic, VA_INVALID_ID);
  g_return_val_if_fail (pic->raw_buffer, VA_INVALID_ID);

  return gst_va_buffer_get_surface (pic->raw_buffer);
}

GstVaEncodePicture *
gst_va_encode_picture_new (GstVaEncoder * self, GstBuffer * raw_buffer)
{
  GstVaEncodePicture *pic;
  VABufferID coded_buffer;
  VADisplay dpy;
  VAStatus status;
  gint codedbuf_size;
  GstBufferPool *recon_pool = NULL;
  GstBuffer *reconstruct_buffer = NULL;
  GstFlowReturn ret;
  GstBufferPoolAcquireParams buffer_pool_params = {
    .flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT,
  };

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), NULL);
  g_return_val_if_fail (GST_IS_BUFFER (raw_buffer), NULL);

  GST_OBJECT_LOCK (self);

  if (!_is_open_unlocked (self)) {
    GST_OBJECT_UNLOCK (self);
    GST_ERROR_OBJECT (self, "encoder has not been opened yet");
    return NULL;
  }

  codedbuf_size = self->codedbuf_size;

  GST_OBJECT_UNLOCK (self);

  recon_pool = _get_reconstructed_buffer_pool (self);
  if (!recon_pool)
    return NULL;

  ret = gst_buffer_pool_acquire_buffer (recon_pool, &reconstruct_buffer,
      &buffer_pool_params);
  gst_clear_object (&recon_pool);

  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Failed to create the reconstruct picture");
    gst_clear_buffer (&reconstruct_buffer);
    return NULL;
  }

  /* this has to be assigned before */
  g_assert (codedbuf_size > 0);

  dpy = gst_va_display_get_va_dpy (self->display);
  status = vaCreateBuffer (dpy, self->context, VAEncCodedBufferType,
      codedbuf_size, 1, NULL, &coded_buffer);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaCreateBuffer: %s", vaErrorStr (status));
    gst_clear_buffer (&reconstruct_buffer);
    return NULL;
  }

  pic = g_new (GstVaEncodePicture, 1);
  pic->raw_buffer = gst_buffer_ref (raw_buffer);
  pic->reconstruct_buffer = reconstruct_buffer;
  pic->coded_buffer = coded_buffer;

  pic->params = g_array_sized_new (FALSE, FALSE, sizeof (VABufferID), 8);

  return pic;
}

void
gst_va_encode_picture_free (GstVaEncodePicture * pic)
{
  GstVaDisplay *display;

  g_return_if_fail (pic);

  _destroy_all_buffers (pic);

  display = gst_va_buffer_peek_display (pic->raw_buffer);
  if (!display)
    return;

  if (pic->coded_buffer != VA_INVALID_ID)
    _destroy_buffer (display, pic->coded_buffer);

  gst_buffer_unref (pic->raw_buffer);
  gst_buffer_unref (pic->reconstruct_buffer);

  g_clear_pointer (&pic->params, g_array_unref);

  g_free (pic);
}

/* currently supported rate controls */
static const GEnumValue rate_control_map[] = {
  {VA_RC_CBR, "Constant Bitrate", "cbr"},
  {VA_RC_VBR, "Variable Bitrate", "vbr"},
  {VA_RC_VCM, "Video Conferencing Mode (Non HRD compliant)", "vcm"},
  {VA_RC_CQP, "Constant Quantizer", "cqp"},
  /* {VA_RC_VBR_CONSTRAINED, "VBR with peak rate higher than average bitrate", */
  /*  "vbr-constrained"}, */
  {VA_RC_ICQ, "Intelligent Constant Quality", "icq"},
  /* {VA_RC_MB, "Macroblock based rate control", "mb"}, */
  /* {VA_RC_CFS, "Constant Frame Size", "cfs"}, */
  /* {VA_RC_PARALLEL, "Parallel BRC", "parallel"}, */
  {VA_RC_QVBR, "Quality defined VBR", "qvbr"},
  /* {VA_RC_AVBR, "Average VBR", "avbr"}, */
};

static gint
_guint32_cmp (gconstpointer a, gconstpointer b)
{
  return *((const guint32 *) a) - *((const guint32 *) b);
}

gboolean
gst_va_encoder_get_rate_control_enum (GstVaEncoder * self,
    GEnumValue ratectl[16])
{
  guint i, j, k = 0;
  guint32 rc, rc_prev = 0;
  VAProfile profile;
  GArray *rcs;

  g_return_val_if_fail (GST_IS_VA_ENCODER (self), FALSE);

  /* reseve the number of supported rate controls per profile */
  rcs = g_array_sized_new (FALSE, FALSE, sizeof (guint32),
      G_N_ELEMENTS (rate_control_map) * self->available_profiles->len);

  for (i = 0; i < self->available_profiles->len; i++) {
    profile = g_array_index (self->available_profiles, VAProfile, i);
    rc = gst_va_display_get_rate_control_mode (self->display, profile,
        self->entrypoint);
    if (rc == 0)
      continue;

    for (j = 0; j < G_N_ELEMENTS (rate_control_map); j++) {
      if (rc & rate_control_map[j].value)
        rcs = g_array_append_val (rcs, rate_control_map[j].value);
    }
  }

  if (rcs->len == 0) {
    g_clear_pointer (&rcs, g_array_unref);
    return FALSE;
  }

  g_array_sort (rcs, _guint32_cmp);

  for (i = 0; i < rcs->len; i++) {
    rc = g_array_index (rcs, guint32, i);
    if (rc == rc_prev)
      continue;

    for (j = 0; j < G_N_ELEMENTS (rate_control_map); j++) {
      if (rc == rate_control_map[j].value && k < 15)
        ratectl[k++] = rate_control_map[j];
    }

    rc_prev = rc;
  }

  g_clear_pointer (&rcs, g_array_unref);
  if (k == 0)
    return FALSE;
  /* *INDENT-OFF* */
  ratectl[k] = (GEnumValue) { 0, NULL, NULL };
  /* *INDENT-ON* */
  return TRUE;
}
