/*
 * Copyright (C) 2009 Ole André Vadla Ravnås <oravnas@cisco.com>
 *               2009 Knut Inge Hvidsten <knuhvids@cisco.com>
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

#include "miovideodevice.h"

#include <gst/video/video.h>

#include <unistd.h>

GST_DEBUG_CATEGORY_EXTERN (gst_mio_video_src_debug);
#define GST_CAT_DEFAULT gst_mio_video_src_debug

enum
{
  PROP_0,
  PROP_CONTEXT,
  PROP_HANDLE,
  PROP_UID,
  PROP_NAME,
  PROP_TRANSPORT
};

G_DEFINE_TYPE (GstMIOVideoDevice, gst_mio_video_device, G_TYPE_OBJECT);

typedef struct _GstMIOVideoFormat GstMIOVideoFormat;
typedef struct _GstMIOSetFormatCtx GstMIOSetFormatCtx;
typedef struct _GstMIOFindRateCtx GstMIOFindRateCtx;

struct _GstMIOVideoFormat
{
  TundraObjectID stream;
  CMFormatDescriptionRef desc;

  UInt32 type;
  CMVideoDimensions dim;
};

struct _GstMIOSetFormatCtx
{
  UInt32 format;
  gint width, height;
  gint fps_n, fps_d;
  gboolean success;
};

struct _GstMIOFindRateCtx
{
  gdouble needle;
  gdouble closest_match;
  gboolean success;
};

static void gst_mio_video_device_collect_format (GstMIOVideoDevice * self,
    GstMIOVideoFormat * format, gpointer user_data);
static GstStructure *gst_mio_video_device_format_basics_to_structure
    (GstMIOVideoDevice * self, GstMIOVideoFormat * format);
static gboolean gst_mio_video_device_add_framerates_to_structure
    (GstMIOVideoDevice * self, GstMIOVideoFormat * format, GstStructure * s);
static void gst_mio_video_device_add_pixel_aspect_to_structure
    (GstMIOVideoDevice * self, GstMIOVideoFormat * format, GstStructure * s);

static void gst_mio_video_device_append_framerate (GstMIOVideoDevice * self,
    GstMIOVideoFormat * format, TundraFramerate * rate, gpointer user_data);
static void gst_mio_video_device_framerate_to_fraction_value
    (TundraFramerate * rate, GValue * fract);
static gdouble gst_mio_video_device_round_to_whole_hundreths (gdouble value);
static void gst_mio_video_device_guess_pixel_aspect_ratio
    (gint width, gint height, gint * par_width, gint * par_height);

static void gst_mio_video_device_activate_matching_format
    (GstMIOVideoDevice * self, GstMIOVideoFormat * format, gpointer user_data);
static void gst_mio_video_device_find_closest_framerate
    (GstMIOVideoDevice * self, GstMIOVideoFormat * format,
    TundraFramerate * rate, gpointer user_data);

typedef void (*GstMIOVideoDeviceEachFormatFunc) (GstMIOVideoDevice * self,
    GstMIOVideoFormat * format, gpointer user_data);
typedef void (*GstMIOVideoDeviceEachFramerateFunc) (GstMIOVideoDevice * self,
    GstMIOVideoFormat * format, TundraFramerate * rate, gpointer user_data);
static void gst_mio_video_device_formats_foreach (GstMIOVideoDevice * self,
    GstMIOVideoDeviceEachFormatFunc func, gpointer user_data);
static void gst_mio_video_device_format_framerates_foreach
    (GstMIOVideoDevice * self, GstMIOVideoFormat * format,
    GstMIOVideoDeviceEachFramerateFunc func, gpointer user_data);

static gint gst_mio_video_device_compare (GstMIOVideoDevice * a,
    GstMIOVideoDevice * b);
static gint gst_mio_video_device_calculate_score (GstMIOVideoDevice * device);

static void
gst_mio_video_device_init (GstMIOVideoDevice * self)
{
}

static void
gst_mio_video_device_dispose (GObject * object)
{
  GstMIOVideoDevice *self = GST_MIO_VIDEO_DEVICE_CAST (object);

  if (self->cached_caps != NULL) {
    gst_caps_unref (self->cached_caps);
    self->cached_caps = NULL;
  }

  G_OBJECT_CLASS (gst_mio_video_device_parent_class)->dispose (object);
}

static void
gst_mio_video_device_finalize (GObject * object)
{
  GstMIOVideoDevice *self = GST_MIO_VIDEO_DEVICE_CAST (object);

  g_free (self->cached_uid);
  g_free (self->cached_name);

  G_OBJECT_CLASS (gst_mio_video_device_parent_class)->finalize (object);
}

static void
gst_mio_video_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMIOVideoDevice *self = GST_MIO_VIDEO_DEVICE (object);

  switch (prop_id) {
    case PROP_CONTEXT:
      g_value_set_pointer (value, self->ctx);
      break;
    case PROP_HANDLE:
      g_value_set_int (value, gst_mio_video_device_get_handle (self));
      break;
    case PROP_UID:
      g_value_set_string (value, gst_mio_video_device_get_uid (self));
      break;
    case PROP_NAME:
      g_value_set_string (value, gst_mio_video_device_get_name (self));
      break;
    case PROP_TRANSPORT:
      g_value_set_uint (value, gst_mio_video_device_get_transport_type (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mio_video_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMIOVideoDevice *self = GST_MIO_VIDEO_DEVICE (object);

  switch (prop_id) {
    case PROP_CONTEXT:
      self->ctx = g_value_get_pointer (value);
      break;
    case PROP_HANDLE:
      self->handle = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

TundraObjectID
gst_mio_video_device_get_handle (GstMIOVideoDevice * self)
{
  return self->handle;
}

const gchar *
gst_mio_video_device_get_uid (GstMIOVideoDevice * self)
{
  if (self->cached_uid == NULL) {
    TundraTargetSpec pspec = { 0, };

    pspec.name = kTundraObjectPropertyUID;
    pspec.scope = kTundraScopeGlobal;
    self->cached_uid =
        gst_mio_object_get_string (self->handle, &pspec, self->ctx->mio);
  }

  return self->cached_uid;
}

const gchar *
gst_mio_video_device_get_name (GstMIOVideoDevice * self)
{
  if (self->cached_name == NULL) {
    TundraTargetSpec pspec = { 0, };

    pspec.name = kTundraObjectPropertyName;
    pspec.scope = kTundraScopeGlobal;
    self->cached_name =
        gst_mio_object_get_string (self->handle, &pspec, self->ctx->mio);
  }

  return self->cached_name;
}

TundraDeviceTransportType
gst_mio_video_device_get_transport_type (GstMIOVideoDevice * self)
{
  if (self->cached_transport == kTundraDeviceTransportInvalid) {
    TundraTargetSpec pspec = { 0, };

    pspec.name = kTundraDevicePropertyTransportType;
    pspec.scope = kTundraScopeGlobal;
    self->cached_transport =
        gst_mio_object_get_uint32 (self->handle, &pspec, self->ctx->mio);
  }

  return self->cached_transport;
}

gboolean
gst_mio_video_device_open (GstMIOVideoDevice * self)
{
  /* nothing for now */
  return TRUE;
}

