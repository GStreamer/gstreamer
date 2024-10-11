/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@gmail.com>
 *
 * gstv4l2.c: plugin for v4l2 elements
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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE            /* O_CLOEXEC */
#endif

#include <glib/gi18n-lib.h>

#include <gst/gst.h>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ext/videodev2.h"
#include "gstv4l2elements.h"
#include "v4l2-utils.h"

#include "gstv4l2object.h"
#include "gstv4l2src.h"
#include "gstv4l2sink.h"
#include "gstv4l2radio.h"
#include "gstv4l2videodec.h"
#include "gstv4l2fwhtenc.h"
#include "gstv4l2h263enc.h"
#include "gstv4l2h264enc.h"
#include "gstv4l2h265enc.h"
#include "gstv4l2jpegenc.h"
#include "gstv4l2mpeg4enc.h"
#include "gstv4l2vp8enc.h"
#include "gstv4l2vp9enc.h"
#include "gstv4l2transform.h"

GST_DEBUG_CATEGORY_EXTERN (v4l2_debug);
#define GST_CAT_DEFAULT v4l2_debug

#ifdef GST_V4L2_ENABLE_PROBE
static gboolean
gst_v4l2_probe_and_register (GstPlugin * plugin)
{
  GstV4l2Iterator *it;
  gint video_fd = -1;
  struct v4l2_capability vcap;
  guint32 device_caps;
  enum v4l2_buf_type output_type, capture_type;

  v4l2_element_init (plugin);

  GST_DEBUG ("Probing devices");

  it = gst_v4l2_iterator_new ();

  while (gst_v4l2_iterator_next (it)) {
    GstCaps *src_caps, *sink_caps;
    gchar *basename;

    if (video_fd >= 0)
      close (video_fd);

    /* FIXME, missing libv4l2 support */
    video_fd = open (it->device_path, O_RDWR | O_CLOEXEC);

    if (video_fd == -1) {
      GST_DEBUG ("Failed to open %s: %s", it->device_path, g_strerror (errno));
      continue;
    }

    memset (&vcap, 0, sizeof (vcap));

    if (ioctl (video_fd, VIDIOC_QUERYCAP, &vcap) < 0) {
      GST_DEBUG ("Failed to get device '%s' capabilities: %s",
          it->device_path, g_strerror (errno));
      continue;
    }

    if (vcap.capabilities & V4L2_CAP_DEVICE_CAPS)
      device_caps = vcap.device_caps;
    else
      device_caps = vcap.capabilities;

    if (!GST_V4L2_IS_M2M (device_caps))
      continue;

    if (device_caps & V4L2_CAP_VIDEO_M2M_MPLANE) {
      output_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
      capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else {
      output_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
      capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }

    GST_DEBUG ("Probing '%s' located at '%s'",
        it->device_name ? it->device_name : (const gchar *) vcap.driver,
        it->device_path);

    /* get sink supported format (no MPLANE for codec) */
    sink_caps = gst_v4l2_object_probe_template_caps (it->device_path,
        video_fd, output_type);
    /* get src supported format */
    src_caps = gst_v4l2_object_probe_template_caps (it->device_path,
        video_fd, capture_type);

    /* Skip devices without any supported formats */
    if (gst_caps_is_empty (sink_caps) || gst_caps_is_empty (src_caps)) {
      gst_caps_unref (sink_caps);
      gst_caps_unref (src_caps);

      GST_DEBUG ("Skipping unsupported device '%s' located at '%s'",
          it->device_name ? it->device_name : (const gchar *) vcap.driver,
          it->device_path);
      continue;
    }

    basename = g_path_get_basename (it->device_path);

    /* Caps won't be freed if the subclass is not instantiated */
    GST_MINI_OBJECT_FLAG_SET (sink_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
    GST_MINI_OBJECT_FLAG_SET (src_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

    if (gst_v4l2_is_video_dec (sink_caps, src_caps)) {
      gst_v4l2_video_dec_register (plugin, basename, it->device_path,
          video_fd, sink_caps, src_caps);
    } else if (gst_v4l2_is_video_enc (sink_caps, src_caps, NULL)) {
      if (gst_v4l2_is_fwht_enc (sink_caps, src_caps))
        gst_v4l2_fwht_enc_register (plugin, basename, it->device_path,
            sink_caps, src_caps);

      if (gst_v4l2_is_h264_enc (sink_caps, src_caps))
        gst_v4l2_h264_enc_register (plugin, basename, it->device_path,
            video_fd, sink_caps, src_caps);

      if (gst_v4l2_is_h265_enc (sink_caps, src_caps))
        gst_v4l2_h265_enc_register (plugin, basename, it->device_path,
            video_fd, sink_caps, src_caps);

      if (gst_v4l2_is_mpeg4_enc (sink_caps, src_caps))
        gst_v4l2_mpeg4_enc_register (plugin, basename, it->device_path,
            video_fd, sink_caps, src_caps);

      if (gst_v4l2_is_h263_enc (sink_caps, src_caps))
        gst_v4l2_h263_enc_register (plugin, basename, it->device_path,
            sink_caps, src_caps);

      if (gst_v4l2_is_jpeg_enc (sink_caps, src_caps))
        gst_v4l2_jpeg_enc_register (plugin, basename, it->device_path,
            sink_caps, src_caps);

      if (gst_v4l2_is_vp8_enc (sink_caps, src_caps))
        gst_v4l2_vp8_enc_register (plugin, basename, it->device_path,
            video_fd, sink_caps, src_caps);

      if (gst_v4l2_is_vp9_enc (sink_caps, src_caps))
        gst_v4l2_vp9_enc_register (plugin, basename, it->device_path,
            video_fd, sink_caps, src_caps);
    } else if (gst_v4l2_is_transform (sink_caps, src_caps)) {
      gst_v4l2_transform_register (plugin, basename, it->device_path,
          sink_caps, src_caps);
    }
    /* else if ( ... etc. */

    gst_caps_unref (sink_caps);
    gst_caps_unref (src_caps);
    g_free (basename);
  }

  if (video_fd >= 0)
    close (video_fd);

  gst_v4l2_iterator_free (it);

  return TRUE;
}
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;
  const gchar *paths[] = { "/dev", "/dev/v4l2", NULL };
  const gchar *names[] = { "video", NULL };

  /* Add some dependency, so the dynamic features get updated upon changes in
   * /dev/video* */
  gst_plugin_add_dependency (plugin,
      NULL, paths, names, GST_PLUGIN_DEPENDENCY_FLAG_FILE_NAME_IS_PREFIX);

#ifdef GST_V4L2_ENABLE_PROBE
  ret |= gst_v4l2_probe_and_register (plugin);
#endif

  ret |= GST_ELEMENT_REGISTER (v4l2src, plugin);
  ret |= GST_ELEMENT_REGISTER (v4l2sink, plugin);
  ret |= GST_ELEMENT_REGISTER (v4l2radio, plugin);
  ret |= GST_DEVICE_PROVIDER_REGISTER (v4l2deviceprovider, plugin);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    video4linux2,
    "elements for Video 4 Linux",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
