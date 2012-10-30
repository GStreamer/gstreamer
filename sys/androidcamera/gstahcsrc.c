/* GStreamer android.hardware.Camera Source
 *
 * Copyright (C) 2012, Cisco Systems, Inc.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
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
#  include "config.h"
#endif

#include <gst/video/video.h>
#include <gst/interfaces/propertyprobe.h>

#include "gstahcsrc.h"
#include "gst-dvm.h"

static void gst_ahc_src_property_probe_interface_init (GstPropertyProbeInterface
    * iface);
static void gst_ahc_src_init_interfaces (GType type);

static void gst_ahc_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ahc_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ahc_src_dispose (GObject * object);

static GstStateChangeReturn gst_ahc_src_change_state (GstElement * element,
    GstStateChange transition);
static GstCaps *gst_ahc_src_getcaps (GstBaseSrc * src);
static gboolean gst_ahc_src_setcaps (GstBaseSrc * src, GstCaps * caps);
static void gst_ahc_src_fixate (GstBaseSrc * basesrc, GstCaps * caps);
static gboolean gst_ahc_src_start (GstBaseSrc * bsrc);
static gboolean gst_ahc_src_stop (GstBaseSrc * bsrc);
static gboolean gst_ahc_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_ahc_src_unlock_stop (GstBaseSrc * bsrc);
static GstFlowReturn gst_ahc_src_create (GstPushSrc * src, GstBuffer ** buffer);
static gboolean gst_ahc_src_query (GstBaseSrc * bsrc, GstQuery * query);

static void gst_ahc_src_close (GstAHCSrc * self);
static void gst_ahc_src_on_preview_frame (jbyteArray data, gpointer user_data);
static void gst_ahc_src_on_error (int error, gpointer user_data);

#define NUM_CALLBACK_BUFFERS 5

#define GST_AHC_SRC_CAPS_STR                                    \
  GST_VIDEO_CAPS_YUV (" { YV12 , YUY2 , NV21 , NV16 }") ";"     \
  GST_VIDEO_CAPS_RGB_16

static GstStaticPadTemplate gst_ahc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AHC_SRC_CAPS_STR));

GST_DEBUG_CATEGORY_STATIC (gst_ahc_src_debug);
#define GST_CAT_DEFAULT gst_ahc_src_debug

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_DEVICE_FACING,
  PROP_DEVICE_ORIENTATION,
};

#define DEFAULT_DEVICE "0"

GST_BOILERPLATE_FULL (GstAHCSrc, gst_ahc_src, GstPushSrc, GST_TYPE_PUSH_SRC,
    gst_ahc_src_init_interfaces);

#define CAMERA_FACING_BACK 0
#define CAMERA_FACING_FRONT 1

static GType
gst_ahc_src_facing_get_type (void)
{
  static GType type = 0;
  static const GEnumValue types[] = {
    {CAMERA_FACING_BACK, "Back", "back"},
    {CAMERA_FACING_FRONT, "Front", "front"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("GstAHCSrcFacing", types);
  }
  return type;
}

#define GST_AHC_SRC_FACING_TYPE (gst_ahc_src_facing_get_type())

static void
gst_ahc_src_init_interfaces (GType type)
{
  static const GInterfaceInfo ahcsrc_propertyprobe_info = {
    (GInterfaceInitFunc) gst_ahc_src_property_probe_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_PROPERTY_PROBE,
      &ahcsrc_propertyprobe_info);
}

static void
gst_ahc_src_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (gst_ahc_src_debug, "ahcsrc", 0,
      "android.hardware.Camera source element");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_ahc_src_pad_template);
  gst_element_class_set_details_simple (gstelement_class,
      "Android Camera Source",
      "Source/Video",
      "Reads frames from android.hardware.Camera class into buffers",
      "Youness Alaoui <youness.alaoui@collabora.co.uk>");
}

static void
gst_ahc_src_class_init (GstAHCSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_ahc_src_set_property;
  gobject_class->get_property = gst_ahc_src_get_property;
  gobject_class->dispose = gst_ahc_src_dispose;

  element_class->change_state = gst_ahc_src_change_state;

  gstbasesrc_class->get_caps = gst_ahc_src_getcaps;
  gstbasesrc_class->set_caps = gst_ahc_src_setcaps;
  gstbasesrc_class->fixate = gst_ahc_src_fixate;
  gstbasesrc_class->start = gst_ahc_src_start;
  gstbasesrc_class->stop = gst_ahc_src_stop;
  gstbasesrc_class->unlock = gst_ahc_src_unlock;
  gstbasesrc_class->unlock_stop = gst_ahc_src_unlock_stop;
  gstbasesrc_class->query = gst_ahc_src_query;

  gstpushsrc_class->create = gst_ahc_src_create;

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "device",
          "Device ID", DEFAULT_DEVICE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEVICE_ORIENTATION,
      g_param_spec_int ("device-orientation", "Device orientation",
          "The orientation of the camera image",
          0, 360, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEVICE_FACING,
      g_param_spec_enum ("device-facing", "Device facing",
          "The direction that the camera faces",
          GST_AHC_SRC_FACING_TYPE, CAMERA_FACING_BACK,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  klass->probe_properties = NULL;
}

static gboolean
_data_queue_check_full (GstDataQueue * queue, guint visible,
    guint bytes, guint64 time, gpointer checkdata)
{
  return FALSE;
}

static void
gst_ahc_src_init (GstAHCSrc * self, GstAHCSrcClass * klass)
{
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (self), FALSE);

  self->camera = NULL;
  self->texture = NULL;
  self->data = NULL;
  self->queue = gst_data_queue_new (_data_queue_check_full, NULL);
  self->start = FALSE;
  self->previous_ts = GST_CLOCK_TIME_NONE;
}

static void
gst_ahc_src_dispose (GObject * object)
{
  GstAHCSrc *self = GST_AHC_SRC (object);

  if (self->queue)
    g_object_unref (self->queue);
  self->queue = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_ahc_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAHCSrc *self = GST_AHC_SRC (object);
  (void) self;

  switch (prop_id) {
    case PROP_DEVICE:{
      const gchar *dev = g_value_get_string (value);
      gchar *endptr = NULL;
      guint64 device;

      device = g_ascii_strtoll (dev, &endptr, 10);
      if (endptr != dev && endptr[0] == 0 && device < G_MAXINT)
        self->device = (gint) device;
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ahc_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAHCSrc *self = GST_AHC_SRC (object);
  (void) self;

  switch (prop_id) {
    case PROP_DEVICE:{
      gchar *dev = g_strdup_printf ("%d", self->device);

      g_value_take_string (value, dev);
    }
      break;
    case PROP_DEVICE_FACING:{
      GstAHCCameraInfo info;

      if (gst_ah_camera_get_camera_info (self->device, &info))
        g_value_set_enum (value, info.facing == CameraInfo_CAMERA_FACING_BACK ?
            CAMERA_FACING_BACK : CAMERA_FACING_FRONT);
      else
        g_value_set_enum (value, CAMERA_FACING_BACK);
    }
      break;
    case PROP_DEVICE_ORIENTATION:{
      GstAHCCameraInfo info;

      if (gst_ah_camera_get_camera_info (self->device, &info))
        g_value_set_int (value, info.orientation);
      else
        g_value_set_int (value, 0);
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static const GList *
gst_ahc_src_probe_get_properties (GstPropertyProbe * probe)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (probe);
  GstAHCSrcClass *ahc_class = GST_AHC_SRC_CLASS (probe);


  if (!ahc_class->probe_properties)
    ahc_class->probe_properties = g_list_append (NULL,
        g_object_class_find_property (klass, "device"));

  return ahc_class->probe_properties;
}

static GValueArray *
gst_ahc_src_probe_get_values (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GValueArray *array = NULL;

  switch (prop_id) {
    case PROP_DEVICE:{
      GValue value = { 0 };
      gint num_cams = gst_ah_camera_get_number_of_cameras ();
      gint i;

      array = g_value_array_new (num_cams);
      g_value_init (&value, G_TYPE_STRING);
      for (i = 0; i < num_cams; i++) {
        g_value_take_string (&value, g_strdup_printf ("%d", i));
        g_value_array_append (array, &value);
      }
      g_value_unset (&value);
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return array;
}

static void
gst_ahc_src_property_probe_interface_init (GstPropertyProbeInterface * iface)
{
  iface->get_properties = gst_ahc_src_probe_get_properties;
  iface->get_values = gst_ahc_src_probe_get_values;
}

static gint
_compare_formats (int f1, int f2)
{
  if (f1 == f2)
    return 0;
  /* YV12 has priority */
  if (f1 == ImageFormat_YV12)
    return -1;
  if (f2 == ImageFormat_YV12)
    return 1;
  /* Then NV21 */
  if (f1 == ImageFormat_NV21)
    return -1;
  if (f2 == ImageFormat_NV21)
    return 1;
  /* Then we don't care */
  return f2 - f1;
}