void
gst_mio_video_device_close (GstMIOVideoDevice * self)
{
  /* nothing for now */
}

GstCaps *
gst_mio_video_device_get_available_caps (GstMIOVideoDevice * self)
{
  if (self->cached_caps == NULL) {
    GstCaps *caps;

    caps = gst_caps_new_empty ();
    gst_mio_video_device_formats_foreach (self,
        gst_mio_video_device_collect_format, caps);

    self->cached_caps = caps;
  }

  return self->cached_caps;
}

static void
gst_mio_video_device_collect_format (GstMIOVideoDevice * self,
    GstMIOVideoFormat * format, gpointer user_data)
{
  GstCaps *caps = user_data;
  GstStructure *s;

  s = gst_mio_video_device_format_basics_to_structure (self, format);
  if (s == NULL)
    goto unsupported_format;

  if (!gst_mio_video_device_add_framerates_to_structure (self, format, s))
    goto no_framerates;

  gst_mio_video_device_add_pixel_aspect_to_structure (self, format, s);

  gst_caps_append_structure (caps, s);

  return;

  /* ERRORS */
unsupported_format:
  {
    gchar *fcc;

    fcc = gst_mio_fourcc_to_string (format->type);
    GST_WARNING ("skipping unsupported format %s", fcc);
    g_free (fcc);

    return;
  }
no_framerates:
  {
    GST_WARNING ("no framerates?");

    gst_structure_free (s);

    return;
  }
}

