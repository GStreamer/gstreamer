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

#include "gstahcsrc.h"
#include "gst-dvm.h"

static void gst_ahc_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ahc_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ahc_src_dispose (GObject * object);

static GstStateChangeReturn gst_ahc_src_change_state (GstElement * element,
    GstStateChange transition);
static GstCaps *gst_ahc_src_getcaps (GstBaseSrc * src);
static gboolean gst_ahc_src_start (GstBaseSrc * bsrc);
static gboolean gst_ahc_src_stop (GstBaseSrc * bsrc);
static gboolean gst_ahc_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_ahc_src_unlock_stop (GstBaseSrc * bsrc);
static GstFlowReturn gst_ahc_src_create (GstPushSrc * src, GstBuffer ** buffer);

static void gst_ahc_src_close (GstAHCSrc * self);

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
  ARG_0,
};
GST_BOILERPLATE (GstAHCSrc, gst_ahc_src, GstPushSrc, GST_TYPE_PUSH_SRC);

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
  gstbasesrc_class->start = gst_ahc_src_start;
  gstbasesrc_class->stop = gst_ahc_src_stop;
  gstbasesrc_class->unlock = gst_ahc_src_unlock;
  gstbasesrc_class->unlock_stop = gst_ahc_src_unlock_stop;

  gstpushsrc_class->create = gst_ahc_src_create;
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
  self->caps = NULL;
}