static gint
_compare_sizes (GstAHCSize * s1, GstAHCSize * s2)
{
  return ((s2->width * s2->height) - (s1->width * s1->height));
}


static gint
_compare_ranges (int *r1, int *r2)
{
  if (r1[1] == r2[1])
    /* Smallest range */
    return (r1[1] - r1[0]) - (r2[1] - r2[0]);
  else
    /* Highest fps */
    return r2[1] - r1[1];
}

static GstCaps *
gst_ahc_src_getcaps (GstBaseSrc * src)
{
  GstAHCSrc *self = GST_AHC_SRC (src);

  if (self->camera) {
    GstCaps *ret = gst_caps_new_empty ();
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      GList *formats, *sizes, *ranges;
      GList *i, *j, *k;
      int previous_format = ImageFormat_UNKNOWN;

      formats = gst_ahc_parameters_get_supported_preview_formats (params);
      formats = g_list_sort (formats, (GCompareFunc) _compare_formats);
      sizes = gst_ahc_parameters_get_supported_preview_sizes (params);
      sizes = g_list_sort (sizes, (GCompareFunc) _compare_sizes);
      ranges = gst_ahc_parameters_get_supported_preview_fps_range (params);
      ranges = g_list_sort (ranges, (GCompareFunc) _compare_ranges);
      GST_DEBUG_OBJECT (self, "Supported preview formats:");

      for (i = formats; i; i = i->next) {
        int f = GPOINTER_TO_INT (i->data);
        GstStructure *format = NULL;

        /* Ignore duplicates */
        if (f == previous_format)
          continue;

        /* Can't use switch/case because the values are not constants */
        if (f == ImageFormat_NV16) {
          GST_DEBUG_OBJECT (self, "    NV16 (%d)", f);
          format = gst_structure_new ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('N', 'V', '1', '6'),
              NULL);
        } else if (f == ImageFormat_NV21) {
          GST_DEBUG_OBJECT (self, "    NV21 (%d)", f);
          format = gst_structure_new ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('N', 'V', '2', '1'),
              NULL);
        } else if (f == ImageFormat_RGB_565) {
          GST_DEBUG_OBJECT (self, "    RGB565 (%d)", f);
          format = gst_structure_new ("video/x-raw-rgb",
              "bpp", G_TYPE_INT, 16,
              "depth", G_TYPE_INT, 16,
              "red_mask", G_TYPE_INT, 0xf800,
              "green_mask", G_TYPE_INT, 0x07e0,
              "blue_mask", G_TYPE_INT, 0x001f,
              "endianness", G_TYPE_INT, G_LITTLE_ENDIAN, NULL);
        } else if (f == ImageFormat_YUY2) {
          GST_DEBUG_OBJECT (self, "    YUY2 (%d)", f);
          format = gst_structure_new ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
              NULL);
        } else if (f == ImageFormat_YV12) {
          GST_DEBUG_OBJECT (self, "    YV12 (%d)", f);
          format = gst_structure_new ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y', 'V', '1', '2'),
              NULL);
        }
        previous_format = f;

        if (format) {
          for (j = sizes; j; j = j->next) {
            GstAHCSize *s = j->data;
            GstStructure *size;

            size = gst_structure_copy (format);
            gst_structure_set (size, "width", G_TYPE_INT, s->width,
                "height", G_TYPE_INT, s->height,
                "interlaced", G_TYPE_BOOLEAN, FALSE,
                "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);

            for (k = ranges; k; k = k->next) {
              int *range = k->data;
              GstStructure *s;

              s = gst_structure_copy (size);
              if (range[0] == range[1]) {
                gst_structure_set (s, "framerate", GST_TYPE_FRACTION,
                    range[0], 1000, NULL);
              } else {
                gst_structure_set (s, "framerate", GST_TYPE_FRACTION_RANGE,
                    range[0], 1000, range[1], 1000, NULL);
              }
              gst_caps_append_structure (ret, s);
            }
            gst_structure_free (size);
          }
          gst_structure_free (format);
        }
      }
      GST_DEBUG_OBJECT (self, "Supported preview sizes:");
      for (i = sizes; i; i = i->next) {
        GstAHCSize *s = i->data;

        GST_DEBUG_OBJECT (self, "    %dx%d", s->width, s->height);
      }
      GST_DEBUG_OBJECT (self, "Supported preview fps range:");
      for (i = ranges; i; i = i->next) {
        int *range = i->data;

        GST_DEBUG_OBJECT (self, "    [%d, %d]", range[0], range[1]);
      }

      gst_ahc_parameters_supported_preview_formats_free (formats);
      gst_ahc_parameters_supported_preview_sizes_free (sizes);
      gst_ahc_parameters_supported_preview_fps_range_free (ranges);
    }
    gst_ahc_parameters_free (params);

    return ret;
  } else {
    return NULL;
  }
}