static GstStructure *
gst_mio_video_device_format_basics_to_structure (GstMIOVideoDevice * self,
    GstMIOVideoFormat * format)
{
  GstStructure *s;

  switch (format->type) {
    case kCVPixelFormatType_422YpCbCr8:
    case kCVPixelFormatType_422YpCbCr8Deprecated:
    {
      guint fcc;

      if (format->type == kCVPixelFormatType_422YpCbCr8)
        fcc = GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
      else
        fcc = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');

      s = gst_structure_new ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, fcc,
          "width", G_TYPE_INT, format->dim.width,
          "height", G_TYPE_INT, format->dim.height, NULL);
      break;
    }
    case kFigVideoCodecType_JPEG_OpenDML:
    {
      s = gst_structure_new ("image/jpeg",
          "width", G_TYPE_INT, format->dim.width,
          "height", G_TYPE_INT, format->dim.height, NULL);
      break;
    }
    default:
      s = NULL;
      break;
  }

  return s;
}

static gboolean
gst_mio_video_device_add_framerates_to_structure (GstMIOVideoDevice * self,
    GstMIOVideoFormat * format, GstStructure * s)
{
  GValue rates = { 0, };
  const GValue *rates_value;

  g_value_init (&rates, GST_TYPE_LIST);

  gst_mio_video_device_format_framerates_foreach (self, format,
      gst_mio_video_device_append_framerate, &rates);
  if (gst_value_list_get_size (&rates) == 0)
    goto no_framerates;

  if (gst_value_list_get_size (&rates) > 1)
    rates_value = &rates;
  else
    rates_value = gst_value_list_get_value (&rates, 0);
  gst_structure_set_value (s, "framerate", rates_value);

  g_value_unset (&rates);

  return TRUE;

  /* ERRORS */
no_framerates:
  {
    g_value_unset (&rates);
    return FALSE;
  }
}

static void
gst_mio_video_device_add_pixel_aspect_to_structure (GstMIOVideoDevice * self,
    GstMIOVideoFormat * format, GstStructure * s)
{
  gint par_width, par_height;

  gst_mio_video_device_guess_pixel_aspect_ratio
      (format->dim.width, format->dim.height, &par_width, &par_height);

  gst_structure_set (s, "pixel-aspect-ratio",
      GST_TYPE_FRACTION, par_width, par_height, NULL);
}

static void
gst_mio_video_device_append_framerate (GstMIOVideoDevice * self,
    GstMIOVideoFormat * format, TundraFramerate * rate, gpointer user_data)
{
  GValue *rates = user_data;
  GValue value = { 0, };

  g_value_init (&value, GST_TYPE_FRACTION);
  gst_mio_video_device_framerate_to_fraction_value (rate, &value);
  gst_value_list_append_value (rates, &value);
  g_value_unset (&value);
}

static void
gst_mio_video_device_framerate_to_fraction_value (TundraFramerate * rate,
    GValue * fract)
{
  gdouble rounded;
  gint n, d;

  rounded = gst_mio_video_device_round_to_whole_hundreths (rate->value);
  gst_util_double_to_fraction (rounded, &n, &d);
  gst_value_set_fraction (fract, n, d);
}

static gdouble
gst_mio_video_device_round_to_whole_hundreths (gdouble value)
{
  gdouble m, x, y, z;

  m = 0.01;
  x = value;
  y = floor ((x / m) + 0.5);
  z = y * m;

  return z;
}

static void
gst_mio_video_device_guess_pixel_aspect_ratio (gint width, gint height,
    gint * par_width, gint * par_height)
{
  /*
   * As we dont have access to the actual pixel aspect, we will try to do a
   * best-effort guess. The guess is based on most sensors being either 4/3
   * or 16/9, and most pixel aspects being close to 1/1.
   */

  if (width == 768 && height == 448) {  /* special case for w448p */
    *par_width = 28;
    *par_height = 27;
  } else {
    if (((gdouble) width / (gdouble) height) < 1.2778) {
      *par_width = 12;
      *par_height = 11;
    } else {
      *par_width = 1;
      *par_height = 1;
    }
  }
}

