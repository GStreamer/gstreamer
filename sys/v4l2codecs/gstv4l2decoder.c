/* GStreamer
 * Copyright (C) 2020 Nicolas Dufresne <nicolas.dufresne@collabora.com>
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
#include <config.h>
#endif

#include "gstv4l2decoder.h"
#include "linux/media.h"
#include "linux/videodev2.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

GST_DEBUG_CATEGORY (v4l2_decoder_debug);
#define GST_CAT_DEFAULT v4l2_decoder_debug

enum
{
  PROP_0,
  PROP_MEDIA_DEVICE,
  PROP_VIDEO_DEVICE,
};

struct _GstV4l2Decoder
{
  GstObject parent;

  gboolean opened;
  gint media_fd;
  gint video_fd;

  /* properties */
  gchar *media_device;
  gchar *video_device;
};

G_DEFINE_TYPE_WITH_CODE (GstV4l2Decoder, gst_v4l2_decoder, GST_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (v4l2_decoder_debug, "v4l2codecs-decoder", 0,
        "V4L2 stateless decoder helper"));

static void
gst_v4l2_decoder_finalize (GObject * obj)
{
  GstV4l2Decoder *self = GST_V4L2_DECODER (obj);

  if (self->media_fd)
    close (self->media_fd);
  if (self->video_fd)
    close (self->media_fd);

  g_free (self->media_device);
  g_free (self->video_device);

  G_OBJECT_CLASS (gst_v4l2_decoder_parent_class)->finalize (obj);
}

static void
gst_v4l2_decoder_init (GstV4l2Decoder * self)
{
}

static void
gst_v4l2_decoder_class_init (GstV4l2DecoderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_v4l2_decoder_finalize;
  gobject_class->get_property = gst_v4l2_decoder_get_property;
  gobject_class->set_property = gst_v4l2_decoder_set_property;

  gst_v4l2_decoder_install_properties (gobject_class, 0, NULL);
}

GstV4l2Decoder *
gst_v4l2_decoder_new (GstV4l2CodecDevice * device)
{
  GstV4l2Decoder *decoder;

  g_return_val_if_fail (device->function == MEDIA_ENT_F_PROC_VIDEO_DECODER,
      NULL);

  decoder = g_object_new (GST_TYPE_V4L2_DECODER,
      "media-device", device->media_device_path,
      "video-device", device->video_device_path, NULL);

  return gst_object_ref_sink (decoder);
}

gboolean
gst_v4l2_decoder_open (GstV4l2Decoder * self)
{
  self->media_fd = open (self->media_device, 0);
  if (self->media_fd < 0) {
    GST_ERROR_OBJECT (self, "Failed to open '%s': %s",
        self->media_device, g_strerror (errno));
    return FALSE;
  }

  self->video_fd = open (self->video_device, 0);
  if (self->video_fd < 0) {
    GST_ERROR_OBJECT (self, "Failed to open '%s': %s",
        self->video_device, g_strerror (errno));
    return FALSE;
  }

  self->opened = TRUE;

  return TRUE;
}

gboolean
gst_v4l2_decoder_enum_input_fmt (GstV4l2Decoder * self, gint i,
    guint32 * out_fmt)
{
  struct v4l2_fmtdesc fmtdesc = { i, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, };
  gint ret;

  g_return_val_if_fail (self->opened, FALSE);

  ret = ioctl (self->video_fd, VIDIOC_ENUM_FMT, &fmtdesc);
  if (ret < 0) {
    if (errno != EINVAL)
      GST_ERROR_OBJECT (self, "VIDIOC_ENUM_FMT failed: %s", g_strerror (errno));
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Found format %" GST_FOURCC_FORMAT " (%s)",
      GST_FOURCC_ARGS (fmtdesc.pixelformat), fmtdesc.description);
  *out_fmt = fmtdesc.pixelformat;

  return TRUE;
}

void
gst_v4l2_decoder_install_properties (GObjectClass * gobject_class,
    gint prop_offset, GstV4l2CodecDevice * device)
{
  const gchar *media_device_path = NULL;
  const gchar *video_device_path = NULL;

  if (device) {
    media_device_path = device->media_device_path;
    video_device_path = device->video_device_path;
  }

  g_object_class_install_property (gobject_class, PROP_MEDIA_DEVICE,
      g_param_spec_string ("media-device", "Media Device Path",
          "Path to the media device node", media_device_path,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VIDEO_DEVICE,
      g_param_spec_string ("video-device", "Video Device Path",
          "Path to the video device node", video_device_path,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

void
gst_v4l2_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstV4l2Decoder *self = GST_V4L2_DECODER (object);

  switch (prop_id) {
    case PROP_MEDIA_DEVICE:
      g_free (self->media_device);
      self->media_device = g_value_dup_string (value);
      break;
    case PROP_VIDEO_DEVICE:
      g_free (self->video_device);
      self->video_device = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

void
gst_v4l2_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstV4l2Decoder *self = GST_V4L2_DECODER (object);

  switch (prop_id) {
    case PROP_MEDIA_DEVICE:
      g_value_set_string (value, self->media_device);
      break;
    case PROP_VIDEO_DEVICE:
      g_value_set_string (value, self->video_device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