static void
gst_ahc_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  GstAHCSrc *self = GST_AHC_SRC (src);
  GstStructure *s = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (self, "Fixating : %" GST_PTR_FORMAT, caps);

  /* Width/height will be fixed already here, format will
   * be left for fixation by the default handler.
   * We only have to fixate framerate here, to the
   * highest possible framerate.
   */
  gst_structure_fixate_field_nearest_fraction (s, "framerate", G_MAXINT, 1);
}

static gboolean
gst_ahc_src_setcaps (GstBaseSrc * src, GstCaps * caps)
{
  GstAHCSrc *self = GST_AHC_SRC (src);
  gboolean ret = FALSE;
  GstAHCParameters *params = NULL;

  if (!self->camera) {
    GST_WARNING_OBJECT (self, "setcaps called without a camera available");
    goto end;
  }

  params = gst_ah_camera_get_parameters (self->camera);
  if (params) {
    GstVideoFormat format;
    gint fmt;
    gint width, height, fps_n, fps_d, buffer_size;
    GList *ranges, *l;
    gint range_size = G_MAXINT;

    if (!gst_video_format_parse_caps (caps, &format, &width, &height) ||
        !gst_video_parse_caps_framerate (caps, &fps_n, &fps_d)) {
      GST_WARNING_OBJECT (self, "unable to parse video caps");
      goto end;
    }
    fps_n *= 1000 / fps_d;

    /* Select the best range that contains our framerate.
     * We *must* set a range of those returned by the camera
     * according to the API docs and can't use a subset of any
     * of those ranges.
     * We chose the smallest range that contains the target
     * framerate.
     */
    self->fps_max = self->fps_min = 0;
    ranges = gst_ahc_parameters_get_supported_preview_fps_range (params);
    ranges = g_list_sort (ranges, (GCompareFunc) _compare_ranges);
    for (l = ranges; l; l = l->next) {
      int *range = l->data;

      if (fps_n >= range[0] && fps_n <= range[1] &&
          range_size > (range[1] - range[0])) {
        self->fps_min = range[0];
        self->fps_max = range[1];
        range_size = range[1] - range[0];
      }
    }
    gst_ahc_parameters_supported_preview_fps_range_free (ranges);
    if (self->fps_max == 0) {
      GST_ERROR_OBJECT (self, "Couldn't find an applicable FPS range");
      goto end;
    }

    switch (format) {
      case GST_VIDEO_FORMAT_YV12:
        fmt = ImageFormat_YV12;
        break;
      case GST_VIDEO_FORMAT_NV21:
        fmt = ImageFormat_NV21;
        break;
      case GST_VIDEO_FORMAT_YUY2:
        fmt = ImageFormat_YUY2;
        break;
      case GST_VIDEO_FORMAT_RGB16:
        fmt = ImageFormat_RGB_565;
        break;
        /* GST_VIDEO_FORMAT_NV16 doesn't exist */
        //case GST_VIDEO_FORMAT_NV16:
        //fmt = ImageFormat_NV16;
        //break;
      default:
        fmt = ImageFormat_UNKNOWN;
        break;
    }

    if (fmt == ImageFormat_UNKNOWN) {
      GST_WARNING_OBJECT (self, "unsupported video format");
      goto end;
    }

    gst_ahc_parameters_set_preview_size (params, width, height);
    gst_ahc_parameters_set_preview_format (params, fmt);
    gst_ahc_parameters_set_preview_fps_range (params, self->fps_min,
        self->fps_max);

    GST_DEBUG_OBJECT (self, "Setting camera parameters : %d %dx%d @ [%f, %f]",
        fmt, width, height, self->fps_min / 1000.0, self->fps_max / 1000.0);

    if (!gst_ah_camera_set_parameters (self->camera, params)) {
      GST_WARNING_OBJECT (self, "Unable to set video parameters");
      goto end;
    }

    self->width = width;
    self->height = height;
    self->format = fmt;
    buffer_size = width * height *
        ((double) gst_ag_imageformat_get_bits_per_pixel (fmt) / 8);
    if (buffer_size > self->buffer_size) {
      JNIEnv *env = gst_dvm_get_env ();
      gint i;

      for (i = 0; i < NUM_CALLBACK_BUFFERS; i++) {
        jbyteArray array = (*env)->NewByteArray (env, buffer_size);

        if (array) {
          gst_ah_camera_add_callback_buffer (self->camera, array);
          (*env)->DeleteLocalRef (env, array);
        }
      }
    }
    self->buffer_size = buffer_size;
    ret = TRUE;
  }

end:
  if (params)
    gst_ahc_parameters_free (params);

  if (ret && self->start) {
    GST_DEBUG_OBJECT (self, "Starting preview");
    ret = gst_ah_camera_start_preview (self->camera);
    if (ret) {
      /* Need to reset callbacks after every startPreview */
      gst_ah_camera_set_preview_callback_with_buffer (self->camera,
          gst_ahc_src_on_preview_frame, self);
      gst_ah_camera_set_error_callback (self->camera, gst_ahc_src_on_error,
          self);
      self->start = FALSE;
    }
  }
  return ret;
}