gboolean
gst_mio_video_device_set_caps (GstMIOVideoDevice * self, GstCaps * caps)
{
  GstVideoFormat format;
  GstMIOSetFormatCtx ctx = { 0, };

  if (gst_video_format_parse_caps (caps, &format, &ctx.width, &ctx.height)) {
    if (format == GST_VIDEO_FORMAT_UYVY)
      ctx.format = kCVPixelFormatType_422YpCbCr8;
    else if (format == GST_VIDEO_FORMAT_YUY2)
      ctx.format = kCVPixelFormatType_422YpCbCr8Deprecated;
    else
      g_assert_not_reached ();
  } else {
    GstStructure *s;

    s = gst_caps_get_structure (caps, 0);
    g_assert (gst_structure_has_name (s, "image/jpeg"));
    gst_structure_get_int (s, "width", &ctx.width);
    gst_structure_get_int (s, "height", &ctx.height);

    ctx.format = kFigVideoCodecType_JPEG_OpenDML;
  }

  gst_video_parse_caps_framerate (caps, &ctx.fps_n, &ctx.fps_d);

  gst_mio_video_device_formats_foreach (self,
      gst_mio_video_device_activate_matching_format, &ctx);

  return ctx.success;
}

static void
gst_mio_video_device_activate_matching_format (GstMIOVideoDevice * self,
    GstMIOVideoFormat * format, gpointer user_data)
{
  GstMIOSetFormatCtx *ctx = user_data;
  GstMIOFindRateCtx find_ctx;
  TundraTargetSpec spec = { 0, };
  TundraStatus status;

  if (format->type != ctx->format)
    return;
  else if (format->dim.width != ctx->width)
    return;
  else if (format->dim.height != ctx->height)
    return;

  find_ctx.needle = (gdouble) ctx->fps_n / (gdouble) ctx->fps_d;
  find_ctx.closest_match = 0.0;
  find_ctx.success = FALSE;
  gst_mio_video_device_format_framerates_foreach (self, format,
      gst_mio_video_device_find_closest_framerate, &find_ctx);
  if (!find_ctx.success)
    goto no_matching_framerate_found;

  spec.scope = kTundraScopeInput;

  spec.name = kTundraStreamPropertyFormatDescription;
  status = self->ctx->mio->TundraObjectSetPropertyData (format->stream, &spec,
      NULL, NULL, sizeof (format->desc), &format->desc);
  if (status != kTundraSuccess)
    goto failed_to_set_format;

  spec.name = kTundraStreamPropertyFrameRate;
  status = self->ctx->mio->TundraObjectSetPropertyData (format->stream, &spec,
      NULL, NULL, sizeof (find_ctx.closest_match), &find_ctx.closest_match);
  if (status != kTundraSuccess)
    goto failed_to_set_framerate;

  self->selected_format = format->desc;
  self->selected_fps_n = ctx->fps_n;
  self->selected_fps_d = ctx->fps_d;

  ctx->success = TRUE;
  return;

  /* ERRORS */
no_matching_framerate_found:
  {
    GST_ERROR ("no matching framerate found");
    return;
  }
failed_to_set_format:
  {
    GST_ERROR ("failed to set format: 0x%08x", status);
    return;
  }
failed_to_set_framerate:
  {
    GST_ERROR ("failed to set framerate: 0x%08x", status);
    return;
  }
}

static void
gst_mio_video_device_find_closest_framerate (GstMIOVideoDevice * self,
    GstMIOVideoFormat * format, TundraFramerate * rate, gpointer user_data)
{
  GstMIOFindRateCtx *ctx = user_data;

  if (fabs (rate->value - ctx->needle) <= 0.1) {
    ctx->closest_match = rate->value;
    ctx->success = TRUE;
  }
}

CMFormatDescriptionRef
gst_mio_video_device_get_selected_format (GstMIOVideoDevice * self)
{
  return self->selected_format;
}

GstClockTime
gst_mio_video_device_get_duration (GstMIOVideoDevice * self)
{
  return gst_util_uint64_scale_int (GST_SECOND,
      self->selected_fps_d, self->selected_fps_n);
}