static void
gst_ahc_src_dispose (GObject * object)
{
  GstAHCSrc *self = GST_AHC_SRC (object);

  gst_ahc_src_close (self);

  if (self->queue)
    g_object_unref (self->queue);
  self->queue = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
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
      GST_WARNING_OBJECT (self, "Supported preview formats:");

      for (i = formats; i; i = i->next) {
        int f = GPOINTER_TO_INT (i->data);
        GstStructure *format = NULL;

        /* Ignore duplicates */
        if (f == previous_format)
          continue;

        /* Can't use switch/case because the values are not constants */
        if (f == ImageFormat_NV16) {
          GST_WARNING_OBJECT (self, "    NV16 (%d)", f);
          format = gst_structure_new ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('N', 'V', '1', '6'),
              NULL);
        } else if (f == ImageFormat_NV21) {
          GST_WARNING_OBJECT (self, "    NV21 (%d)", f);
          format = gst_structure_new ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('N', 'V', '2', '1'),
              NULL);
        } else if (f == ImageFormat_RGB_565) {
          GST_WARNING_OBJECT (self, "    RGB565 (%d)", f);
          format = gst_structure_new ("video/x-raw-rgb",
              "bpp", G_TYPE_INT, 16,
              "depth", G_TYPE_INT, 16,
              "red_mask", G_TYPE_INT, 0xf800,
              "green_mask", G_TYPE_INT, 0x07e0,
              "blue_mask", G_TYPE_INT, 0x001f,
              "endianness", G_TYPE_INT, G_LITTLE_ENDIAN, NULL);
        } else if (f == ImageFormat_YUY2) {
          GST_WARNING_OBJECT (self, "    YUY2 (%d)", f);
          format = gst_structure_new ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
              NULL);
        } else if (f == ImageFormat_YV12) {
          GST_WARNING_OBJECT (self, "    YV12 (%d)", f);
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
      GST_WARNING_OBJECT (self, "Supported preview sizes:");
      for (i = sizes; i; i = i->next) {
        GstAHCSize *s = i->data;

        GST_WARNING_OBJECT (self, "    %dx%d", s->width, s->height);
      }
      GST_WARNING_OBJECT (self, "Supported preview fps range:");
      for (i = ranges; i; i = i->next) {
        int *range = i->data;

        GST_WARNING_OBJECT (self, "    [%d, %d]", range[0], range[1]);
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
gst_ahc_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAHCSrc *self = GST_AHC_SRC (object);
  (void) self;

  switch (prop_id) {
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
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

  g_slice_free (FreeFuncBuffer, data);
}

static void
_data_queue_item_free (GstDataQueueItem * item)
{
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static void
gst_ahc_src_on_preview_frame (jbyteArray data, gpointer user_data)
{
  GstAHCSrc *self = GST_AHC_SRC (user_data);
  JNIEnv *env = gst_dvm_get_env ();
  GstBuffer *buffer;
  GstDataQueueItem *item = NULL;
  FreeFuncBuffer *malloc_data = NULL;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE;
  GstClockTime duration = 0;
  GstClock *clock;

  if ((clock = GST_ELEMENT_CLOCK (self))) {
    GstClockTime base_time = GST_ELEMENT_CAST (self)->base_time;
    GstClockTime current_ts;

    gst_object_ref (clock);
    current_ts = gst_clock_get_time (clock) - base_time;
    if (GST_CLOCK_TIME_IS_VALID (self->previous_ts)) {
      timestamp = self->previous_ts;
      duration = current_ts - self->previous_ts;
    } else {
      /* Drop the first buffer */
      gst_ah_camera_add_callback_buffer (self->camera, data);
      return;
    }
    self->previous_ts = current_ts;
    gst_object_unref (clock);
    gst_base_src_set_do_timestamp (GST_BASE_SRC (self), FALSE);
  } else {
    gst_base_src_set_do_timestamp (GST_BASE_SRC (self), TRUE);
  }

  //GST_WARNING_OBJECT (self, "Received data buffer %p", data);
  malloc_data = g_slice_new0 (FreeFuncBuffer);
  malloc_data->self = self;
  malloc_data->array = (*env)->NewGlobalRef (env, data);
  malloc_data->data = (*env)->GetByteArrayElements (env, data, NULL);

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = (guint8 *) malloc_data->data;
  GST_BUFFER_SIZE (buffer) = self->buffer_size;
  GST_BUFFER_MALLOCDATA (buffer) = (gpointer) malloc_data;
  GST_BUFFER_FREE_FUNC (buffer) = gst_ahc_src_buffer_free_func;
  GST_BUFFER_DURATION (buffer) = duration;
  GST_BUFFER_TIMESTAMP (buffer) = timestamp;

  gst_buffer_set_caps (buffer, self->caps);

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->size = GST_BUFFER_SIZE (buffer);
  item->duration = GST_BUFFER_DURATION (buffer);
  item->visible = TRUE;
  item->destroy = (GDestroyNotify) _data_queue_item_free;

  gst_data_queue_push (self->queue, item);
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
  GST_WARNING_OBJECT (self, "Openning camera");

  self->camera = gst_ah_camera_open (0);

  if (self->camera) {
    JNIEnv *env = gst_dvm_get_env ();
    GstAHCParameters *params;
    gint i;

    GST_WARNING_OBJECT (self, "Opened camera");

    self->texture = gst_ag_surfacetexture_new (0);
    gst_ah_camera_set_preview_texture (self->camera, self->texture);
    gst_ah_camera_set_error_callback (self->camera, gst_ahc_src_on_error, self);

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      GstAHCSize *size;

      GST_WARNING_OBJECT (self, "Params : %s",
          gst_ahc_parameters_flatten (params));
      gst_ahc_parameters_set_preview_size (params, 1280, 720);
      gst_ahc_parameters_set_preview_format (params, ImageFormat_YV12);

      GST_WARNING_OBJECT (self, "Setting new params (%d) : %s",
          gst_ah_camera_set_parameters (self->camera, params),
          gst_ahc_parameters_flatten (params));
      size = gst_ahc_parameters_get_preview_size (params);
      self->caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y', 'V', '1', '2'),
          "width", G_TYPE_INT, size->width,
          "height", G_TYPE_INT, size->height,
          "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
      self->buffer_size = size->width * size->height *
          gst_ag_imageformat_get_bits_per_pixel
          (gst_ahc_parameters_get_preview_format (params)) / 8;
      gst_ahc_size_free (size);
      gst_ahc_parameters_free (params);
    }
    for (i = 0; i < NUM_CALLBACK_BUFFERS; i++) {
      jbyteArray array = (*env)->NewByteArray (env, self->buffer_size);

      gst_ah_camera_add_callback_buffer (self->camera, array);
      (*env)->DeleteLocalRef (env, array);
    }
  } else {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Unable to open device '%d'.", 0), GST_ERROR_SYSTEM);
  }

  return (self->camera != NULL);
}

static void
gst_ahc_src_close (GstAHCSrc * self)
{
  if (self->camera)
    gst_ah_camera_release (self->camera);
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
      gint i;

      GST_WARNING_OBJECT (self, "Found %d cameras on the system", num_cams);

      for (i = 0; i < num_cams; i++) {
        GstAHCCameraInfo info;
        if (gst_ah_camera_get_camera_info (i, &info)) {
          GST_WARNING_OBJECT (self, "Camera info for %d", i);
          GST_WARNING_OBJECT (self, "    Facing: %s (%d)",
              info.facing == CameraInfo_CAMERA_FACING_BACK ? "Back" : "Front",
              info.facing);
          GST_WARNING_OBJECT (self, "    Orientation: %d degrees",
              info.orientation);
        } else {
          GST_WARNING_OBJECT (self, "Error getting camera info for %d", i);
        }
      }

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

  GST_WARNING_OBJECT (self, "Starting preview");
  if (self->camera) {
    gboolean ret = gst_ah_camera_start_preview (self->camera);
    if (ret) {
      self->previous_ts = GST_CLOCK_TIME_NONE;
      gst_ah_camera_set_preview_callback_with_buffer (self->camera,
          gst_ahc_src_on_preview_frame, self);
    }
    return ret;
  } else {
    return FALSE;
  }
}

static gboolean
gst_ahc_src_stop (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_WARNING_OBJECT (self, "Stopping preview");
  if (self->camera) {
    gst_data_queue_flush (self->queue);
    return gst_ah_camera_stop_preview (self->camera);
  }
  return TRUE;
}

static gboolean
gst_ahc_src_unlock (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_WARNING_OBJECT (self, "Unlocking create");
  gst_data_queue_set_flushing (self->queue, TRUE);

  return TRUE;
}

static gboolean
gst_ahc_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_WARNING_OBJECT (self, "Stopping unlock");
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