typedef struct
{
  GstAHCSrc *self;
  jbyteArray array;
  jbyte *data;
} FreeFuncBuffer;

static void
gst_ahc_src_buffer_free_func (gpointer priv)
{
  FreeFuncBuffer *data = (FreeFuncBuffer *) priv;
  GstAHCSrc *self = data->self;
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->ReleaseByteArrayElements (env, data->array, data->data, JNI_ABORT);
  if (self->camera)
    gst_ah_camera_add_callback_buffer (self->camera, data->array);
  (*env)->DeleteGlobalRef (env, data->array);
  gst_object_unref (self);

  g_slice_free (FreeFuncBuffer, data);
}

static void
_data_queue_item_free (GstDataQueueItem * item)
{
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static void
gst_ahc_src_on_preview_frame (jbyteArray array, gpointer user_data)
{
  GstAHCSrc *self = GST_AHC_SRC (user_data);
  JNIEnv *env = gst_dvm_get_env ();
  GstBuffer *buffer;
  GstDataQueueItem *item = NULL;
  FreeFuncBuffer *malloc_data = NULL;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE;
  GstClockTime duration = 0;
  GstClock *clock;

  if (array == NULL) {
    GST_DEBUG_OBJECT (self, "Size of array in queue is too small, dropping it");
    return;
  }

  if ((clock = GST_ELEMENT_CLOCK (self))) {
    GstClockTime base_time = GST_ELEMENT_CAST (self)->base_time;
    GstClockTime current_ts;

    gst_object_ref (clock);
    current_ts = gst_clock_get_time (clock) - base_time;
    gst_object_unref (clock);
    if (GST_CLOCK_TIME_IS_VALID (self->previous_ts)) {
      timestamp = self->previous_ts;
      duration = current_ts - self->previous_ts;
      self->previous_ts = current_ts;
    } else {
      /* Drop the first buffer */
      self->previous_ts = current_ts;
      gst_ah_camera_add_callback_buffer (self->camera, array);
      return;
    }
  } else {
    gst_ah_camera_add_callback_buffer (self->camera, array);
    return;
  }
  //GST_WARNING_OBJECT (self, "Received data buffer %p", data);
  malloc_data = g_slice_new0 (FreeFuncBuffer);
  malloc_data->self = gst_object_ref (self);
  malloc_data->array = (*env)->NewGlobalRef (env, array);
  malloc_data->data = (*env)->GetByteArrayElements (env, array, NULL);

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = (guint8 *) malloc_data->data;
  GST_BUFFER_SIZE (buffer) = self->buffer_size;
  GST_BUFFER_MALLOCDATA (buffer) = (gpointer) malloc_data;
  GST_BUFFER_FREE_FUNC (buffer) = gst_ahc_src_buffer_free_func;
  GST_BUFFER_DURATION (buffer) = duration;
  GST_BUFFER_TIMESTAMP (buffer) = timestamp;

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->size = GST_BUFFER_SIZE (buffer);
  item->duration = GST_BUFFER_DURATION (buffer);
  item->visible = TRUE;
  item->destroy = (GDestroyNotify) _data_queue_item_free;

  if (!gst_data_queue_push (self->queue, item)) {
    /* Can't add buffer to queue. Must be flushing. */
    _data_queue_item_free (item);
  }
}

static void
gst_ahc_src_on_error (int error, gpointer user_data)
{
  GstAHCSrc *self = GST_AHC_SRC (user_data);

  GST_WARNING_OBJECT (self, "Received error code : %d", error);
}

static gboolean
gst_ahc_src_open (GstAHCSrc * self)
{
  GST_DEBUG_OBJECT (self, "Openning camera");

  self->camera = gst_ah_camera_open (self->device);

  if (self->camera) {
    GST_DEBUG_OBJECT (self, "Opened camera");

    self->texture = gst_ag_surfacetexture_new (0);
    gst_ah_camera_set_preview_texture (self->camera, self->texture);
    self->buffer_size = 0;
  } else {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Unable to open device '%d'.", 0), GST_ERROR_SYSTEM);
  }

  return (self->camera != NULL);
}

static void
gst_ahc_src_close (GstAHCSrc * self)
{
  if (self->camera) {
    gst_ah_camera_set_error_callback (self->camera, NULL, NULL);
    gst_ah_camera_set_preview_callback_with_buffer (self->camera, NULL, NULL);
    gst_ah_camera_release (self->camera);
  }
  self->camera = NULL;

  if (self->texture)
    gst_ag_surfacetexture_release (self->texture);
  self->texture = NULL;
}

static GstStateChangeReturn
gst_ahc_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstAHCSrc *self = GST_AHC_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      gint num_cams = gst_ah_camera_get_number_of_cameras ();

      if (num_cams > 0) {
        if (!gst_ahc_src_open (self))
          return GST_STATE_CHANGE_FAILURE;
      } else {
        GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
            ("There are no cameras available on this device."),
            GST_ERROR_SYSTEM);
        return GST_STATE_CHANGE_FAILURE;
      }
    }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_ahc_src_close (self);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_ahc_src_start (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_DEBUG_OBJECT (self, "Starting preview");
  if (self->camera) {
    self->previous_ts = GST_CLOCK_TIME_NONE;
    self->fps_min = self->fps_max = self->width = self->height = 0;
    self->format = ImageFormat_UNKNOWN;
    self->start = TRUE;

    return TRUE;
  } else {
    return FALSE;
  }
}