static void
gst_mio_video_device_formats_foreach (GstMIOVideoDevice * self,
    GstMIOVideoDeviceEachFormatFunc func, gpointer user_data)
{
  GstCMApi *cm = self->ctx->cm;
  GstMIOApi *mio = self->ctx->mio;
  TundraTargetSpec spec = { 0, };
  GArray *streams;
  guint stream_idx;

  spec.name = kTundraDevicePropertyStreams;
  spec.scope = kTundraScopeInput;
  streams = gst_mio_object_get_array (self->handle, &spec,
      sizeof (TundraObjectID), mio);

  /* TODO: We only consider the first stream for now */
  for (stream_idx = 0; stream_idx != MIN (streams->len, 1); stream_idx++) {
    TundraObjectID stream;
    CFArrayRef formats;
    CFIndex num_formats, fmt_idx;

    stream = g_array_index (streams, TundraObjectID, stream_idx);

    spec.name = kTundraStreamPropertyFormatDescriptions;
    spec.scope = kTundraScopeInput;

    formats = gst_mio_object_get_pointer (stream, &spec, mio);
    num_formats = CFArrayGetCount (formats);

    for (fmt_idx = 0; fmt_idx != num_formats; fmt_idx++) {
      GstMIOVideoFormat fmt;

      fmt.stream = stream;
      fmt.desc = (CMFormatDescriptionRef)
          CFArrayGetValueAtIndex (formats, fmt_idx);
      if (cm->CMFormatDescriptionGetMediaType (fmt.desc) != kFigMediaTypeVideo)
        continue;
      fmt.type = cm->CMFormatDescriptionGetMediaSubType (fmt.desc);
      fmt.dim = cm->CMVideoFormatDescriptionGetDimensions (fmt.desc);

      func (self, &fmt, user_data);
    }
  }

  g_array_free (streams, TRUE);
}

static void
gst_mio_video_device_format_framerates_foreach (GstMIOVideoDevice * self,
    GstMIOVideoFormat * format, GstMIOVideoDeviceEachFramerateFunc func,
    gpointer user_data)
{
  TundraTargetSpec spec = { 0, };
  GArray *rates;
  guint rate_idx;

  spec.name = kTundraStreamPropertyFrameRates;
  spec.scope = kTundraScopeInput;
  rates = gst_mio_object_get_array_full (format->stream, &spec,
      sizeof (format->desc), &format->desc, sizeof (TundraFramerate),
      self->ctx->mio);

  for (rate_idx = 0; rate_idx != rates->len; rate_idx++) {
    TundraFramerate *rate;

    rate = &g_array_index (rates, TundraFramerate, rate_idx);

    func (self, format, rate, user_data);
  }

  g_array_free (rates, TRUE);
}

void
gst_mio_video_device_print_debug_info (GstMIOVideoDevice * self)
{
  GstCMApi *cm = self->ctx->cm;
  GstMIOApi *mio = self->ctx->mio;
  TundraTargetSpec spec = { 0, };
  gchar *str;
  GArray *streams;
  guint stream_idx;

  g_print ("Device %p with handle %d\n", self, self->handle);

  spec.scope = kTundraScopeGlobal;

  spec.name = kTundraObjectPropertyClass;
  str = gst_mio_object_get_fourcc (self->handle, &spec, mio);
  g_print ("  Class: '%s'\n", str);
  g_free (str);

  spec.name = kTundraObjectPropertyCreator;
  str = gst_mio_object_get_string (self->handle, &spec, mio);
  g_print ("  Creator: \"%s\"\n", str);
  g_free (str);

  spec.name = kTundraDevicePropertyModelUID;
  str = gst_mio_object_get_string (self->handle, &spec, mio);
  g_print ("  Model UID: \"%s\"\n", str);
  g_free (str);

  spec.name = kTundraDevicePropertyTransportType;
  str = gst_mio_object_get_fourcc (self->handle, &spec, mio);
  g_print ("  Transport Type: '%s'\n", str);
  g_free (str);

  g_print ("  Streams:\n");
  spec.name = kTundraDevicePropertyStreams;
  spec.scope = kTundraScopeInput;
  streams = gst_mio_object_get_array (self->handle, &spec,
      sizeof (TundraObjectID), mio);
  for (stream_idx = 0; stream_idx != streams->len; stream_idx++) {
    TundraObjectID stream;
    CFArrayRef formats;
    CFIndex num_formats, fmt_idx;

    stream = g_array_index (streams, TundraObjectID, stream_idx);

    g_print ("    stream[%u] = %d\n", stream_idx, stream);

    spec.scope = kTundraScopeInput;
    spec.name = kTundraStreamPropertyFormatDescriptions;

    formats = gst_mio_object_get_pointer (stream, &spec, mio);
    num_formats = CFArrayGetCount (formats);

    g_print ("      <%u formats>\n", (guint) num_formats);

    for (fmt_idx = 0; fmt_idx != num_formats; fmt_idx++) {
      CMFormatDescriptionRef fmt;
      gchar *media_type;
      gchar *media_sub_type;
      CMVideoDimensions dim;
      GArray *rates;
      guint rate_idx;

      fmt = CFArrayGetValueAtIndex (formats, fmt_idx);
      media_type = gst_mio_fourcc_to_string
          (cm->CMFormatDescriptionGetMediaType (fmt));
      media_sub_type = gst_mio_fourcc_to_string
          (cm->CMFormatDescriptionGetMediaSubType (fmt));
      dim = cm->CMVideoFormatDescriptionGetDimensions (fmt);

      g_print ("      format[%u]: MediaType='%s' MediaSubType='%s' %ux%u\n",
          (guint) fmt_idx, media_type, media_sub_type,
          (guint) dim.width, (guint) dim.height);

      spec.name = kTundraStreamPropertyFrameRates;
      rates = gst_mio_object_get_array_full (stream, &spec, sizeof (fmt), &fmt,
          sizeof (TundraFramerate), mio);
      for (rate_idx = 0; rate_idx != rates->len; rate_idx++) {
        TundraFramerate *rate;

        rate = &g_array_index (rates, TundraFramerate, rate_idx);
        g_print ("        %f\n", rate->value);
      }
      g_array_free (rates, TRUE);

      g_free (media_sub_type);
      g_free (media_type);
    }
  }

  g_array_free (streams, TRUE);
}