static gboolean
gst_ahc_src_stop (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_DEBUG_OBJECT (self, "Stopping preview");
  if (self->camera) {
    gst_data_queue_flush (self->queue);
    self->start = FALSE;
    gst_ah_camera_set_error_callback (self->camera, NULL, NULL);
    return gst_ah_camera_stop_preview (self->camera);
  }
  return TRUE;
}

static gboolean
gst_ahc_src_unlock (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_DEBUG_OBJECT (self, "Unlocking create");
  gst_data_queue_set_flushing (self->queue, TRUE);

  return TRUE;
}

static gboolean
gst_ahc_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_DEBUG_OBJECT (self, "Stopping unlock");
  gst_data_queue_set_flushing (self->queue, FALSE);

  return TRUE;
}

static GstFlowReturn
gst_ahc_src_create (GstPushSrc * src, GstBuffer ** buffer)
{
  GstAHCSrc *self = GST_AHC_SRC (src);
  GstDataQueueItem *item;

  if (!gst_data_queue_pop (self->queue, &item))
    return GST_FLOW_WRONG_STATE;

  *buffer = GST_BUFFER (item->object);
  g_slice_free (GstDataQueueItem, item);

  return GST_FLOW_OK;
}

static gboolean
gst_ahc_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:{
      GstClockTime min, max;

      gst_query_parse_latency (query, NULL, &min, &max);
      min = gst_util_uint64_scale (GST_SECOND, 1000, self->fps_max);
      max = gst_util_uint64_scale (GST_SECOND, 1000, self->fps_min);
      GST_DEBUG_OBJECT (self,
          "Reporting latency min: %" GST_TIME_FORMAT " max: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min), GST_TIME_ARGS (max));
      gst_query_set_latency (query, TRUE, min, max);

      return TRUE;
      break;
    }
    default:
      return GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      break;
  }

  g_assert_not_reached ();
}