static void
gst_mio_video_device_class_init (GstMIOVideoDeviceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_mio_video_device_dispose;
  gobject_class->finalize = gst_mio_video_device_finalize;
  gobject_class->get_property = gst_mio_video_device_get_property;
  gobject_class->set_property = gst_mio_video_device_set_property;

  g_object_class_install_property (gobject_class, PROP_CONTEXT,
      g_param_spec_pointer ("context", "CoreMedia Context",
          "CoreMedia context to use",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_HANDLE,
      g_param_spec_int ("handle", "Handle",
          "MIO handle of this video capture device",
          G_MININT, G_MAXINT, -1,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_UID,
      g_param_spec_string ("uid", "Unique ID",
          "Unique ID of this video capture device", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_NAME,
      g_param_spec_string ("name", "Device Name",
          "Name of this video capture device", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TRANSPORT,
      g_param_spec_uint ("transport", "Transport",
          "Transport type of this video capture device",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

GList *
gst_mio_video_device_list_create (GstCoreMediaCtx * ctx)
{
  GList *devices = NULL;
  TundraTargetSpec pspec = { 0, };
  GArray *handles;
  guint handle_idx;

  pspec.name = kTundraSystemPropertyDevices;
  pspec.scope = kTundraScopeGlobal;
  handles = gst_mio_object_get_array (TUNDRA_SYSTEM_OBJECT_ID, &pspec,
      sizeof (TundraObjectID), ctx->mio);
  if (handles == NULL)
    goto beach;

  for (handle_idx = 0; handle_idx != handles->len; handle_idx++) {
    TundraObjectID handle;
    GstMIOVideoDevice *device;

    handle = g_array_index (handles, TundraObjectID, handle_idx);
    device = g_object_new (GST_TYPE_MIO_VIDEO_DEVICE,
        "context", ctx, "handle", handle, NULL);

    /* TODO: Skip screen input devices for now */
    if (gst_mio_video_device_get_transport_type (device) !=
        kTundraDeviceTransportScreen) {
      devices = g_list_prepend (devices, device);
    } else {
      g_object_unref (device);
    }
  }

  devices = g_list_sort (devices, (GCompareFunc) gst_mio_video_device_compare);

  g_array_free (handles, TRUE);

beach:
  return devices;
}

void
gst_mio_video_device_list_destroy (GList * devices)
{
  g_list_foreach (devices, (GFunc) g_object_unref, NULL);
  g_list_free (devices);
}

static gint
gst_mio_video_device_compare (GstMIOVideoDevice * a, GstMIOVideoDevice * b)
{
  gint score_a, score_b;

  score_a = gst_mio_video_device_calculate_score (a);
  score_b = gst_mio_video_device_calculate_score (b);

  if (score_a > score_b)
    return -1;
  else if (score_a < score_b)
    return 1;

  return g_ascii_strcasecmp (gst_mio_video_device_get_name (a),
      gst_mio_video_device_get_name (b));
}

static gint
gst_mio_video_device_calculate_score (GstMIOVideoDevice * device)
{
  switch (gst_mio_video_device_get_transport_type (device)) {
    case kTundraDeviceTransportScreen:
      return 0;
    case kTundraDeviceTransportBuiltin:
      return 1;
    case kTundraDeviceTransportUSB:
      return 2;
    default:
      return 3;
  }
}
